from __future__ import annotations

import unittest

from shadow_evaluator import ShadowGestureEvaluator, ShadowObservation, ShadowStats
from tiny_landmark_classifier import TinyGesturePrediction


class FakeModel:
    def __init__(self, gesture: str, confidence: float = 0.9, margin: float = 0.5):
        self.gesture = gesture
        self.confidence = confidence
        self.margin = margin
        self.calls = 0

    def predict_landmarks(self, normalized_landmarks, world_landmarks=None):
        self.calls += 1
        return TinyGesturePrediction(self.gesture, self.confidence, self.margin)


class BrokenModel:
    def predict_landmarks(self, normalized_landmarks, world_landmarks=None):
        raise ValueError("bad sample")


class ShadowEvaluatorTests(unittest.TestCase):
    def test_missing_model_is_fail_closed_and_does_not_count_sample(self) -> None:
        evaluator = ShadowGestureEvaluator(None)
        observation = evaluator.evaluate([], "Open_Palm")
        self.assertFalse(observation.available)
        self.assertFalse(observation.agrees)
        self.assertEqual(evaluator.stats.samples, 0)

    def test_agreement_updates_stats_without_control_side_effects(self) -> None:
        model = FakeModel("Victory", 0.94, 0.41)
        evaluator = ShadowGestureEvaluator(model)
        observation = evaluator.evaluate([[0.0, 0.0, 0.0]], "Victory")
        self.assertTrue(observation.available)
        self.assertTrue(observation.agrees)
        self.assertEqual(model.calls, 1)
        self.assertEqual(evaluator.stats.samples, 1)
        self.assertEqual(evaluator.stats.disagreements, 0)
        self.assertGreaterEqual(observation.inference_ms, 0.0)

    def test_disagreement_and_confusion_are_recorded(self) -> None:
        evaluator = ShadowGestureEvaluator(FakeModel("Point_Left"))
        observation = evaluator.evaluate([], "Point_Right")
        self.assertFalse(observation.agrees)
        summary = evaluator.stats.summary()
        self.assertEqual(summary["samples"], 1)
        self.assertEqual(summary["disagreements"], 1)
        self.assertEqual(summary["confusion"]["Point_Right"]["Point_Left"], 1)

    def test_broken_model_returns_available_none_prediction_instead_of_crashing(self) -> None:
        evaluator = ShadowGestureEvaluator(BrokenModel())
        observation = evaluator.evaluate([], "Thumb_Up")
        self.assertTrue(observation.available)
        self.assertEqual(observation.model_gesture, "None")
        self.assertEqual(evaluator.stats.samples, 1)

    def test_percentiles_use_recent_latency_window(self) -> None:
        stats = ShadowStats(max_recent_samples=3)
        for latency in [1.0, 2.0, 3.0, 100.0]:
            stats.observe(ShadowObservation(
                available=True,
                heuristic_gesture="Open_Palm",
                model_gesture="Open_Palm",
                inference_ms=latency,
            ))
        summary = stats.summary()
        self.assertEqual(summary["samples"], 4)
        self.assertEqual(len(stats.inference_ms_recent), 3)
        self.assertAlmostEqual(summary["p50_inference_ms"], 3.0)
        self.assertGreater(summary["p95_inference_ms"], 3.0)


if __name__ == "__main__":
    unittest.main()
