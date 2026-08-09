from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class GestureStabilizer:
    allowed_gestures: set[str] = field(default_factory=set)
    hold_ms: int = 120
    release_ms: int = 100
    min_confidence: float = 0.80
    stable: str = "None"
    candidate: str = "None"
    candidate_since_ms: int = 0
    invalid_since_ms: int = 0

    def update(self, raw: str, confidence: float, now_ms: int) -> str:
        valid = raw in self.allowed_gestures and confidence >= self.min_confidence
        if not valid:
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

        return self.stable

    def reset(self) -> None:
        self.stable = "None"
        self.candidate = "None"
        self.candidate_since_ms = 0
        self.invalid_since_ms = 0
