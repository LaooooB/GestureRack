from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Sequence

import numpy as np

from landmark_features import FEATURE_VERSION, extract_landmark_features


DEFAULT_MODEL_NAME = "right_gesture_landmark_v1.npz"


@dataclass(frozen=True)
class TinyGesturePrediction:
    gesture: str = "None"
    confidence: float = 0.0
    margin: float = 0.0


class TinyLandmarkClassifier:
    """Pure-NumPy one-hidden-layer classifier for shadow-mode evaluation.

    The runtime deliberately has no scikit-learn/TensorFlow dependency. Offline
    training can use richer tooling, then exports only standardization vectors
    and two dense layers. The model stays in shadow mode until real held-out
    sessions show that it beats the existing heuristic/canned fusion per class.
    """

    def __init__(self, labels: Sequence[str], mean: np.ndarray, scale: np.ndarray,
                 w1: np.ndarray, b1: np.ndarray, w2: np.ndarray, b2: np.ndarray,
                 feature_version: int = FEATURE_VERSION):
        self.labels = tuple(str(label) for label in labels)
        self.mean = np.asarray(mean, dtype=np.float32)
        self.scale = np.asarray(scale, dtype=np.float32)
        self.w1 = np.asarray(w1, dtype=np.float32)
        self.b1 = np.asarray(b1, dtype=np.float32)
        self.w2 = np.asarray(w2, dtype=np.float32)
        self.b2 = np.asarray(b2, dtype=np.float32)
        self.feature_version = int(feature_version)
        self._validate()

    def _validate(self) -> None:
        if self.feature_version != FEATURE_VERSION:
            raise ValueError(
                f"feature version mismatch: model={self.feature_version}, runtime={FEATURE_VERSION}")
        if not self.labels or len(set(self.labels)) != len(self.labels):
            raise ValueError("model labels must be unique and non-empty")
        if self.mean.shape != (81,) or self.scale.shape != (81,):
            raise ValueError("model standardization vectors must contain 81 values")
        if self.w1.ndim != 2 or self.w1.shape[0] != 81:
            raise ValueError("w1 must have shape (81, hidden)")
        hidden = self.w1.shape[1]
        if hidden <= 0 or hidden > 128 or self.b1.shape != (hidden,):
            raise ValueError("invalid hidden layer")
        if self.w2.shape != (hidden, len(self.labels)):
            raise ValueError("w2 must have shape (hidden, classes)")
        if self.b2.shape != (len(self.labels),):
            raise ValueError("b2 must match class count")
        for array in (self.mean, self.scale, self.w1, self.b1, self.w2, self.b2):
            if not np.isfinite(array).all():
                raise ValueError("model contains non-finite values")
        self.scale = np.maximum(np.abs(self.scale), 1.0e-5).astype(np.float32)

    @classmethod
    def load(cls, path: Path) -> "TinyLandmarkClassifier":
        with np.load(Path(path), allow_pickle=False) as data:
            labels = [str(value) for value in data["labels"].tolist()]
            feature_version = int(np.asarray(data["feature_version"]).reshape(-1)[0])
            return cls(
                labels=labels,
                mean=data["mean"],
                scale=data["scale"],
                w1=data["w1"],
                b1=data["b1"],
                w2=data["w2"],
                b2=data["b2"],
                feature_version=feature_version,
            )

    @classmethod
    def load_optional(cls, path: Path) -> Optional["TinyLandmarkClassifier"]:
        path = Path(path)
        if not path.exists():
            return None
        try:
            return cls.load(path)
        except (OSError, KeyError, ValueError):
            return None

    def predict_features(self, features: np.ndarray) -> TinyGesturePrediction:
        x = np.asarray(features, dtype=np.float32)
        if x.shape != (81,) or not np.isfinite(x).all():
            return TinyGesturePrediction()

        standardized = (x - self.mean) / self.scale
        hidden = np.maximum(0.0, standardized @ self.w1 + self.b1)
        logits = hidden @ self.w2 + self.b2
        logits = logits - float(np.max(logits))
        exp_logits = np.exp(logits).astype(np.float32, copy=False)
        denominator = float(np.sum(exp_logits))
        if denominator <= 0.0 or not np.isfinite(denominator):
            return TinyGesturePrediction()
        probabilities = exp_logits / denominator
        order = np.argsort(probabilities)
        top_index = int(order[-1])
        second = float(probabilities[order[-2]]) if len(order) > 1 else 0.0
        confidence = float(probabilities[top_index])
        return TinyGesturePrediction(
            gesture=self.labels[top_index],
            confidence=confidence,
            margin=max(0.0, confidence - second),
        )

    def predict_landmarks(self, normalized_landmarks,
                          world_landmarks=None) -> TinyGesturePrediction:
        try:
            feature = extract_landmark_features(normalized_landmarks, world_landmarks)
        except ValueError:
            return TinyGesturePrediction()
        return self.predict_features(feature.values)
