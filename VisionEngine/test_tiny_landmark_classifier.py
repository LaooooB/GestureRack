from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import numpy as np

from landmark_features import FEATURE_VERSION
from tiny_landmark_classifier import TinyLandmarkClassifier


class TinyLandmarkClassifierTests(unittest.TestCase):
    def make_classifier(self) -> TinyLandmarkClassifier:
        labels = ["None", "Open_Palm", "Victory"]
        mean = np.zeros(81, dtype=np.float32)
        scale = np.ones(81, dtype=np.float32)
        w1 = np.zeros((81, 2), dtype=np.float32)
        b1 = np.zeros(2, dtype=np.float32)
        w2 = np.zeros((2, 3), dtype=np.float32)
        b2 = np.asarray([0.0, 2.0, -1.0], dtype=np.float32)
        return TinyLandmarkClassifier(labels, mean, scale, w1, b1, w2, b2)

    def test_predict_returns_softmax_top_class_and_margin(self) -> None:
        classifier = self.make_classifier()
        prediction = classifier.predict_features(np.zeros(81, dtype=np.float32))
        self.assertEqual(prediction.gesture, "Open_Palm")
        self.assertGreater(prediction.confidence, 0.8)
        self.assertGreater(prediction.margin, 0.6)

    def test_invalid_feature_vector_returns_none(self) -> None:
        classifier = self.make_classifier()
        prediction = classifier.predict_features(np.zeros(80, dtype=np.float32))
        self.assertEqual(prediction.gesture, "None")
        self.assertEqual(prediction.confidence, 0.0)

    def test_npz_round_trip(self) -> None:
        classifier = self.make_classifier()
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "model.npz"
            np.savez(
                path,
                labels=np.asarray(classifier.labels),
                feature_version=np.asarray([FEATURE_VERSION], dtype=np.int32),
                mean=classifier.mean,
                scale=classifier.scale,
                w1=classifier.w1,
                b1=classifier.b1,
                w2=classifier.w2,
                b2=classifier.b2,
            )
            loaded = TinyLandmarkClassifier.load(path)
            prediction = loaded.predict_features(np.zeros(81, dtype=np.float32))
            self.assertEqual(prediction.gesture, "Open_Palm")

    def test_feature_version_mismatch_is_rejected(self) -> None:
        classifier = self.make_classifier()
        with self.assertRaises(ValueError):
            TinyLandmarkClassifier(
                classifier.labels,
                classifier.mean,
                classifier.scale,
                classifier.w1,
                classifier.b1,
                classifier.w2,
                classifier.b2,
                feature_version=FEATURE_VERSION + 1,
            )

    def test_hidden_layer_is_bounded(self) -> None:
        with self.assertRaises(ValueError):
            TinyLandmarkClassifier(
                ["None", "Open_Palm"],
                np.zeros(81, dtype=np.float32),
                np.ones(81, dtype=np.float32),
                np.zeros((81, 129), dtype=np.float32),
                np.zeros(129, dtype=np.float32),
                np.zeros((129, 2), dtype=np.float32),
                np.zeros(2, dtype=np.float32),
            )


if __name__ == "__main__":
    unittest.main()
