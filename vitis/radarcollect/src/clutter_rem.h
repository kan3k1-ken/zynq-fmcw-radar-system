/*
 * clutter_rem.h
 * Static Clutter Removal (TI mmWave SDK standard).
 *
 * Layer 2 of the industrial-grade pipeline:
 *   Per-bin mean subtraction across all frames.
 *
 * Principle:
 *   Self-coupling / leakage signals appear in EVERY frame.
 *   Target signals appear only in SOME frames.
 *   Mean across all frames ≈ self-coupling + DC offset.
 *   Subtract mean → self-coupling removed, targets preserved.
 *
 * Memory layout at CLUTTER_REM_BASE:
 *   [0x0000..0x1FFF]: u64 per-bin sum (8192 bytes)
 *   [0x2000..0x2FFF]: u32 per-bin mean (4096 bytes)
 */

#ifndef CLUTTER_REM_H_
#define CLUTTER_REM_H_

#include "radar_config.h"

/*
 * Reset the accumulation buffer to zero.
 * Must be called once before accumulating frames.
 */
void clutter_rem_init(void);

/*
 * Accumulate one frame's spectrum into the per-bin sum.
 * Call this for every frame during calibration pass.
 *
 * spectrum: 1024 u32 magnitude values
 * len:      FFT_HALF_SIZE (1024)
 */
void clutter_rem_accumulate(const u32 *spectrum, u32 len);

/*
 * Compute per-bin mean from accumulated sum.
 * Must be called after all frames have been accumulated.
 *
 * num_frames: total number of frames accumulated
 */
void clutter_rem_compute_mean(u32 num_frames);

/*
 * Apply static clutter removal to one frame.
 * For each bin: output[bin] = max(0, spectrum[bin] - mean[bin])
 *
 * spectrum: 1024 u32 magnitude values (input)
 * output:   1024 u32 cleaned values (output, may alias spectrum)
 * len:      FFT_HALF_SIZE (1024)
 */
void clutter_rem_apply(const u32 *spectrum, u32 *output, u32 len);

/*
 * Get the per-bin mean array (for diagnostics).
 * Returns pointer to u32 array of length FFT_HALF_SIZE.
 */
const u32 *clutter_rem_get_mean(void);

/*
 * Adaptive near-field boundary detection.
 *
 * Analyzes the clutter profile (per-bin mean) to find where
 * self-coupling / leakage power drops to the noise floor.
 *
 * Algorithm:
 *   1. Estimate noise floor + MAD from far-field bins (400..1023)
 *   2. Threshold = noise_floor + 5 * MAD  (statistical 4-sigma equiv.)
 *   3. Scan from bin 0, find last bin above threshold
 *   4. Add safety margin (+10 bins)
 *   5. Clamp to [PHYSICS_NEAR_FIELD_BINS, FFT_HALF_SIZE]
 *
 * This is industrial-grade: adapts to any dataset's self-coupling
 * profile without hardcoded bin numbers.  Only the physics-based
 * lower bound is fixed.
 *
 * Returns: adaptive near-field boundary (in bins)
 */
u32 clutter_rem_find_boundary(const u32 *mean_profile, u32 len);

#endif /* CLUTTER_REM_H_ */