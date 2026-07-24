/*
 * frame_buffer.h
 * 48x48 spectral matrix manager for 2D radar processing.
 *
 * Stores one 1024-bin packed complex spectrum per (row, col) position,
 * forming a 48x48x1024 matrix. Each word contains signed I16/Q16.
 */

#ifndef FRAME_BUFFER_H_
#define FRAME_BUFFER_H_

#include "radar_config.h"

/*
 * Initialize the frame buffer.
 * Must be called once before storing any frames.
 */
void frame_buffer_init(void);

/*
 * Store one frame's FFT spectrum into the matrix.
 * row, col: spatial position in 48x48 scan
 * spectrum: 1024 packed complex words from the PL range FFT
 * Returns 1 if matrix is now full (SCAN_TOTAL_FRAMES reached), 0 otherwise.
 */
int frame_buffer_store_complex(u32 row, u32 col, const u32 *spectrum,
                               int valid);

/*
 * Check if 48x48 matrix is complete.
 */
int frame_buffer_is_full(void);

/*
 * Get the current frame count.
 */
u32 frame_buffer_count(void);

/*
 * Get the full 3D matrix: spectrum[row][col][bin]
 * Returns a pointer to packed complex words in the internal DDR buffer.
 */
const u32 *frame_buffer_get_matrix(void);

int frame_buffer_element_valid(u32 row, u32 col);

u32 frame_buffer_valid_count(void);

/*
 * Get one row of spectra for horizontal angle FFT.
 * row: 0..47
 * bin: 0..1023
 * out: array of 48 packed complex words (one per column)
 */
void frame_buffer_get_row(u32 row, u32 bin, u32 *out);

/*
 * Get one column of spectra for vertical angle FFT.
 * col: 0..47
 * bin: 0..1023
 * out: array of 48 packed complex words (one per row)
 */
void frame_buffer_get_col(u32 col, u32 bin, u32 *out);

/*
 * Find the top N range bins by total energy (summed over all frames).
 * Returns the number of bins found (up to max_bins).
 * Sorted descending by energy.
 * NOTE: This method is biased toward near-field/clutter energy.
 *       Prefer frame_buffer_top_bins_by_cfar for target detection.
 */
u32 frame_buffer_top_bins(u32 *bins, u32 max_bins);

/*
 * Find the top N range bins by CFAR peak histogram.
 * Uses CFAR detection frequency across all frames to select bins
 * that contain real targets (not clutter/noise).
 *
 * histogram: u32 array of length FFT_HALF_SIZE, each entry = count
 *            of CFAR detections at that bin across all frames.
 * bins:      output array for top bin indices
 * max_bins:  maximum number of bins to return (typically TOP_RANGE_BINS)
 * Returns:   number of bins found
 */
u32 frame_buffer_top_bins_by_cfar(const u32 *histogram, u32 *bins,
                                  u32 max_bins);

#endif /* FRAME_BUFFER_H_ */
