"""
Replay a recorded radar TXT capture to the Zynq over UDP.

The TXT file is converted back to its original byte stream, including the
"AllDataBack" frame header. The Zynq therefore uses the same parser and
processing pipeline as the legacy UART path.
"""

import argparse
import math
import socket
import time
import zlib
from pathlib import Path

from radar_host.protocol import (
    RAW_FLAG_FIRST,
    RAW_FLAG_LAST,
    ProtocolError,
    Status,
    StatusCode,
    build_control_start,
    build_raw_chunk,
    decode_packet,
)

FRAME_HEADER = b"AllDataBack"


def load_capture(path: Path) -> bytes:
    tokens = path.read_text(encoding="utf-8").split()
    data = bytes(int(token, 16) for token in tokens)
    if not data.startswith(FRAME_HEADER):
        raise ValueError(f"{path} does not start with {FRAME_HEADER!r}")
    return data


def send_scan(sock: socket.socket, destination: tuple[str, int], data: bytes,
              scan_id: int, chunk_size: int, pace_us: int,
              chunk_repeats: int, first_chunk_repeats: int,
              first_chunk_gap_ms: int) -> None:
    chunk_count = math.ceil(len(data) / chunk_size)
    scan_crc32 = zlib.crc32(data) & 0xFFFFFFFF
    start = time.monotonic()

    for chunk_index in range(chunk_count):
        offset = chunk_index * chunk_size
        chunk = data[offset:offset + chunk_size]
        flags = 0
        if chunk_index == 0:
            flags |= RAW_FLAG_FIRST
        if chunk_index == chunk_count - 1:
            flags |= RAW_FLAG_LAST

        packet = build_raw_chunk(
            scan_id=scan_id,
            total_length=len(data),
            offset=offset,
            chunk_index=chunk_index,
            chunk_count=chunk_count,
            flags=flags,
            scan_crc32=scan_crc32,
            data=chunk,
        )
        repeats = max(chunk_repeats, first_chunk_repeats) if chunk_index == 0 else chunk_repeats
        for attempt in range(repeats):
            sock.sendto(packet, destination)
            if attempt + 1 < repeats:
                gap_us = first_chunk_gap_ms * 1000 if chunk_index == 0 else pace_us
                if gap_us:
                    time.sleep(gap_us / 1_000_000)
        if pace_us and chunk_index + 1 < chunk_count:
            time.sleep(pace_us / 1_000_000)

    elapsed = time.monotonic() - start
    rate_mib_s = len(data) / elapsed / (1024 * 1024) if elapsed else 0.0
    print(
        f"[REPLAY] scan={scan_id} bytes={len(data)} chunks={chunk_count} "
        f"datagrams={chunk_count * chunk_repeats + max(first_chunk_repeats - chunk_repeats, 0)} "
        f"crc32=0x{scan_crc32:08X} elapsed={elapsed:.3f}s rate={rate_mib_s:.2f}MiB/s"
    )


def wait_for_ready(sock: socket.socket, destination: tuple[str, int],
                   session_id: int, retries: int, timeout_ms: int) -> None:
    packet = build_control_start(session_id)

    for attempt in range(1, retries + 1):
        print(f"[REPLAY] START session={session_id} attempt={attempt}/{retries}")
        sock.sendto(packet, destination)
        deadline = time.monotonic() + timeout_ms / 1000

        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            sock.settimeout(remaining)
            try:
                response, address = sock.recvfrom(256)
            except socket.timeout:
                break

            try:
                status = decode_packet(response)
            except ProtocolError:
                continue
            if (isinstance(status, Status) and
                    status.code == StatusCode.READY and
                    status.session_id == session_id):
                print(f"[REPLAY] READY from {address[0]}:{address[1]}")
                sock.settimeout(None)
                return

    raise TimeoutError(
        f"target did not return READY after {retries} START attempts; "
        "verify the board is waiting for UDP control"
    )


def wait_for_process_done(sock: socket.socket, session_id: int, timeout_s: float) -> None:
    deadline = time.monotonic() + timeout_s
    started = time.monotonic()
    print("[REPLAY] raw scan sent; waiting for board processing and report upload...")

    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(
                f"target did not confirm processing completion within {timeout_s:.0f}s; "
                "the board may still be analyzing or needs the updated ELF"
            )
        sock.settimeout(min(remaining, 5.0))
        try:
            response, address = sock.recvfrom(256)
        except socket.timeout:
            print(f"[REPLAY] board still processing ({time.monotonic() - started:.0f}s elapsed)")
            continue

        try:
            status = decode_packet(response)
        except ProtocolError:
            continue
        if (isinstance(status, Status) and
                status.code == StatusCode.PROCESS_DONE and
                status.session_id == session_id):
            print(f"[REPLAY] PROCESS DONE scan={status.detail} from {address[0]}:{address[1]}")
            sock.settimeout(None)
            return


def main() -> None:
    parser = argparse.ArgumentParser(description="Replay radar TXT data to Zynq UDP ingress")
    parser.add_argument("capture", nargs="?", default="medium.txt", type=Path,
                        help="TXT capture to replay (default: medium.txt)")
    parser.add_argument("--ip", default="192.168.1.10", help="Zynq IP address")
    parser.add_argument("--port", type=int, default=10000, help="Zynq raw-scan UDP port")
    parser.add_argument("--scan-id", type=int,
                        help="first scan identifier (default: a unique value per run)")
    parser.add_argument("--chunk-size", type=int, default=1024,
                        help="payload bytes per UDP chunk (1..1400)")
    parser.add_argument("--pace-us", type=int, default=3000,
                        help="delay between chunks; 3000us is the reliable default")
    parser.add_argument("--chunk-repeats", type=int, default=2,
                        help="send every raw chunk this many times (default: 2)")
    parser.add_argument("--first-chunk-repeats", type=int, default=3,
                        help="number of scan-start datagrams sent before chunk 1")
    parser.add_argument("--first-chunk-gap-ms", type=int, default=250,
                        help="delay between scan-start retries for ARP resolution")
    parser.add_argument("--control-port", type=int, default=10001,
                        help="Zynq UDP control port for START/READY")
    parser.add_argument("--ready-timeout-ms", type=int, default=2000,
                        help="wait time per READY attempt")
    parser.add_argument("--ready-retries", type=int, default=5,
                        help="maximum START retries before failing")
    parser.add_argument("--process-timeout-s", type=float, default=300.0,
                        help="wait for board processing completion (default: 300 seconds)")
    parser.add_argument("--no-wait-process", action="store_true",
                        help="exit after raw transfer instead of waiting for processing completion")
    parser.add_argument("--loop", action="store_true", help="replay continuously")
    parser.add_argument("--interval", type=float, default=1.0,
                        help="delay between scans in loop mode")
    args = parser.parse_args()

    if not 1 <= args.chunk_size <= 1400:
        parser.error("--chunk-size must be between 1 and 1400")
    if (args.pace_us < 0 or args.interval < 0 or
            args.chunk_repeats < 1 or args.first_chunk_repeats < 1 or
            args.first_chunk_gap_ms < 0):
        parser.error("pacing values must be non-negative")
    if not 1 <= args.control_port <= 65535:
        parser.error("--control-port must be between 1 and 65535")
    if args.ready_timeout_ms < 1 or args.ready_retries < 1 or args.process_timeout_s <= 0:
        parser.error("READY timeout and retries must be positive")

    data = load_capture(args.capture)
    print(f"[REPLAY] loaded {args.capture}: {len(data)} bytes")

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        scan_id = args.scan_id
        if scan_id is None:
            scan_id = int(time.monotonic_ns() & 0xFFFFFFFF)
            if scan_id == 0:
                scan_id = 1
        while True:
            wait_for_ready(sock, (args.ip, args.control_port), scan_id,
                           args.ready_retries, args.ready_timeout_ms)
            send_scan(sock, (args.ip, args.port), data, scan_id,
                      args.chunk_size, args.pace_us,
                      args.chunk_repeats, args.first_chunk_repeats,
                      args.first_chunk_gap_ms)
            if not args.no_wait_process:
                wait_for_process_done(sock, scan_id, args.process_timeout_s)
            if not args.loop:
                break
            scan_id = (scan_id + 1) & 0xFFFFFFFF
            time.sleep(args.interval)


if __name__ == "__main__":
    main()
