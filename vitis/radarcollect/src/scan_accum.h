/*
 * scan_accum.h
 * Multi-scan accumulation for noise reduction.
 *
 * Accumulates SCAN_ACCUM_N complete 48x48 scans into a u64 buffer,
 * then averages and stores to frame_buffer for angle processing.
 *
 * SNR improvement: 10*log10(sqrt(SCAN_ACCUM_N)) dB
 *   N=4:  +6.0 dB
 *   N=8:  +9.0 dB
 *   N=16: +12.0 dB
 *   N=64: +18.1 dB
 */

#ifndef SCAN_ACCUM_H_
#define SCAN_ACCUM_H_

#include "radar_config.h"

/*
 * Initialize the accumulation buffer to zero.
 * Must be called once before any accumulation.
 */
void scan_accum_init(void);

/*
 * Accumulate one frame's spectrum into the buffer.
 * row, col: spatial position (0..47, 0..47)
 * spectrum: 1024 u32 magnitude values (after bg_cancel)
 */
void scan_accum_add(u32 row, u32 col, const u32 *spectrum);

/*
 * Check if SCAN_ACCUM_N scans have been accumulated.
 * Returns 1 when ready for averaging.
 */
int scan_accum_is_ready(void);

/*
 * Average accumulated data and store to frame_buffer.
 * Divides each bin by scan_count, then calls frame_buffer_store().
 * Returns 1 if frame_buffer is now full.
 */
int scan_accum_average_and_store(void);

/*
 * Reset the accumulation buffer to zero.
 * Also resets the internal scan counter.
 */
void scan_accum_reset(void);

/*
 * Get current accumulation count (number of fully completed scans).
 */
u32 scan_accum_count(void);

#endif /* SCAN_ACCUM_H_ */