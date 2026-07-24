"""Stateful helpers for duplicate suppression and text report assembly."""

from __future__ import annotations

import time
import zlib
from dataclasses import dataclass, field

from .protocol import AngleMapChunk, ScanSummary, TextChunk


@dataclass
class _PartialReport:
    chunk_count: int
    updated_at: float
    chunks: dict[int, bytes] = field(default_factory=dict)


@dataclass
class _PartialAngleMap:
    chunk_count: int
    rows: int
    cols: int
    value_min: float
    value_max: float
    map_crc32: int
    updated_at: float
    chunks: dict[int, bytes] = field(default_factory=dict)


@dataclass(frozen=True)
class AngleHeatmap:
    scan_id: int
    range_bin: int
    map_kind: int
    rows: int
    cols: int
    value_min: float
    value_max: float
    data: bytes


class ReportAssembler:
    def __init__(self, expiry_seconds: float = 30.0) -> None:
        self.expiry_seconds = expiry_seconds
        self._reports: dict[int, _PartialReport] = {}
        self._completed: dict[int, float] = {}
        self.duplicate_conflicts = 0

    def add(self, chunk: TextChunk, now: float | None = None) -> str | None:
        current_time = time.monotonic() if now is None else now
        self._reports = {
            report_id: report
            for report_id, report in self._reports.items()
            if current_time - report.updated_at < self.expiry_seconds
        }
        self._completed = {
            report_id: completed_at
            for report_id, completed_at in self._completed.items()
            if current_time - completed_at < self.expiry_seconds
        }
        if chunk.report_id in self._completed:
            return None

        report = self._reports.get(chunk.report_id)
        if report is None or report.chunk_count != chunk.chunk_count:
            report = _PartialReport(chunk.chunk_count, current_time)
            self._reports[chunk.report_id] = report
        report.updated_at = current_time
        existing = report.chunks.get(chunk.chunk_index)
        if existing is not None:
            if existing != chunk.data:
                self.duplicate_conflicts += 1
            return None
        report.chunks[chunk.chunk_index] = chunk.data

        if len(report.chunks) != report.chunk_count:
            return None
        text = b"".join(report.chunks[index] for index in range(report.chunk_count))
        del self._reports[chunk.report_id]
        self._completed[chunk.report_id] = current_time
        return text.decode("utf-8", errors="replace")

    def progress(self, report_id: int) -> tuple[int, int] | None:
        report = self._reports.get(report_id)
        if report is None:
            return None
        return len(report.chunks), report.chunk_count


class SummaryDeduplicator:
    def __init__(self, window_seconds: float = 2.0) -> None:
        self.window_seconds = window_seconds
        self._last_scan_id: int | None = None
        self._last_seen = 0.0

    def accept(self, summary: ScanSummary, now: float | None = None) -> bool:
        current_time = time.monotonic() if now is None else now
        duplicate = (
            summary.scan_id == self._last_scan_id
            and current_time - self._last_seen < self.window_seconds
        )
        self._last_scan_id = summary.scan_id
        self._last_seen = current_time
        return not duplicate

    def reset(self) -> None:
        self._last_scan_id = None
        self._last_seen = 0.0


class AngleMapAssembler:
    def __init__(self, expiry_seconds: float = 30.0) -> None:
        self.expiry_seconds = expiry_seconds
        self._maps: dict[tuple[int, int, int], _PartialAngleMap] = {}
        self._completed: dict[tuple[int, int, int], float] = {}
        self.duplicate_conflicts = 0

    def add(self, chunk: AngleMapChunk, now: float | None = None) -> AngleHeatmap | None:
        current_time = time.monotonic() if now is None else now
        self._maps = {
            key: partial for key, partial in self._maps.items()
            if current_time - partial.updated_at < self.expiry_seconds
        }
        self._completed = {
            key: completed_at for key, completed_at in self._completed.items()
            if current_time - completed_at < self.expiry_seconds
        }
        key = (chunk.scan_id, chunk.map_kind, chunk.range_bin)
        if key in self._completed:
            return None

        partial = self._maps.get(key)
        metadata = (
            chunk.chunk_count, chunk.rows, chunk.cols,
            chunk.value_min, chunk.value_max, chunk.map_crc32,
        )
        if partial is None or metadata != (
            partial.chunk_count, partial.rows, partial.cols,
            partial.value_min, partial.value_max, partial.map_crc32,
        ):
            partial = _PartialAngleMap(*metadata, current_time)
            self._maps[key] = partial
        partial.updated_at = current_time
        existing = partial.chunks.get(chunk.chunk_index)
        if existing is not None:
            if existing != chunk.data:
                self.duplicate_conflicts += 1
            return None
        partial.chunks[chunk.chunk_index] = chunk.data
        if len(partial.chunks) != partial.chunk_count:
            return None

        data = b"".join(partial.chunks[index] for index in range(partial.chunk_count))
        if len(data) != partial.rows * partial.cols:
            del self._maps[key]
            raise ValueError(f"angle map size {len(data)} != {partial.rows}x{partial.cols}")
        if zlib.crc32(data) & 0xFFFFFFFF != partial.map_crc32:
            del self._maps[key]
            raise ValueError(f"angle map CRC mismatch for scan {chunk.scan_id}")
        del self._maps[key]
        self._completed[key] = current_time
        return AngleHeatmap(
            chunk.scan_id, chunk.range_bin, chunk.map_kind,
            partial.rows, partial.cols, partial.value_min, partial.value_max, data,
        )

    def progress(self, chunk: AngleMapChunk) -> tuple[int, int] | None:
        partial = self._maps.get((chunk.scan_id, chunk.map_kind, chunk.range_bin))
        if partial is None:
            return None
        return len(partial.chunks), partial.chunk_count
