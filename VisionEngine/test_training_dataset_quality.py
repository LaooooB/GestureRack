from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import numpy as np

from gesture_dataset import RIGHT_DATASET_LABELS, RIGHT_FAMILY_LABELS
from landmark_features import FEATURE_VERSION
from train_tiny_classifier import load_records, validate_dataset_quality


class TrainingDatasetQualityTests(unittest.TestCase):
    def write_dataset(self, path: Path, group: str, samples_per_label: int = 2) -> None:
        with path.open("w", encoding="utf-8") as handle:
            for label in RIGHT_DATASET_LABELS:
                for index in range(samples_per_label):
                    record = {
                        "feature_version": FEATURE_VERSION,
                        "features": [float(index)] * 81,
                        "label": label,
                        "recording_group": group,
                        "session_id": "same-sidecar-session",
                        "heuristic": {"gesture": label},
                    }
                    handle.write(json.dumps(record) + "\n")

    def test_explicit_groups_and_family_projection(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_dataset(root / "a.jsonl", "group-a")
            self.write_dataset(root / "b.jsonl", "group-b")
            _, gestures, families, groups, _ = load_records([root / "a.jsonl", root / "b.jsonl"])
            self.assertEqual(set(groups.tolist()), {"group-a", "group-b"})
            self.assertEqual(set(families.tolist()), set(RIGHT_FAMILY_LABELS))
            summary = validate_dataset_quality(
                gestures,
                groups,
                min_samples_per_class=4,
                min_groups_per_class=2,
            )
            self.assertTrue(summary["passed"])
            for label in RIGHT_DATASET_LABELS:
                self.assertEqual(summary["label_counts"][label], 4)
                self.assertEqual(summary["groups_per_label"][label], 2)

    def test_missing_none_hard_negatives_is_rejected(self) -> None:
        gestures = np.asarray([label for label in RIGHT_DATASET_LABELS if label != "None"], dtype=str)
        groups = np.asarray(["group-a"] * len(gestures), dtype=str)
        with self.assertRaisesRegex(ValueError, "missing required classes"):
            validate_dataset_quality(gestures, groups, 1, 1)

    def test_single_recording_group_is_rejected(self) -> None:
        gestures = np.asarray(list(RIGHT_DATASET_LABELS), dtype=str)
        groups = np.asarray(["group-a"] * len(gestures), dtype=str)
        with self.assertRaisesRegex(ValueError, "not enough independent groups"):
            validate_dataset_quality(gestures, groups, 1, 2)


if __name__ == "__main__":
    unittest.main()
