/*
 * bg_cancel.h
 * Background cancellation for self-coupling compensation.
 *
 * FMCW radar TX-RX direct coupling produces a strong static signal
 * that masks weak targets.  By capturing the empty-scene spectrum
 * as a background and subtracting it from each frame, the real
 * target signals become more prominent.
 *
 * Background stored at BG_CANCEL_BASE in DDR (48x48x1024 u32).
 */

#ifndef BG_CANCEL_H_
#define BG_CANCEL_H_

#include "radar_config.h"

/*
 * Initialize background buffer to zero.
 * Must be called once before any capture/apply.
 */
void bg_cancel_init(void);

/*
 * Capture one frame's spectrum as background for position (row, col).
 * Call this during calibration mode, when no target is present.
 * spectrum: 1024 u32 magnitude values from PL FFT.
 */
void bg_cancel_capture(u32 row, u32 col, const u32 *spectrum);

/*
 * Apply background cancellation to one frame's spectrum.
 * For each bin: output[bin] = max(0, spectrum[bin] - background[bin])
 * spectrum: 1024 u32 magnitude values from PL FFT (input)
 * output:   1024 u32 cleaned values (output, can be same as spectrum)
 */
void bg_cancel_apply(u32 row, u32 col, const u32 *spectrum, u32 *output);

/*
 * Check if background has been fully captured (all 48x48 positions).
 */
int bg_cancel_is_calibrated(void);

/*
 * Get background value at a specific position.
 * For debugging / verification only.
 */
u32 bg_cancel_get(u32 row, u32 col, u32 bin);

#endif /* BG_CANCEL_H_ */