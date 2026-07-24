/*
 * pre_filter.c
 * Pre-filtering layer for radar signal processing.
 *
 * Layer 1 of the industrial-grade pipeline:
 *   - Near-field blanking: zero bins 0..(near_field_bins-1)
 *     Boundary is adaptive, computed from clutter profile.
 *   - Saturation detection: check for ADC saturation
 */

#include "pre_filter.h"

/*
 * Adaptive near-field boundary.
 * Initialized to physics-based minimum.  Updated by
 * clutter_rem_find_boundary() after Pass 1 clutter accumulation.
 */
static u32 g_near_field_bins = PHYSICS_NEAR_FIELD_BINS;

void pre_filter_set_near_field_bins(u32 bins)
{
    if (bins >= PHYSICS_NEAR_FIELD_BINS && bins <= FFT_HALF_SIZE)
        g_near_field_bins = bins;
}

u32 pre_filter_get_near_field_bins(void)
{
    return g_near_field_bins;
}

/*
 * Saturation detection thresholds for 16-bit signed IQ.
 * Typical ADC saturation: I or Q = ±32767 (0x7FFF or 0x8000)
 * Use ±32760 as threshold to catch near-saturation.
 */
#define SAT_THRESHOLD  32760

void near_field_mask(u32 *spectrum, u32 len)
{
    u32 i;
    u32 end = (g_near_field_bins < len) ? g_near_field_bins : len;

    for (i = 0; i < end; i++) {
        spectrum[i] = 0;
    }
}

int saturation_detect(const u32 *iq_raw, u32 count)
{
    u32 i;

    for (i = 0; i < count; i++) {
        u32 word = iq_raw[i];
        s16 iv = (s16)(word & 0xFFFF);
        s16 qv = (s16)((word >> 16) & 0xFFFF);

        if (iv > SAT_THRESHOLD || iv < -SAT_THRESHOLD)
            return 1;
        if (qv > SAT_THRESHOLD || qv < -SAT_THRESHOLD)
            return 1;
    }
    return 0;
}