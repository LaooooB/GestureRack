from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

from hand_role_resolver import DetectedHand


@dataclass(frozen=True)
class CalibrationSnapshot:
    active: bool = False
    status: str = "READY"
    sample_count: int = 0
    swap_handedness: Optional[bool] = None
    confidence: float = 0.0


class RightHandCalibration:
    """Calibrate MediaPipe handedness against a known physical RIGHT hand.

    The user shows only the physical right hand. Raw MediaPipe labels are sampled
    before any role swapping. A stable majority of raw Left labels means this
    camera/mirror pipeline needs handedness swapping; raw Right means it does not.
    """

    def __init__(self, *, duration_ms: int = 1200, min_samples: int = 12,
                 min_confidence: float = 0.72, consensus_ratio: float = 0.72):
        self.duration_ms = int(duration_ms)
        self.min_samples = int(min_samples)
        self.min_confidence = float(min_confidence)
        self.consensus_ratio = float(consensus_ratio)
        self.active = False
        self.started_ms = 0
        self.left_weight = 0.0
        self.right_weight = 0.0
        self.sample_count = 0
        self.status = "READY"
        self.last_result: Optional[CalibrationSnapshot] = None

    def start(self, now_ms: int) -> None:
        self.active = True
        self.started_ms = int(now_ms)
        self.left_weight = 0.0
        self.right_weight = 0.0
        self.sample_count = 0
        self.status = "SHOW RIGHT HAND ONLY"
        self.last_result = None

    def cancel(self) -> None:
        self.active = False
        self.status = "CANCELLED"

    def snapshot(self) -> CalibrationSnapshot:
        if self.last_result is not None:
            return self.last_result
        return CalibrationSnapshot(
            active=self.active,
            status=self.status,
            sample_count=self.sample_count,
        )

    def observe(self, hands: list[DetectedHand], now_ms: int) -> Optional[CalibrationSnapshot]:
        if not self.active:
            return None

        if len(hands) != 1:
            self.status = "SHOW RIGHT HAND ONLY"
        else:
            hand = hands[0]
            label = (hand.handedness_label or "").strip().lower()
            confidence = max(0.0, min(1.0, float(hand.handedness_confidence)))
            if label in {"left", "right"} and confidence >= self.min_confidence:
                if label == "left":
                    self.left_weight += confidence
                else:
                    self.right_weight += confidence
                self.sample_count += 1
                self.status = "SAMPLING RIGHT HAND"
            else:
                self.status = "HOLD RIGHT HAND STEADY"

        elapsed = int(now_ms) - self.started_ms
        if elapsed < self.duration_ms:
            return None

        total = self.left_weight + self.right_weight
        if self.sample_count < self.min_samples or total <= 1.0e-6:
            return self._finish(False, None, 0.0, "CALIBRATION FAILED - TRY AGAIN")

        winning = max(self.left_weight, self.right_weight)
        ratio = winning / total
        if ratio < self.consensus_ratio:
            return self._finish(False, None, ratio, "CALIBRATION AMBIGUOUS - TRY AGAIN")

        # The known physical hand is RIGHT. If MediaPipe calls it Left, swap.
        should_swap = self.left_weight > self.right_weight
        return self._finish(
            True,
            should_swap,
            ratio,
            "CALIBRATED - SWAPPED" if should_swap else "CALIBRATED - NORMAL",
        )

    def _finish(self, success: bool, should_swap: Optional[bool], confidence: float,
                status: str) -> CalibrationSnapshot:
        self.active = False
        self.status = status
        result = CalibrationSnapshot(
            active=False,
            status=status,
            sample_count=self.sample_count,
            swap_handedness=should_swap if success else None,
            confidence=float(confidence),
        )
        self.last_result = result
        return result
