import unittest

from radar_host.reporting import AngleHeatmap
from radar_host.visualization import (
    angle_bin_to_degrees,
    heat_color,
    integrated_angle_peak,
    planar_angles_from_bins,
    point_cloud_projection,
)


class VisualizationTests(unittest.TestCase):
    def test_angle_bin_conversion_handles_fft_wrap(self) -> None:
        self.assertAlmostEqual(angle_bin_to_degrees(0), 0.0)
        self.assertAlmostEqual(angle_bin_to_degrees(16), 30.0, places=5)
        self.assertAlmostEqual(angle_bin_to_degrees(48), -30.0, places=5)

    def test_integrated_peak_sums_rows(self) -> None:
        data = bytes((0, 8, 1, 0, 0, 9, 1, 0))
        heatmap = AngleHeatmap(1, 10, 1, 2, 4, 0.0, 1.0, data)
        peak = integrated_angle_peak(heatmap)
        self.assertEqual(peak.bin_index, 1)
        self.assertGreater(peak.peak, peak.baseline)

    def test_planar_angles_couple_horizontal_and_vertical_bins(self) -> None:
        angles = planar_angles_from_bins(52, 43)
        self.assertIsNotNone(angles)
        azimuth, elevation = angles
        self.assertAlmostEqual(azimuth, -29.80, delta=0.1)
        self.assertAlmostEqual(elevation, -41.01, delta=0.1)

    def test_planar_angles_reject_non_physical_pair(self) -> None:
        self.assertIsNone(planar_angles_from_bins(24, 24))

    def test_heat_color_spans_multiple_hues(self) -> None:
        self.assertNotEqual(heat_color(0), heat_color(128))
        self.assertNotEqual(heat_color(128), heat_color(255))

    def test_point_cloud_projection_keeps_negative_elevation_visible(self) -> None:
        projection = point_cloud_projection(400, 250)
        point_x, point_y = projection.project(-33.9, 58.3, -59.9)
        self.assertGreaterEqual(point_x, 0.0)
        self.assertLessEqual(point_x, 400.0)
        self.assertGreaterEqual(point_y, 0.0)
        self.assertLessEqual(point_y, 250.0)

    def test_point_cloud_projection_contains_full_radar_volume(self) -> None:
        projection = point_cloud_projection(800, 420)
        for x_value in (-120.0, 120.0):
            for y_value in (0.0, 120.0):
                for z_value in (-120.0, 120.0):
                    point_x, point_y = projection.project(x_value, y_value, z_value)
                    self.assertGreaterEqual(point_x, 0.0)
                    self.assertLessEqual(point_x, 800.0)
                    self.assertGreaterEqual(point_y, 0.0)
                    self.assertLessEqual(point_y, 420.0)

    def test_point_cloud_projection_centers_height_limited_scene(self) -> None:
        projection = point_cloud_projection(1000, 380)
        corners = [
            projection.project(x_value, y_value, z_value)
            for x_value in (-120.0, 120.0)
            for y_value in (0.0, 120.0)
            for z_value in (-120.0, 120.0)
        ]
        scene_center_x = (min(point[0] for point in corners) + max(point[0] for point in corners)) / 2.0
        scene_center_y = (min(point[1] for point in corners) + max(point[1] for point in corners)) / 2.0
        self.assertAlmostEqual(scene_center_x, 502.0)
        self.assertAlmostEqual(scene_center_y, 195.0)


if __name__ == "__main__":
    unittest.main()
