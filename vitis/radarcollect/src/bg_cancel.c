/*
 * bg_cancel.c
 * Background cancellation for self-coupling compensation.
 *
 * Background stored at fixed DDR address BG_CANCEL_BASE.
 * Layout: bg[(row * SCAN_COLS + col) * FFT_HALF_SIZE + bin]
 *
 * Calibration flow:
 *   1. bg_cancel_init()     → zero all background
 *   2. bg_cancel_capture()  → store empty-scene spectrum per position
 *   3. bg_cancel_apply()    → subtract background from each frame
 *
 * Subtraction clamps to 0 (no negative values).
 */

#include "bg_cancel.h"
#include <string.h>
#ifdef Linux
#else
#include "xil_cache.h"
#endif

static volatile u32 *bg_ptr;

void bg_cancel_init(void)
{
    bg_ptr = (volatile u32 *)BG_CANCEL_BASE;

    /* zero-fill the background buffer */
    {
        u32 i;
        u32 total_words = SCAN_ROWS * SCAN_COLS * FFT_HALF_SIZE;
        for (i = 0; i < total_words; i++) {
            bg_ptr[i] = 0;
        }
    }

#ifndef Linux
    Xil_DCacheFlushRange((UINTPTR)bg_ptr, BG_CANCEL_SIZE);
#endif
}

void bg_cancel_capture(u32 row, u32 col, const u32 *spectrum)
{
    u32 base;
    u32 bin;

    if (row >= SCAN_ROWS || col >= SCAN_COLS)
        return;

    base = (row * SCAN_COLS + col) * FFT_HALF_SIZE;

    for (bin = 0; bin < FFT_HALF_SIZE; bin++) {
        bg_ptr[base + bin] = spectrum[bin];
    }

#ifndef Linux
    Xil_DCacheFlushRange((UINTPTR)(bg_ptr + base),
                          FFT_HALF_SIZE * sizeof(u32));
#endif
}

void bg_cancel_apply(u32 row, u32 col, const u32 *spectrum, u32 *output)
{
    u32 base;
    u32 bin;

    if (row >= SCAN_ROWS || col >= SCAN_COLS) {
        /* passthrough if out of range */
        for (bin = 0; bin < FFT_HALF_SIZE; bin++)
            output[bin] = spectrum[bin];
        return;
    }

    base = (row * SCAN_COLS + col) * FFT_HALF_SIZE;

    for (bin = 0; bin < FFT_HALF_SIZE; bin++) {
        u32 bg = bg_ptr[base + bin];
        u32 sig = spectrum[bin];
        /* clamp to 0: if background > signal, output 0 */
        output[bin] = (sig > bg) ? (sig - bg) : 0;
    }
}

int bg_cancel_is_calibrated(void)
{
    u32 i;
    u32 total_words = SCAN_ROWS * SCAN_COLS * FFT_HALF_SIZE;

    /* check if any background value is non-zero */
    for (i = 0; i < total_words; i++) {
        if (bg_ptr[i] != 0)
            return 1;
    }
    return 0;
}

u32 bg_cancel_get(u32 row, u32 col, u32 bin)
{
    u32 base;
    if (row >= SCAN_ROWS || col >= SCAN_COLS || bin >= FFT_HALF_SIZE)
        return 0;
    base = (row * SCAN_COLS + col) * FFT_HALF_SIZE;
    return bg_ptr[base + bin];
}