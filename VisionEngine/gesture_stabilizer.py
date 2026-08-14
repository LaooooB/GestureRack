from __future__ import annotations

from dataclasses import dataclass, field


# Product-tuned thresholds are intentionally per class. Gesture Rack's seven
# control poses do not have equal classifier calibration: Open Palm, thumbs and
# horizontal points are naturally lower-confidence than Fist/Victory on the
# canned MediaPipe model. A single 0.80 gate was therefore rejecting valid
# controls while adding no useful protection to the already-strong classes.
DEFAULT_ACTIVATE_THRESHOLDS = {
    "Open_Palm": 0.58,
    "Closed_Fist": 0.68,
    "Victory": 0.68,
    "Thumb_Up": 0.62,
    "Thumb_Down": 0.62,
    "Point_Right": 0.62,
    "Point_Left": 0.62,
}

DEFAULT_RELEASE_THRESHOLDS = {
    "Open_Palm": 0.46,
    "Closed_Fist": 0.54,
    "Victory": 0.54,
    "Thumb_Up": 0.48,
    "Thumb_Down": 0.48,
    "Point_Right": 0.48,
    "Point_Left": 0.48,
}


@dataclass
class GestureStabilizer:
    allowed_gestures: set[str] = field(default_factory=set)
    hold_ms: int = 120
    release_ms: int = 100
    # Kept for backwards compatibility and as a fallback for any future gesture
    # that does not yet have a per-class threshold.
    min_confidence: float = 0.80
    activate_thresholds: dict[str, float] = field(
        default_factory=lambda: dict(DEFAULT_ACTIVATE_THRESHOLDS)
    )
    release_thresholds: dict[str, float] = field(
        default_factory=lambda: dict(DEFAULT_RELEASE_THRESHOLDS)
    )
    stable: str = "None"
    candidate: str = "None"
    candidate_since_ms: int = 0
    invalid_since_ms: int = 0

    def _activate_threshold(self, gesture: str) -> float:
        return float(self.activate_thresholds.get(gesture, self.min_confidence))

    def _release_threshold(self, gesture: str) -> float:
        activate = self._activate_threshold(gesture)
        return float(self.release_thresholds.get(gesture, max(0.0, activate - 0.12)))

    def update(self, raw: str, confidence: float, now_ms: int) -> str:
        raw_allowed = raw in self.allowed_gestures

        # Hysteresis: once a gesture is stable, allow it to remain active down to
        # its lower release threshold. This prevents one weak frame from creating
        # control chatter without increasing onset latency.
        if self.stable != "None" and raw == self.stable and raw_allowed:
            if confidence >= self._release_threshold(self.stable):
                self.candidate = raw
                self.candidate_since_ms = 0
                self.invalid_since_ms = 0
                return self.stable

        valid_for_activation = (
            raw_allowed and confidence >= self._activate_threshold(raw)
        )
        if not valid_for_activation:
            self.candidate = "None"
            self.candidate_since_ms = 0
            if self.stable != "None":
                if self.invalid_since_ms == 0:
                    self.invalid_since_ms = now_ms
                elif now_ms - self.invalid_since_ms >= self.release_ms:
                    self.stable = "None"
                    self.invalid_since_ms = 0
            return self.stable

        self.invalid_since_ms = 0
        if raw != self.candidate:
            self.candidate = raw
            self.candidate_since_ms = now_ms
            return self.stable

        if self.candidate_since_ms and now_ms - self.candidate_since_ms >= self.hold_ms:
            self.stable = raw
            self.candidate_since_ms = 0

        return self.stable

    def reset(self) -> None:
        self.stable = "None"
        self.candidate = "None"
        self.candidate_since_ms = 0
        self.invalid_since_ms = 0
