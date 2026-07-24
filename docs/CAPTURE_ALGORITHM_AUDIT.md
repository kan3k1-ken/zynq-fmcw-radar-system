# Capture and Algorithm Audit

Date: 2026-07-23

## Scope

The audit uses the three captures in `vitis/radarcollect` and the same declared
geometry as the active `soc` design:

- 64 signed I16/Q16 samples per array element.
- Zero padding to a 2048-point range FFT.
- Row-major 48x48 planar array.
- 48 spatial samples windowed and zero-padded to a 64-point angle FFT.
- Half-wavelength element spacing.

Run the repeatable audit with:

```powershell
python vitis\radarcollect\audit_captures.py --no-cfar
```

The NumPy range FFT is an algorithm proxy, not a bit-exact Xilinx XFFT model.
It is suitable for capture classification and stable signed-bin regression.

## Capture Results

| Capture | Frames | Raw saturated | Spatially valid | Range evidence | AoA decision |
| --- | ---: | ---: | ---: | --- | --- |
| `8_head.txt` | 767 | 173 | 594/2304 | Strong peak near 30.7 cm | Rejected: incomplete 48x48 array |
| `medium.txt` | 2304 | 821 | 1483/2304 (64.4%) | Near-field/interference dominated | Rejected: below 75% valid-element gate |
| `单目标.txt` | 2304 | 354 | 1950/2304 (84.6%) | Repeatable peak near 90.2 cm | Complex AoA estimate accepted |

At the single-target range gate, the offline firmware-equivalent spatial chain
produces a horizontal signed bin near `-12.0` and a vertical signed bin near
`-21.25`. Peak-to-median ratios are approximately `51.9x` and `47.4x`. These
are golden regression values with a tolerance of 0.75 bin.

## Implemented Corrections

- Noise-floor estimation preserves the original input when source and
  destination alias.
- Saturated frames are excluded from clutter, range detection, and spatial
  processing.
- The PL retains complex FFT phase as packed I16/Q16 instead of power only.
- The active `soc` stream padder expands 64 DMA beats to 2048 XFFT beats;
  zero padding is implemented in hardware rather than assumed by software.
- The 64-sample unscaled range FFT is shifted by six before I16 saturation.
  Firmware multiplies reconstructed power by eight, preserving the historical
  `(I28^2 + Q28^2) >> 9` range-chain scale.
- The `CXI6` marker prevents incompatible bitstream/ELF combinations.
- Horizontal and vertical cuts are independently normalized and
  non-coherently combined before wrapped peak interpolation.
- Planar direction-cosine geometry rejects non-physical bin pairs.
- The host distinguishes uncalibrated estimates from calibrated angles.

## Qualification Boundary

The three files validate data flow, range repeatability, spatial coherence, and
quality-gate behavior. They cannot establish physical azimuth/elevation
accuracy because none has known angle truth or per-channel phase calibration.

The single-target bins currently map to an uncalibrated estimate near azimuth
`-30.1 deg` and elevation `-41.6 deg`. This must remain `ANGLE_ESTIMATE`, not
`ANGLE_VALID`. Set `RADAR_AOA_PHASE_CALIBRATED=1` only after known-angle data
determines channel phase corrections, array orientation, signs, and offsets.

Industrial qualification still requires labeled empty-scene, multi-range,
positive/negative azimuth and elevation, repeated-scan, two-target, weak-near-
strong, and saturation datasets. Threshold tuning against only these three
captures would overfit interference rather than prove detection performance.
