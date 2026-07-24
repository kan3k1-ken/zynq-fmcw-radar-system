# Hardware-Validated Release Baseline

## Purpose

This repository publishes the last known hardware-validated radar system state.
The FPGA and bare-metal firmware sources are intentionally frozen while the
development board is unavailable. Repository maintenance must not be presented
as a new on-board validation result.

## Included evidence

- Three raw captures: `8_head.txt`, `medium.txt` and `单目标.txt`.
- Offline capture and AoA audit in `vitis/radarcollect/audit_captures.py`.
- Host-side protocol, service and visualization tests in `tests/`.
- Hardware, protocol and processing documentation in `docs/`.

The current offline test baseline is 27 passing tests. These tests validate host
logic and capture-processing proxies; they do not substitute for a hardware
regression of PL, AXI DMA, cache, GEM or PHY behavior.

## Validated behavior

- UDP scan sessions use `START`, `READY`, scan chunks, result reports and
  `PROCESS DONE` acknowledgements.
- Raw scans are frame-checked, reordered and processed by the Vitis application.
- Detailed analysis is sent over UDP while COM17 remains a bounded operational log.
- The single-target capture has repeatable range evidence near 90.2 cm and a
  phase-based planar-array estimate.

## Qualification limits

- The angle result remains `ESTIMATE / PHASE UNCALIBRATED`.
- The supplied captures do not establish calibrated azimuth/elevation accuracy,
  probability of detection, false-alarm rate or multi-target resolution.
- No modification to `b220.srcs/`, `ip/`, `soc.tcl`, `soc_wrapper.xsa` or
  `vitis/radarcollect/src/` may be described as hardware validated until the
  board-side workflow is run again.

## Source integrity check

Before a hardware-oriented change, record a baseline hash for the board-facing
sources and compare it after the change:

```powershell
Get-ChildItem b220.srcs, ip, vitis\radarcollect\src -Recurse -File |
  Get-FileHash -Algorithm SHA256 |
  Sort-Object Path |
  Format-Table -AutoSize
```

`soc.tcl` and `soc_wrapper.xsa` should be included in the same review. Host-only
changes can be checked with the offline test suite; they still must not claim a
new hardware result.

## Release rules

1. Keep generated Vivado/Vitis artifacts out of version control.
2. Keep raw captures and the offline test suite versioned.
3. Run the offline tests before every host/documentation release.
4. Tag this snapshot as a hardware-validated baseline before creating an
   experimental branch for future board-side work.
