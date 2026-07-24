# Complex-IQ Planar AoA Implementation

## Data Contract

- Active hardware design: `soc`; `design_1` is the simulation design.
- Input per array element: 64 complex samples, signed 16-bit I and Q.
- Range transform: 64 complex samples followed by zero padding to 2048 points.
  In `soc`, the path is `DMA MM2S -> stream_padder -> XFFT`; the padder is
  configured for 256 input bytes and 8192 output bytes, so it appends exactly
  1984 zero-valued complex beats and asserts `TLAST` on beat 2048.
- XFFT output: signed 28-bit unscaled I and Q in natural order. `mod_fft`
  explicitly sign-extends bits `[27:0]` and `[59:32]`; AXI padding bits are
  never interpreted as signal data.
- DMA output remains 2048 words / 8192 bytes.
- Word layout: signed I in bits 15:0 and signed Q in bits 31:16.
- Quantization: arithmetic shift right by 6 with signed saturation. Because
  only 64 samples are non-zero, this bounds each full-scale FFT component to
  the signed 16-bit range without avoidable packed-IQ clipping.
- Word 2047 is `0x43584936` (`CXI6`) and is outside the positive-frequency
  0..1023 range used by the algorithm.

Firmware verifies `CXI6` before processing. This intentionally prevents a new
ELF from silently interpreting a legacy power-only bitstream as complex data.

## Range Chain

The legacy PL power was approximately `(I28^2 + Q28^2) >> 9`. With each
component shifted by six, firmware calculates `8 * (I16^2 + Q16^2)`, which
preserves the same nominal scale. Existing clutter removal, noise-floor
subtraction, OS-CFAR, prominence checks, and range conversion therefore retain
their configured scale. Saturated raw frames or packed-component clipping are
excluded from both range and angle processing.

## Angle Chain

The 2304 frames are mapped row-major to a 48x48 planar array. At the selected
range gate:

1. Each valid row forms a horizontal complex 48-element cut.
2. Each valid column forms a vertical complex 48-element cut.
3. A triangular spatial window is applied and each cut is zero-padded to 64.
4. Complex 64-point FFT power is normalized by the valid-element count.
5. Valid cuts are non-coherently averaged to one azimuth and one elevation
   spectrum, avoiding cancellation from an unknown orthogonal-axis phase.
6. Peak bins use wrapped parabolic interpolation and are converted through
   planar-array direction cosines for half-wavelength spacing.

Angle output requires at least 75% valid array elements, at least 24 samples
per accepted cut, at least 24 accepted cuts on each axis, a 4x median peak
ratio on both spectra, and a physical direction-cosine pair.

## Calibration Boundary

The three existing TXT captures prove that coherent phase exists and exercise
the estimator, but they do not provide known azimuth/elevation truth. Therefore
the firmware emits `ANGLE_ESTIMATE` and the host prefixes values with `~` or
`[EST]`. Set `RADAR_AOA_PHASE_CALIBRATED=1` only after known-angle captures
establish per-element phase corrections, row/column orientation, axis signs,
and zero-angle offsets. This is validation/calibration, not unfinished signal
processing code.

## User Build Sequence

1. Repackage or refresh the local `mod_fft` IP from `ip/mod_fft_1.0`.
2. Regenerate the `soc` block-design output products.
3. Run synthesis/implementation and generate the bitstream/XSA.
4. Rebuild `vitis/radarcollect` against the refreshed hardware platform.
5. Program the matching bitstream and ELF together.

The first processed frame must pass the `CXI6` marker check. A mismatch is an
intentional fail-safe indicating that the bitstream and firmware do not match.
