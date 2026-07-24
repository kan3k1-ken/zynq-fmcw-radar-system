/*
 * pre_filter.h
 * Pre-filtering layer for radar signal processing.
 *
 * Layer 1 of the industrial-grade pipeline:
 *   - Near-field blanking: zero bins 0..(near_field_bins-1)
 *     Boundary is computed ADAPTIVELY from the clutter profile
 *     (per-bin mean), not hardcoded.  Lower-bounded by antenna
 *     physics (PHYSICS_NEAR_FIELD_BINS).
 *   - Saturation detection: check for ADC saturation in raw IQ
 */

#ifndef PRE_FILTER_H_
#define PRE_FILTER_H_

#include "radar_config.h"

/*
 * Set the adaptive near-field boundary (in bins).
 * Called after clutter_rem_find_boundary() computes the
 * boundary from the clutter profile.
 * Must be >= PHYSICS_NEAR_FIELD_BINS.
 */
void pre_filter_set_near_field_bins(u32 bins);

/*
 * Get the current near-field boundary (in bins).
 * Used by SNR estimation and diagnostic output.
 */
u32 pre_filter_get_near_field_bins(void);

/*
 * Zero out near-field range bins in the spectrum.
 * Bins 0..(near_field_bins-1) are set to 0.
 * Uses the dynamically-set boundary, not a hardcoded macro.
 *
 * spectrum: 1024 u32 magnitude values (in-place)
 * len:       FFT_HALF_SIZE (1024)
 */
void near_field_mask(u32 *spectrum, u32 len);

/*
 * Check if raw IQ data contains saturated samples.
 * Saturation occurs when ADC hits its maximum/minimum value.
 * Saturated frames should be flagged for reduced weight in
 * multi-scan accumulation.
 *
 * iq_raw: pointer to raw IQ words (u32, each word = I16|Q16)
 * count:  number of words
 * Returns: 0 = normal, 1 = saturated
 */
int saturation_detect(const u32 *iq_raw, u32 count);

#endif /* PRE_FILTER_H_ */