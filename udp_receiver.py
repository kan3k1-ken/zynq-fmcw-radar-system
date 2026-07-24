"""
udp_receiver.py - UDP radar data receiver
Listens on UDP port 9999, parses binary radar protocol packets.
Usage: python udp_receiver.py
"""
import argparse
import socket
import sys

from radar_host.protocol import (
    DETECTION_PACKET_SIZE,
    SCAN_SUMMARY_PACKET_SIZE,
    AngleMapChunk,
    Detection,
    ProtocolError,
    ScanSummary,
    TextChunk,
    decode_packet,
)
from radar_host.reporting import AngleMapAssembler, ReportAssembler, SummaryDeduplicator

def main():
    parser = argparse.ArgumentParser(description="UDP Radar Data Receiver")
    parser.add_argument("--port", type=int, default=9999, help="UDP port to listen on")
    parser.add_argument("--ip", type=str, default="0.0.0.0", help="IP to bind")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.ip, args.port))
    print(f"[UDP] Listening on {args.ip}:{args.port} ...")
    print(
        f"[UDP] Protocol: legacy-target={DETECTION_PACKET_SIZE}B "
        f"scan-summary={SCAN_SUMMARY_PACKET_SIZE}B analysis-text=type5"
    )

    detection_count = 0
    summary_count = 0
    reports = ReportAssembler()
    heatmaps = AngleMapAssembler()
    summaries = SummaryDeduplicator()
    while True:
        data, addr = sock.recvfrom(65536)
        try:
            packet = decode_packet(data)
        except ProtocolError as error:
            print(f"[WARN] Invalid packet from {addr}: {error}")
            continue

        if isinstance(packet, Detection):
            detection_count += 1
            print(f"[TARGET {detection_count:04d}] "
                  f"frame={packet.frame_id} range={packet.range_cm:.1f}cm "
                  f"az={packet.azimuth_deg:.1f}deg el={packet.elevation_deg:.1f}deg "
                  f"X={packet.x_cm:.1f} Y={packet.y_cm:.1f} Z={packet.z_cm:.1f} "
                  f"SNR={packet.snr_db:.1f}dB conf={int(packet.confirmed)}")
        elif isinstance(packet, ScanSummary):
            if not summaries.accept(packet):
                continue
            summary_count += 1
            if not packet.target_valid:
                result = "NO TARGET"
            elif packet.cfar_confirmed and not packet.degraded:
                result = "TARGET"
            else:
                result = "RANGE CANDIDATE"
            angle_text = (
                f"az={packet.azimuth_deg:.1f}deg el={packet.elevation_deg:.1f}deg "
                f"{'[CAL]' if packet.angle_valid else '[EST]'} "
                if packet.angle_available else "az=N/A el=N/A "
            )
            print(f"[SCAN {summary_count:04d}] id={packet.scan_id} {result} "
                  f"range={packet.range_cm:.1f}cm {angle_text}"
                  f"SNR={packet.snr_db:.1f}dB cfar={int(packet.cfar_confirmed)} "
                  f"quality={'DEGRADED' if packet.degraded else 'VALID'} "
                  f"frames={packet.frame_count} peaks={packet.peak_count} "
                  f"sat={packet.saturated_frames} near={packet.near_field_bins} "
                  f"input={packet.input_bytes}B packets={packet.packet_count}")
        elif isinstance(packet, TextChunk):
            report = reports.add(packet)
            if report is not None:
                sys.stdout.write(report)
                sys.stdout.flush()
                summaries.reset()
        elif isinstance(packet, AngleMapChunk):
            try:
                heatmap = heatmaps.add(packet)
            except ValueError as error:
                print(f"[WARN] {error} from {addr}")
                continue
            if heatmap is not None:
                axis = "horizontal" if heatmap.map_kind == 1 else "vertical"
                print(
                    f"[ANGLE MAP] scan={heatmap.scan_id} axis={axis} "
                    f"range_bin={heatmap.range_bin} size={heatmap.rows}x{heatmap.cols}"
                )
        else:
            print(
                f"[WARN] Unsupported packet type=0x{packet.header.packet_type:02X} "
                f"size={len(data)} from {addr}"
            )

if __name__ == "__main__":
    main()
