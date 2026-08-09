from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Optional


@dataclass
class DetectedHand:
    landmarks: list[list[float]]
    raw_gesture: str = "None"
    gesture_confidence: float = 0.0
    handedness_label: str = "Unknown"
    handedness_confidence: float = 0.0

    @property
    def wrist(self) -> tuple[float, float]:
        if not self.landmarks:
            return (0.5, 0.5)
        return (float(self.landmarks[0][0]), float(self.landmarks[0][1]))


@dataclass
class RoleTrack:
    wrist: Optional[tuple[float, float]] = None
    last_seen_ms: int = 0

    def is_fresh(self, now_ms: int, ttl_ms: int) -> bool:
        return self.wrist is not None and now_ms - self.last_seen_ms <= ttl_ms


class HandRoleResolver:
    """Assign detections to physical left/right roles without frame-to-frame swapping.

    MediaPipe handedness is the primary cue. Wrist trajectory is a continuity cue,
    especially when hands cross and handedness confidence briefly drops. The optional
    swap flag exists because camera mirroring conventions differ between pipelines.
    """

    def __init__(self, *, swap_handedness: bool = False, ttl_ms: int = 420,
                 max_track_distance: float = 0.42, ambiguous_margin: float = 0.30):
        self.swap_handedness = swap_handedness
        self.ttl_ms = ttl_ms
        self.max_track_distance = max_track_distance
        self.ambiguous_margin = ambiguous_margin
        self.left_track = RoleTrack()
        self.right_track = RoleTrack()

    def _normalized_label(self, label: str) -> str:
        value = (label or "").strip().lower()
        if value not in {"left", "right"}:
            return "unknown"
        if self.swap_handedness:
            return "right" if value == "left" else "left"
        return value

    @staticmethod
    def _distance(a: tuple[float, float], b: tuple[float, float]) -> float:
        return math.hypot(a[0] - b[0], a[1] - b[1])

    def _score(self, hand: DetectedHand, role: str, now_ms: int) -> float:
        score = 0.0
        label = self._normalized_label(hand.handedness_label)
        confidence = max(0.0, min(1.0, hand.handedness_confidence))

        if label == role:
            score += 2.4 * confidence
        elif label in {"left", "right"}:
            score -= 2.0 * confidence

        track = self.left_track if role == "left" else self.right_track
        if track.is_fresh(now_ms, self.ttl_ms) and track.wrist is not None:
            distance = self._distance(hand.wrist, track.wrist)
            continuity = 1.0 - min(1.0, distance / self.max_track_distance)
            score += 1.8 * continuity

        return score

    def _trajectory_preference(self, hands: list[DetectedHand], now_ms: int) -> Optional[tuple[int, int]]:
        if len(hands) != 2:
            return None
        if not (self.left_track.is_fresh(now_ms, self.ttl_ms)
                and self.right_track.is_fresh(now_ms, self.ttl_ms)):
            return None
        assert self.left_track.wrist is not None and self.right_track.wrist is not None

        direct = (self._distance(hands[0].wrist, self.left_track.wrist)
                  + self._distance(hands[1].wrist, self.right_track.wrist))
        crossed = (self._distance(hands[1].wrist, self.left_track.wrist)
                   + self._distance(hands[0].wrist, self.right_track.wrist))
        return (0, 1) if direct <= crossed else (1, 0)

    def resolve(self, hands: list[DetectedHand], now_ms: int) -> tuple[Optional[DetectedHand], Optional[DetectedHand]]:
        hands = hands[:2]
        if not hands:
            return None, None

        left: Optional[DetectedHand] = None
        right: Optional[DetectedHand] = None

        if len(hands) == 1:
            hand = hands[0]
            left_score = self._score(hand, "left", now_ms)
            right_score = self._score(hand, "right", now_ms)
            if left_score > right_score:
                left = hand
            elif right_score > left_score:
                right = hand
            else:
                label = self._normalized_label(hand.handedness_label)
                if label == "left":
                    left = hand
                elif label == "right":
                    right = hand
                elif self.left_track.is_fresh(now_ms, self.ttl_ms) and not self.right_track.is_fresh(now_ms, self.ttl_ms):
                    left = hand
                else:
                    right = hand
        else:
            score_direct = self._score(hands[0], "left", now_ms) + self._score(hands[1], "right", now_ms)
            score_crossed = self._score(hands[1], "left", now_ms) + self._score(hands[0], "right", now_ms)

            if abs(score_direct - score_crossed) < self.ambiguous_margin:
                preference = self._trajectory_preference(hands, now_ms)
                if preference is not None:
                    left, right = hands[preference[0]], hands[preference[1]]
                elif score_direct >= score_crossed:
                    left, right = hands[0], hands[1]
                else:
                    left, right = hands[1], hands[0]
            elif score_direct >= score_crossed:
                left, right = hands[0], hands[1]
            else:
                left, right = hands[1], hands[0]

        if left is not None:
            self.left_track.wrist = left.wrist
            self.left_track.last_seen_ms = now_ms
        if right is not None:
            self.right_track.wrist = right.wrist
            self.right_track.last_seen_ms = now_ms

        return left, right
