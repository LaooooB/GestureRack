from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from gesture_dataset import GestureDatasetRecorder
from hand_role_resolver import DetectedHand
from right_gesture_classifier import RightGestureClassification


def sample_hand() -> DetectedHand:
    points = [[0.5, 0.8, 0.0] for _ in range(21)]
    points[0] = [0.50, 0.84, 0.00]
    points[5] = [0.40, 0.63, 0.00]
    points[9] = [0.47, 0.62, 0.00]
    points[13] = [0.54, 0.63, 0.00]
    points[17] = [0.61, 0.64, 0.00]
    groups = [(1, 2, 3, 4), (5, 6, 7, 8), (9, 10, 11, 12),
              (13, 14, 15, 16), (17, 18, 19, 20)]
    for finger, group in enumerate(groups):
        base_x = 0.34 + finger * 0.065
        a, b, c, d = group
        if finger == 0:
            points[a] = [0.43, 0.75, 0.0]
        points[b] = [base_x, 0.53, -0.01]
        points[c] = [base_x, 0.41, -0.02]
        points[d] = [base_x, 0.30, -0.03]
    return DetectedHand(
        landmarks=points,
        world_landmarks=[[p[0] * 0.1, p[1] * 0.1, p[2] * 0.1] for p in points],
        raw_gesture="Open_Palm",
        gesture_confidence=0.91,
        handedness_label="Right",
        handedness_confidence=0.97,
    )


class GestureDatasetRecorderTests(unittest.TestCase):
    def test_record_contains_role_features_and_runtime_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            recorder = GestureDatasetRecorder(Path(temp), min_interval_ms=50)
            status = recorder.start("Open_Palm", {
                "session_id": "abc",
                "camera_index": 0,
                "backend": "DSHOW",
                "width": 640,
                "height": 480,
            })
            self.assertTrue(status.active)
            self.assertTrue(recorder.observe(
                sample_hand(), RightGestureClassification("Open_Palm", 0.88), 1000))
            final = recorder.stop()
            self.assertEqual(final.samples, 1)

            line = Path(final.path).read_text(encoding="utf-8").strip()
            record = json.loads(line)
            self.assertEqual(record["physical_role"], "right")
            self.assertEqual(record["label"], "Open_Palm")
            self.assertEqual(record["session_id"], "abc")
            self.assertEqual(record["camera"]["backend"], "DSHOW")
            self.assertEqual(record["canned"]["gesture"], "Open_Palm")
            self.assertEqual(record["heuristic"]["gesture"], "Open_Palm")
            self.assertEqual(len(record["features"]), 81)
            self.assertEqual(len(record["normalized_landmarks"]), 21)

    def test_sampling_interval_prevents_near_duplicate_frames(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            recorder = GestureDatasetRecorder(Path(temp), min_interval_ms=50)
            recorder.start("Victory", {})
            hand = sample_hand()
            classification = RightGestureClassification("Victory", 0.9)
            self.assertTrue(recorder.observe(hand, classification, 1000))
            self.assertFalse(recorder.observe(hand, classification, 1020))
            self.assertTrue(recorder.observe(hand, classification, 1050))
            final = recorder.stop()
            self.assertEqual(final.samples, 2)

    def test_invalid_label_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            recorder = GestureDatasetRecorder(Path(temp))
            with self.assertRaises(ValueError):
                recorder.start("Not_A_Gesture", {})


if __name__ == "__main__":
    unittest.main()
