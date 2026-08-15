from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from hand_role_calibration import RightHandCalibration
from hand_role_resolver import DetectedHand
from vision_profile import VisionProfileStore


def hand(label: str, confidence: float = 0.95) -> DetectedHand:
    return DetectedHand(
        landmarks=[[0.5, 0.5, 0.0] for _ in range(21)],
        handedness_label=label,
        handedness_confidence=confidence,
    )


class RightHandCalibrationTests(unittest.TestCase):
    def test_physical_right_reported_right_keeps_normal_mapping(self) -> None:
        calibration = RightHandCalibration(duration_ms=100, min_samples=3)
        calibration.start(1000)
        self.assertIsNone(calibration.observe([hand("Right")], 1010))
        self.assertIsNone(calibration.observe([hand("Right")], 1040))
        self.assertIsNone(calibration.observe([hand("Right")], 1070))
        result = calibration.observe([hand("Right")], 1110)
        self.assertIsNotNone(result)
        assert result is not None
        self.assertFalse(result.swap_handedness)
        self.assertGreaterEqual(result.confidence, 0.90)

    def test_physical_right_reported_left_enables_swap(self) -> None:
        calibration = RightHandCalibration(duration_ms=100, min_samples=3)
        calibration.start(2000)
        calibration.observe([hand("Left")], 2010)
        calibration.observe([hand("Left")], 2040)
        calibration.observe([hand("Left")], 2070)
        result = calibration.observe([hand("Left")], 2110)
        self.assertIsNotNone(result)
        assert result is not None
        self.assertTrue(result.swap_handedness)
        self.assertIn("SWAPPED", result.status)

    def test_click_to_first_valid_hand_delay_does_not_consume_sampling_window(self) -> None:
        calibration = RightHandCalibration(duration_ms=100, min_samples=2, overall_timeout_ms=1000)
        calibration.start(3000)
        self.assertIsNone(calibration.observe([], 3200))
        self.assertIsNone(calibration.observe([hand("Right"), hand("Left")], 3400))
        self.assertEqual(calibration.sample_count, 0)
        self.assertIsNone(calibration.observe([hand("Right")], 3500))
        self.assertIsNone(calibration.observe([hand("Right")], 3550))
        result = calibration.observe([hand("Right")], 3600)
        self.assertIsNotNone(result)
        assert result is not None
        self.assertFalse(result.swap_handedness)

    def test_two_hands_are_ignored_until_overall_timeout(self) -> None:
        calibration = RightHandCalibration(
            duration_ms=100, min_samples=1, overall_timeout_ms=200)
        calibration.start(4000)
        self.assertIsNone(calibration.observe([hand("Left"), hand("Right")], 4110))
        result = calibration.observe([hand("Left"), hand("Right")], 4200)
        self.assertIsNotNone(result)
        assert result is not None
        self.assertIsNone(result.swap_handedness)
        self.assertEqual(result.sample_count, 0)

    def test_low_confidence_samples_are_ignored_until_timeout(self) -> None:
        calibration = RightHandCalibration(
            duration_ms=100,
            min_samples=1,
            min_confidence=0.8,
            overall_timeout_ms=200,
        )
        calibration.start(5000)
        self.assertIsNone(calibration.observe([hand("Left", 0.4)], 5110))
        result = calibration.observe([hand("Left", 0.4)], 5200)
        self.assertIsNotNone(result)
        assert result is not None
        self.assertIsNone(result.swap_handedness)

    def test_ambiguous_votes_continue_collecting_until_consensus(self) -> None:
        calibration = RightHandCalibration(
            duration_ms=100,
            min_samples=3,
            consensus_ratio=0.72,
            overall_timeout_ms=1000,
        )
        calibration.start(6000)
        calibration.observe([hand("Right")], 6010)
        calibration.observe([hand("Left")], 6050)
        calibration.observe([hand("Right")], 6110)
        self.assertTrue(calibration.active)
        self.assertIsNone(calibration.observe([hand("Right")], 6150))
        result = calibration.observe([hand("Right")], 6190)
        self.assertIsNotNone(result)
        assert result is not None
        self.assertFalse(result.swap_handedness)
        self.assertGreaterEqual(result.confidence, 0.72)


class VisionProfileStoreTests(unittest.TestCase):
    def test_round_trip_is_scoped_by_camera_and_backend(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            store = VisionProfileStore(Path(temp) / "vision_profile.json")
            self.assertIsNone(store.get_swap_handedness(0, "DSHOW"))
            store.set_swap_handedness(0, "DSHOW", True, "calibration")
            store.set_swap_handedness(0, "MSMF", False, "manual")
            self.assertTrue(store.get_swap_handedness(0, "DSHOW"))
            self.assertFalse(store.get_swap_handedness(0, "MSMF"))
            self.assertIsNone(store.get_swap_handedness(1, "DSHOW"))

    def test_corrupt_profile_falls_back_to_empty(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "vision_profile.json"
            path.write_text("{broken", encoding="utf-8")
            store = VisionProfileStore(path)
            self.assertIsNone(store.get_swap_handedness(0, "DSHOW"))


if __name__ == "__main__":
    unittest.main()
