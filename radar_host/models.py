"""Host application events and runtime statistics."""

from __future__ import annotations

import enum
from dataclasses import dataclass
from datetime import datetime

from .protocol import Packet
from .reporting import AngleHeatmap


class EventKind(enum.Enum):
    LISTENING = "listening"
    STOPPED = "stopped"
    PACKET = "packet"
    REPORT_PROGRESS = "report_progress"
    REPORT = "report"
    HEATMAP_PROGRESS = "heatmap_progress"
    HEATMAP = "heatmap"
    WARNING = "warning"


@dataclass(frozen=True)
class ReceiverEvent:
    kind: EventKind
    received_at: datetime
    source: tuple[str, int] | None = None
    packet: Packet | None = None
    report_id: int | None = None
    report_progress: tuple[int, int] | None = None
    report_text: str | None = None
    heatmap: AngleHeatmap | None = None
    heatmap_progress: tuple[int, int] | None = None
    message: str | None = None


@dataclass(frozen=True)
class ReceiverStats:
    packets_received: int
    invalid_packets: int
    integrity_warnings: int
    events_dropped: int
    last_source: tuple[str, int] | None
    last_packet_at: datetime | None
