/*
 * radar_config.h
 * Centralized radar system parameters.
 * Single source of truth for all tunable constants.
 */

#ifndef RADAR_CONFIG_H_
#define RADAR_CONFIG_H_

#include "xil_types.h"

/* ============================================================
 * System Parameters
 * ============================================================ */
#define RX_STORE_BASE       0x10000000u
#define RX_STORE_SIZE       0x04000000u
#define SPECTRUM_BASE       (RX_STORE_BASE + 0x02000000u)
#define SPECTRUM_SIZE       (8192 * 10000u)
#define PEAK_STORE_BASE     (RX_STORE_BASE + 0x07000000u)
#define PEAK_STORE_SIZE     (100000u * sizeof(u32))

#define DMA_PKT_SIZE        8192
#define DMA_DATA_WORDS      (DMA_PKT_SIZE / sizeof(u32))
#define IQ_SAMPLES_PER_ELEMENT 64u
#define DMA_TX_BYTES        (IQ_SAMPLES_PER_ELEMENT * sizeof(u32))
#define DMA_TX_WORDS        (DMA_TX_BYTES / sizeof(u32))
#define FFT_OUTPUT_BYTES    DMA_PKT_SIZE

/* ============================================================
 * FFT Parameters (PL: soc_xfft_0_0)
 * ============================================================ */
#define FFT_SIZE            2048
#define FFT_HALF_SIZE       (FFT_SIZE / 2)
#define FFT_OUTPUT_WORDS    (FFT_OUTPUT_BYTES / sizeof(u32))
#define PL_COMPLEX_FORMAT_MAGIC 0x43584936u
#define PL_FFT_COMPONENT_SHIFT  6
#define PL_POWER_RESCALE_SHIFT  3
#if (2 * PL_FFT_COMPONENT_SHIFT - PL_POWER_RESCALE_SHIFT) != 9
#error "Packed complex scaling must preserve the legacy FFT power >> 9 scale"
#endif
#define SAMPLE_RATE_HZ      1600000u

/* ============================================================
 * Radar Parameters (FMCW)
 *   B = 4.0 GHz, T = 40 us
 *   Range resolution: c/(2*B) = 3.75 cm
 *   Distance per bin: 0.1171875 cm/bin
 * ============================================================ */
#define SPEED_OF_LIGHT_MPS  300000000u
#define BANDWIDTH_HZ        4000000000u
#define CHIRP_DURATION_US   40u

/* Distance per bin = c / (2 * B * 32) = 0.1171875 cm */
#define PER_BIN_CM_F  ((float)SPEED_OF_LIGHT_MPS * 100.0f /     \
                       (2.0f * (float)BANDWIDTH_HZ * 32.0f))

/* ============================================================
 * UART / Frame Parameters
 * ============================================================ */
#define FRAME_HEADER        "AllDataBack"
#define FRAME_HEADER_LEN    11
#define BYTES_PER_ELEMENT   DMA_TX_BYTES
#define IDLE_TIMEOUT        5000000

/* Input source selection. UDP replay is the default release path; UART
 * remains available as a compatibility fallback without changing PL. */
#define RADAR_INPUT_UART    0
#define RADAR_INPUT_UDP     1
#ifndef RADAR_INPUT_MODE
#define RADAR_INPUT_MODE    RADAR_INPUT_UDP
#endif

/* ============================================================
 * Scan Parameters (48x48 raster scan)
 * ============================================================ */
#define SCAN_ROWS           48
#define SCAN_COLS           48
#define SCAN_TOTAL_FRAMES   (SCAN_ROWS * SCAN_COLS)

/* ============================================================
 * Angle FFT Parameters
 *   48-point FFT → zero-pad to 64 for radix-2
 *   Angle resolution depends on element spacing (d/lambda)
 * ============================================================ */
#define ANGLE_FFT_SIZE      64
#define ANGLE_FFT_HALF      (ANGLE_FFT_SIZE / 2)
#define ANGLE_ELEMENTS      48
#define ANTENNA_D_OVER_LAMBDA 0.5f  /* d/λ = 0.5 (half-wavelength element spacing) */

/* ============================================================
 * Pre-Filter Parameters (Near-Field Blanking)
 *
 *   PHYSICS_NEAR_FIELD_BINS: antenna near-field lower bound (physical law)
 *     R_nf = 2*D^2/lambda  (D = antenna aperture, lambda = wavelength)
 *     For 60 GHz (lambda=5mm), D=2cm: R_nf = 2*(0.02)^2/0.005 = 16cm
 *     Bin at 16cm: 16/0.1171875 ≈ 137 → rounded to 100 for safety margin
 *
 *   The ACTUAL near-field boundary is computed ADAPTIVELY from the
 *   clutter profile (per-bin mean) by clutter_rem_find_boundary().
 *   PHYSICS_NEAR_FIELD_BINS is only a lower bound — the adaptive
 *   boundary will never be smaller than this physics-based minimum.
 *
 *   This is industrial-grade: no hardcoded data-dependent bin numbers.
 * ============================================================ */
#define PHYSICS_NEAR_FIELD_BINS  100

/* ============================================================
 * Adaptive Near-Field Boundary Detection
 *
 *   Approach: Peak-Relative Decay
 *     The boundary is the first bin where the clutter profile
 *     drops to BOUNDARY_DB_DOWN (dB) below the profile peak.
 *     This is purely adaptive — it depends on the actual
 *     self-coupling strength, not on noise-floor statistics.
 *
 *     BOUNDARY_DB_DOWN = 20: 20 dB below peak = 1% of peak.
 *     The self-coupling decays rapidly in the near-field;
 *     at 20 dB down, the residual self-coupling is 1% of peak
 *     and is safely removed by the per-bin mean subtraction
 *     (Layer 2).  Higher values (e.g., 30 dB) give a tighter
 *     boundary but risk false positives from profile noise.
 *
 *   BOUNDARY_MAX_BINS: hard upper bound on the adaptive result.
 *     Physics near-field for 60 GHz, 2 cm antenna ≈ 137 bins.
 *     200 bins provides a generous safety margin while preventing
 *     runaway boundaries from unusual profiles.
 *
 *   BOUNDARY_MARGIN: safety margin added to the detected boundary.
 *     Must be >= CFAR_HALF_WIN (10) to prevent the CFAR from
 *     detecting the blanking edge as a false peak.  10 bins ≈ 1.17 cm.
 * ============================================================ */
#define BOUNDARY_DB_DOWN         20
#define BOUNDARY_MAX_BINS        200
#define BOUNDARY_MARGIN          10

/* ============================================================
 * CFAR Parameters (Industrial-Grade)
 *
 *   PFA-based threshold design:
 *     CA-CFAR: T = N * (PFA^(-1/N) - 1), N = 2*CFAR_TRAIN = 16
 *     PFA=1e-3: T=8.64  T_Q8=2212
 *     PFA=1e-4: T=12.45 T_Q8=3187  (recommended production)
 *     PFA=1e-6: T=21.94 T_Q8=5617
 *
 *   CFAR_MODE:
 *     CA (0) - Cell Averaging, homogeneous background
 *     OS (1) - Ordered Statistics, multi-target (RECOMMENDED)
 *     GO (2) - Greatest Of, clutter edges
 *     SO (3) - Smallest Of, closely spaced targets
 *
 *   OS-CFAR k: the k-th smallest value in sorted training cells
 *     (out of 2*CFAR_TRAIN=16).  k=12 selects ~75th percentile.
 *
 *   Noise-floor estimation window:
 *     NOISE_WIN = 64 (was 32).  Wider window spans self-coupling
 *     wide peak (~10 bins) for unbiased median estimation.
 *     NOISE_MEDIAN_K = 24: selects the 24/64 = 37.5th percentile
 *     of the sorted window.  This is deliberately below the median
 *     (50th) to produce a conservative (lower) noise-floor estimate,
 *     increasing detection sensitivity while maintaining a low PFA
 *     because the CFAR threshold multiplier compensates.
 *     Effective samples after guard exclusion: ~59.  Index = 59*24/64 ≈ 22,
 *     which is the 22/59 ≈ 37th percentile of the usable samples.
 * ============================================================ */
#define DC_SKIP_BINS        3
#define CFAR_GUARD          2
#define CFAR_TRAIN          8
#define CFAR_HALF_WIN       (CFAR_GUARD + CFAR_TRAIN)
#define CFAR_MODE           1
#define CFAR_PFA_Q24        2576980u
#define CFAR_THRESH_Q8      2212
#define CFAR_OS_K           12
#define NOISE_WIN           64
#define NOISE_MEDIAN_K      24
#define MAX_PEAKS_PER_FRAME 50
#define MAX_TARGETS         20
/*
 * MERGE_BINS: adjacent-peak merging distance.
 *   Range resolution = c/(2*B) = 3.75 cm = 32 bins.
 *   MERGE_BINS = RANGE_RES_BINS / 8 = 4 bins = 0.47 cm.
 *   Peaks within this distance are considered the same target.
 *   Conservative choice: 1/8 of resolution cell width.
 */
#define RANGE_RES_BINS      32
#define MERGE_BINS          (RANGE_RES_BINS / 8)
#define PEAK_INTERP_ENABLE  1

/* ============================================================
 * Peak Validation (Industrial-Grade)
 *   Rejects false detections by SNR, width, and continuity.
 *   VALIDATE_MIN_SNR_Q8: minimum SNR ratio (Q8 fixed-point)
 *     6.0 dB → 10^(6/20) = 2.00 → Q8 = 2.00*256 = 512
 *   VALIDATE_MIN_WIDTH: minimum peak width in bins (< 2 = noise spike)
 *   VALIDATE_MAX_WIDTH: maximum peak width in bins (> 20 = clutter)
 * ============================================================ */
#define VALIDATE_MIN_SNR_Q8   512
#define VALIDATE_MIN_WIDTH    2
#define VALIDATE_MAX_WIDTH    20
#define PROMINENCE_MIN_RATIO_Q8  768u  /* 3.0x peak-to-baseline */

/* ============================================================
 * Static Clutter Removal (TI Standard, per-bin mean subtraction)
 *   CLUTTER_REM_BASE: DDR address for u64 sum + u32 mean buffers
 *   CLUTTER_REM_SUM:  FFT_HALF_SIZE * sizeof(u64) = 8192 bytes
 *   CLUTTER_REM_MEAN: FFT_HALF_SIZE * sizeof(u32) = 4096 bytes
 *   0x1B000000 ~ 0x1B003000 (12 KB)
 *
 *   Applied to ALL range bins.  Near-field bins (0..adaptive_boundary-1)
 *   are already zeroed by pre-filter; clutter subtraction on them
 *   is a no-op.  Far-field bins benefit from full static removal.
 * ============================================================ */
#define CLUTTER_REM_BASE     0x1B000000u
#define CLUTTER_REM_SUM_SIZE (FFT_HALF_SIZE * sizeof(u64))
#define CLUTTER_REM_MEAN_SIZE (FFT_HALF_SIZE * sizeof(u32))

/* ============================================================
 * CFAR Peak Histogram (for range bin selection)
 *   CFAR_HIST_BASE: histogram of CFAR peak bin frequencies
 *   FFT_HALF_SIZE * sizeof(u32) = 4096 bytes
 *   0x1B004000 ~ 0x1B005000 (4 KB)
 *
 *   CFAR_WGT_BASE: magnitude-weighted histogram
 *   Each CFAR detection adds its peak magnitude to the bin's score.
 *   The bin with the highest total magnitude is the primary target.
 *   This naturally favors strong, consistent targets over weak noise.
 *   0x1B005000 ~ 0x1B006000 (4 KB)
 * ============================================================ */
#define CFAR_HIST_BASE       0x1B004000u
#define CFAR_HIST_SIZE       (FFT_HALF_SIZE * sizeof(u32))
#define CFAR_WGT_BASE        0x1B005000u
#define CFAR_WGT_SIZE        (FFT_HALF_SIZE * sizeof(u32))

/* ============================================================
 * Range Profile Accumulators (dual-view)
 *   RAW:  clutter-removed, pre-noise-floor.  Shows absolute signal
 *         level — used for interference rejection (self-coupling
 *         residual, edge artifacts, saturation effects).
 *   NF:   noise-floor-subtracted.  Shows signal above local noise
 *         floor — used for weak target detection.  This is what
 *         CFAR sees.
 *   RAW: 0x1B006000 ~ 0x1B008000 (8 KB)
 *   NF:  0x1B008000 ~ 0x1B00A000 (8 KB)
 * ============================================================ */
#define RANGE_PROFILE_BASE    0x1B006000u
#define RANGE_PROFILE_SIZE    (FFT_HALF_SIZE * sizeof(u64))
#define RANGE_PROFILE_NF_BASE 0x1B008000u
#define RANGE_PROFILE_NF_SIZE (FFT_HALF_SIZE * sizeof(u64))

/* ============================================================
 * Angle Processing: only process top N range bins
 * ============================================================ */
#define TOP_RANGE_BINS      10

/* PL emits packed complex range FFT samples: I in bits 15:0, Q in 31:16. */
/* Set to 1 only after a known-angle phase calibration has been applied. */
#define RADAR_AOA_PHASE_CALIBRATED   0
#define AOA_MIN_VALID_PERCENT        75u
#define AOA_MIN_CUT_ELEMENTS         24u
#define AOA_MIN_PEAK_RATIO_Q8        1024u
#define AOA_HORIZONTAL_SIGN          1.0f
#define AOA_VERTICAL_SIGN            1.0f
#define AOA_AZIMUTH_OFFSET_DEG       0.0f
#define AOA_ELEVATION_OFFSET_DEG     0.0f

/* ============================================================
 * Background Cancellation (self-coupling compensation)
 *   BG_CANCEL_BASE: DDR address for 48x48x1024 u32 background
 *   0x18000000 ~ 0x188FFFFF (9.4 MB), after PEAK_STORE
 * ============================================================ */
#define BG_CANCEL_BASE      0x18000000u
#define BG_CANCEL_SIZE      (SCAN_ROWS * SCAN_COLS * FFT_HALF_SIZE * sizeof(u32))
#define BG_CALIBRATION_MODE 1

/* ============================================================
 * Multi-Scan Accumulation (noise reduction)
 *   SCAN_ACCUM_N: number of 48x48 scans to accumulate
 *   SCAN_ACCUM_BASE: DDR for u64 accumulator 48x48x1024
 *   0x19000000 ~ 0x1A1FFFFF (18.9 MB), after BG_CANCEL
 *   SNR improvement: 10*log10(sqrt(N)) dB
 *   N=1: no accumulation (passthrough), zero overhead
 * ============================================================ */
#define SCAN_ACCUM_N        1
#define SCAN_ACCUM_BASE     0x19000000u
#define SCAN_ACCUM_SIZE     (SCAN_ROWS * SCAN_COLS * FFT_HALF_SIZE * sizeof(u64))

/* ============================================================
 * Frame Buffer (48x48x1024 float matrix)
 *   0x1A200000 ~ 0x1AAFFFFF (9.4 MB), after SCAN_ACCUM
 * ============================================================ */
#define FRAME_BUFFER_BASE   0x1A200000u
#define FRAME_BUFFER_SIZE   (SCAN_ROWS * SCAN_COLS * FFT_HALF_SIZE * sizeof(u32))

#endif /* RADAR_CONFIG_H_ */
