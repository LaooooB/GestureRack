from __future__ import annotations

from dataclasses import dataclass, field


DEFAULT_ACTIVATE_THRESHOLDS = {
    "Open_Palm": 0.60,
    "Closed_Fist": 0.66,
    "Victory": 0.68,
    "Thumb_Up": 0.65,
    "Thumb_Down": 0.65,
    "Thumb_Left": 0.65,
    "Thumb_Right": 0.65,
}

DEFAULT_RELEASE_THRESHOLDS = {
    "Open_Palm": 0.46,
    "Closed_Fist": 0.51,
    "Victory": 0.54,
    "Thumb_Up": 0.50,
    "Thumb_Down": 0.50,
    "Thumb_Left": 0.50,
    "Thumb_Right": 0.50,
}


def _is_thumb(gesture: str) -> bool:
    return gesture.startswith("Thumb_")


def _is_fist_thumb_pair(a: str, b: str) -> bool:
    return (a == "Closed_Fist" and _is_thumb(b)) or (_is_thumb(a) and b == "Closed_Fist")


def _is_thumb_direction_pair(a: str, b: str) -> bool:
    return _is_thumb(a) and _is_thumb(b) and a != b


@dataclass
class GestureStabilizer:
    """Low-latency temporal gate with class/pair-specific hysteresis.

    High-confidence clean gestures can activate after two inference callbacks.
    Known-dangerous Fist<->Thumb transitions require extra evidence. Thumb
    direction changes also use a smaller transition barrier so diagonal boundary
    frames do not create chatter.
    """

    allowed_gestures: set[str] = field(default_factory=set)
    hold_ms: int = 50
    release_ms: int = 50
    min_confidence: float = 0.80
    activate_thresholds: dict[str, float] = field(
        default_factory=lambda: dict(DEFAULT_ACTIVATE_THRESHOLDS)
    )
    release_thresholds: dict[str, float] = field(
        default_factory=lambda: dict(DEFAULT_RELEASE_THRESHOLDS)
    )
    candidate_grace_ms: int = 75
    thumb_release_grace_ms: int = 105
    dangerous_extra_hold_ms: int = 35
    direction_extra_hold_ms: int = 18
    near_threshold_extra_hold_ms: int = 25
    high_confidence_reduction_ms: int = 25

    stable: str = "None"
    candidate: str = "None"
    candidate_since_ms: int = 0
    candidate_last_valid_ms: int = 0
    invalid_since_ms: int = 0

    def _activate_threshold(self, gesture: str) -> float:
        return float(self.activate_thresholds.get(gesture, self.min_confidence))

    def _release_threshold(self, gesture: str) -> float:
        activate = self._activate_threshold(gesture)
        return float(self.release_thresholds.get(gesture, max(0.0, activate - 0.14)))

    def _required_hold_ms(self, gesture: str, confidence: float) -> int:
        required = max(15, int(self.hold_ms))
        threshold = self._activate_threshold(gesture)

        if confidence >= max(0.90, threshold + 0.20):
            required = max(15, required - int(self.high_confidence_reduction_ms))
        elif confidence < threshold + 0.07:
            required += int(self.near_threshold_extra_hold_ms)

        if self.stable != "None":
            if _is_fist_thumb_pair(self.stable, gesture):
                required += int(self.dangerous_extra_hold_ms)
            elif _is_thumb_direction_pair(self.stable, gesture):
                required += int(self.direction_extra_hold_ms)
        return max(15, required)

    def _release_hold_ms(self) -> int:
        if _is_thumb(self.stable):
            return max(int(self.release_ms), int(self.thumb_release_grace_ms))
        return int(self.release_ms)

    def _update_stable_release(self, now_ms: int) -> None:
        if self.stable == "None":
            return
        if self.invalid_since_ms == 0:
            self.invalid_since_ms = now_ms
            return
        if now_ms - self.invalid_since_ms >= self._release_hold_ms():
            self.stable = "None"
            self.invalid_since_ms = 0

    def update(self, raw: str, confidence: float, now_ms: int) -> str:
        raw = str(raw)
        confidence = float(confidence)
        now_ms = int(now_ms)
        raw_allowed = raw in self.allowed_gestures

        if self.stable != "None" and raw == self.stable and raw_allowed:
            if confidence >= self._release_threshold(self.stable):
                self.candidate = raw
                self.candidate_since_ms = 0
                self.candidate_last_valid_ms = now_ms
                self.invalid_since_ms = 0
                return self.stable

        valid_for_activation = raw_allowed and confidence >= self._activate_threshold(raw)
        if not valid_for_activation:
            candidate_in_grace = (
                self.candidate != "None"
                and self.candidate_last_valid_ms > 0
                and now_ms - self.candidate_last_valid_ms <= self.candidate_grace_ms
            )
            self._update_stable_release(now_ms)
            if candidate_in_grace:
                return self.stable

            self.candidate = "None"
            self.candidate_since_ms = 0
            self.candidate_last_valid_ms = 0
            return self.stable

        self.invalid_since_ms = 0
        if raw != self.candidate:
            self.candidate = raw
            self.candidate_since_ms = now_ms
            self.candidate_last_valid_ms = now_ms
            return self.stable

        self.candidate_last_valid_ms = now_ms
        required = self._required_hold_ms(raw, confidence)
        if self.candidate_since_ms and now_ms - self.candidate_since_ms >= required:
            self.stable = raw
            self.candidate_since_ms = 0
            self.invalid_since_ms = 0

        return self.stable

    def reset(self) -> None:
        self.stable = "None"
        self.candidate = "None"
        self.candidate_since_ms = 0
        self.candidate_last_valid_ms = 0
        self.invalid_since_ms = 0
