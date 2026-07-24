# Firmware Architecture Target

## Constraints

- Preserve the validated GEM descriptor path in `eth_udp.c`.
- Preserve protocol version 1 and UDP ports 9999, 10000 and 10001.
- Use static or fixed-address storage only in the processing path.
- Keep COM17 operational messages bounded and publish detailed analysis over UDP.
- Require golden-output comparison before and after every extraction step.

## Module Boundaries

### `app_main`

Owns boot sequencing and the top-level state machine only:

`BOOT -> WAIT_START -> RECEIVE -> PROCESS -> PUBLISH -> WAIT_START`

It does not access DMA registers, parse packet bytes or implement signal
processing. Every state transition returns an explicit status code and records
the elapsed time.

### `platform_runtime`

Owns exception installation, PS UART configuration, COM18 status output, DMA
initialization, cache maintenance and timeout policy. This module is the only
owner of `XAxiDma` and the DMA TX/RX buffers currently declared in `main.c`.

### `radar_capture`

Owns UDP/UART input selection, frame-header validation, byte reordering and the
immutable scan descriptor:

```c
typedef struct {
    const uint8_t *data;
    uint32_t data_len;
    uint32_t scan_id;
    uint32_t packet_count;
    uint32_t frame_count;
} radar_scan_t;
```

The first extraction moves the current UDP wait/reset/poll loop here without
changing `eth_udp.c`.

### `radar_pipeline`

Owns the two processing passes and produces measurements, not presentation
text. Existing operators remain in `fft_proc`, `clutter_rem`, `cfar_detect`,
and `frame_buffer`.

```c
typedef struct {
    uint32_t processed_frames;
    uint32_t saturated_frames;
    uint32_t detection_count;
    uint16_t near_field_bins;
    uint16_t peak_bin;
    uint32_t peak_value;
    uint32_t baseline_value;
    uint32_t cfar_near_count;
    float interpolated_range_cm;
    float azimuth_deg;
    float elevation_deg;
} radar_pipeline_result_t;
```

Pipeline configuration becomes a `const radar_pipeline_config_t` instance.
Compile-time memory geometry stays in `radar_config.h`; tunable thresholds move
out of preprocessor conditionals so every result can identify its parameter set.

### `radar_product`

Converts pipeline measurements into a product decision. It owns target-valid,
CFAR-confirmed, SNR and Cartesian-coordinate policy. It must return reason
codes for rejected candidates instead of relying on diagnostic text.

### `radar_report`

Formats bounded diagnostics and publishes text, summary and completion status.
Publishing failures are returned to `app_main`; PROCESS DONE is sent only after
both report and summary publication succeed.

## Memory Ownership

| Region | Owner | Lifetime |
| --- | --- | --- |
| Raw scan DDR | `radar_capture` | One scan |
| DMA TX/RX buffers | `platform_runtime` | Process lifetime |
| Frame matrix | `frame_buffer` | One scan/accumulation window |
| Clutter sums and means | `radar_pipeline` | One scan |
| CFAR histogram/weights | `radar_pipeline` | One scan |
| UDP diagnostic log | `radar_report` | One report |

No module writes another module's storage through a fixed address. Fixed memory
addresses are exposed as typed storage views during initialization.

## Migration Sequence

1. Capture current single-target output as golden JSON and report text.
2. Extract `radar_capture` with byte-for-byte input equivalence checks.
3. Extract `platform_runtime` and retain existing DMA timeout values.
4. Extract `radar_report`; propagate every network send failure.
5. Extract pass orchestration into `radar_pipeline` without changing thresholds.
6. Extract final target selection into `radar_product`.
7. Replace globals with explicit contexts one ownership group at a time.

Each step must produce the same frame count, saturated count, near-field
boundary, peak bin, interpolated range, CFAR count, angles and summary flags for
all captures in the qualification set.

## Algorithm Release Gates

- Detection probability by range and angle cell.
- False alarms per no-target scan.
- Range absolute error and 95th percentile error.
- Azimuth/elevation absolute error and 95th percentile error.
- Repeatability standard deviation for a stationary target.
- Two-target separation success rate.
- Saturated-frame and rejected-scan rate.
- Processing latency, report latency and total scan latency.
- Zero incomplete reports during a 1,000-scan soak test.

Threshold optimization starts only after these metrics can be generated from a
labeled dataset. A single target file is a regression sample, not a tuning set.
