from __future__ import annotations

import unittest

from analyze_gesture_dataset import OTHER_LABEL, analyze_records
from gesture_dataset import RIGHT_DATASET_LABELS


class GestureDatasetAnalysisTests(unittest.TestCase):
    def record(self, truth: str, predicted: str, confidence: float = 0.8,
               group: str = "g1", *, world: bool = True) -> dict:
        return {
            "label": truth,
            "recording_group": group,
            "session_id": "s1",
            "heuristic": {"gesture": predicted, "confidence": confidence},
            "handedness": {"label": "resolved-right", "confidence": 0.95},
            "canned": {"gesture": predicted, "confidence": confidence},
            "world_landmarks": [[0.0, 0.0, 0.0] for _ in range(21)] if world else [],
            "role_config": {"source": "CALIBRATION"},
        }

    def test_reports_exact_per_class_confusion(self) -> None:
        records = []
        for label in RIGHT_DATASET_LABELS:
            records.append(self.record(label, label))
        records.append(self.record("Open_Palm", "Closed_Fist", 0.7))

        report = analyze_records(records)
        palm = report["per_class"]["Open_Palm"]
        self.assertEqual(palm["support"], 2)
        self.assertAlmostEqual(palm["recall"], 0.5)
        self.assertEqual(palm["top_confusions"][0]["predicted"], "Closed_Fist")
        self.assertEqual(palm["top_confusions"][0]["count"], 1)
        self.assertAlmostEqual(report["world_landmark_coverage"], 1.0)

    def test_unknown_prediction_is_preserved_as_other(self) -> None:
        records = [self.record(label, label) for label in RIGHT_DATASET_LABELS]
        records.append(self.record("Thumb_Up", "Unexpected_Class"))
        report = analyze_records(records)
        self.assertEqual(report["prediction_counts"][OTHER_LABEL], 1)
        thumb = report["per_class"]["Thumb_Up"]
        self.assertTrue(any(item["predicted"] == OTHER_LABEL
                            for item in thumb["top_confusions"]))

    def test_recording_groups_are_counted_per_class(self) -> None:
        records = []
        for label in RIGHT_DATASET_LABELS:
            records.append(self.record(label, label, group="g1"))
            records.append(self.record(label, label, group="g2", world=False))
        report = analyze_records(records)
        self.assertEqual(len(report["recording_groups"]), 2)
        for label in RIGHT_DATASET_LABELS:
            self.assertEqual(report["per_class"][label]["recording_groups"], 2)
        self.assertAlmostEqual(report["world_landmark_coverage"], 0.5)


if __name__ == "__main__":
    unittest.main()
