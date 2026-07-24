/*
 * fft_proc.h
 * Software FFT for angle-domain processing.
 * 48-point input → zero-pad to 64 → radix-2 FFT
 */

#ifndef FFT_PROC_H_
#define FFT_PROC_H_

#include "radar_config.h"

/*
 * Radix-2 complex FFT (in-place, decimation-in-time).
 * n: must be power of 2
 * real, imag: input/output arrays of length n
 */
void fft_radix2(float *real, float *imag, int n);

/*
 * Compute magnitude spectrum from FFT result.
 * real, imag: FFT output arrays of length n
 * mag: output magnitude array of length n
 */
void fft_magnitude(const float *real, const float *imag, int n, float *mag);

/*
 * Horizontal angle processing for one range bin.
 * For each of the 48 rows, does 48→64 FFT across columns.
 * Result: h_angle[48][64] = horizontal angle spectrum per row.
 * Caller must provide h_angle as float[48][64].
 */
void angle_fft_horizontal_complex(const u32 *frame_matrix, u32 range_bin,
                                  float *angle_map, float *spectrum,
                                  u32 *valid_cuts);

/*
 * Vertical angle processing for one range bin.
 * For each of the 48 columns, does 48→64 FFT across rows.
 * Result: v_angle[48][64] = vertical angle spectrum per column.
 * Caller must provide v_angle as float[48][64].
 */
void angle_fft_vertical_complex(const u32 *frame_matrix, u32 range_bin,
                                float *angle_map, float *spectrum,
                                u32 *valid_cuts);

int angle_spectrum_peak(const float *spectrum, float *peak_bin,
                        float *peak_value, float *baseline,
                        float *peak_ratio);

#endif /* FFT_PROC_H_ */
