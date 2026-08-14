from __future__ import annotations

from dataclasses import dataclass, field
import math
from typing import Optional


@dataclass
class DetectedHand:
    landmarks: list[list[float]]
    raw_gesture: str = "None"
    gesture_confidence: float = 0.0
    handedness_label: str = "Unknown"
    handedness_confidence: float = 0.0
    # GestureRecognizer already provides world landmarks. Carry them in the role
    # object now so the future role-specific tiny classifier can add palm-local
    # 3D features without changing this API again. Appending preserves the
    # existing positional constructor order.
    world_landmarks: list[list[float]] = field(default_factory=list)

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
    """Assign detections to physical left/right roles.

    Priority is deliberately one-way:
      high-confidence handedness (after calibration swap) > trajectory fallback.

    Gesture shape is never used to infer physical role. This structurally
    separates physical-left Slot 2 from physical-right Victory even though the
    visible finger pose is the same.
    """

    def __init__(self, *, swap_handedness: bool = False, ttl_ms: int = 420,
                 max_track_distance: float = 0.42, ambiguous_margin: float = 0.30,
                 role_switch_hold_ms: int = 140,
                 handedness_lock_confidence: float = 0.72):
        self.swap_handedness = swap_handedness
        self.ttl_ms = ttl_ms
        self.max_track_distance = max_track_distance
        self.ambiguous_margin = ambiguous_margin
        self.role_switch_hold_ms = role_switch_hold_ms
        self.handedness_lock_confidence = handedness_lock_confidence
        self.left_track = RoleTrack()
        self.right_track = RoleTrack()
        self._pending_assignment: Optional[tuple[int, int]] = None
        self._pending_since_ms = 0

    def set_swap_handedness(self, should_swap: bool) -> None:
        if self.swap_handedness == should_swap:
            return
        self.swap_handedness = should_swap
        self.reset()

    def reset(self) -> None:
        self.left_track = RoleTrack()
        self.right_track = RoleTrack()
        self._pending_assignment = None
        self._pending_since_ms = 0

    def _normalized_label(self, label: str) -> str:
        value = (label or "").strip().lower()
        if value not in {"left", "right"}:
            return "unknown"
        if self.swap_handedness:
            return "right" if value == "left" else "left"
        return value

    def _strong_role(self, hand: DetectedHand) -> Optional[str]:
        if hand.handedness_confidence < self.handedness_lock_confidence:
            return None
        label = self._normalized_label(hand.handedness_label)
        return label if label in {"left", "right"} else None

    @staticmethod
    def _distance(a: tuple[float, float], b: tuple[float, float]) -> float:
        return math.hypot(a[0] - b[0], a[1] - b[1])

    def _score(self, hand: DetectedHand, role: str, now_ms: int) -> float:
        score = 0.0
        label = self._normalized_label(hand.handedness_label)
        confidence = max(0.0, min(1.0, hand.handedness_confidence))

        if label == role:
            score += 2.8 * confidence
        elif label in {"left", "right"}:
            score -= 2.4 * confidence

        track = self.left_track if role == "left" else self.right_track
        if track.is_fresh(now_ms, self.ttl_ms) and track.wrist is not None:
            distance = self._distance(hand.wrist, track.wrist)
            continuity = 1.0 - min(1.0, distance / self.max_track_distance)
            score += 1.4 * continuity

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

    def _held_assignment(self, scored: tuple[int, int], trajectory: tuple[int, int],
                         now_ms: int) -> tuple[int, int]:
        if scored == trajectory:
            self._pending_assignment = None
            self._pending_since_ms = 0
            return scored

        if self._pending_assignment != scored:
            self._pending_assignment = scored
            self._pending_since_ms = now_ms
            return trajectory

        if now_ms - self._pending_since_ms < self.role_switch_hold_ms:
            return trajectory

        self._pending_assignment = None
        self._pending_since_ms = 0
        return scored

    def _single_track_role(self, hand: DetectedHand, now_ms: int) -> Optional[str]:
        left_fresh = self.left_track.is_fresh(now_ms, self.ttl_ms)
        right_fresh = self.right_track.is_fresh(now_ms, self.ttl_ms)
        left_distance = (self._distance(hand.wrist, self.left_track.wrist)
                         if left_fresh and self.left_track.wrist is not None else 999.0)
        right_distance = (self._distance(hand.wrist, self.right_track.wrist)
                          if right_fresh and self.right_track.wrist is not None else 999.0)

        close_threshold = 0.20
        if left_distance <= close_threshold and left_distance + 0.05 < right_distance:
            return "left"
        if right_distance <= close_threshold and right_distance + 0.05 < left_distance:
            return "right"
        return None

    def _direct_two_hand_assignment(self, hands: list[DetectedHand]) -> Optional[tuple[int, int]]:
        roles = [self._strong_role(hand) for hand in hands]
        if roles == ["left", "right"]:
            return (0, 1)
        if roles == ["right", "left"]:
            return (1, 0)

        # If exactly one detection has strong handedness, the other physical hand
        # is unambiguous in a two-hand frame. This avoids trajectory inertia from
        # preserving an old wrong assignment.
        if roles[0] in {"left", "right"} and roles[1] is None:
            return (0, 1) if roles[0] == "left" else (1, 0)
        if roles[1] in {"left", "right"} and roles[0] is None:
            return (1, 0) if roles[1] == "left" else (0, 1)
        return None

    def resolve(self, hands: list[DetectedHand], now_ms: int) -> tuple[Optional[DetectedHand], Optional[DetectedHand]]:
        hands = hands[:2]
        if not hands:
            return None, None

        left: Optional[DetectedHand] = None
        right: Optional[DetectedHand] = None

        if len(hands) == 1:
            hand = hands[0]

            # High-confidence handedness always wins over trajectory continuity.
            # The old order did the reverse and could keep one initial role
            # mistake alive for seconds.
            strong_role = self._strong_role(hand)
            if strong_role == "left":
                left = hand
            elif strong_role == "right":
                right = hand
            else:
                continuity_role = self._single_track_role(hand, now_ms)
                if continuity_role == "left":
                    left = hand
                elif continuity_role == "right":
                    right = hand
                else:
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
            direct = self._direct_two_hand_assignment(hands)
            if direct is not None:
                assignment = direct
                self._pending_assignment = None
                self._pending_since_ms = 0
            else:
                score_direct = self._score(hands[0], "left", now_ms) + self._score(hands[1], "right", now_ms)
                score_crossed = self._score(hands[1], "left", now_ms) + self._score(hands[0], "right", now_ms)
                scored = (0, 1) if score_direct >= score_crossed else (1, 0)
                trajectory = self._trajectory_preference(hands, now_ms)

                if trajectory is not None:
                    if abs(score_direct - score_crossed) < self.ambiguous_margin:
                        assignment = trajectory
                        self._pending_assignment = None
                        self._pending_since_ms = 0
                    else:
                        assignment = self._held_assignment(scored, trajectory, now_ms)
                else:
                    assignment = scored
                    self._pending_assignment = None
                    self._pending_since_ms = 0

            left, right = hands[assignment[0]], hands[assignment[1]]

        if left is not None:
            self.left_track.wrist = left.wrist
            self.left_track.last_seen_ms = now_ms
        if right is not None:
            self.right_track.wrist = right.wrist
            self.right_track.last_seen_ms = now_ms

        return left, right
