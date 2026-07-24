import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AUDIT_PATH = ROOT / "vitis" / "radarcollect" / "audit_captures.py"
SPEC = importlib.util.spec_from_file_location("capture_audit", AUDIT_PATH)
assert SPEC is not None and SPEC.loader is not None
capture_audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(capture_audit)


class CaptureAoaRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        capture_dir = ROOT / "vitis" / "radarcollect"
        cls.reports = {
            name: capture_audit.capture_report(capture_dir / name, run_cfar=False)
            for name in ("8_head.txt", "medium.txt", "\u5355\u76ee\u6807.txt")
        }

    def test_incomplete_capture_rejects_aoa(self) -> None:
        report = self.reports["8_head.txt"]
        self.assertEqual(report["complete_frames"], 767)
        self.assertEqual(report["packed_component_clipped_frames_proxy"], 0)
        self.assertEqual(report["complex_aoa"]["status"], "incomplete_array")

    def test_medium_capture_fails_spatial_validity_gate(self) -> None:
        aoa = self.reports["medium.txt"]["complex_aoa"]
        self.assertEqual(
            self.reports["medium.txt"]["packed_component_clipped_frames_proxy"], 0
        )
        self.assertEqual(aoa["status"], "valid_elements_below_threshold")
        self.assertGreater(aoa["valid_percent"], 60.0)
        self.assertLess(aoa["valid_percent"], 70.0)

    def test_single_target_has_stable_uncalibrated_phase_peak(self) -> None:
        aoa = self.reports["\u5355\u76ee\u6807.txt"]["complex_aoa"]
        self.assertEqual(
            self.reports["\u5355\u76ee\u6807.txt"]["packed_component_clipped_frames_proxy"], 0
        )
        self.assertEqual(aoa["status"], "estimate_uncalibrated")
        self.assertGreaterEqual(aoa["valid_percent"], 75.0)
        self.assertGreaterEqual(aoa["horizontal_cuts"], 24)
        self.assertGreaterEqual(aoa["vertical_cuts"], 24)
        self.assertAlmostEqual(aoa["horizontal_peak"]["signed_bin"], -12.0, delta=0.75)
        self.assertAlmostEqual(aoa["vertical_peak"]["signed_bin"], -21.25, delta=0.75)
        self.assertGreater(aoa["horizontal_peak"]["ratio"], 4.0)
        self.assertGreater(aoa["vertical_peak"]["ratio"], 4.0)


if __name__ == "__main__":
    unittest.main()
