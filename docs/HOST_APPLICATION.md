# Radar Operations Console

## Start

Close `udp_receiver.py` before starting the desktop application because both
programs own UDP port 9999.

```powershell
cd <repository-root>
python radar_monitor.py
```

The application starts listening on `0.0.0.0:9999`. Replay a scan from a second
PowerShell window:

```powershell
cd <repository-root>
python udp_scan_replay.py "vitis\radarcollect\单目标.txt" --ip 192.168.1.10 --pace-us 3000
```

## Session Output

Every application launch creates `radar_sessions/<timestamp>/` containing:

- `scans.jsonl`: one structured scan summary per line.
- `report_<id>_<timestamp>.txt`: the complete detailed algorithm report.

The UI suppresses the board's three summary retransmissions and duplicate text
chunks while preserving distinct scan IDs. Invalid protocol packets are shown
in the status bar and counted rather than terminating the receiver.

## Spatial Views

The `Top View` uses Cartesian coordinates from the scan result. `Angle
Heatmaps` displays the board's normalized horizontal and vertical 48x64 angle
FFT maps; the marked angle is an energy peak and is labeled `UNCONFIRMED` until
angle CFAR validates it. `3D Point Trace` accumulates recent target points
across scans and marks estimated-angle points separately.
