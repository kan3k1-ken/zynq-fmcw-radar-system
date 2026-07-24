/*
 * clutter_rem.c
 * Static Clutter Removal (TI mmWave SDK standard).
 *
 * Layer 2 of the industrial-grade pipeline:
 *   Per-bin mean subtraction across all frames.
 *
 * Reference:
 *   TI mmWave SDK "Static Clutter Removal DPU":
 *   "For each range bin, compute the mean of all samples
 *    and subtract it from each sample."
 *
 * Memory layout at CLUTTER_REM_BASE:
 *   [0x0000..0x1FFF]: u64 per-bin sum (8192 bytes)
 *   [0x2000..0x2FFF]: u32 per-bin mean (4096 bytes)
 */

#include "clutter_rem.h"
#include <string.h>
#ifdef Linux
#else
#include "xil_cache.h"
#endif

static volatile u64 *clutter_sum;
static volatile u32 *clutter_mean;

void clutter_rem_init(void)
{
    clutter_sum  = (volatile u64 *)CLUTTER_REM_BASE;
    clutter_mean = (volatile u32 *)(CLUTTER_REM_BASE + CLUTTER_REM_SUM_SIZE);

    memset((void *)clutter_sum, 0, CLUTTER_REM_SUM_SIZE);
    memset((void *)clutter_mean, 0, CLUTTER_REM_MEAN_SIZE);

#ifndef Linux
    Xil_DCacheFlushRange((UINTPTR)clutter_sum, CLUTTER_REM_SUM_SIZE);
    Xil_DCacheFlushRange((UINTPTR)clutter_mean, CLUTTER_REM_MEAN_SIZE);
#endif
}

void clutter_rem_accumulate(const u32 *spectrum, u32 len)
{
    u32 i;

    for (i = 0; i < len && i < FFT_HALF_SIZE; i++) {
        clutter_sum[i] += (u64)spectrum[i];
    }
}

void clutter_rem_compute_mean(u32 num_frames)
{
    u32 i;

    if (num_frames == 0)
        return;

/*
     * No cache invalidate needed here: clutter_sum is only accessed
     * by the CPU.  CPU cache is coherent for CPU reads, so the
     * hardware will see the correct values written by the Pass 1
     * accumulation loop.  Xil_DCacheInvalidateRange would discard
     * dirty cache lines on write-back caches, causing data loss.
     */
    for (i = 0; i < FFT_HALF_SIZE; i++) {
        clutter_mean[i] = (u32)(clutter_sum[i] / (u64)num_frames);
    }

#ifndef Linux
    Xil_DCacheFlushRange((UINTPTR)clutter_mean, CLUTTER_REM_MEAN_SIZE);
#endif
}

void clutter_rem_apply(const u32 *spectrum, u32 *output, u32 len)
{
    u32 i;

    for (i = 0; i < len && i < FFT_HALF_SIZE; i++) {
        u32 cl = clutter_mean[i];
        u32 sig = spectrum[i];
        output[i] = (sig > cl) ? (sig - cl) : 0;
    }
}

const u32 *clutter_rem_get_mean(void)
{
    return (const u32 *)clutter_mean;
}

/*
 * clutter_rem_find_boundary
 *
 * Adaptive near-field boundary detection via peak-relative decay.
 *
 * The self-coupling produces a strong peak at bin 0 that decays with
 * distance.  The boundary is the first bin (after PHYSICS_NEAR_FIELD_BINS)
 * where the profile drops to BOUNDARY_DB_DOWN (20 dB) below the peak.
 * 20 dB = factor of 100, so the threshold is peak / 100.
 *
 * Why this approach:
 *   - Does NOT depend on noise-floor statistics (noise floor can be
 *     extremely low, making noise-based thresholds unreliable).
 *   - Does NOT depend on cumulative energy (far-field bins are
 *     numerous, and their collective energy can be significant).
 *   - Depends ONLY on the self-coupling peak strength, which is an
 *     intrinsic property of the radar hardware.
 *   - The BOUNDARY_MAX_BINS cap prevents runaway boundaries when
 *     the profile is unusually flat (e.g., from FFT leakage in
 *     unwindowed FFTs).
 *
 * Algorithm:
 *   1. Find peak value in bins [0..49] (self-coupling region)
 *   2. Divider = 10^(BOUNDARY_DB_DOWN/10) = 100 (for 20 dB)
 *   3. Threshold = peak / divider
 *   4. Scan forward from PHYSICS_NEAR_FIELD_BINS, find first bin
 *      below threshold
 *   5. Add BOUNDARY_MARGIN, clamp to [PHYSICS_NEAR_FIELD_BINS, BOUNDARY_MAX_BINS]
 */
u32 clutter_rem_find_boundary(const u32 *mean_profile, u32 len)
{
    u32 noise_end = (len < FFT_HALF_SIZE) ? len : FFT_HALF_SIZE;
    u32 i, boundary;
    u32 peak = 0;

    for (i = 0; i < 50 && i < noise_end; i++) {
        if (mean_profile[i] > peak)
            peak = mean_profile[i];
    }

    if (peak == 0)
        return PHYSICS_NEAR_FIELD_BINS;

    {
        u32 divisor = 1;
        int db;
        for (db = 0; db < BOUNDARY_DB_DOWN; db += 10)
            divisor *= 10;

        u32 threshold = peak / divisor;
        if (threshold < 1) threshold = 1;

        boundary = PHYSICS_NEAR_FIELD_BINS;
        for (i = PHYSICS_NEAR_FIELD_BINS; i < noise_end; i++) {
            if (mean_profile[i] < threshold) {
                boundary = i;
                break;
            }
        }

        boundary += BOUNDARY_MARGIN;
    }

    if (boundary < PHYSICS_NEAR_FIELD_BINS)
        boundary = PHYSICS_NEAR_FIELD_BINS;
    if (boundary > BOUNDARY_MAX_BINS)
        boundary = BOUNDARY_MAX_BINS;

    return boundary;
}