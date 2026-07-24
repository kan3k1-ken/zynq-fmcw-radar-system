/*
 * output_fmt.h
 * Structured output formatting with physical units.
 *
 * Converts raw radar data (bin indices, magnitudes) into
 * human-readable physical units (cm, dB, angle) for UART output.
 *
 * Output format:
 *   1D:  F<frame>: D=<dist>cm SNR=<snr>dB [N: D=<d1>|D=<d2>...]
 *   2D:  T<N>: D=<dist>cm H=<h_ang>° V=<v_ang>° SNR=<snr>dB
 */

#ifndef OUTPUT_FMT_H_
#define OUTPUT_FMT_H_

#include "radar_config.h"
#include "cfar_detect.h"

/*
 * Convert range bin index to distance in cm.
 * R = bin * c / (2 * B * 32)
 * Result: centimeters (float)
 */
float fmt_dist_cm(u32 bin);

/*
 * Estimate SNR for a peak in dB.
 * SNR = 10 * log10(signal_power / noise_power)
 * noise_floor is estimated from the spectrum excluding DC and peaks.
 */
float fmt_snr_db(u32 signal_mag, const u32 *spectrum);

float fmt_power_ratio_db(float ratio);

/*
 * Output 1D CFAR results for one frame.
 * Prints one line: F<frame>: D=<dist>cm SNR=<snr>dB [<n>: <dists>]
 */
void output_1d_frame(u32 frame_idx, const peak_info_t *peaks,
                     int num_peaks, const u32 *spectrum);

/*
 * Convert angle FFT bin to degrees.
 * sin(θ) = bin / (ANGLE_FFT_SIZE * ANTENNA_D_OVER_LAMBDA)
 * For d=λ/2: sin(θ) = bin / 32, θ = asin(sin_θ) * 180/π
 */
float fmt_angle_deg(u32 bin);

int fmt_planar_angles(float horizontal_bin, float vertical_bin,
                      float *azimuth_deg, float *elevation_deg);

/*
 * Output 2D CFAR target list.
 * is_vertical: 0 for horizontal, 1 for vertical.
 * Shows angle in degrees, deduplicates same-bin targets.
 */
void output_2d_targets(const target_info_t *targets, int num_targets,
                       int is_vertical);

/*
 * Output 2D processing header (scan info, top bins).
 */
void output_2d_header(const u32 *top_bins, u32 nb);

/*
 * Convert spherical (range, azimuth, elevation) to Cartesian (x, y, z).
 * Prints 3D coordinates in a formatted block.
 * Convention: X=cross-range, Y=down-range, Z=height.
 */
void output_cartesian(float range_cm, float az_deg, float el_deg);

/*
 * Print CFAR peak histogram statistics.
 * Shows detection rate, sparsity, and top 10 range bins
 * by detection frequency.
 */
void output_cfar_histogram(const u32 *histogram, u32 total_frames);

#endif /* OUTPUT_FMT_H_ */
