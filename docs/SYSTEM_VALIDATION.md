# System Validation Summary

## End-to-End Workflow

The system uses a scan-oriented Ethernet workflow:

1. The PC replay client sends a `START` command.
2. The Zynq firmware returns `READY` after the UDP receiver is prepared.
3. The replay client sends the raw scan in numbered, CRC-protected chunks.
4. Firmware validates and reconstructs the scan, then runs the range and spatial
   processing pipeline.
5. The board publishes structured target data, diagnostic text and angle-map data.
6. The host application reassembles the report and presents range, angle and 3D views.

## Capture Dataset Results

| Capture | Frames | Processing role | Result |
| --- | ---: | --- | --- |
| `8_head.txt` | 767 | Frame layout and decode check | Header and I/Q decode verified |
| `medium.txt` | 2304 | Interference and quality-gate exercise | Near-field/interference behavior characterized |
| `单目标.txt` | 2304 | End-to-end range and spatial processing | Repeatable range evidence near 90.2 cm with phase-based angle estimate |

## Processing Outputs

- Acquisition statistics: bytes, packets, frame count and frame-header position.
- Signal quality: saturation count, valid-frame count and near-field guard.
- Range processing: clutter-removed profile, detection-input profile, peak bin,
  interpolated range and CFAR support.
- Spatial processing: 48 x 48 planar data organization, horizontal/vertical cuts,
  angle-spectrum peaks and azimuth/elevation estimates.
- Product reporting: range, Cartesian coordinates, quality fields and structured
  UDP report messages.

## Automated Validation

The offline suite covers 27 checks across four areas:

- Raw capture classification and spatial peak regression.
- UDP packet encoding, CRC, ordering, duplicate handling and report reassembly.
- Host packet routing, completion events and diagnostic warnings.
- Angle-bin conversion, heatmap generation and 3D point projection.

Run the full host-side verification with:

```powershell
python -m unittest discover -s tests -v
python vitis\radarcollect\audit_captures.py --no-cfar
```
