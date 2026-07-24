/*
 * output_fmt.c
 * Structured output formatting with physical units.
 *
 * Distance formula:
 *   R(cm) = bin * c / (2 * B * 32) * 100
 *   = bin * 3e10 / (2 * 4e9 * 32) * 100
 *   = bin * 0.1171875 cm
 *
 * SNR formula:
 *   SNR(dB) = 10 * log10(signal_power / noise_power)
 *   noise_floor = mean of spectrum skipping DC and peaks
 *
 * All math functions are self-implemented (no libm dependency).
 */

#include "output_fmt.h"
#include "pre_filter.h"
#include <stdio.h>
#ifdef Linux
#else
#include "xil_printf.h"
#endif

/*
 * Self-implemented log10f via log2 decomposition.
 * x = m * 2^e, 1 <= m < 2
 * log2(x) = e + log2(m)
 * log10(x) = log2(x) * log10(2) = log2(x) * 0.30103
 * log2(m) approximated by polynomial for m in [1, 2)
 */
static float my_log10f(float x)
{
    int e;
    float m, y;
    if (x <= 0.0f) return 0.0f;

    e = 0;
    m = x;
    while (m >= 2.0f) { m *= 0.5f; e++; }
    while (m < 1.0f)  { m *= 2.0f; e--; }

    y = m - 1.0f;
    return ((float)e + y * (1.442695f - 0.721348f * y)) * 0.30103f;
}

float fmt_dist_cm(u32 bin)
{
    return (float)bin * PER_BIN_CM_F;
}

/*
 * Self-implemented asin(x) via polynomial approximation.
 * asin(x) ≈ x * (1 + x²*(1/6 + x²*(3/40 + x²*5/112)))
 * The polynomial is direct for |x| <= 0.5. Larger inputs use a half-angle
 * transform, avoiding the recursive cycle around 45 degrees.
 */
static float my_sqrtf(float x)
{
    float estimate;
    int iteration;
    if (x <= 0.0f) return 0.0f;
    estimate = x > 1.0f ? x : 1.0f;
    for (iteration = 0; iteration < 6; iteration++)
        estimate = 0.5f * (estimate + x / estimate);
    return estimate;
}

static float my_asinf(float x)
{
    float sign = 1.0f;
    float x2;
    float poly;
    if (x < 0.0f) {
        sign = -1.0f;
        x = -x;
    }
    if (x >= 1.0f)
        return sign * 1.570796327f;
    if (x > 0.5f)
        return sign * (1.570796327f -
                       2.0f * my_asinf(my_sqrtf(0.5f * (1.0f - x))));
    x2 = x * x;
    poly = 1.0f + x2 * (0.1666667f + x2 * (0.075f + x2 * 0.0446429f));
    return sign * x * poly;
}

/*
 * sin(deg) via Taylor: sin(x) ≈ x - x³/6 + x⁵/120 - x⁷/5040
 * Valid for |deg| < 90°, error < 0.001.
 */
static float my_sinf_deg(float deg)
{
    float rad = deg * 0.017453293f;  /* π/180 */
    float x2 = rad * rad;
    return rad * (1.0f - x2 * (0.16666667f - x2 * (0.00833333f - x2 * 0.00019841f)));
}

/*
 * cos(deg) via Taylor: cos(x) ≈ 1 - x²/2 + x⁴/24 - x⁶/720
 * Valid for |deg| < 90°, error < 0.001.
 */
static float my_cosf_deg(float deg)
{
    float rad = deg * 0.017453293f;
    float x2 = rad * rad;
    return 1.0f - x2 * (0.5f - x2 * (0.041666667f - x2 * 0.001388889f));
}

float fmt_angle_deg(u32 bin)
{
    float sin_theta;
    if (bin < ANGLE_FFT_HALF) {
        sin_theta = (float)bin / ((float)ANGLE_FFT_SIZE * ANTENNA_D_OVER_LAMBDA);
    } else {
        sin_theta = ((float)bin - (float)ANGLE_FFT_SIZE)
                    / ((float)ANGLE_FFT_SIZE * ANTENNA_D_OVER_LAMBDA);
    }
    return my_asinf(sin_theta) * 57.29578f;  /* 180/π */
}

float fmt_snr_db(u32 signal_mag, const u32 *spectrum)
{
    float noise_sum = 0.0f;
    int noise_count = 0;
    int i;

    if (signal_mag == 0)
        return 0.0f;

    /*
     * Estimate noise floor: average of bins [near_field_bins, FFT_HALF_SIZE)
     * excluding DC region.  Near-field bins are zeroed by pre-filter;
     * far-field bins contain residual noise + target signals.
     * Excluding near-field prevents zero-biased noise floor from inflating SNR.
     * Uses the adaptive boundary (not a hardcoded constant).
     */
    {
        u32 near_field = pre_filter_get_near_field_bins();
        for (i = DC_SKIP_BINS + (int)near_field + 1; i < FFT_HALF_SIZE; i++) {
            noise_sum += (float)spectrum[i];
            noise_count++;
        }
    }

    if (noise_count == 0)
        return 0.0f;

    float noise_floor = noise_sum / (float)noise_count;
    if (noise_floor < 1.0f)
        noise_floor = 1.0f;

    float snr = 10.0f * my_log10f((float)signal_mag / noise_floor);
    return (snr < 0.0f) ? 0.0f : snr;
}

float fmt_power_ratio_db(float ratio)
{
    return ratio > 1.0f ? 10.0f * my_log10f(ratio) : 0.0f;
}

void output_1d_frame(u32 frame_idx, const peak_info_t *peaks,
                     int num_peaks, const u32 *spectrum)
{
    int p;

    if (num_peaks <= 0)
        return;

    /* Primary peak: distance + SNR */
    float dist = fmt_dist_cm(peaks[0].freq_index);
    float snr = fmt_snr_db(peaks[0].magnitude, spectrum);

    printf("F%lu: D=%.1fcm SNR=%.1fdB",
           (unsigned long)frame_idx,
           (double)dist,
           (double)snr);

    /* Additional peaks: distance only */
    if (num_peaks > 1) {
        printf(" [%d:", num_peaks);
        for (p = 0; p < num_peaks; p++) {
            printf(" D=%.1fcm", (double)fmt_dist_cm(peaks[p].freq_index));
            if (p < num_peaks - 1) printf("|");
        }
        printf("]");
    }

    printf("\n");
}

void output_2d_targets(const target_info_t *targets, int num_targets,
                       int is_vertical)
{
    int t, printed = 0;
    u32 seen_bins[32] = {0};
    int   seen_cnt = 0;

    for (t = 0; t < num_targets && printed < 5; t++) {
        u32 angle_bin = targets[t].h_angle_bin;
        u32 i;
        int duplicate = 0;

        for (i = 0; i < (u32)seen_cnt; i++) {
            if (seen_bins[i] == angle_bin) { duplicate = 1; break; }
        }
        if (duplicate) continue;
        if (seen_cnt < 32) seen_bins[seen_cnt++] = angle_bin;

        printf("    T%d: %s_bin=%lu angle=%.1f deg mag=%.0f\n",
               printed + 1,
               is_vertical ? "V" : "H",
               (unsigned long)angle_bin,
               (double)fmt_angle_deg(angle_bin),
               (double)targets[t].magnitude);
        printed++;
    }
}

/*
 * Convert spherical (range, azimuth, elevation) to Cartesian (x, y, z).
 * Convention: x = cross-range (horizontal), y = down-range (forward),
 *             z = height (vertical).
 *   x = r * cos(el) * sin(az)
 *   y = r * cos(el) * cos(az)
 *   z = r * sin(el)
 */
void output_cartesian(float range_cm, float az_deg, float el_deg)
{
    float cos_el = my_cosf_deg(el_deg);
    float sin_el = my_sinf_deg(el_deg);
    float sin_az = my_sinf_deg(az_deg);
    float cos_az = my_cosf_deg(az_deg);
    float x = range_cm * cos_el * sin_az;
    float y = range_cm * cos_el * cos_az;
    float z = range_cm * sin_el;

    printf("\n===== 3D Cartesian Coordinates =====\n");
    printf("  Spherical: R=%.2f cm  Az=%.1f deg  El=%.1f deg\n",
           (double)range_cm, (double)az_deg, (double)el_deg);
    printf("  Cartesian: X=%.2f cm  Y=%.2f cm  Z=%.2f cm\n",
           (double)x, (double)y, (double)z);
    printf("  (X: cross-range, Y: down-range, Z: height)\n");
}

void output_2d_header(const u32 *top_bins, u32 nb)
{
    u32 bi;

    printf("Top %lu range bins (by CFAR peak frequency):\n", (unsigned long)nb);
    for (bi = 0; bi < nb; bi++) {
        printf("  Bin %4lu: %.1f cm\n",
               (unsigned long)top_bins[bi],
               (double)fmt_dist_cm(top_bins[bi]));
    }
}

void output_cfar_histogram(const u32 *histogram, u32 total_frames)
{
    u32 i;
    u32 total_detections = 0;
    u32 active_bins = 0;

    for (i = 0; i < FFT_HALF_SIZE; i++) {
        total_detections += histogram[i];
        if (histogram[i] > 0)
            active_bins++;
    }

    printf("\n===== CFAR Peak Histogram Statistics =====\n");
    printf("Total detections: %lu\n", (unsigned long)total_detections);
    printf("Active bins: %lu\n", (unsigned long)active_bins);
    printf("Frames: %lu\n", (unsigned long)total_frames);

    if (total_frames > 0) {
        printf("Detection rate: %.1f peaks/frame\n",
               (double)total_detections / (double)total_frames);
    }

    if (active_bins > 0) {
        printf("Sparsity: %.1f%% of range bins have detections\n",
               (double)active_bins * 100.0 / (double)FFT_HALF_SIZE);
    }

    printf("Top 10 bins by detection count:\n");
    {
        u32 top10[10] = {0};
        u32 top10_idx[10] = {0};
        u32 j;
        for (i = 0; i < 10; i++) {
            u32 best = 0;
            u32 best_idx = 0;
            for (j = 0; j < FFT_HALF_SIZE; j++) {
                if (histogram[j] > best) {
                    int already = 0;
                    u32 k;
                    for (k = 0; k < i; k++) {
                        if (top10_idx[k] == j) { already = 1; break; }
                    }
                    if (!already) { best = histogram[j]; best_idx = j; }
                }
            }
            top10[i] = best;
            top10_idx[i] = best_idx;
            if (best > 0) {
                printf("  #%lu: Bin %lu (%.1f cm) - %lu detections\n",
                       (unsigned long)(i + 1),
                       (unsigned long)best_idx,
                       (double)fmt_dist_cm(best_idx),
                       (unsigned long)best);
            }
        }
    }
    printf("===========================================\n\n");
}

int fmt_planar_angles(float horizontal_bin, float vertical_bin,
                      float *azimuth_deg, float *elevation_deg)
{
    float horizontal_signed = horizontal_bin;
    float vertical_signed = vertical_bin;
    float direction_u;
    float direction_v;
    float horizontal_scale;

    if (horizontal_signed >= (float)ANGLE_FFT_HALF)
        horizontal_signed -= (float)ANGLE_FFT_SIZE;
    if (vertical_signed >= (float)ANGLE_FFT_HALF)
        vertical_signed -= (float)ANGLE_FFT_SIZE;

    direction_u = AOA_HORIZONTAL_SIGN * horizontal_signed /
        ((float)ANGLE_FFT_SIZE * ANTENNA_D_OVER_LAMBDA);
    direction_v = AOA_VERTICAL_SIGN * vertical_signed /
        ((float)ANGLE_FFT_SIZE * ANTENNA_D_OVER_LAMBDA);

    if (direction_u * direction_u + direction_v * direction_v >= 1.0f)
        return -1;

    horizontal_scale = my_sqrtf(1.0f - direction_v * direction_v);
    if (horizontal_scale < 1.0e-6f)
        return -1;

    *azimuth_deg = my_asinf(direction_u / horizontal_scale) * 57.29578f +
                   AOA_AZIMUTH_OFFSET_DEG;
    *elevation_deg = my_asinf(direction_v) * 57.29578f +
                     AOA_ELEVATION_OFFSET_DEG;
    return 0;
}
