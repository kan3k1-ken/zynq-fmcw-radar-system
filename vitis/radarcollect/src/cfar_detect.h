/*
 * cfar_detect.h
 * CFAR (Constant False Alarm Rate) target detection.
 * Industrial-grade implementation supporting:
 *   CA-CFAR, OS-CFAR, GO-CFAR, SO-CFAR
 *   Peak interpolation (parabolic, sub-bin)
 *   Median noise-floor estimation
 */

#ifndef CFAR_DETECT_H_
#define CFAR_DETECT_H_

#include "radar_config.h"

typedef struct {
    u32   freq_index;
    u32   magnitude;
    float interp_pos;
    float interp_mag;
} peak_info_t;

typedef struct {
    u32   range_bin;
    float distance_cm;
    float interp_range_cm;
    u32   h_angle_bin;
    u32   v_angle_bin;
    float magnitude;
} target_info_t;

void noise_floor_estimate(const u32 *src, u32 *dst, u32 len);

int cfar_detect_1d(const u32 *spectrum, peak_info_t *peaks, u32 start_bin);

int cfar_detect_2d(const float *map, u32 spatial_dim, u32 angle_dim,
                   target_info_t *targets, u32 max_targets);

/*
 * Peak validation: reject false detections.
 *
 * spectrum:       cleaned spectrum for -3dB width check (post-noise-floor)
 * spectrum_snr:   pre-noise-floor spectrum for SNR noise estimation
 *                 (clutter-removed but not noise-floor-subtracted)
 *
 * Returns number of peaks after validation.
 */
int peak_validate(peak_info_t *peaks, int num_peaks,
                  const u32 *spectrum, const u32 *spectrum_snr);

#endif /* CFAR_DETECT_H_ */
