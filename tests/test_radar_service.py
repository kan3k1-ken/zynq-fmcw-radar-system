import unittest
import zlib
from datetime import datetime, timezone

from radar_host.models import EventKind
from radar_host.protocol import (
    HEADER, MAGIC, SCAN_SUMMARY, TEXT_CHUNK, VERSION, PacketType,
    build_angle_map_chunk,
)
from radar_host.service import PacketRouter


def packet(packet_type: int, payload: bytes, sequence: int = 1) -> bytes:
    return HEADER.pack(MAGIC, VERSION, packet_type, sequence, 0, len(payload), 0) + payload


class PacketRouterTests(unittest.TestCase):
    def setUp(self) -> None:
        self.router = PacketRouter()
        self.source = ("192.168.1.10", 8888)
        self.now = datetime(2026, 1, 1, tzinfo=timezone.utc)

    def test_summary_duplicates_are_suppressed(self) -> None:
        payload = SCAN_SUMMARY.pack(
            7, 589835, 577, 2304, 1132, 354, 110,
            90.4, 0.0, 0.0, 0.0, 90.4, 0.0, 3.0, 1, 1, b"\0\0",
        )
        data = packet(PacketType.SCAN_SUMMARY, payload)
        first = self.router.route(data, self.source, self.now)
        second = self.router.route(data, self.source, self.now)
        self.assertEqual(first[0].kind, EventKind.PACKET)
        self.assertEqual(second, [])

    def test_text_report_emits_progress_then_complete(self) -> None:
        first = packet(PacketType.TEXT, TEXT_CHUNK.pack(4, 0, 2) + b"radar ")
        second = packet(PacketType.TEXT, TEXT_CHUNK.pack(4, 1, 2) + b"report")
        progress = self.router.route(first, self.source, self.now)
        complete = self.router.route(second, self.source, self.now)
        self.assertEqual(progress[0].report_progress, (1, 2))
        self.assertEqual(complete[0].kind, EventKind.REPORT)
        self.assertEqual(complete[0].report_text, "radar report")
        self.assertEqual(self.router.route(second, self.source, self.now), [])

    def test_invalid_packet_becomes_warning(self) -> None:
        events = self.router.route(b"invalid", self.source, self.now)
        self.assertEqual(events[0].kind, EventKind.WARNING)

    def test_conflicting_report_duplicate_becomes_warning(self) -> None:
        first = packet(PacketType.TEXT, TEXT_CHUNK.pack(6, 0, 2) + b"first")
        conflicting = packet(PacketType.TEXT, TEXT_CHUNK.pack(6, 0, 2) + b"other")
        self.router.route(first, self.source, self.now)
        events = self.router.route(conflicting, self.source, self.now)
        self.assertEqual(events[0].kind, EventKind.WARNING)
        self.assertIn("first copy retained", events[0].message)

    def test_angle_map_emits_complete_event(self) -> None:
        values = bytes(range(12))
        data = build_angle_map_chunk(
            scan_id=10, range_bin=771, map_kind=1, rows=3, cols=4,
            chunk_index=0, chunk_count=1, value_min=0.0, value_max=11.0,
            map_crc32=zlib.crc32(values) & 0xFFFFFFFF, data=values,
            timestamp_ms=1,
        )
        events = self.router.route(data, self.source, self.now)
        self.assertEqual(events[0].kind, EventKind.HEATMAP)
        self.assertEqual(events[0].heatmap.data, values)


if __name__ == "__main__":
    unittest.main()
