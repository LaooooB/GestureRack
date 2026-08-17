from __future__ import annotations

import unittest

from gesture_event_metrics import evaluate_timeline


class GestureEventMetricTests(unittest.TestCase):
    def test_clean_segments_measure_activation_latency(self) -> None:
        timeline = [
            {"recording_group": "g1", "timestamp_ms": 1000, "truth": "Thumb_Up", "raw": "Thumb_Up", "confidence": 0.96},
            {"recording_group": "g1", "timestamp_ms": 1030, "truth": "Thumb_Up", "raw": "Thumb_Up", "confidence": 0.96},
            {"recording_group": "g1", "timestamp_ms": 1100, "truth": "Thumb_Right", "raw": "Thumb_Right", "confidence": 0.96},
            {"recording_group": "g1", "timestamp_ms": 1130, "truth": "Thumb_Right", "raw": "Thumb_Right", "confidence": 0.96},
            {"recording_group": "g1", "timestamp_ms": 1160, "truth": "Thumb_Right", "raw": "Thumb_Right", "confidence": 0.96},
        ]
        report = evaluate_timeline(timeline)
        self.assertEqual(report["expected_command_segments"], 2)
        self.assertEqual(report["correct_enters"], 2)
        self.assertEqual(report["false_enters"], 0)
        self.assertEqual(report["missed_enters"], 0)
        self.assertEqual(report["activation_latency_ms"]["samples"], 2)
        self.assertGreaterEqual(report["activation_latency_ms"]["median"], 30.0)

    def test_fist_thumb_false_enter_is_visible(self) -> None:
        timeline = [
            {"recording_group": "g1", "timestamp_ms": 1000, "truth": "Closed_Fist", "raw": "Thumb_Up", "confidence": 0.96},
            {"recording_group": "g1", "timestamp_ms": 1030, "truth": "Closed_Fist", "raw": "Thumb_Up", "confidence": 0.96},
            {"recording_group": "g1", "timestamp_ms": 1060, "truth": "Closed_Fist", "raw": "Thumb_Up", "confidence": 0.96},
        ]
        report = evaluate_timeline(timeline)
        self.assertEqual(report["fist_thumb_frame_confusions"], 3)
        self.assertEqual(report["false_enters"], 1)
        self.assertEqual(report["missed_enters"], 1)

    def test_wrong_thumb_direction_is_counted_separately(self) -> None:
        timeline = [
            {"recording_group": "g1", "timestamp_ms": 1000, "truth": "Thumb_Left", "raw": "Thumb_Right", "confidence": 0.96},
            {"recording_group": "g1", "timestamp_ms": 1030, "truth": "Thumb_Left", "raw": "Thumb_Right", "confidence": 0.96},
        ]
        report = evaluate_timeline(timeline)
        self.assertEqual(report["thumb_direction_frame_confusions"], 2)
        self.assertEqual(report["false_enters"], 1)

    def test_single_weak_segment_can_be_reported_missed_without_false_enter(self) -> None:
        timeline = [
            {"recording_group": "g1", "timestamp_ms": 1000, "truth": "Victory", "raw": "Victory", "confidence": 0.70},
        ]
        report = evaluate_timeline(timeline)
        self.assertEqual(report["expected_command_segments"], 1)
        self.assertEqual(report["missed_enters"], 1)
        self.assertEqual(report["false_enters"], 0)

    def test_new_recording_group_resets_temporal_state(self) -> None:
        timeline = [
            {"recording_group": "g1", "timestamp_ms": 1000, "truth": "Open_Palm", "raw": "Open_Palm", "confidence": 0.96},
            {"recording_group": "g1", "timestamp_ms": 1030, "truth": "Open_Palm", "raw": "Open_Palm", "confidence": 0.96},
            {"recording_group": "g2", "timestamp_ms": 2000, "truth": "Open_Palm", "raw": "Open_Palm", "confidence": 0.96},
            {"recording_group": "g2", "timestamp_ms": 2030, "truth": "Open_Palm", "raw": "Open_Palm", "confidence": 0.96},
        ]
        report = evaluate_timeline(timeline)
        self.assertEqual(report["expected_command_segments"], 2)
        self.assertEqual(report["correct_enters"], 2)
        self.assertEqual(report["chatter_enters"], 0)


if __name__ == "__main__":
    unittest.main()
