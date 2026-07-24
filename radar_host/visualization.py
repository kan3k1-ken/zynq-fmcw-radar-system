"""Numerical helpers for angle-map and point-cloud visualization."""

from __future__ import annotations

import math
from dataclasses import dataclass

from .reporting import AngleHeatmap


@dataclass(frozen=True)
class AnglePeak:
    bin_index: int
    angle_deg: float
    peak: float
    baseline: float
    ratio: float


@dataclass(frozen=True)
class PointCloudProjection:
    origin_x: float
    origin_y: float
    scale: float

    def project(
        self, x_value: float, y_value: float, z_value: float
    ) -> tuple[float, float]:
        return (
            self.origin_x + (x_value - 0.35 * y_value) * self.scale,
            self.origin_y - (0.48 * y_value + z_value) * self.scale,
        )


def point_cloud_projection(
    width: float,
    height: float,
    max_range_cm: float = 120.0,
) -> PointCloudProjection:
    if width <= 44.0 or height <= 46.0:
        raise ValueError("point-cloud canvas is too small")
    if max_range_cm <= 0.0:
        raise ValueError("max_range_cm must be positive")

    projected_x: list[float] = []
    projected_y: list[float] = []
    for x_value in (-max_range_cm, max_range_cm):
        for y_value in (0.0, max_range_cm):
            for z_value in (-max_range_cm, max_range_cm):
                projected_x.append(x_value - 0.35 * y_value)
                projected_y.append(0.48 * y_value + z_value)

    left, top, right, bottom = 24.0, 28.0, width - 20.0, height - 18.0
    projected_width = max(projected_x) - min(projected_x)
    projected_height = max(projected_y) - min(projected_y)
    available_width = right - left
    available_height = bottom - top
    scale = min(
        available_width / projected_width,
        available_height / projected_height,
    )
    scene_left = left + (available_width - projected_width * scale) / 2.0
    scene_top = top + (available_height - projected_height * scale) / 2.0
    return PointCloudProjection(
        origin_x=scene_left - min(projected_x) * scale,
        origin_y=scene_top + max(projected_y) * scale,
        scale=scale,
    )


def angle_bin_to_degrees(
    bin_index: int,
    fft_size: int = 64,
    antenna_spacing_wavelengths: float = 0.5,
) -> float:
    signed_bin = bin_index if bin_index < fft_size // 2 else bin_index - fft_size
    denominator = fft_size * antenna_spacing_wavelengths
    sine_value = max(-1.0, min(1.0, signed_bin / denominator))
    return math.degrees(math.asin(sine_value))


def planar_angles_from_bins(
    horizontal_bin: int,
    vertical_bin: int,
    fft_size: int = 64,
    antenna_spacing_wavelengths: float = 0.5,
) -> tuple[float, float] | None:
    denominator = fft_size * antenna_spacing_wavelengths
    horizontal_signed = (
        horizontal_bin if horizontal_bin < fft_size // 2 else horizontal_bin - fft_size
    )
    vertical_signed = (
        vertical_bin if vertical_bin < fft_size // 2 else vertical_bin - fft_size
    )
    direction_u = horizontal_signed / denominator
    direction_v = vertical_signed / denominator
    if direction_u * direction_u + direction_v * direction_v >= 1.0:
        return None
    horizontal_scale = math.sqrt(1.0 - direction_v * direction_v)
    azimuth = math.degrees(math.asin(direction_u / horizontal_scale))
    elevation = math.degrees(math.asin(direction_v))
    return azimuth, elevation


def integrated_angle_peak(heatmap: AngleHeatmap) -> AnglePeak:
    profile = [0.0] * heatmap.cols
    for row in range(heatmap.rows):
        base = row * heatmap.cols
        for column in range(heatmap.cols):
            profile[column] += heatmap.data[base + column]
    peak_bin = max(range(heatmap.cols), key=profile.__getitem__)
    sorted_profile = sorted(profile)
    baseline = sorted_profile[len(sorted_profile) // 2] if sorted_profile else 0.0
    peak = profile[peak_bin]
    ratio = peak / baseline if baseline > 0.0 else 0.0
    return AnglePeak(
        peak_bin,
        angle_bin_to_degrees(peak_bin, heatmap.cols),
        peak,
        baseline,
        ratio,
    )


def heat_color(value: int) -> str:
    normalized = max(0, min(255, value)) / 255.0
    stops = (
        (0.00, (12, 16, 22)),
        (0.25, (24, 75, 108)),
        (0.50, (30, 180, 170)),
        (0.75, (242, 184, 75)),
        (1.00, (225, 70, 73)),
    )
    for index in range(len(stops) - 1):
        left_position, left_color = stops[index]
        right_position, right_color = stops[index + 1]
        if normalized <= right_position:
            span = right_position - left_position
            amount = (normalized - left_position) / span if span else 0.0
            red = round(left_color[0] + (right_color[0] - left_color[0]) * amount)
            green = round(left_color[1] + (right_color[1] - left_color[1]) * amount)
            blue = round(left_color[2] + (right_color[2] - left_color[2]) * amount)
            return f"#{red:02x}{green:02x}{blue:02x}"
    return "#e14649"
