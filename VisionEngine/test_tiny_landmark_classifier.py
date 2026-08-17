from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import numpy as np

from landmark_features import FEATURE_VERSION
from tiny_landmark_classifier import MODEL_TASK, TinyLandmarkClassifier


class TinyLandmarkClassifierTests(unittest.TestCase):
    def make_classifier(self) -> TinyLandmarkClassifier:
        labels = ["FoldedFour", "OpenPalm", "Other", "Victory"]
        mean = np.zeros(81, dtype=np.float32)
        scale = np.ones(81, dtype=np.float32)
        w1 = np.zeros((81, 2), dtype=np.float32)
        b1 = np.zeros(2, dtype=np.float32)
        w2 = np.zeros((2, 4), dtype=np.float32)
        b2 = np.asarray([0.0, 2.0, -1.0, 0.5], dtype=np.float32)
        return TinyLandmarkClassifier(labels, mean, scale, w1, b1, w2, b2)

    def test_predict_returns_family_softmax_top_class(self) -> None:
        prediction = self.make_classifier().predict_features(np.zeros(81, dtype=np.float32))
        self.assertEqual(prediction.gesture, "OpenPalm")
        self.assertGreater(prediction.confidence, 0.6)

    def test_invalid_feature_vector_returns_other(self) -> None:
        prediction = self.make_classifier().predict_features(np.zeros(80, dtype=np.float32))
        self.assertEqual(prediction.gesture, "Other")
        self.assertEqual(prediction.confidence, 0.0)

    def test_npz_round_trip_requires_task_marker(self) -> None:
        classifier = self.make_classifier()
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "model.npz"
            np.savez(
                path,
                labels=np.asarray(classifier.labels),
                feature_version=np.asarray([FEATURE_VERSION], dtype=np.int32),
                task=np.asarray([MODEL_TASK]),
                mean=classifier.mean,
                scale=classifier.scale,
                w1=classifier.w1,
                b1=classifier.b1,
                w2=classifier.w2,
                b2=classifier.b2,
            )
            loaded = TinyLandmarkClassifier.load(path)
            self.assertEqual(loaded.task, MODEL_TASK)
            self.assertEqual(loaded.predict_features(np.zeros(81, dtype=np.float32)).gesture, "OpenPalm")

    def test_old_final_gesture_task_is_rejected(self) -> None:
        classifier = self.make_classifier()
        with self.assertRaises(ValueError):
            TinyLandmarkClassifier(
                classifier.labels, classifier.mean, classifier.scale,
                classifier.w1, classifier.b1, classifier.w2, classifier.b2,
                task="old_final_gesture",
            )

    def test_wrong_label_space_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            TinyLandmarkClassifier(
                ["Open_Palm", "Victory"],
                np.zeros(81, dtype=np.float32),
                np.ones(81, dtype=np.float32),
                np.zeros((81, 2), dtype=np.float32),
                np.zeros(2, dtype=np.float32),
                np.zeros((2, 2), dtype=np.float32),
                np.zeros(2, dtype=np.float32),
            )


if __name__ == "__main__":
    unittest.main()
