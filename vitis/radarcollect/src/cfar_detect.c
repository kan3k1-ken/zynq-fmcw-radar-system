/*
 * cfar_detect.c
 * CFAR (Constant False Alarm Rate) target detection.
 *
 * Industrial-grade implementation:
 *   1. noise_floor_estimate: median-of-means floor removal
 *   2. cfar_detect_1d: CA/OS/GO/SO CFAR with sliding window
 *   3. Parabolic peak interpolation (sub-bin accuracy)
 *   4. Adjacent-peak merging
 *   5. cfar_detect_2d: 2D range-angle joint detection
 */

#include "cfar_detect.h"
#include <string.h>

static void insert_sort(u32 *arr, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        u32 key = arr[i];
        for (j = i - 1; j >= 0 && arr[j] > key; j--)
            arr[j + 1] = arr[j];
        arr[j + 1] = key;
    }
}

/*
 * noise_floor_estimate
 *   Robust local noise-floor estimation using a sliding median window.
 *   For each bin, collects NOISE_WIN neighbours, excludes the central
 *   bin and its guard band (dist <= CFAR_GUARD), sorts the remaining
 *   samples, takes the k-th smallest (NOISE_MEDIAN_K), and subtracts it.
 *   This removes self-coupling, DC pedestal, and slow-varying clutter
 *   while preserving narrow target peaks.
 *
 *   src/dst: length = FFT_HALF_SIZE (1024)
 *   The input is copied before processing so src and dst may safely alias.
 */
void noise_floor_estimate(const u32 *src, u32 *dst, u32 len)
{
    u32 i;
    u32 buf[NOISE_WIN];
    static u32 source_copy[FFT_HALF_SIZE];

    if (len > FFT_HALF_SIZE)
        len = FFT_HALF_SIZE;

    memcpy(source_copy, src, len * sizeof(u32));
    src = source_copy;

    for (i = 0; i < len; i++) {
        u32 half = NOISE_WIN / 2;
        int  start = (int)i - (int)half;
        int  end   = (int)i + (int)half;
        int  cnt   = 0;
        int  k;

        if (start < 0)          start = 0;
        if ((u32)end >= len)    end   = (int)(len - 1);

        for (k = start; k <= end; k++) {
            int dist = (k > (int)i) ? (k - (int)i) : ((int)i - k);
            if (dist <= CFAR_GUARD) continue;
            if (src[k] == 0) continue;
            buf[cnt++] = src[k];
        }

        if (cnt > 0) {
            int median_idx = (cnt * NOISE_MEDIAN_K) / NOISE_WIN;
            if (median_idx >= cnt) median_idx = cnt - 1;
            insert_sort(buf, cnt);
            {
                u32 floor_val = buf[median_idx];
                dst[i] = (src[i] > floor_val) ? (src[i] - floor_val) : 0;
            }
        } else {
            dst[i] = src[i];
        }
    }
}

static int merge_adjacent_peaks(peak_info_t *peaks, int count)
{
    int i, j;
    int removed = 0;

    for (i = 0; i < count; i++) {
        if (peaks[i].freq_index == 0xFFFFFFFFu) continue;
        for (j = i + 1; j < count; j++) {
            if (peaks[j].freq_index == 0xFFFFFFFFu) continue;
            {
                int diff = (int)peaks[i].freq_index - (int)peaks[j].freq_index;
                if (diff < 0) diff = -diff;
                if (diff <= MERGE_BINS) {
                    if (peaks[i].magnitude >= peaks[j].magnitude) {
                        peaks[j].freq_index = 0xFFFFFFFFu;
                    } else {
                        peaks[i] = peaks[j];
                        peaks[j].freq_index = 0xFFFFFFFFu;
                    }
                    removed++;
                }
            }
        }
    }

    if (removed > 0) {
        int wr = 0;
        for (i = 0; i < count; i++) {
            if (peaks[i].freq_index != 0xFFFFFFFFu)
                peaks[wr++] = peaks[i];
        }
        count = wr;
    }
    return count;
}

/*
 * peak_interpolate
 *   3-point parabolic interpolation for sub-bin accuracy.
 *   y = spectrum values at bin-1, bin, bin+1
 *   delta = 0.5 * (y0 - y2) / (y0 - 2*y1 + y2)
 *   pos   = bin + delta
 *   mag   = y1 - 0.25 * (y0 - y2) * delta
 *
 *   Returns 0 on success, -1 if denominator is zero.
 */
static int peak_interpolate(const u32 *spectrum, u32 bin,
                            float *pos, float *mag)
{
    float y0, y1, y2, denom;

    if (bin == 0 || bin >= FFT_HALF_SIZE - 1)
        return -1;

    y0 = (float)spectrum[bin - 1];
    y1 = (float)spectrum[bin];
    y2 = (float)spectrum[bin + 1];

    denom = y0 - 2.0f * y1 + y2;
    if (denom >= -0.001f && denom <= 0.001f)
        return -1;

    {
        float delta = 0.5f * (y0 - y2) / denom;
        *pos = (float)bin + delta;
        *mag = y1 - 0.25f * (y0 - y2) * delta;
    }
    return 0;
}

/*
 * peak_validate
 *   Industrial-grade peak validation to reject false detections.
 *
 *   spectrum:       cleaned spectrum (post-noise-floor-subtraction)
 *                   Used for -3dB width check — baseline is near zero,
 *                   giving accurate half-power width measurement.
 *   spectrum_snr:   pre-noise-floor spectrum (clutter-removed only)
 *                   Used for SNR noise estimation — contains the true
 *                   local noise floor for meaningful SNR calculation.
 *
 *   Checks:
 *     1. SNR: peak magnitude vs local noise floor (from spectrum_snr)
 *     2. Width: -3dB width in bins (from spectrum)
 *        too narrow = noise spike, too wide = clutter
 *
 *   Returns filtered count.
 */
int peak_validate(peak_info_t *peaks, int num_peaks,
                  const u32 *spectrum, const u32 *spectrum_snr)
{
    int i;
    int valid_count = 0;

    for (i = 0; i < num_peaks; i++) {
        u32 bin = peaks[i].freq_index;
        u32 mag = peaks[i].magnitude;
        u32 noise_est = 0;
        int width, left, right;
        int k;
        int valid = 1;

        if (bin == 0xFFFFFFFFu || mag == 0)
            continue;

        /*
         * SNR check: estimate local noise floor from spectrum_snr
         * (pre-noise-floor-subtraction).  This gives a meaningful
         * noise estimate because the noise floor has not been removed.
         * Skip zero samples (near-field blanked bins) for unbiased estimate.
         */
        {
            u32 noise_buf[NOISE_WIN];
            int nc = 0;
            int half = NOISE_WIN / 2;
            int start = (int)bin - half;
            int end   = (int)bin + half;

            if (start < 0) start = 0;
            if (end >= (int)FFT_HALF_SIZE) end = FFT_HALF_SIZE - 1;

            for (k = start; k <= end; k++) {
                int dist = (k > (int)bin) ? (k - (int)bin) : ((int)bin - k);
                if (dist <= CFAR_GUARD) continue;
                if (spectrum_snr[k] == 0) continue;
                if (nc < (int)NOISE_WIN)
                    noise_buf[nc++] = spectrum_snr[k];
            }

            if (nc > 0) {
                insert_sort(noise_buf, nc);
                noise_est = noise_buf[(nc * NOISE_MEDIAN_K) / NOISE_WIN];
            }
        }

        if (noise_est > 0) {
            u64 snr = (u64)mag * 256u / (u64)noise_est;
            if (snr < VALIDATE_MIN_SNR_Q8)
                valid = 0;
        }

        /*
         * Width check: find -3dB (half-power) width on cleaned spectrum.
         * The cleaned spectrum has near-zero baseline, giving accurate
         * half-power width measurement.
         * -3dB = mag / sqrt(2) ≈ mag * 0.7071
         * Q8 approximation: (mag * 181) >> 8 ≈ mag * 0.7070
         */
        if (valid) {
            u32 half_mag = (mag * 181u) >> 8;

            left = (int)bin;
            while (left > 0 && spectrum[left] > half_mag)
                left--;
            right = (int)bin;
            while (right < (int)FFT_HALF_SIZE - 1 && spectrum[right] > half_mag)
                right++;
            width = right - left;

            if (width < VALIDATE_MIN_WIDTH || width > VALIDATE_MAX_WIDTH)
                valid = 0;
        }

        if (valid) {
            peaks[valid_count++] = peaks[i];
        }
    }

    return valid_count;
}

/*
 * cfar_detect_1d
 *   Industrial-grade 1D CFAR with sliding-window optimisation.
 *   Supports CA-CFAR, OS-CFAR, GO-CFAR, SO-CFAR via CFAR_MODE macro.
 *
 *   Pipeline:
 *     a. Sliding-window noise estimation
 *     b. Mode-dependent threshold comparison
 *     c. Peak interpolation (sub-bin)
 *     d. Sort descending by magnitude
 *     e. Merge adjacent peaks
 *
 *   Returns number of peaks found.
 */
int cfar_detect_1d(const u32 *spectrum, peak_info_t *peaks, u32 start_bin)
{
    int num_peaks = 0;
    u32 noise_sum = 0;
    int j, t;
    u32 first_j = start_bin + CFAR_HALF_WIN;

    if (first_j < (u32)(DC_SKIP_BINS + CFAR_HALF_WIN))
        first_j = DC_SKIP_BINS + CFAR_HALF_WIN;

    {
        for (t = (int)first_j - CFAR_HALF_WIN; t < (int)first_j - CFAR_GUARD; t++)
            noise_sum += spectrum[t];
        for (t = (int)first_j + CFAR_GUARD + 1; t <= (int)(first_j + CFAR_HALF_WIN); t++)
            noise_sum += spectrum[t];
    }

    for (j = (int)first_j;
         j < (int)(FFT_HALF_SIZE - CFAR_HALF_WIN);
         j++) {
        int leaving_left  = j - CFAR_HALF_WIN - 1;
        int entering_left = j - CFAR_GUARD - 1;
        int leaving_right = j + CFAR_GUARD;
        int entering_right= j + CFAR_HALF_WIN;

        if (j > (int)first_j) {
            if (leaving_left  >= 0) noise_sum -= spectrum[leaving_left];
            if (entering_left >= 0) noise_sum += spectrum[entering_left];
            noise_sum -= spectrum[leaving_right];
            noise_sum += spectrum[entering_right];
        }

        {
            u32 noise_ref;
            u64 cut_scaled;

#if CFAR_MODE == 0
            noise_ref = noise_sum / (2 * CFAR_TRAIN);
#elif CFAR_MODE == 1
            {
                u32 train[2 * CFAR_TRAIN];
                int ti = 0;
                int k;
                for (k = j - CFAR_HALF_WIN; k < j - CFAR_GUARD; k++)
                    train[ti++] = spectrum[k];
                for (k = j + CFAR_GUARD + 1; k <= j + CFAR_HALF_WIN; k++)
                    train[ti++] = spectrum[k];
                insert_sort(train, 2 * CFAR_TRAIN);
                noise_ref = train[CFAR_OS_K];
            }
#elif CFAR_MODE == 2
            {
                u32 left_sum = 0, right_sum = 0;
                int k;
                u32 noise_left, noise_right;
                for (k = j - CFAR_HALF_WIN; k < j - CFAR_GUARD; k++)
                    left_sum += spectrum[k];
                for (k = j + CFAR_GUARD + 1; k <= j + CFAR_HALF_WIN; k++)
                    right_sum += spectrum[k];
                noise_left  = left_sum / CFAR_TRAIN;
                noise_right = right_sum / CFAR_TRAIN;
                noise_ref = (noise_left > noise_right) ? noise_left : noise_right;
            }
#elif CFAR_MODE == 3
            {
                u32 left_sum = 0, right_sum = 0;
                int k;
                u32 noise_left, noise_right;
                for (k = j - CFAR_HALF_WIN; k < j - CFAR_GUARD; k++)
                    left_sum += spectrum[k];
                for (k = j + CFAR_GUARD + 1; k <= j + CFAR_HALF_WIN; k++)
                    right_sum += spectrum[k];
                noise_left  = left_sum / CFAR_TRAIN;
                noise_right = right_sum / CFAR_TRAIN;
                noise_ref = (noise_left < noise_right) ? noise_left : noise_right;
            }
#else
            noise_ref = noise_sum / (2 * CFAR_TRAIN);
#endif

            cut_scaled = (u64)spectrum[j] * 256u;

            if (cut_scaled > (u64)CFAR_THRESH_Q8 * noise_ref) {
                if (num_peaks < MAX_PEAKS_PER_FRAME) {
                    peaks[num_peaks].freq_index = (u32)j;
                    peaks[num_peaks].magnitude = spectrum[j];
#if PEAK_INTERP_ENABLE
                    {
                        float ipos, imag;
                        if (peak_interpolate(spectrum, (u32)j, &ipos, &imag) == 0) {
                            peaks[num_peaks].interp_pos = ipos;
                            peaks[num_peaks].interp_mag = imag;
                        } else {
                            peaks[num_peaks].interp_pos = (float)j;
                            peaks[num_peaks].interp_mag = (float)spectrum[j];
                        }
                    }
#else
                    peaks[num_peaks].interp_pos = (float)j;
                    peaks[num_peaks].interp_mag = (float)spectrum[j];
#endif
                    num_peaks++;
                }
            }
        }
    }

    if (num_peaks == 0) {
        u32 max_val = 0;
        u32 max_idx = 0;
        for (j = (int)start_bin; j < (int)(FFT_HALF_SIZE - CFAR_HALF_WIN); j++) {
            if (spectrum[j] > max_val) {
                max_val = spectrum[j];
                max_idx = (u32)j;
            }
        }
        peaks[0].freq_index = max_idx;
        peaks[0].magnitude = max_val;
        peaks[0].interp_pos = (float)max_idx;
        peaks[0].interp_mag = (float)max_val;
        num_peaks = 1;
    }

    {
        int pi, pk;
        for (pi = 0; pi < num_peaks - 1; pi++) {
            for (pk = pi + 1; pk < num_peaks; pk++) {
                if (peaks[pk].interp_mag > peaks[pi].interp_mag) {
                    peak_info_t tmp = peaks[pi];
                    peaks[pi] = peaks[pk];
                    peaks[pk] = tmp;
                }
            }
        }
    }

    num_peaks = merge_adjacent_peaks(peaks, num_peaks);

    return num_peaks;
}

int cfar_detect_2d(const float *map, u32 spatial_dim, u32 angle_dim,
                   target_info_t *targets, u32 max_targets)
{
    u32 a, r, num_targets = 0;
    u32 guard_a = CFAR_GUARD, guard_r = CFAR_GUARD;
    u32 train_a = CFAR_TRAIN, train_r = CFAR_TRAIN;
    float thresh_factor = (float)CFAR_THRESH_Q8 / 256.0f;

    for (a = train_a + guard_a; a + train_a + guard_a < spatial_dim; a++) {
        for (r = train_r + guard_r; r + train_r + guard_r < angle_dim; r++) {
            float sum = 0.0f;
            u32 cnt = 0;
            u32 da, dr;

            for (da = 0; da <= 2 * train_a + 2 * guard_a; da++) {
                for (dr = 0; dr <= 2 * train_r + 2 * guard_r; dr++) {
                    int ia = (int)a - (int)(train_a + guard_a) + (int)da;
                    int ir = (int)r - (int)(train_r + guard_r) + (int)dr;
                    {
                        int dist_a = (ia < (int)a) ? ((int)a - ia) : (ia - (int)a);
                        int dist_r = (ir < (int)r) ? ((int)r - ir) : (ir - (int)r);
                        if (dist_a <= (int)guard_a && dist_r <= (int)guard_r)
                            continue;
                    }
                    if (ia >= 0 && (u32)ia < spatial_dim && ir >= 0 && (u32)ir < angle_dim) {
                        sum += map[ia * angle_dim + ir];
                        cnt++;
                    }
                }
            }

            if (cnt > 0) {
                float noise_avg = sum / (float)cnt;
                float cell_val = map[a * angle_dim + r];
                if (cell_val > thresh_factor * noise_avg) {
                    if (num_targets < max_targets) {
                        targets[num_targets].range_bin   = 0;
                        targets[num_targets].distance_cm = 0.0f;
                        targets[num_targets].h_angle_bin = r;
                        targets[num_targets].v_angle_bin = 0;
                        targets[num_targets].magnitude   = cell_val;
                        num_targets++;
                    }
                }
            }
        }
    }

    return (int)num_targets;
}
