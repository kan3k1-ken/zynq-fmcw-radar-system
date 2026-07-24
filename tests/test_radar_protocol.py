import struct
import unittest
import zlib

from radar_host.protocol import (
    HEADER,
    MAGIC,
    SCAN_SUMMARY,
    SCAN_SUMMARY_PACKET_SIZE,
    SUMMARY_FLAG_ANGLE_VALID,
    SUMMARY_FLAG_ANGLE_ESTIMATE,
    SUMMARY_FLAG_DEGRADED,
    TEXT_CHUNK,
    VERSION,
    PacketType,
    ProtocolError,
    ScanSummary,
    TextChunk,
    build_control_start,
    build_angle_map_chunk,
    build_raw_chunk,
    decode_packet,
)
from radar_host.reporting import ReportAssembler, SummaryDeduplicator
from radar_host.reporting import AngleMapAssembler


def packet(packet_type: int, payload: bytes, sequence: int = 1) -> bytes:
    return HEADER.pack(MAGIC, VERSION, packet_type, sequence, 0, len(payload), 0) + payload


class ProtocolTests(unittest.TestCase):
    def test_control_start_round_trip_header(self) -> None:
        data = build_control_start(1234, timestamp_ms=99)
        magic, version, packet_type, sequence, timestamp, payload_length, _ = HEADER.unpack_from(data)
        self.assertEqual(magic, MAGIC)
        self.assertEqual(version, VERSION)
        self.assertEqual(packet_type, PacketType.CONTROL)
        self.assertEqual(sequence, 1234)
        self.assertEqual(timestamp, 99)
        self.assertEqual(payload_length, 8)

    def test_decode_scan_summary(self) -> None:
        self.assertEqual(SCAN_SUMMARY_PACKET_SIZE, 72)
        payload = SCAN_SUMMARY.pack(
            7, 589835, 577, 2304, 1132, 354, 110,
            90.4, 0.0, 0.0, 90.4, 0.0, 0.0, 3.0, 1, 0,
            bytes([SUMMARY_FLAG_ANGLE_VALID | SUMMARY_FLAG_ANGLE_ESTIMATE |
                   SUMMARY_FLAG_DEGRADED, 0]),
        )
        decoded = decode_packet(packet(PacketType.SCAN_SUMMARY, payload))
        self.assertIsInstance(decoded, ScanSummary)
        self.assertEqual(decoded.scan_id, 7)
        self.assertEqual(decoded.frame_count, 2304)
        self.assertTrue(decoded.target_valid)
        self.assertFalse(decoded.cfar_confirmed)
        self.assertTrue(decoded.angle_valid)
        self.assertTrue(decoded.angle_estimate)
        self.assertTrue(decoded.angle_available)
        self.assertTrue(decoded.degraded)

    def test_build_raw_chunk_crc_and_metadata(self) -> None:
        data = build_raw_chunk(
            scan_id=9,
            total_length=6,
            offset=0,
            chunk_index=0,
            chunk_count=1,
            flags=3,
            scan_crc32=0x12345678,
            data=b"radar!",
            timestamp_ms=100,
        )
        decoded = decode_packet(data)
        self.assertEqual(decoded.header.packet_type, PacketType.RAW_CHUNK)
        self.assertEqual(decoded.header.timestamp_ms, 100)
        self.assertEqual(len(decoded.payload), 34)

    def test_angle_map_chunk_round_trip(self) -> None:
        data = build_angle_map_chunk(
            scan_id=8, range_bin=771, map_kind=1, rows=2, cols=3,
            chunk_index=0, chunk_count=1, value_min=0.0, value_max=10.0,
            map_crc32=0xAABBCCDD, data=b"abcdef", timestamp_ms=101,
        )
        decoded = decode_packet(data)
        self.assertEqual(decoded.map_kind, 1)
        self.assertEqual(decoded.rows * decoded.cols, len(decoded.data))
        self.assertEqual(decoded.header.timestamp_ms, 101)

    def test_angle_map_reassembly_and_crc(self) -> None:
        raw = bytes(range(12))
        checksum = zlib.crc32(raw) & 0xFFFFFFFF
        packets = [
            decode_packet(build_angle_map_chunk(
                scan_id=9, range_bin=500, map_kind=1, rows=3, cols=4,
                chunk_index=index, chunk_count=2, value_min=1.0, value_max=8.0,
                map_crc32=checksum, data=part, timestamp_ms=1,
            ))
            for index, part in enumerate((raw[:7], raw[7:]))
        ]
        assembler = AngleMapAssembler()
        self.assertIsNone(assembler.add(packets[1], now=1.0))
        heatmap = assembler.add(packets[0], now=1.1)
        self.assertEqual(heatmap.data, raw)
        self.assertEqual((heatmap.rows, heatmap.cols), (3, 4))

    def test_reassemble_out_of_order_duplicate_chunks(self) -> None:
        assembler = ReportAssembler()
        second = decode_packet(packet(PacketType.TEXT, TEXT_CHUNK.pack(2, 1, 2) + b"world"))
        first = decode_packet(packet(PacketType.TEXT, TEXT_CHUNK.pack(2, 0, 2) + b"hello "))
        self.assertIsInstance(first, TextChunk)
        self.assertIsNone(assembler.add(second, now=1.0))
        self.assertIsNone(assembler.add(second, now=1.1))
        self.assertEqual(assembler.add(first, now=1.2), "hello world")

    def test_active_report_expiry_tracks_latest_chunk(self) -> None:
        assembler = ReportAssembler(expiry_seconds=2.0)
        chunks = [
            decode_packet(packet(PacketType.TEXT, TEXT_CHUNK.pack(3, index, 3) + data))
            for index, data in enumerate((b"a", b"b", b"c"))
        ]
        self.assertIsNone(assembler.add(chunks[0], now=1.0))
        self.assertIsNone(assembler.add(chunks[1], now=2.5))
        self.assertEqual(assembler.progress(3), (2, 3))
        self.assertEqual(assembler.add(chunks[2], now=4.0), "abc")

    def test_conflicting_duplicate_keeps_first_copy(self) -> None:
        assembler = ReportAssembler()
        first = decode_packet(packet(PacketType.TEXT, TEXT_CHUNK.pack(5, 0, 2) + b"good "))
        conflicting = decode_packet(packet(PacketType.TEXT, TEXT_CHUNK.pack(5, 0, 2) + b"wrong "))
        final = decode_packet(packet(PacketType.TEXT, TEXT_CHUNK.pack(5, 1, 2) + b"report"))
        self.assertIsNone(assembler.add(first, now=1.0))
        self.assertIsNone(assembler.add(conflicting, now=1.1))
        self.assertEqual(assembler.duplicate_conflicts, 1)
        self.assertEqual(assembler.add(final, now=1.2), "good report")

    def test_summary_duplicate_window(self) -> None:
        payload = SCAN_SUMMARY.pack(
            7, 1, 1, 1, 1, 0, 0,
            1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1, 1, b"\0\0",
        )
        summary = decode_packet(packet(PacketType.SCAN_SUMMARY, payload))
        deduplicator = SummaryDeduplicator()
        self.assertTrue(deduplicator.accept(summary, now=1.0))
        self.assertFalse(deduplicator.accept(summary, now=1.5))
        self.assertTrue(deduplicator.accept(summary, now=4.0))

    def test_reject_bad_magic(self) -> None:
        data = struct.pack("<IBBHIHH", 0, VERSION, PacketType.STATUS, 0, 0, 0, 0)
        with self.assertRaises(ProtocolError):
            decode_packet(data)

    def test_reject_payload_length_mismatch(self) -> None:
        data = HEADER.pack(MAGIC, VERSION, PacketType.STATUS, 0, 0, 12, 0) + b"short"
        with self.assertRaises(ProtocolError):
            decode_packet(data)


if __name__ == "__main__":
    unittest.main()
