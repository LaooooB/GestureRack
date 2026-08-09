from __future__ import annotations

from dataclasses import dataclass
import math

RIGHT_GESTURES = {
    "Open_Palm",
    "Closed_Fist",
    "Victory",
    "Thumb_Up",
    "Thumb_Down",
    "Point_Right",
    "Point_Left",
}

CANNED_TO_CONTROL = {
    "Open_Palm": "Open_Palm",
    "Closed_Fist": "Closed_Fist",
    "Victory": "Victory",
    "Thumb_Up": "Thumb_Up",
    "Thumb_Down": "Thumb_Down",
}


@dataclass(frozen=True)
class RightGestureClassification:
    gesture: str = "None"
    confidence: float = 0.0


def _clamp01(value: float) -> float:
    return max(0.0, min(1.0, value))


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


def _finger_extension_score(landmarks: list[list[float]], mcp: int, pip: int, dip: int, tip: int) -> float:
    wrist = _point(landmarks, 0)
    p_mcp = _point(landmarks, mcp)
    p_pip = _point(landmarks, pip)
    p_dip = _point(landmarks, dip)
    p_tip = _point(landmarks, tip)

    straight_score = _clamp01(((_angle_degrees(p_mcp, p_pip, p_dip)
                                + _angle_degrees(p_pip, p_dip, p_tip)) * 0.5 - 112.0) / 55.0)
    reach_score = _clamp01((_distance(wrist, p_tip)
                            / max(1.0e-5, _distance(wrist, p_pip)) - 1.00) / 0.38)
    length_score = _clamp01((_distance(p_mcp, p_tip)
                             / max(1.0e-5, _distance(p_mcp, p_pip)) - 1.35) / 0.95)
    return 0.52 * straight_score + 0.30 * reach_score + 0.18 * length_score


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

    straight_score = _clamp01(((_angle_degrees(thumb_cmc, thumb_mcp, thumb_ip)
                                + _angle_degrees(thumb_mcp, thumb_ip, thumb_tip)) * 0.5 - 103.0) / 58.0)
    radial_score = _clamp01((_distance(thumb_tip, palm_center) / palm_width - 0.55) / 0.55)
    reach_score = _clamp01((_distance(thumb_tip, palm_center)
                            / max(1.0e-5, _distance(thumb_ip, palm_center)) - 1.01) / 0.38)
    return 0.42 * straight_score + 0.38 * radial_score + 0.20 * reach_score


def _pattern_confidence(scores: dict[str, float], expected: dict[str, bool]) -> float:
    confidence = [scores[name] if extended else 1.0 - scores[name]
                  for name, extended in expected.items()]
    return _clamp01(sum(confidence) / len(confidence))


def _pointing_candidate(landmarks: list[list[float]], scores: dict[str, float]) -> RightGestureClassification:
    shape = _pattern_confidence(scores, {
        "index": True,
        "middle": False,
        "ring": False,
        "pinky": False,
    })
    if shape < 0.72:
        return RightGestureClassification()

    index_mcp = _point(landmarks, 5)
    index_tip = _point(landmarks, 8)
    dx = index_tip[0] - index_mcp[0]
    dy = index_tip[1] - index_mcp[1]
    magnitude = math.hypot(dx, dy)
    if magnitude < 0.07:
        return RightGestureClassification()

    horizontal = abs(dx) / max(1.0e-6, magnitude)
    if horizontal < 0.78 or abs(dx) < 0.08:
        return RightGestureClassification()

    direction_confidence = (_clamp01((horizontal - 0.70) / 0.28)
                            * _clamp01((abs(dx) - 0.06) / 0.20))
    confidence = _clamp01(0.58 * shape + 0.42 * direction_confidence)
    return RightGestureClassification("Point_Right" if dx > 0.0 else "Point_Left", confidence)


def _thumb_direction_candidate(landmarks: list[list[float]], scores: dict[str, float]) -> RightGestureClassification:
    shape = _pattern_confidence(scores, {
        "thumb": True,
        "index": False,
        "middle": False,
        "ring": False,
        "pinky": False,
    })
    if shape < 0.66:
        return RightGestureClassification()

    thumb_mcp = _point(landmarks, 2)
    thumb_tip = _point(landmarks, 4)
    dx = thumb_tip[0] - thumb_mcp[0]
    dy = thumb_tip[1] - thumb_mcp[1]
    magnitude = math.hypot(dx, dy)
    if magnitude < 0.06:
        return RightGestureClassification()

    vertical = abs(dy) / max(1.0e-6, magnitude)
    if vertical < 0.68 or abs(dy) < 0.06:
        return RightGestureClassification()

    direction_confidence = (_clamp01((vertical - 0.60) / 0.35)
                            * _clamp01((abs(dy) - 0.05) / 0.20))
    confidence = _clamp01(0.58 * shape + 0.42 * direction_confidence)
    return RightGestureClassification("Thumb_Up" if dy < 0.0 else "Thumb_Down", confidence)


def classify_right_gesture(landmarks: list[list[float]], canned_gesture: str = "None",
                           canned_confidence: float = 0.0) -> RightGestureClassification:
    """Classify the seven physical-right control gestures.

    Point Right/Left intentionally share one index-pointing pose classifier and are
    split only by the index MCP -> tip direction. Because the camera frame is mirrored
    before MediaPipe runs, positive image X corresponds to the preview/user's right.
    """
    if len(landmarks) < 21:
        return RightGestureClassification()

    scores = {
        "index": _finger_extension_score(landmarks, 5, 6, 7, 8),
        "middle": _finger_extension_score(landmarks, 9, 10, 11, 12),
        "ring": _finger_extension_score(landmarks, 13, 14, 15, 16),
        "pinky": _finger_extension_score(landmarks, 17, 18, 19, 20),
        "thumb": _thumb_extension_score(landmarks),
    }

    point = _pointing_candidate(landmarks, scores)
    if point.gesture != "None" and point.confidence >= 0.72:
        return point

    geometry = {
        "Open_Palm": _pattern_confidence(scores, {
            "thumb": True, "index": True, "middle": True, "ring": True, "pinky": True,
        }),
        "Closed_Fist": _pattern_confidence(scores, {
            "index": False, "middle": False, "ring": False, "pinky": False,
        }),
        "Victory": _pattern_confidence(scores, {
            "index": True, "middle": True, "ring": False, "pinky": False,
        }),
    }

    thumb = _thumb_direction_candidate(landmarks, scores)
    if thumb.gesture != "None":
        geometry[thumb.gesture] = thumb.confidence

    canonical = CANNED_TO_CONTROL.get(canned_gesture, "None")
    if canonical != "None":
        geometry_confidence = geometry.get(canonical, 0.0)
        confidence = _clamp01(0.72 * _clamp01(canned_confidence) + 0.28 * geometry_confidence)
        if confidence >= 0.45:
            return RightGestureClassification(canonical, confidence)

    gesture, confidence = max(geometry.items(), key=lambda item: item[1])
    if confidence < 0.78:
        return RightGestureClassification()
    return RightGestureClassification(gesture, _clamp01(confidence))
