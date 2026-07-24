# Radar System Industrialization Roadmap

## Baseline

The current release baseline is the validated UDP path:

- PC to board raw scan: UDP 10000
- PC to board control: UDP 10001
- Board to PC results and reports: UDP 9999
- Protocol version: 1
- Scan geometry: 48 x 48 frames
- Input capture size: 589835 bytes for the current recorded scan

Protocol version 1 and the working GEM transport must remain behaviorally stable
while the application and algorithm layers are reorganized.

## Target Architecture

### Firmware

1. `platform`: DMA, GEM, UART, cache and memory-map ownership.
2. `transport`: capture state machine, protocol validation and result publisher.
3. `pipeline`: frame preparation, FFT passes, clutter removal, CFAR and angle processing.
4. `product`: target selection, quality metrics and scan result construction.
5. `app`: boot, state transitions, supervision and fault policy only.

The first firmware refactor should extract the UDP capture loop from `main.c`.
Algorithm extraction follows only after golden-output regression tests exist.

### Host Application

1. `radar_host.protocol`: the only binary protocol implementation.
2. `radar_host.transport`: receiver thread, replay control and connection health.
3. `radar_host.models`: scan, target, health and report state.
4. `radar_host.ui`: live target view, range profile, Cartesian plot and diagnostics.
5. `radar_host.storage`: timestamped session recording and export.

The CLI receiver remains a supported diagnostic tool and uses the same protocol
package as the GUI.

## Algorithm Qualification

Algorithm changes require a labeled capture set. Each sample needs:

- capture path and capture timestamp
- target count
- reference range, azimuth and elevation for each target
- environment identifier and clutter condition
- expected no-target regions
- sensor configuration and firmware version

Minimum qualification set:

- no-target background captures
- single target at multiple ranges and angles
- two closely spaced targets
- weak target near strong target
- near-field and saturation cases
- repeated scans for stability and false-alarm measurement

Release metrics include range error, angle error, detection probability, false
alarms per scan, processing latency, saturation rate and transport completeness.

## Delivery Phases

1. Shared protocol package and protocol tests.
2. Host receiver service and industrial desktop UI.
3. Dataset manifest, replay automation and golden result comparison.
4. Firmware capture/pipeline/product module extraction.
5. Parameter tuning and algorithm qualification against labeled data.
6. Long-duration soak test, fault injection and release packaging.
