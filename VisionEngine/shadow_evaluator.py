from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional
import math
import time

from tiny_landmark_classifier import TinyGesturePrediction


@dataclass(frozen=True)
class ShadowObservation:
    available: bool = False
    heuristic_gesture: str = "None"
    model_gesture: str = "None"
    confidence: float = 0.0
    margin: float = 0.0
    inference_ms: float = 0.0

    @property
    def agrees(self) -> bool:
        return self.available and self.heuristic_gesture == self.model_gesture


@dataclass
class ShadowStats:
    samples: int = 0
    disagreements: int = 0
    inference_ms_total: float = 0.0
    inference_ms_recent: list[float] = field(default_factory=list)
    confusion: dict[str, dict[str, int]] = field(default_factory=dict)
    max_recent_samples: int = 2048

    def observe(self, observation: ShadowObservation) -> None:
        if not observation.available:
            return

        self.samples += 1
        if not observation.agrees:
            self.disagreements += 1
        self.inference_ms_total += max(0.0, float(observation.inference_ms))
        self.inference_ms_recent.append(max(0.0, float(observation.inference_ms)))
        if len(self.inference_ms_recent) > self.max_recent_samples:
            del self.inference_ms_recent[:len(self.inference_ms_recent) - self.max_recent_samples]

        by_model = self.confusion.setdefault(observation.heuristic_gesture, {})
        by_model[observation.model_gesture] = by_model.get(observation.model_gesture, 0) + 1

    @property
    def disagreement_rate(self) -> float:
        if self.samples <= 0:
            return 0.0
        return self.disagreements / self.samples

    @property
    def agreement_rate(self) -> float:
        if self.samples <= 0:
            return 0.0
        return 1.0 - self.disagreement_rate

    @property
    def mean_inference_ms(self) -> float:
        if self.samples <= 0:
            return 0.0
        return self.inference_ms_total / self.samples

    @staticmethod
    def _percentile(values: list[float], quantile: float) -> float:
        if not values:
            return 0.0
        ordered = sorted(values)
        q = min(1.0, max(0.0, float(quantile)))
        position = q * (len(ordered) - 1)
        lower = int(math.floor(position))
        upper = int(math.ceil(position))
        if lower == upper:
            return float(ordered[lower])
        fraction = position - lower
        return float(ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction)

    def summary(self) -> dict:
        return {
            "samples": self.samples,
            "agreements": self.samples - self.disagreements,
            "disagreements": self.disagreements,
            "agreement_rate": self.agreement_rate,
            "disagreement_rate": self.disagreement_rate,
            "mean_inference_ms": self.mean_inference_ms,
            "p50_inference_ms": self._percentile(self.inference_ms_recent, 0.50),
            "p95_inference_ms": self._percentile(self.inference_ms_recent, 0.95),
            "confusion": self.confusion,
        }


class ShadowGestureEvaluator:
    """Run a tiny landmark model beside production classification without control authority."""

    def __init__(self, model=None):
        self.model = model
        self.stats = ShadowStats()

    @property
    def available(self) -> bool:
        return self.model is not None

    def reset_stats(self) -> None:
        self.stats = ShadowStats()

    def evaluate(self, normalized_landmarks, heuristic_gesture: str,
                 world_landmarks=None) -> ShadowObservation:
        if self.model is None:
            return ShadowObservation(heuristic_gesture=str(heuristic_gesture))

        started_ns = time.perf_counter_ns()
        prediction: Optional[TinyGesturePrediction]
        try:
            prediction = self.model.predict_landmarks(normalized_landmarks, world_landmarks)
        except (ValueError, TypeError, RuntimeError):
            prediction = None
        elapsed_ms = (time.perf_counter_ns() - started_ns) / 1_000_000.0

        if prediction is None:
            prediction = TinyGesturePrediction()

        observation = ShadowObservation(
            available=True,
            heuristic_gesture=str(heuristic_gesture),
            model_gesture=str(prediction.gesture),
            confidence=float(prediction.confidence),
            margin=float(prediction.margin),
            inference_ms=max(0.0, float(elapsed_ms)),
        )
        self.stats.observe(observation)
        return observation
