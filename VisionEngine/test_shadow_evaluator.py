from __future__ import annotations

import unittest

from shadow_evaluator import ShadowGestureEvaluator, ShadowObservation, ShadowStats
from tiny_landmark_classifier import TinyGesturePrediction


class FakeModel:
    def __init__(self, family: str, confidence: float = 0.9, margin: float = 0.5):
        self.family = family
        self.confidence = confidence
        self.margin = margin
        self.calls = 0

    def predict_landmarks(self, normalized_landmarks, world_landmarks=None):
        self.calls += 1
        return TinyGesturePrediction(self.family, self.confidence, self.margin)


class BrokenModel:
    def predict_landmarks(self, normalized_landmarks, world_landmarks=None):
        raise ValueError("bad sample")


class ShadowEvaluatorTests(unittest.TestCase):
    def test_missing_model_is_fail_closed(self) -> None:
        observation = ShadowGestureEvaluator(None).evaluate([], "Open_Palm")
        self.assertFalse(observation.available)
        self.assertFalse(observation.agrees)

    def test_thumb_final_gesture_agrees_with_folded_four_model(self) -> None:
        evaluator = ShadowGestureEvaluator(FakeModel("FoldedFour"))
        observation = evaluator.evaluate([], "Thumb_Left")
        self.assertTrue(observation.agrees)
        self.assertEqual(observation.heuristic_gesture, "FoldedFour")
        self.assertEqual(evaluator.stats.samples, 1)

    def test_family_disagreement_is_recorded(self) -> None:
        evaluator = ShadowGestureEvaluator(FakeModel("Victory"))
        observation = evaluator.evaluate([], "Thumb_Right")
        self.assertFalse(observation.agrees)
        summary = evaluator.stats.summary()
        self.assertEqual(summary["confusion"]["FoldedFour"]["Victory"], 1)

    def test_broken_model_fails_to_other(self) -> None:
        evaluator = ShadowGestureEvaluator(BrokenModel())
        observation = evaluator.evaluate([], "Thumb_Up")
        self.assertTrue(observation.available)
        self.assertEqual(observation.model_gesture, "Other")

    def test_percentiles_use_recent_latency_window(self) -> None:
        stats = ShadowStats(max_recent_samples=3)
        for latency in [1.0, 2.0, 3.0, 100.0]:
            stats.observe(ShadowObservation(
                available=True,
                heuristic_gesture="OpenPalm",
                model_gesture="OpenPalm",
                inference_ms=latency,
            ))
        summary = stats.summary()
        self.assertEqual(summary["samples"], 4)
        self.assertEqual(len(stats.inference_ms_recent), 3)
        self.assertAlmostEqual(summary["p50_inference_ms"], 3.0)
        self.assertGreater(summary["p95_inference_ms"], 3.0)


if __name__ == "__main__":
    unittest.main()
