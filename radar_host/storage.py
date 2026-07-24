"""Durable session recording for scan summaries and diagnostic reports."""

from __future__ import annotations

import json
from datetime import datetime
from pathlib import Path

from .protocol import ScanSummary


class SessionRecorder:
    def __init__(self, root: Path = Path("radar_sessions")) -> None:
        started_at = datetime.now().astimezone()
        self.session_id = started_at.strftime("%Y%m%d_%H%M%S_%f")
        self.directory = root / self.session_id
        self.directory.mkdir(parents=True, exist_ok=True)
        self._summary_path = self.directory / "scans.jsonl"

    def record_summary(self, summary: ScanSummary, received_at: datetime) -> None:
        record = {
            "received_at": received_at.isoformat(),
            "scan_id": summary.scan_id,
            "input_bytes": summary.input_bytes,
            "packet_count": summary.packet_count,
            "frame_count": summary.frame_count,
            "peak_count": summary.peak_count,
            "saturated_frames": summary.saturated_frames,
            "near_field_bins": summary.near_field_bins,
            "range_cm": summary.range_cm,
            "azimuth_deg": summary.azimuth_deg,
            "elevation_deg": summary.elevation_deg,
            "x_cm": summary.x_cm,
            "y_cm": summary.y_cm,
            "z_cm": summary.z_cm,
            "snr_db": summary.snr_db,
            "target_valid": summary.target_valid,
            "cfar_confirmed": summary.cfar_confirmed,
            "quality_degraded": summary.degraded,
            "angle_valid": summary.angle_valid,
            "angle_estimate": summary.angle_estimate,
            "summary_flags": summary.flags,
        }
        with self._summary_path.open("a", encoding="utf-8") as output:
            output.write(json.dumps(record, ensure_ascii=True) + "\n")

    def record_report(self, report_id: int, text: str, received_at: datetime) -> Path:
        timestamp = received_at.strftime("%Y%m%d_%H%M%S_%f")
        path = self.directory / f"report_{report_id:08d}_{timestamp}.txt"
        path.write_text(text, encoding="utf-8")
        return path

    def record_heatmap(self, heatmap, received_at: datetime) -> Path:
        timestamp = received_at.strftime("%Y%m%d_%H%M%S_%f")
        axis = "horizontal" if heatmap.map_kind == 1 else "vertical"
        path = self.directory / (
            f"heatmap_{heatmap.scan_id:08d}_{axis}_{timestamp}.pgm"
        )
        header = f"P5\n{heatmap.cols} {heatmap.rows}\n255\n".encode("ascii")
        path.write_bytes(header + heatmap.data)
        return path
