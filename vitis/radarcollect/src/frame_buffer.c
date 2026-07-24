/*
 * frame_buffer.c
 * 48x48 spectral matrix manager for 2D radar processing.
 *
 * Layout: g_matrix[row * SCAN_COLS * FFT_HALF_SIZE + col * FFT_HALF_SIZE + bin]
 * Each element is one packed complex word: I16 in [15:0], Q16 in [31:16].
 *
 * Matrix stored in DDR at FRAME_BUFFER_BASE (not BSS) to avoid
 * inflating the .bss section which can cause linker/runtime issues.
 */

#include "frame_buffer.h"
#include <string.h>
#ifdef Linux
#else
#include "xil_cache.h"
#endif

#define MATRIX_ELEMENTS (SCAN_ROWS * SCAN_COLS * FFT_HALF_SIZE)

static u32 *g_matrix;         /* DDR pointer, not BSS */
static float g_row_energy[SCAN_ROWS * FFT_HALF_SIZE];
static float g_col_energy[SCAN_COLS * FFT_HALF_SIZE];
static u8    g_valid[SCAN_TOTAL_FRAMES];
static u32   g_frame_count;
static u32   g_valid_count;

static float packed_power(u32 value)
{
    s32 real = (s16)(value & 0xFFFFu);
    s32 imag = (s16)(value >> 16);
    return (float)((u32)(real * real) + (u32)(imag * imag));
}

void frame_buffer_init(void)
{
    g_matrix = (u32 *)FRAME_BUFFER_BASE;

    /* zero-fill the DDR matrix */
    {
        u32 i;
        for (i = 0; i < MATRIX_ELEMENTS; i++) {
            g_matrix[i] = 0u;
        }
    }

    memset(g_row_energy, 0, sizeof(g_row_energy));
    memset(g_col_energy, 0, sizeof(g_col_energy));
    memset(g_valid, 0, sizeof(g_valid));
    g_frame_count = 0;
    g_valid_count = 0;

#ifndef Linux
    Xil_DCacheFlushRange((UINTPTR)g_matrix, FRAME_BUFFER_SIZE);
#endif
}

int frame_buffer_store_complex(u32 row, u32 col, const u32 *spectrum,
                               int valid)
{
    u32 bin;
    u32 base = (row * SCAN_COLS + col) * FFT_HALF_SIZE;
    u32 position = row * SCAN_COLS + col;

    if (row >= SCAN_ROWS || col >= SCAN_COLS)
        return 0;

    for (bin = 0; bin < FFT_HALF_SIZE; bin++) {
        u32 packed = valid ? spectrum[bin] : 0u;
        float val = packed_power(packed);
        g_matrix[base + bin] = packed;
        g_row_energy[row * FFT_HALF_SIZE + bin] += val;
        g_col_energy[col * FFT_HALF_SIZE + bin] += val;
    }

    if (valid && !g_valid[position]) {
        g_valid[position] = 1u;
        g_valid_count++;
    }

#ifndef Linux
    Xil_DCacheFlushRange((UINTPTR)(g_matrix + base),
                          FFT_HALF_SIZE * sizeof(u32));
#endif

    g_frame_count++;
    return (g_frame_count >= SCAN_TOTAL_FRAMES) ? 1 : 0;
}

int frame_buffer_is_full(void)
{
    return (g_frame_count >= SCAN_TOTAL_FRAMES) ? 1 : 0;
}

u32 frame_buffer_count(void)
{
    return g_frame_count;
}

const u32 *frame_buffer_get_matrix(void)
{
    return g_matrix;
}

int frame_buffer_element_valid(u32 row, u32 col)
{
    if (row >= SCAN_ROWS || col >= SCAN_COLS)
        return 0;
    return g_valid[row * SCAN_COLS + col] != 0u;
}

u32 frame_buffer_valid_count(void)
{
    return g_valid_count;
}

void frame_buffer_get_row(u32 row, u32 bin, u32 *out)
{
    u32 col;
    u32 base = row * SCAN_COLS * FFT_HALF_SIZE + bin;

    for (col = 0; col < SCAN_COLS; col++) {
        out[col] = g_matrix[base + col * FFT_HALF_SIZE];
    }
}

void frame_buffer_get_col(u32 col, u32 bin, u32 *out)
{
    u32 row;
    u32 base = col * FFT_HALF_SIZE + bin;

    for (row = 0; row < SCAN_ROWS; row++) {
        out[row] = g_matrix[row * SCAN_COLS * FFT_HALF_SIZE + base];
    }
}

u32 frame_buffer_top_bins(u32 *bins, u32 max_bins)
{
    u32 bin;
    static float total_energy[FFT_HALF_SIZE];
    u32 n;

    for (bin = 0; bin < FFT_HALF_SIZE; bin++) {
        total_energy[bin] = 0.0f;
    }

    for (bin = 0; bin < FFT_HALF_SIZE; bin++) {
        u32 row, col;
        float sum = 0.0f;
        for (row = 0; row < SCAN_ROWS; row++) {
            for (col = 0; col < SCAN_COLS; col++) {
                u32 idx = (row * SCAN_COLS + col) * FFT_HALF_SIZE + bin;
                sum += packed_power(g_matrix[idx]);
            }
        }
        total_energy[bin] = sum;
    }

    /* simple selection sort for top N (N is small, typically 10) */
    {
        u32 i, j;
        for (i = 0; i < max_bins && i < FFT_HALF_SIZE - DC_SKIP_BINS; i++) {
            u32 best = i + DC_SKIP_BINS;
            for (j = i + DC_SKIP_BINS + 1; j < FFT_HALF_SIZE; j++) {
                if (total_energy[j] > total_energy[best])
                    best = j;
            }
            if (best != i + DC_SKIP_BINS) {
                float tmp_e = total_energy[i + DC_SKIP_BINS];
                total_energy[i + DC_SKIP_BINS] = total_energy[best];
                total_energy[best] = tmp_e;
            }
            bins[i] = best;
        }
    }

    n = (max_bins < FFT_HALF_SIZE - DC_SKIP_BINS)
        ? max_bins : FFT_HALF_SIZE - DC_SKIP_BINS;
    return n;
}

u32 frame_buffer_top_bins_by_cfar(const u32 *histogram, u32 *bins,
                                  u32 max_bins)
{
    u32 bin;
    u32 i, j;
    u32 n = 0;

    /*
     * Selection sort for top-N histogram bins.
     * Skip DC_SKIP_BINS and near-field bins.
     * Only consider bins with non-zero histogram entries.
     */
    for (i = 0; i < max_bins && i < FFT_HALF_SIZE - DC_SKIP_BINS; i++) {
        u32 best = DC_SKIP_BINS;
        u32 best_count = 0;

        for (bin = DC_SKIP_BINS; bin < FFT_HALF_SIZE; bin++) {
            /* Skip bins already selected */
            int already = 0;
            for (j = 0; j < i; j++) {
                if (bins[j] == bin) { already = 1; break; }
            }
            if (already) continue;

            if (histogram[bin] > best_count) {
                best_count = histogram[bin];
                best = bin;
            }
        }

        if (best_count == 0)
            break;

        bins[i] = best;
        n++;
    }

    return n;
}
