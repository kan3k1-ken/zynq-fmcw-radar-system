/*
 * scan_accum.c
 * Multi-scan accumulation for noise reduction.
 *
 * Accumulator: u64 at SCAN_ACCUM_BASE (DDR 0x19000000)
 * Layout: accum[(row * SCAN_COLS + col) * FFT_HALF_SIZE + bin]
 *
 * Flow:
 *   scan_accum_add()         → accumulate each frame
 *   scan_accum_is_ready()    → check if N scans done
 *   scan_accum_average_and_store() → divide by N, push to frame_buffer
 *   scan_accum_reset()       → zero for next batch
 */

#include "scan_accum.h"
#include "frame_buffer.h"
#include <string.h>
#ifdef Linux
#else
#include "xil_cache.h"
#endif

static volatile u64 *accum_ptr;
static u32 accum_frame_count;  /* frames within current scan (0..2303) */
static u32 accum_scan_count;   /* completed scans (0..SCAN_ACCUM_N-1) */

void scan_accum_init(void)
{
    accum_ptr = (volatile u64 *)SCAN_ACCUM_BASE;
    accum_frame_count = 0;
    accum_scan_count = 0;

    /* zero-fill the accumulator */
    {
        u32 i;
        u32 total_words = SCAN_ROWS * SCAN_COLS * FFT_HALF_SIZE;
        for (i = 0; i < total_words; i++) {
            accum_ptr[i] = 0ULL;
        }
    }

#ifndef Linux
    Xil_DCacheFlushRange((UINTPTR)accum_ptr, SCAN_ACCUM_SIZE);
#endif
}

void scan_accum_add(u32 row, u32 col, const u32 *spectrum)
{
    u32 base;
    u32 bin;

    if (row >= SCAN_ROWS || col >= SCAN_COLS)
        return;

    base = (row * SCAN_COLS + col) * FFT_HALF_SIZE;

    for (bin = 0; bin < FFT_HALF_SIZE; bin++) {
        accum_ptr[base + bin] += (u64)spectrum[bin];
    }

    accum_frame_count++;

    if (accum_frame_count >= SCAN_TOTAL_FRAMES) {
        accum_frame_count = 0;
        accum_scan_count++;
    }
}

int scan_accum_is_ready(void)
{
    return (accum_scan_count >= SCAN_ACCUM_N) ? 1 : 0;
}

int scan_accum_average_and_store(void)
{
    u32 row, col, bin;
    u32 ready_scans = accum_scan_count;

    if (ready_scans == 0)
        return 0;

    /*
     * Walk all 48x48 positions, average each bin, store to frame_buffer.
     * Use a stack-allocated temporary buffer for one row of spectra
     * to avoid large stack allocation.
     */
    for (row = 0; row < SCAN_ROWS; row++) {
        for (col = 0; col < SCAN_COLS; col++) {
            u32 base = (row * SCAN_COLS + col) * FFT_HALF_SIZE;
            static u32 temp[FFT_HALF_SIZE];

            for (bin = 0; bin < FFT_HALF_SIZE; bin++) {
                u64 sum = accum_ptr[base + bin];
                temp[bin] = (u32)(sum / (u64)ready_scans);
            }

            frame_buffer_store_complex(row, col, temp, 0);
        }
    }

    return frame_buffer_is_full();
}

void scan_accum_reset(void)
{
    u32 i;
    u32 total_words = SCAN_ROWS * SCAN_COLS * FFT_HALF_SIZE;

    for (i = 0; i < total_words; i++) {
        accum_ptr[i] = 0ULL;
    }

    accum_frame_count = 0;
    accum_scan_count = 0;

#ifndef Linux
    Xil_DCacheFlushRange((UINTPTR)accum_ptr, SCAN_ACCUM_SIZE);
#endif
}

u32 scan_accum_count(void)
{
    return accum_scan_count;
}
