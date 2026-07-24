/*
 * fft_proc.c
 * Software FFT for angle-domain processing.
 *
 * Radix-2 DIT FFT for 64-point transforms.
 * 48-point input → zero-pad to 64 → FFT → magnitude.
 *
 * All math functions are self-implemented (no libm dependency)
 * to avoid Vitis auto-generated makefile linker issues.
 */

#include "fft_proc.h"
#include "frame_buffer.h"
#include <string.h>

/* ============================================================
 * Precomputed cos/sin tables for 64 angles (0, 2PI/64, ..., 63*2PI/64)
 * cos_tab[i] = cos(2*PI*i/64), sin_tab[i] = sin(2*PI*i/64)
 * ============================================================ */
static const float cos_tab[64] = {
     1.000000f,  0.995185f,  0.980785f,  0.956940f,
     0.923880f,  0.881921f,  0.831470f,  0.773010f,
     0.707107f,  0.634393f,  0.555570f,  0.471397f,
     0.382683f,  0.290285f,  0.195090f,  0.098017f,
     0.000000f, -0.098017f, -0.195090f, -0.290285f,
    -0.382683f, -0.471397f, -0.555570f, -0.634393f,
    -0.707107f, -0.773010f, -0.831470f, -0.881921f,
    -0.923880f, -0.956940f, -0.980785f, -0.995185f,
    -1.000000f, -0.995185f, -0.980785f, -0.956940f,
    -0.923880f, -0.881921f, -0.831470f, -0.773010f,
    -0.707107f, -0.634393f, -0.555570f, -0.471397f,
    -0.382683f, -0.290285f, -0.195090f, -0.098017f,
    -0.000000f,  0.098017f,  0.195090f,  0.290285f,
     0.382683f,  0.471397f,  0.555570f,  0.634393f,
     0.707107f,  0.773010f,  0.831470f,  0.881921f,
     0.923880f,  0.956940f,  0.980785f,  0.995185f
};

static const float sin_tab[64] = {
     0.000000f,  0.098017f,  0.195090f,  0.290285f,
     0.382683f,  0.471397f,  0.555570f,  0.634393f,
     0.707107f,  0.773010f,  0.831470f,  0.881921f,
     0.923880f,  0.956940f,  0.980785f,  0.995185f,
     1.000000f,  0.995185f,  0.980785f,  0.956940f,
     0.923880f,  0.881921f,  0.831470f,  0.773010f,
     0.707107f,  0.634393f,  0.555570f,  0.471397f,
     0.382683f,  0.290285f,  0.195090f,  0.098017f,
     0.000000f, -0.098017f, -0.195090f, -0.290285f,
    -0.382683f, -0.471397f, -0.555570f, -0.634393f,
    -0.707107f, -0.773010f, -0.831470f, -0.881921f,
    -0.923880f, -0.956940f, -0.980785f, -0.995185f,
    -1.000000f, -0.995185f, -0.980785f, -0.956940f,
    -0.923880f, -0.881921f, -0.831470f, -0.773010f,
    -0.707107f, -0.634393f, -0.555570f, -0.471397f,
    -0.382683f, -0.290285f, -0.195090f, -0.098017f
};

/* Newton sqrt, no libm needed.
 * Uses bit-level initial guess for fast convergence.
 * For x up to ~1e18, 4 iterations give <1e-6 relative error. */
static float my_sqrtf(float x)
{
    float y;
    int i;
    if (x <= 0.0f) return 0.0f;

    /* Bit-manipulation initial guess: sqrt(x) ≈ (x_bits >> 1) + magic */
    {
        union { float f; unsigned int u; } u;
        u.f = x;
        u.u = (u.u >> 1) + 0x1FC00000u;
        y = u.f;
    }

    for (i = 0; i < 4; i++) {
        y = 0.5f * (y + x / y);
    }
    return y;
}

static void bit_reverse(float *real, float *imag, int n)
{
    int i, j, k;
    for (i = 0, j = 0; i < n; i++) {
        if (j > i) {
            float tr = real[i], ti = imag[i];
            real[i] = real[j]; imag[i] = imag[j];
            real[j] = tr;      imag[j] = ti;
        }
        k = n >> 1;
        while (k & j) { j ^= k; k >>= 1; }
        j ^= k;
    }
}

void fft_radix2(float *real, float *imag, int n)
{
    int group, pair;
    int half_size = 1;

    if (n <= 1) return;

    bit_reverse(real, imag, n);

    /*
     * Twiddle factor: W = exp(-j*2*PI/N) = cos(-2*PI/N) + j*sin(-2*PI/N)
     * For stage with half_size butterflies:
     *   angle_step = -PI / half_size
     *   angle = -PI * pair / half_size
     * In units of 2*PI/64:  idx = (32 * pair / half_size) % 64
     * cos(-x) = cos(x), sin(-x) = -sin(x)
     */
    while (half_size < n) {
        int full_size = half_size << 1;
        int idx_step = 32 / half_size;

        for (group = 0; group < n; group += full_size) {
            for (pair = 0; pair < half_size; pair++) {
                int idx = (pair * idx_step) & 63;
                float wr = cos_tab[idx];
                float wi = -sin_tab[idx];

                int even = group + pair;
                int odd  = even + half_size;

                float tr = wr * real[odd] - wi * imag[odd];
                float ti = wr * imag[odd] + wi * real[odd];

                real[odd] = real[even] - tr;
                imag[odd] = imag[even] - ti;
                real[even] = real[even] + tr;
                imag[even] = imag[even] + ti;
            }
        }
        half_size = full_size;
    }
}

void fft_magnitude(const float *real, const float *imag, int n, float *mag)
{
    int i;
    for (i = 0; i < n; i++) {
        mag[i] = my_sqrtf(real[i] * real[i] + imag[i] * imag[i]);
    }
}

static float spatial_window(u32 index)
{
    float center = ((float)ANGLE_ELEMENTS - 1.0f) * 0.5f;
    float distance = (float)index - center;
    if (distance < 0.0f)
        distance = -distance;
    return 1.0f - distance / center;
}

static void unpack_complex(u32 packed, float *real, float *imag)
{
    *real = (float)(s16)(packed & 0xFFFFu);
    *imag = (float)(s16)(packed >> 16);
}

void angle_fft_horizontal_complex(const u32 *frame_matrix, u32 range_bin,
                                  float *angle_map, float *spectrum,
                                  u32 *valid_cuts)
{
    u32 row;
    float real[ANGLE_FFT_SIZE];
    float imag[ANGLE_FFT_SIZE];

    memset(angle_map, 0, SCAN_ROWS * ANGLE_FFT_SIZE * sizeof(float));
    memset(spectrum, 0, ANGLE_FFT_SIZE * sizeof(float));
    *valid_cuts = 0;

    for (row = 0; row < SCAN_ROWS; row++) {
        u32 col;
        u32 valid = 0;
        memset(real, 0, sizeof(real));
        memset(imag, 0, sizeof(imag));

        for (col = 0; col < SCAN_COLS; col++) {
            if (frame_buffer_element_valid(row, col)) {
                u32 idx = (row * SCAN_COLS + col) * FFT_HALF_SIZE + range_bin;
                float weight = spatial_window(col);
                unpack_complex(frame_matrix[idx], &real[col], &imag[col]);
                real[col] *= weight;
                imag[col] *= weight;
                valid++;
            }
        }

        if (valid < AOA_MIN_CUT_ELEMENTS)
            continue;

        fft_radix2(real, imag, ANGLE_FFT_SIZE);
        for (col = 0; col < ANGLE_FFT_SIZE; col++) {
            float power = real[col] * real[col] + imag[col] * imag[col];
            float normalized = power / ((float)valid * (float)valid);
            angle_map[row * ANGLE_FFT_SIZE + col] = my_sqrtf(normalized);
            spectrum[col] += normalized;
        }
        (*valid_cuts)++;
    }

    if (*valid_cuts > 0) {
        u32 bin;
        for (bin = 0; bin < ANGLE_FFT_SIZE; bin++)
            spectrum[bin] /= (float)*valid_cuts;
    }
}

void angle_fft_vertical_complex(const u32 *frame_matrix, u32 range_bin,
                                float *angle_map, float *spectrum,
                                u32 *valid_cuts)
{
    u32 col;
    float real[ANGLE_FFT_SIZE];
    float imag[ANGLE_FFT_SIZE];

    memset(angle_map, 0, SCAN_COLS * ANGLE_FFT_SIZE * sizeof(float));
    memset(spectrum, 0, ANGLE_FFT_SIZE * sizeof(float));
    *valid_cuts = 0;

    for (col = 0; col < SCAN_COLS; col++) {
        u32 row;
        u32 valid = 0;
        memset(real, 0, sizeof(real));
        memset(imag, 0, sizeof(imag));

        for (row = 0; row < SCAN_ROWS; row++) {
            if (frame_buffer_element_valid(row, col)) {
                u32 idx = (row * SCAN_COLS + col) * FFT_HALF_SIZE + range_bin;
                float weight = spatial_window(row);
                unpack_complex(frame_matrix[idx], &real[row], &imag[row]);
                real[row] *= weight;
                imag[row] *= weight;
                valid++;
            }
        }

        if (valid < AOA_MIN_CUT_ELEMENTS)
            continue;

        fft_radix2(real, imag, ANGLE_FFT_SIZE);
        for (row = 0; row < ANGLE_FFT_SIZE; row++) {
            float power = real[row] * real[row] + imag[row] * imag[row];
            float normalized = power / ((float)valid * (float)valid);
            angle_map[col * ANGLE_FFT_SIZE + row] = my_sqrtf(normalized);
            spectrum[row] += normalized;
        }
        (*valid_cuts)++;
    }

    if (*valid_cuts > 0) {
        u32 bin;
        for (bin = 0; bin < ANGLE_FFT_SIZE; bin++)
            spectrum[bin] /= (float)*valid_cuts;
    }
}

int angle_spectrum_peak(const float *spectrum, float *peak_bin,
                        float *peak_value, float *baseline,
                        float *peak_ratio)
{
    float noise[ANGLE_FFT_SIZE];
    u32 peak = 0;
    u32 noise_count = 0;
    u32 bin;

    *peak_value = spectrum[0];
    for (bin = 1; bin < ANGLE_FFT_SIZE; bin++) {
        if (spectrum[bin] > *peak_value) {
            *peak_value = spectrum[bin];
            peak = bin;
        }
    }

    for (bin = 0; bin < ANGLE_FFT_SIZE; bin++) {
        u32 distance = (bin > peak) ? (bin - peak) : (peak - bin);
        if (distance > ANGLE_FFT_SIZE / 2)
            distance = ANGLE_FFT_SIZE - distance;
        if (distance > 2u)
            noise[noise_count++] = spectrum[bin];
    }

    for (bin = 1; bin < noise_count; bin++) {
        float value = noise[bin];
        int position = (int)bin - 1;
        while (position >= 0 && noise[position] > value) {
            noise[position + 1] = noise[position];
            position--;
        }
        noise[position + 1] = value;
    }

    *baseline = noise_count > 0 ? noise[noise_count / 2] : 0.0f;
    *peak_ratio = *baseline > 0.0f ? *peak_value / *baseline : 0.0f;

    {
        u32 left = (peak + ANGLE_FFT_SIZE - 1u) % ANGLE_FFT_SIZE;
        u32 right = (peak + 1u) % ANGLE_FFT_SIZE;
        float denominator = 2.0f *
            (spectrum[left] - 2.0f * spectrum[peak] + spectrum[right]);
        float delta = (denominator > 1.0e-9f || denominator < -1.0e-9f)
                    ? (spectrum[left] - spectrum[right]) / denominator : 0.0f;
        if (delta > 0.5f) delta = 0.5f;
        if (delta < -0.5f) delta = -0.5f;
        *peak_bin = (float)peak + delta;
        if (*peak_bin >= (float)ANGLE_FFT_SIZE)
            *peak_bin -= (float)ANGLE_FFT_SIZE;
        if (*peak_bin < 0.0f)
            *peak_bin += (float)ANGLE_FFT_SIZE;
    }

    return (*peak_value > 0.0f && *baseline > 0.0f) ? 0 : -1;
}
