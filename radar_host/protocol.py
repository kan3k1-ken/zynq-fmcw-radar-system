"""Typed encoder and decoder for radar UDP protocol version 1."""

from __future__ import annotations

import enum
import struct
import time
import zlib
from dataclasses import dataclass


MAGIC = 0x52414441
VERSION = 1

HEADER = struct.Struct("<IBBHIHH")
DETECTION = struct.Struct("<IfffffffB3s")
STATUS = struct.Struct("<III")
SCAN_SUMMARY = struct.Struct("<IIIIIHHfffffffBB2s")
TEXT_CHUNK = struct.Struct("<IHH")
ANGLE_MAP_CHUNK = struct.Struct("<IHBBHHHHHHffI")
RAW_CHUNK = struct.Struct("<IIIHHHHII")
CONTROL = struct.Struct("<II")

HEADER_SIZE = HEADER.size
DETECTION_PACKET_SIZE = HEADER.size + DETECTION.size
SCAN_SUMMARY_PACKET_SIZE = HEADER.size + SCAN_SUMMARY.size

CONTROL_START = 0x00000001
RAW_FLAG_FIRST = 0x0001
RAW_FLAG_LAST = 0x0002
ANGLE_MAP_HORIZONTAL = 1
ANGLE_MAP_VERTICAL = 2
ANGLE_MAP_ENCODING_U8_NORMALIZED = 1
SUMMARY_FLAG_ANGLE_VALID = 0x01
SUMMARY_FLAG_DEGRADED = 0x02
SUMMARY_FLAG_ANGLE_ESTIMATE = 0x04


class PacketType(enum.IntEnum):
    DETECTION = 0x01
    SPECTRUM = 0x02
    STATUS = 0x03
    SCAN_SUMMARY = 0x04
    TEXT = 0x05
    RAW_CHUNK = 0x10
    CONTROL = 0x11


class StatusCode(enum.IntEnum):
    READY = 0x00000001
    CAPTURE_DONE = 0x00000002
    ERROR = 0x00000003
    PROCESS_DONE = 0x00000004


class ProtocolError(ValueError):
    pass


@dataclass(frozen=True)
class Header:
    packet_type: int
    sequence: int
    timestamp_ms: int
    payload_length: int


@dataclass(frozen=True)
class Detection:
    header: Header
    frame_id: int
    range_cm: float
    azimuth_deg: float
    elevation_deg: float
    x_cm: float
    y_cm: float
    z_cm: float
    snr_db: float
    confirmed: bool


@dataclass(frozen=True)
class Status:
    header: Header
    code: int
    session_id: int
    detail: int


@dataclass(frozen=True)
class ScanSummary:
    header: Header
    scan_id: int
    input_bytes: int
    packet_count: int
    frame_count: int
    peak_count: int
    saturated_frames: int
    near_field_bins: int
    range_cm: float
    azimuth_deg: float
    elevation_deg: float
    x_cm: float
    y_cm: float
    z_cm: float
    snr_db: float
    target_valid: bool
    cfar_confirmed: bool
    flags: int

    @property
    def angle_valid(self) -> bool:
        return bool(self.flags & SUMMARY_FLAG_ANGLE_VALID)

    @property
    def angle_estimate(self) -> bool:
        return bool(self.flags & SUMMARY_FLAG_ANGLE_ESTIMATE)

    @property
    def angle_available(self) -> bool:
        return self.angle_valid or self.angle_estimate

    @property
    def degraded(self) -> bool:
        return bool(self.flags & SUMMARY_FLAG_DEGRADED)


@dataclass(frozen=True)
class TextChunk:
    header: Header
    report_id: int
    chunk_index: int
    chunk_count: int
    data: bytes


@dataclass(frozen=True)
class AngleMapChunk:
    header: Header
    scan_id: int
    range_bin: int
    map_kind: int
    encoding: int
    rows: int
    cols: int
    chunk_index: int
    chunk_count: int
    value_min: float
    value_max: float
    map_crc32: int
    data: bytes


@dataclass(frozen=True)
class UnknownPacket:
    header: Header
    payload: bytes


Packet = Detection | Status | ScanSummary | TextChunk | AngleMapChunk | UnknownPacket


def _timestamp_ms() -> int:
    return int(time.monotonic() * 1000) & 0xFFFFFFFF


def _encode_header(packet_type: int, sequence: int, timestamp_ms: int,
                   payload_length: int) -> bytes:
    return HEADER.pack(
        MAGIC,
        VERSION,
        packet_type,
        sequence & 0xFFFF,
        timestamp_ms & 0xFFFFFFFF,
        payload_length,
        0,
    )


def build_control_start(session_id: int, timestamp_ms: int | None = None) -> bytes:
    payload = CONTROL.pack(CONTROL_START, session_id)
    timestamp = _timestamp_ms() if timestamp_ms is None else timestamp_ms
    return _encode_header(PacketType.CONTROL, session_id, timestamp, len(payload)) + payload


def build_raw_chunk(scan_id: int, total_length: int, offset: int,
                    chunk_index: int, chunk_count: int, flags: int,
                    scan_crc32: int, data: bytes,
                    timestamp_ms: int | None = None) -> bytes:
    chunk_crc32 = zlib.crc32(data) & 0xFFFFFFFF
    raw_header = RAW_CHUNK.pack(
        scan_id,
        total_length,
        offset,
        chunk_index,
        chunk_count,
        len(data),
        flags,
        chunk_crc32,
        scan_crc32,
    )
    timestamp = _timestamp_ms() if timestamp_ms is None else timestamp_ms
    payload_length = len(raw_header) + len(data)
    return (
        _encode_header(PacketType.RAW_CHUNK, chunk_index, timestamp, payload_length)
        + raw_header
        + data
    )


def build_angle_map_chunk(
    scan_id: int,
    range_bin: int,
    map_kind: int,
    rows: int,
    cols: int,
    chunk_index: int,
    chunk_count: int,
    value_min: float,
    value_max: float,
    map_crc32: int,
    data: bytes,
    timestamp_ms: int | None = None,
) -> bytes:
    metadata = ANGLE_MAP_CHUNK.pack(
        scan_id,
        range_bin,
        map_kind,
        ANGLE_MAP_ENCODING_U8_NORMALIZED,
        rows,
        cols,
        chunk_index,
        chunk_count,
        len(data),
        0,
        value_min,
        value_max,
        map_crc32,
    )
    timestamp = _timestamp_ms() if timestamp_ms is None else timestamp_ms
    return (
        _encode_header(PacketType.SPECTRUM, chunk_index, timestamp, len(metadata) + len(data))
        + metadata
        + data
    )


def decode_packet(data: bytes) -> Packet:
    if len(data) < HEADER.size:
        raise ProtocolError(f"packet is {len(data)} bytes; header requires {HEADER.size}")

    magic, version, packet_type, sequence, timestamp, payload_length, _ = HEADER.unpack_from(data)
    if magic != MAGIC:
        raise ProtocolError(f"bad magic 0x{magic:08X}")
    if version != VERSION:
        raise ProtocolError(f"unsupported protocol version {version}")
    if payload_length != len(data) - HEADER.size:
        raise ProtocolError(
            f"payload length {payload_length} does not match {len(data) - HEADER.size}"
        )

    header = Header(packet_type, sequence, timestamp, payload_length)
    payload = memoryview(data)[HEADER.size:]

    if packet_type == PacketType.DETECTION:
        if len(payload) != DETECTION.size:
            raise ProtocolError(f"detection payload has invalid size {len(payload)}")
        frame_id, rng, azimuth, elevation, x, y, z, snr, confirmed, _ = DETECTION.unpack(payload)
        return Detection(
            header, frame_id, rng, azimuth, elevation, x, y, z, snr, bool(confirmed)
        )

    if packet_type == PacketType.STATUS:
        if len(payload) != STATUS.size:
            raise ProtocolError(f"status payload has invalid size {len(payload)}")
        code, session_id, detail = STATUS.unpack(payload)
        return Status(header, code, session_id, detail)

    if packet_type == PacketType.SCAN_SUMMARY:
        if len(payload) != SCAN_SUMMARY.size:
            raise ProtocolError(f"scan summary payload has invalid size {len(payload)}")
        values = SCAN_SUMMARY.unpack(payload)
        reserved = values[16]
        return ScanSummary(
            header=header,
            scan_id=values[0],
            input_bytes=values[1],
            packet_count=values[2],
            frame_count=values[3],
            peak_count=values[4],
            saturated_frames=values[5],
            near_field_bins=values[6],
            range_cm=values[7],
            azimuth_deg=values[8],
            elevation_deg=values[9],
            x_cm=values[10],
            y_cm=values[11],
            z_cm=values[12],
            snr_db=values[13],
            target_valid=bool(values[14]),
            cfar_confirmed=bool(values[15]),
            flags=reserved[0],
        )

    if packet_type == PacketType.TEXT:
        if len(payload) < TEXT_CHUNK.size:
            raise ProtocolError(f"text payload has invalid size {len(payload)}")
        report_id, chunk_index, chunk_count = TEXT_CHUNK.unpack_from(payload)
        if chunk_count == 0 or chunk_index >= chunk_count:
            raise ProtocolError(f"invalid text chunk {chunk_index}/{chunk_count}")
        return TextChunk(
            header,
            report_id,
            chunk_index,
            chunk_count,
            bytes(payload[TEXT_CHUNK.size:]),
        )

    if packet_type == PacketType.SPECTRUM:
        if len(payload) < ANGLE_MAP_CHUNK.size:
            raise ProtocolError(f"angle map payload has invalid size {len(payload)}")
        values = ANGLE_MAP_CHUNK.unpack_from(payload)
        (scan_id, range_bin, map_kind, encoding, rows, cols,
         chunk_index, chunk_count, data_length, _reserved,
         value_min, value_max, map_crc32) = values
        chunk_data = bytes(payload[ANGLE_MAP_CHUNK.size:])
        if map_kind not in (ANGLE_MAP_HORIZONTAL, ANGLE_MAP_VERTICAL):
            raise ProtocolError(f"invalid angle map kind {map_kind}")
        if encoding != ANGLE_MAP_ENCODING_U8_NORMALIZED:
            raise ProtocolError(f"unsupported angle map encoding {encoding}")
        if rows == 0 or cols == 0 or rows * cols > 65535:
            raise ProtocolError(f"invalid angle map dimensions {rows}x{cols}")
        if chunk_count == 0 or chunk_index >= chunk_count:
            raise ProtocolError(f"invalid angle map chunk {chunk_index}/{chunk_count}")
        if data_length != len(chunk_data):
            raise ProtocolError(
                f"angle map chunk length {data_length} does not match {len(chunk_data)}"
            )
        return AngleMapChunk(
            header, scan_id, range_bin, map_kind, encoding, rows, cols,
            chunk_index, chunk_count, value_min, value_max, map_crc32, chunk_data,
        )

    return UnknownPacket(header, bytes(payload))
