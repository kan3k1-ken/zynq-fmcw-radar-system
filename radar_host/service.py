"""Threaded UDP receiver service shared by desktop and automated tools."""

from __future__ import annotations

import queue
import socket
import threading
from datetime import datetime

from .models import EventKind, ReceiverEvent, ReceiverStats
from .protocol import (
    Detection,
    ProtocolError,
    ScanSummary,
    Status,
    TextChunk,
    AngleMapChunk,
    decode_packet,
)
from .reporting import AngleMapAssembler, ReportAssembler, SummaryDeduplicator


class PacketRouter:
    def __init__(self) -> None:
        self._reports = ReportAssembler()
        self._heatmaps = AngleMapAssembler()
        self._summaries = SummaryDeduplicator()

    def route(
        self,
        data: bytes,
        source: tuple[str, int],
        received_at: datetime | None = None,
    ) -> list[ReceiverEvent]:
        timestamp = datetime.now().astimezone() if received_at is None else received_at
        try:
            packet = decode_packet(data)
        except ProtocolError as error:
            return [ReceiverEvent(
                EventKind.WARNING,
                timestamp,
                source=source,
                message=str(error),
            )]

        if isinstance(packet, ScanSummary):
            if not self._summaries.accept(packet):
                return []
            return [ReceiverEvent(EventKind.PACKET, timestamp, source, packet=packet)]

        if isinstance(packet, TextChunk):
            conflicts_before = self._reports.duplicate_conflicts
            report = self._reports.add(packet)
            if self._reports.duplicate_conflicts != conflicts_before:
                return [ReceiverEvent(
                    EventKind.WARNING,
                    timestamp,
                    source,
                    report_id=packet.report_id,
                    message=(
                        f"report {packet.report_id} chunk {packet.chunk_index} "
                        "duplicate payload mismatch; first copy retained"
                    ),
                )]
            if report is not None:
                self._summaries.reset()
                return [ReceiverEvent(
                    EventKind.REPORT,
                    timestamp,
                    source,
                    report_id=packet.report_id,
                    report_text=report,
                )]
            progress = self._reports.progress(packet.report_id)
            if progress is None:
                return []
            return [ReceiverEvent(
                EventKind.REPORT_PROGRESS,
                timestamp,
                source,
                report_id=packet.report_id,
                report_progress=progress,
            )]

        if isinstance(packet, AngleMapChunk):
            try:
                heatmap = self._heatmaps.add(packet)
            except ValueError as error:
                return [ReceiverEvent(
                    EventKind.WARNING, timestamp, source=source,
                    message=str(error),
                )]
            if heatmap is not None:
                return [ReceiverEvent(
                    EventKind.HEATMAP, timestamp, source=source, heatmap=heatmap,
                )]
            progress = self._heatmaps.progress(packet)
            if progress is None:
                return []
            return [ReceiverEvent(
                EventKind.HEATMAP_PROGRESS, timestamp, source=source,
                report_id=packet.scan_id, heatmap_progress=progress,
            )]

        if isinstance(packet, (Detection, Status)):
            return [ReceiverEvent(EventKind.PACKET, timestamp, source, packet=packet)]

        return [ReceiverEvent(
            EventKind.WARNING,
            timestamp,
            source,
            packet=packet,
            message=f"unsupported packet type 0x{packet.header.packet_type:02X}",
        )]


class RadarReceiverService:
    def __init__(
        self,
        bind_ip: str = "0.0.0.0",
        port: int = 9999,
        queue_size: int = 2048,
    ) -> None:
        self.bind_ip = bind_ip
        self.port = port
        self._events: queue.Queue[ReceiverEvent] = queue.Queue(queue_size)
        self._router = PacketRouter()
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self._socket: socket.socket | None = None
        self._lock = threading.Lock()
        self._packets_received = 0
        self._invalid_packets = 0
        self._integrity_warnings = 0
        self._events_dropped = 0
        self._last_source: tuple[str, int] | None = None
        self._last_packet_at: datetime | None = None

    @property
    def running(self) -> bool:
        return self._thread is not None and self._thread.is_alive()

    def start(self) -> None:
        if self.running:
            return
        receiver_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
                receiver_socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            else:
                receiver_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            receiver_socket.bind((self.bind_ip, self.port))
        except OSError:
            receiver_socket.close()
            raise
        receiver_socket.settimeout(0.25)
        self._socket = receiver_socket
        self._stop_event.clear()
        self._thread = threading.Thread(
            target=self._receive_loop,
            name="radar-udp-receiver",
            daemon=True,
        )
        self._thread.start()
        self._emit(ReceiverEvent(
            EventKind.LISTENING,
            datetime.now().astimezone(),
            message=f"{self.bind_ip}:{self.port}",
        ))

    def stop(self) -> None:
        thread = self._thread
        if thread is None:
            return
        self._stop_event.set()
        thread.join(timeout=1.0)
        receiver_socket = self._socket
        self._socket = None
        self._thread = None
        if receiver_socket is not None:
            receiver_socket.close()
        self._emit(ReceiverEvent(EventKind.STOPPED, datetime.now().astimezone()))

    def drain_events(self, limit: int = 256) -> list[ReceiverEvent]:
        events: list[ReceiverEvent] = []
        for _ in range(limit):
            try:
                events.append(self._events.get_nowait())
            except queue.Empty:
                break
        return events

    def stats(self) -> ReceiverStats:
        with self._lock:
            return ReceiverStats(
                self._packets_received,
                self._invalid_packets,
                self._integrity_warnings,
                self._events_dropped,
                self._last_source,
                self._last_packet_at,
            )

    def _receive_loop(self) -> None:
        receiver_socket = self._socket
        if receiver_socket is None:
            return
        while not self._stop_event.is_set():
            try:
                data, source = receiver_socket.recvfrom(65536)
            except socket.timeout:
                continue
            except OSError as error:
                if not self._stop_event.is_set():
                    self._emit(ReceiverEvent(
                        EventKind.WARNING,
                        datetime.now().astimezone(),
                        message=f"socket error: {error}",
                    ))
                break

            received_at = datetime.now().astimezone()
            events = self._router.route(data, source, received_at)
            with self._lock:
                self._packets_received += 1
                self._last_source = source
                self._last_packet_at = received_at
                if events and events[0].kind is EventKind.WARNING:
                    if "duplicate payload mismatch" in (events[0].message or ""):
                        self._integrity_warnings += 1
                    else:
                        self._invalid_packets += 1
            for event in events:
                self._emit(event)

    def _emit(self, event: ReceiverEvent) -> None:
        try:
            self._events.put_nowait(event)
        except queue.Full:
            try:
                self._events.get_nowait()
            except queue.Empty:
                pass
            with self._lock:
                self._events_dropped += 1
            self._events.put_nowait(event)
