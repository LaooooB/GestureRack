from __future__ import annotations

from dataclasses import dataclass
import math


@dataclass(frozen=True)
class SlotClassification:
    slot: int = 0
    confidence: float = 0.0


def _point(landmarks: list[list[float]], index: int) -> tuple[float, float]:
    return float(landmarks[index][0]), float(landmarks[index][1])


def _distance(a: tuple[float, float], b: tuple[float, float]) -> float:
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _angle_degrees(a: tuple[float, float], b: tuple[float, float], c: tuple[float, float]) -> float:
    ba = (a[0] - b[0], a[1] - b[1])
    bc = (c[0] - b[0], c[1] - b[1])
    ba_len = math.hypot(*ba)
    bc_len = math.hypot(*bc)
    if ba_len < 1.0e-6 or bc_len < 1.0e-6:
        return 0.0
    dot = ba[0] * bc[0] + ba[1] * bc[1]
    cosine = max(-1.0, min(1.0, dot / (ba_len * bc_len)))
    return math.degrees(math.acos(cosine))


def _clamp01(value: float) -> float:
    return max(0.0, min(1.0, value))


def _finger_extension_score(landmarks: list[list[float]], mcp: int, pip: int, dip: int, tip: int) -> float:
    wrist = _point(landmarks, 0)
    p_mcp = _point(landmarks, mcp)
    p_pip = _point(landmarks, pip)
    p_dip = _point(landmarks, dip)
    p_tip = _point(landmarks, tip)

    pip_angle = _angle_degrees(p_mcp, p_pip, p_dip)
    dip_angle = _angle_degrees(p_pip, p_dip, p_tip)
    straight_score = _clamp01(((pip_angle + dip_angle) * 0.5 - 115.0) / 50.0)

    wrist_tip = _distance(wrist, p_tip)
    wrist_pip = max(1.0e-5, _distance(wrist, p_pip))
    reach_ratio = wrist_tip / wrist_pip
    reach_score = _clamp01((reach_ratio - 1.02) / 0.34)

    mcp_tip = _distance(p_mcp, p_tip)
    mcp_pip = max(1.0e-5, _distance(p_mcp, p_pip))
    length_ratio = mcp_tip / mcp_pip
    length_score = _clamp01((length_ratio - 1.45) / 0.85)

    return 0.50 * straight_score + 0.30 * reach_score + 0.20 * length_score


def _thumb_extension_score(landmarks: list[list[float]]) -> float:
    wrist = _point(landmarks, 0)
    thumb_cmc = _point(landmarks, 1)
    thumb_mcp = _point(landmarks, 2)
    thumb_ip = _point(landmarks, 3)
    thumb_tip = _point(landmarks, 4)
    index_mcp = _point(landmarks, 5)
    middle_mcp = _point(landmarks, 9)
    pinky_mcp = _point(landmarks, 17)

    palm_center = ((wrist[0] + index_mcp[0] + middle_mcp[0] + pinky_mcp[0]) / 4.0,
                   (wrist[1] + index_mcp[1] + middle_mcp[1] + pinky_mcp[1]) / 4.0)
    palm_width = max(1.0e-5, _distance(index_mcp, pinky_mcp))

    mcp_angle = _angle_degrees(thumb_cmc, thumb_mcp, thumb_ip)
    ip_angle = _angle_degrees(thumb_mcp, thumb_ip, thumb_tip)
    straight_score = _clamp01(((mcp_angle + ip_angle) * 0.5 - 105.0) / 55.0)

    radial_distance = _distance(thumb_tip, palm_center) / palm_width
    radial_score = _clamp01((radial_distance - 0.58) / 0.50)

    tip_vs_ip = (_distance(thumb_tip, palm_center)
                 / max(1.0e-5, _distance(thumb_ip, palm_center)))
    reach_score = _clamp01((tip_vs_ip - 1.03) / 0.35)

    return 0.40 * straight_score + 0.40 * radial_score + 0.20 * reach_score


def classify_slot_1_to_5(landmarks: list[list[float]]) -> SlotClassification:
    """Classify the physical-left hand as logical slot 1..5.

    Baseline hand shapes:
      1 = index
      2 = index + middle
      3 = index + middle + ring
      4 = index + middle + ring + pinky, thumb tucked
      5 = all five fingers extended

    Slots 6..9 are intentionally not classified here.
    """
    if len(landmarks) < 21:
        return SlotClassification()

    scores = {
        "index": _finger_extension_score(landmarks, 5, 6, 7, 8),
        "middle": _finger_extension_score(landmarks, 9, 10, 11, 12),
        "ring": _finger_extension_score(landmarks, 13, 14, 15, 16),
        "pinky": _finger_extension_score(landmarks, 17, 18, 19, 20),
        "thumb": _thumb_extension_score(landmarks),
    }

    extended = {name: score >= 0.58 for name, score in scores.items()}
    non_thumb = [extended["index"], extended["middle"], extended["ring"], extended["pinky"]]

    slot = 0
    expected: dict[str, bool] = {}
    if non_thumb == [True, False, False, False]:
        slot = 1
        expected = {"index": True, "middle": False, "ring": False, "pinky": False}
    elif non_thumb == [True, True, False, False]:
        slot = 2
        expected = {"index": True, "middle": True, "ring": False, "pinky": False}
    elif non_thumb == [True, True, True, False]:
        slot = 3
        expected = {"index": True, "middle": True, "ring": True, "pinky": False}
    elif non_thumb == [True, True, True, True] and not extended["thumb"]:
        slot = 4
        expected = {"index": True, "middle": True, "ring": True, "pinky": True, "thumb": False}
    elif non_thumb == [True, True, True, True] and extended["thumb"]:
        slot = 5
        expected = {"index": True, "middle": True, "ring": True, "pinky": True, "thumb": True}

    if slot == 0:
        return SlotClassification()

    confidences = [scores[name] if should_extend else 1.0 - scores[name]
                   for name, should_extend in expected.items()]
    confidence = sum(confidences) / len(confidences)

    if slot <= 3:
        confidence *= 1.0 - 0.18 * scores["thumb"]

    return SlotClassification(slot=slot, confidence=_clamp01(confidence))


@dataclass
class SlotStabilizer:
    hold_ms: int = 150
    min_confidence: float = 0.80
    stable: int = 0
    candidate: int = 0
    candidate_since_ms: int = 0

    def update(self, raw_slot: int, confidence: float, now_ms: int) -> int:
        valid = 1 <= raw_slot <= 5 and confidence >= self.min_confidence
        if not valid:
            self.candidate = 0
            self.candidate_since_ms = 0
            return self.stable

        if raw_slot != self.candidate:
            self.candidate = raw_slot
            self.candidate_since_ms = now_ms
            return self.stable

        if self.candidate_since_ms and now_ms - self.candidate_since_ms >= self.hold_ms:
            self.stable = raw_slot

        return self.stable
