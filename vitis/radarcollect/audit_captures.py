"""Offline audit of the three recorded radar captures.

The FFT in this file is a NumPy proxy for the PL output. It is useful for
comparisons between captures, but it is not presented as bit-exact firmware
reproduction.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


FRAME_HEADER = b"AllDataBack"
FRAME_BYTES = 256
IQ_SAMPLES = 64
FFT_SIZE = 2048
FFT_HALF = FFT_SIZE // 2
SCAN_ROWS = 48
SCAN_COLS = 48
SCAN_FRAMES = SCAN_ROWS * SCAN_COLS
PER_BIN_CM = 0.1171875
SATURATION_THRESHOLD = 32760
PL_POWER_LIMIT = 1 << 41
PL_COMPONENT_SHIFT = 6
DC_SKIP_BINS = 3
DEFAULT_NEAR_FIELD_BINS = 100
CFAR_GUARD = 2
CFAR_TRAIN = 8
CFAR_THRESHOLD = 2212 / 256.0
CFAR_OS_K = 12
ANGLE_FFT_SIZE = 64
ANGLE_MIN_VALID_PERCENT = 75
ANGLE_MIN_CUT_ELEMENTS = 24
ANGLE_MIN_PEAK_RATIO = 4.0
ANTENNA_D_OVER_LAMBDA = 0.5


def decode_capture(path: Path) -> tuple[bytes, int, int]:
    tokens = path.read_text(encoding="utf-8").split()
    data = bytes(int(token, 16) for token in tokens)
    header_offset = data.find(FRAME_HEADER)
    if header_offset < 0:
        raise ValueError(f"{path}: frame header not found")
    return data, header_offset, data.count(FRAME_HEADER)


def parse_iq(data: bytes, header_offset: int) -> tuple[np.ndarray, int, int]:
    payload = data[header_offset + len(FRAME_HEADER):]
    complete_bytes = (len(payload) // FRAME_BYTES) * FRAME_BYTES
    usable = payload[:complete_bytes]
    reordered = np.frombuffer(usable, dtype=np.uint8).reshape(-1, 4)[:, ::-1]
    words = reordered.reshape(-1, 2).view("<i2").reshape(-1, IQ_SAMPLES, 2)
    iq = words[:, :, 0].astype(np.float32) + 1j * words[:, :, 1].astype(np.float32)
    return iq, len(payload), len(payload) - complete_bytes


def range_complex_spectra(iq: np.ndarray) -> np.ndarray:
    return np.fft.fft(iq, n=FFT_SIZE, axis=1)[:, :FFT_HALF].astype(np.complex64)


def range_spectra(iq: np.ndarray) -> np.ndarray:
    spectra = range_complex_spectra(iq)
    return (spectra.real * spectra.real + spectra.imag * spectra.imag).astype(np.float64)


def packed_component_clipping(spectra: np.ndarray) -> np.ndarray:
    scale = float(1 << PL_COMPONENT_SHIFT)
    scaled_real = np.floor(spectra.real / scale)
    scaled_imag = np.floor(spectra.imag / scale)
    return np.any(
        (scaled_real <= -32768.0) | (scaled_real >= 32767.0) |
        (scaled_imag <= -32768.0) | (scaled_imag >= 32767.0),
        axis=1,
    )


def spatial_window() -> np.ndarray:
    center = (SCAN_COLS - 1.0) * 0.5
    return (1.0 - np.abs(np.arange(SCAN_COLS) - center) / (center + 1.0)).astype(
        np.float32
    )


def integrated_spatial_spectrum(
    range_fft: np.ndarray,
    valid: np.ndarray,
    range_bin: int,
    axis: int,
) -> tuple[np.ndarray, int]:
    matrix = range_fft[:SCAN_FRAMES, range_bin].reshape(SCAN_ROWS, SCAN_COLS)
    valid_matrix = valid[:SCAN_FRAMES].reshape(SCAN_ROWS, SCAN_COLS)
    if axis == 0:
        matrix = matrix.T
        valid_matrix = valid_matrix.T

    window = spatial_window()
    integrated = np.zeros(ANGLE_FFT_SIZE, dtype=np.float64)
    accepted = 0
    for values, mask in zip(matrix, valid_matrix, strict=True):
        count = int(np.count_nonzero(mask))
        if count < ANGLE_MIN_CUT_ELEMENTS:
            continue
        cut = np.where(mask, values * window, 0.0)
        transformed = np.fft.fft(cut, n=ANGLE_FFT_SIZE)
        integrated += np.abs(transformed) ** 2 / float(count * count)
        accepted += 1
    if accepted:
        integrated /= float(accepted)
    return integrated, accepted


def interpolated_angle_peak(spectrum: np.ndarray) -> dict[str, float]:
    peak = int(np.argmax(spectrum))
    left = float(spectrum[(peak - 1) % ANGLE_FFT_SIZE])
    center = float(spectrum[peak])
    right = float(spectrum[(peak + 1) % ANGLE_FFT_SIZE])
    denominator = 2.0 * (left - 2.0 * center + right)
    delta = (left - right) / denominator if abs(denominator) > 1.0e-9 else 0.0
    delta = float(np.clip(delta, -0.5, 0.5))
    peak_bin = (peak + delta) % ANGLE_FFT_SIZE
    signed_bin = peak_bin if peak_bin < ANGLE_FFT_SIZE / 2 else peak_bin - ANGLE_FFT_SIZE
    baseline = float(np.median(spectrum))
    ratio = center / baseline if baseline > 0.0 else 0.0
    return {
        "bin": float(peak_bin),
        "signed_bin": float(signed_bin),
        "peak": center,
        "baseline": baseline,
        "ratio": float(ratio),
    }


def planar_angles(horizontal_bin: float, vertical_bin: float) -> tuple[float, float] | None:
    denominator = ANGLE_FFT_SIZE * ANTENNA_D_OVER_LAMBDA
    direction_u = horizontal_bin / denominator
    direction_v = vertical_bin / denominator
    if direction_u * direction_u + direction_v * direction_v >= 1.0:
        return None
    horizontal_scale = np.sqrt(1.0 - direction_v * direction_v)
    azimuth = np.degrees(np.arcsin(direction_u / horizontal_scale))
    elevation = np.degrees(np.arcsin(direction_v))
    return float(azimuth), float(elevation)


def complex_aoa_report(
    range_fft: np.ndarray,
    valid: np.ndarray,
    range_bin: int,
) -> dict[str, object]:
    valid_elements = int(np.count_nonzero(valid[:SCAN_FRAMES]))
    valid_percent = valid_elements * 100.0 / SCAN_FRAMES
    result: dict[str, object] = {
        "range_bin": int(range_bin),
        "range_cm": round(float(range_bin * PER_BIN_CM), 3),
        "valid_elements": valid_elements,
        "valid_percent": round(valid_percent, 3),
        "calibrated": False,
    }
    if len(range_fft) < SCAN_FRAMES:
        result["status"] = "incomplete_array"
        return result
    if valid_percent < ANGLE_MIN_VALID_PERCENT:
        result["status"] = "valid_elements_below_threshold"
        return result

    horizontal, horizontal_cuts = integrated_spatial_spectrum(
        range_fft, valid, range_bin, axis=1
    )
    vertical, vertical_cuts = integrated_spatial_spectrum(
        range_fft, valid, range_bin, axis=0
    )
    horizontal_peak = interpolated_angle_peak(horizontal)
    vertical_peak = interpolated_angle_peak(vertical)
    result.update({
        "horizontal_cuts": horizontal_cuts,
        "vertical_cuts": vertical_cuts,
        "horizontal_peak": horizontal_peak,
        "vertical_peak": vertical_peak,
    })
    if (
        horizontal_cuts < ANGLE_MIN_CUT_ELEMENTS or
        vertical_cuts < ANGLE_MIN_CUT_ELEMENTS or
        horizontal_peak["ratio"] < ANGLE_MIN_PEAK_RATIO or
        vertical_peak["ratio"] < ANGLE_MIN_PEAK_RATIO
    ):
        result["status"] = "angle_quality_gate_failed"
        return result

    angles = planar_angles(
        float(horizontal_peak["signed_bin"]),
        float(vertical_peak["signed_bin"]),
    )
    if angles is None:
        result["status"] = "invalid_direction_cosines"
        return result
    result["status"] = "estimate_uncalibrated"
    result["azimuth_deg"] = round(angles[0], 3)
    result["elevation_deg"] = round(angles[1], 3)
    return result


def local_peak_bins(profile: np.ndarray, start_bin: int, count: int) -> list[int]:
    candidate = profile[start_bin:].copy()
    selected: list[int] = []
    for _ in range(count):
        if candidate.size == 0:
            break
        relative = int(np.argmax(candidate))
        selected_bin = start_bin + relative
        selected.append(selected_bin)
        left = max(0, relative - 4)
        right = min(candidate.size, relative + 5)
        candidate[left:right] = -np.inf
    return selected


def summarize_bins(profile: np.ndarray, bins: list[int]) -> list[dict[str, float | int]]:
    return [
        {
            "bin": int(bin_index),
            "range_cm": round(float(bin_index * PER_BIN_CM), 3),
            "amplitude": int(profile[bin_index]),
        }
        for bin_index in bins
    ]


def nearest_peak_distance(frame_spectra: np.ndarray, start_bin: int) -> np.ndarray:
    return np.argmax(frame_spectra[:, start_bin:], axis=1) + start_bin


def top_counts(bin_values: np.ndarray, limit: int = 8) -> list[dict[str, float | int]]:
    unique, counts = np.unique(bin_values.astype(np.int32), return_counts=True)
    order = np.argsort(counts)[::-1][:limit]
    return [
        {
            "bin": int(unique[index]),
            "range_cm": round(float(unique[index] * PER_BIN_CM), 3),
            "frames": int(counts[index]),
            "percent": round(float(counts[index] * 100.0 / len(bin_values)), 2),
        }
        for index in order
    ]


def noise_floor_profile(profile: np.ndarray, in_place: bool) -> np.ndarray:
    source = profile.copy()
    output = source if in_place else np.empty_like(source)
    window_size = 64
    median_rank = 24
    for center in range(len(source)):
        start = max(0, center - window_size // 2)
        end = min(len(source), center + window_size // 2 + 1)
        distances = np.abs(np.arange(start, end) - center)
        values = source[start:end][distances > CFAR_GUARD]
        values = values[values > 0]
        if values.size == 0:
            output[center] = source[center]
            continue
        sorted_values = np.sort(values)
        rank = min((len(sorted_values) * median_rank) // window_size,
                   len(sorted_values) - 1)
        floor_value = sorted_values[rank]
        output[center] = max(source[center] - floor_value, 0)
    return output


def cfar_proxy_hits(spectra: np.ndarray, clutter_mean: np.ndarray,
                    start_bin: int) -> tuple[np.ndarray, np.ndarray]:
    first_bin = max(start_bin + CFAR_GUARD + CFAR_TRAIN, DC_SKIP_BINS + 10)
    last_bin = FFT_HALF - CFAR_GUARD - CFAR_TRAIN
    detection_counts = np.zeros(FFT_HALF, dtype=np.int64)
    selected_bins: list[int] = []
    for frame_spectrum in spectra:
        cleaned = np.maximum(frame_spectrum - clutter_mean, 0.0)
        candidate_bins = np.arange(first_bin, last_bin)
        left_training = np.stack(
            [cleaned[candidate_bins - delta]
             for delta in range(CFAR_GUARD + 1, CFAR_GUARD + CFAR_TRAIN + 1)],
            axis=1,
        )
        right_training = np.stack(
            [cleaned[candidate_bins + delta]
             for delta in range(CFAR_GUARD + 1, CFAR_GUARD + CFAR_TRAIN + 1)],
            axis=1,
        )
        training = np.concatenate((left_training, right_training), axis=1)
        noise_reference = np.partition(training, CFAR_OS_K, axis=1)[:, CFAR_OS_K]
        detected = cleaned[candidate_bins] > CFAR_THRESHOLD * noise_reference
        detected_bins = candidate_bins[detected]
        detection_counts[detected_bins] += 1
        selected_bins.append(
            int(detected_bins[np.argmax(cleaned[detected_bins])])
            if detected_bins.size else int(candidate_bins[np.argmax(cleaned[candidate_bins])])
        )
    return detection_counts, np.asarray(selected_bins, dtype=np.int32)


def capture_report(path: Path, run_cfar: bool) -> dict:
    data, header_offset, header_count = decode_capture(path)
    iq, payload_bytes, trailing_bytes = parse_iq(data, header_offset)
    complex_spectra = range_complex_spectra(iq)
    spectra = (complex_spectra.real * complex_spectra.real +
               complex_spectra.imag * complex_spectra.imag).astype(np.float64)
    frame_count = len(iq)
    pl_overflow = spectra >= PL_POWER_LIMIT
    saturated = np.any(np.abs(iq.real) >= SATURATION_THRESHOLD, axis=1) | np.any(
        np.abs(iq.imag) >= SATURATION_THRESHOLD, axis=1
    )
    iq_rms = np.sqrt(np.mean(iq.real * iq.real + iq.imag * iq.imag, axis=1))
    raw_profile = np.mean(spectra, axis=0)
    clean_frame_mask = ~saturated
    packed_clipped = packed_component_clipping(complex_spectra)
    spatial_valid_mask = clean_frame_mask & ~packed_clipped
    clutter_mean = np.mean(spectra[clean_frame_mask], axis=0) if np.any(clean_frame_mask) else raw_profile
    residual_spectra = np.maximum(spectra - clutter_mean, 0.0)
    residual_profile = np.mean(residual_spectra, axis=0)
    clean_residual_profile = (
        np.mean(residual_spectra[clean_frame_mask], axis=0)
        if np.any(clean_frame_mask) else residual_profile
    )
    raw_peaks = local_peak_bins(raw_profile, DEFAULT_NEAR_FIELD_BINS, 8)
    residual_peaks = local_peak_bins(residual_profile, DEFAULT_NEAR_FIELD_BINS, 8)
    raw_frame_peaks = nearest_peak_distance(spectra, DEFAULT_NEAR_FIELD_BINS)
    residual_frame_peaks = nearest_peak_distance(residual_spectra, DEFAULT_NEAR_FIELD_BINS)
    noise_copy = noise_floor_profile(residual_profile, in_place=False)
    noise_alias = noise_floor_profile(residual_profile, in_place=True)
    corrected_noise_peaks = local_peak_bins(noise_copy, DEFAULT_NEAR_FIELD_BINS + 10, 8)
    alias_noise_peaks = local_peak_bins(noise_alias, DEFAULT_NEAR_FIELD_BINS + 10, 8)
    differing_noise_bins = int(np.count_nonzero(noise_copy != noise_alias))
    noise_alias_error = float(np.mean(np.abs(noise_copy - noise_alias)))

    report = {
        "file": path.name,
        "text_bytes": path.stat().st_size,
        "decoded_bytes": len(data),
        "header_offset": header_offset,
        "header_count": header_count,
        "payload_bytes": payload_bytes,
        "complete_frames": frame_count,
        "trailing_payload_bytes": trailing_bytes,
        "complete_scan": frame_count >= SCAN_FRAMES and frame_count % SCAN_FRAMES == 0,
        "scan_count": frame_count // SCAN_FRAMES,
        "saturated_frames": int(np.count_nonzero(saturated)),
        "saturated_percent": round(float(np.mean(saturated) * 100.0), 3),
        "packed_component_clipped_frames_proxy": int(np.count_nonzero(packed_clipped)),
        "pl_power_overflow_bins_proxy": int(np.count_nonzero(pl_overflow)),
        "pl_power_overflow_frames_proxy": int(np.count_nonzero(np.any(pl_overflow, axis=1))),
        "pl_power_overflow_frame_percent_proxy": round(
            float(np.mean(np.any(pl_overflow, axis=1)) * 100.0), 3
        ),
        "fft_power_max_proxy": int(np.max(spectra)),
        "iq_rms_mean": round(float(np.mean(iq_rms)), 3),
        "iq_rms_median": round(float(np.median(iq_rms)), 3),
        "iq_rms_p95": round(float(np.percentile(iq_rms, 95)), 3),
        "dc_i_mean": round(float(np.mean(iq.real)), 3),
        "dc_q_mean": round(float(np.mean(iq.imag)), 3),
        "raw_profile_peaks": summarize_bins(raw_profile, raw_peaks),
        "residual_profile_peaks": summarize_bins(residual_profile, residual_peaks),
        "corrected_noise_profile_peaks": summarize_bins(noise_copy, corrected_noise_peaks),
        "in_place_noise_profile_peaks": summarize_bins(noise_alias, alias_noise_peaks),
        "raw_frame_peak_modes": top_counts(raw_frame_peaks),
        "residual_frame_peak_modes": top_counts(residual_frame_peaks),
        "noise_floor_alias_different_bins": differing_noise_bins,
        "noise_floor_alias_mean_abs_error": round(noise_alias_error, 3),
        "saturated_frame_profile_delta_percent": round(
            float(
                np.mean(np.abs(residual_profile - clean_residual_profile))
                * 100.0 / max(float(np.mean(clean_residual_profile)), 1e-6)
            ),
            3,
        ),
        "same_scan_mean_residual_ratio": {
            str(bin_index): round(float(residual_profile[bin_index] / max(raw_profile[bin_index], 1e-6)), 4)
            for bin_index in sorted(set(raw_peaks[:5] + residual_peaks[:5]))
        },
    }

    angle_profile = noise_floor_profile(clean_residual_profile, in_place=False)
    angle_start = DEFAULT_NEAR_FIELD_BINS + CFAR_GUARD + CFAR_TRAIN
    angle_range_bin = int(np.argmax(angle_profile[angle_start:]) + angle_start)
    report["complex_aoa"] = complex_aoa_report(
        complex_spectra, spatial_valid_mask, angle_range_bin
    )

    if run_cfar:
        cfar_counts, cfar_selected = cfar_proxy_hits(
            spectra, clutter_mean, DEFAULT_NEAR_FIELD_BINS
        )
        report["cfar_proxy"] = {
            "threshold": CFAR_THRESHOLD,
            "total_hits": int(np.sum(cfar_counts)),
            "active_bins": int(np.count_nonzero(cfar_counts)),
            "hit_rate_per_frame": round(float(np.sum(cfar_counts) / max(frame_count, 1)), 4),
            "selected_bin_modes": top_counts(cfar_selected),
            "top_bins": summarize_bins(cfar_counts.astype(np.float32),
                                        local_peak_bins(cfar_counts.astype(np.float32),
                                                        DEFAULT_NEAR_FIELD_BINS, 8)),
        }

    if frame_count >= SCAN_FRAMES:
        first_scan = spectra[:SCAN_FRAMES]
        spatial_bins = residual_peaks[:5]
        report["spatial_maps"] = []
        for bin_index in spatial_bins:
            spatial = first_scan[:, bin_index].reshape(SCAN_ROWS, SCAN_COLS)
            max_position = np.unravel_index(int(np.argmax(spatial)), spatial.shape)
            report["spatial_maps"].append({
                "bin": int(bin_index),
                "range_cm": round(float(bin_index * PER_BIN_CM), 3),
                "max": int(np.max(spatial)),
                "median": round(float(np.median(spatial)), 3),
                "p95": round(float(np.percentile(spatial, 95)), 3),
                "max_row": int(max_position[0]),
                "max_col": int(max_position[1]),
            })

    return report


def print_report(report: dict) -> None:
    print(f"\n=== {report['file']} ===")
    print(
        f"decoded={report['decoded_bytes']}B frames={report['complete_frames']} "
        f"trailing={report['trailing_payload_bytes']}B complete_scan={report['complete_scan']}"
    )
    print(
        f"saturation={report['saturated_frames']} "
        f"({report['saturated_percent']:.3f}%) "
        f"IQ_RMS median={report['iq_rms_median']:.1f} p95={report['iq_rms_p95']:.1f} "
        f"DC=({report['dc_i_mean']:.1f},{report['dc_q_mean']:.1f})"
    )
    print(
        "PL power truncation proxy: "
        f"bins={report['pl_power_overflow_bins_proxy']} "
        f"frames={report['pl_power_overflow_frames_proxy']} "
        f"({report['pl_power_overflow_frame_percent_proxy']:.3f}%) "
        f"max_power={report['fft_power_max_proxy']}"
    )
    print("raw profile peaks:")
    for item in report["raw_profile_peaks"][:5]:
        print(f"  bin={item['bin']:4d} range={item['range_cm']:7.2f}cm amp={item['amplitude']}")
    print("after same-scan mean subtraction:")
    for item in report["residual_profile_peaks"][:5]:
        ratio = report["same_scan_mean_residual_ratio"].get(str(item["bin"]), "n/a")
        print(f"  bin={item['bin']:4d} range={item['range_cm']:7.2f}cm amp={item['amplitude']} residual/raw={ratio}")
    print(
        "in-place noise-floor effect: "
        f"{report['noise_floor_alias_different_bins']} bins differ, "
        f"mean abs error={report['noise_floor_alias_mean_abs_error']:.3f}"
    )
    print("correct noise-floor peaks:")
    for item in report["corrected_noise_profile_peaks"][:4]:
        print(f"  bin={item['bin']:4d} range={item['range_cm']:7.2f}cm amp={item['amplitude']}")
    print("current in-place noise-floor peaks:")
    for item in report["in_place_noise_profile_peaks"][:4]:
        print(f"  bin={item['bin']:4d} range={item['range_cm']:7.2f}cm amp={item['amplitude']}")
    print(
        "saturated-frame residual-profile delta: "
        f"{report['saturated_frame_profile_delta_percent']:.2f}%"
    )
    print("raw peak modes:", report["raw_frame_peak_modes"][:4])
    print("residual peak modes:", report["residual_frame_peak_modes"][:4])
    aoa = report["complex_aoa"]
    print(
        f"complex AoA: status={aoa['status']} range_bin={aoa['range_bin']} "
        f"valid={aoa['valid_elements']}/{SCAN_FRAMES} ({aoa['valid_percent']:.1f}%)"
    )
    if "horizontal_peak" in aoa:
        horizontal = aoa["horizontal_peak"]
        vertical = aoa["vertical_peak"]
        print(
            f"  H signed_bin={horizontal['signed_bin']:.3f} ratio={horizontal['ratio']:.2f}x "
            f"cuts={aoa['horizontal_cuts']}"
        )
        print(
            f"  V signed_bin={vertical['signed_bin']:.3f} ratio={vertical['ratio']:.2f}x "
            f"cuts={aoa['vertical_cuts']}"
        )
    if "azimuth_deg" in aoa:
        print(
            f"  estimate az={aoa['azimuth_deg']:+.2f} deg "
            f"el={aoa['elevation_deg']:+.2f} deg (uncalibrated)"
        )
    if "cfar_proxy" in report:
        cfar = report["cfar_proxy"]
        print(
            f"CFAR proxy: hits={cfar['total_hits']} active_bins={cfar['active_bins']} "
            f"hits/frame={cfar['hit_rate_per_frame']}"
        )
        print("CFAR selected modes:", cfar["selected_bin_modes"][:4])
    for item in report.get("spatial_maps", [])[:3]:
        print(
            f"spatial bin={item['bin']} range={item['range_cm']:.2f}cm "
            f"max@({item['max_row']},{item['max_col']}) "
            f"median={item['median']:.1f} p95={item['p95']:.1f}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="*", type=Path, help="capture TXT files")
    parser.add_argument("--json", type=Path, help="write machine-readable report")
    parser.add_argument("--no-cfar", action="store_true", help="skip the CFAR proxy")
    args = parser.parse_args()
    script_dir = Path(__file__).resolve().parent
    files = args.files or [
        script_dir / "8_head.txt",
        script_dir / "medium.txt",
        script_dir / "单目标.txt",
    ]
    reports = [capture_report(path, run_cfar=not args.no_cfar) for path in files]
    for report in reports:
        print_report(report)
    if args.json:
        args.json.write_text(json.dumps(reports, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"\nJSON report written to {args.json}")


if __name__ == "__main__":
    main()
