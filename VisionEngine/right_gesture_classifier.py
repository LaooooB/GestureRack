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

# Classifier ambiguity gate. Temporal/per-class activation thresholds live in
# gesture_stabilizer.py.
DEFAULT_TOP2_MARGIN = 0.055


@dataclass(frozen=True)
class RightGestureClassification:
    gesture: str = "None"
    confidence: float = 0.0
    runner_up_confidence: float = 0.0
    margin: float = 0.0


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
                                + _angle_degrees(p_pip, p_dip, p_tip)) * 0.5 - 108.0) / 58.0)
    reach_score = _clamp01((_distance(wrist, p_tip)
                            / max(1.0e-5, _distance(wrist, p_pip)) - 0.98) / 0.40)
    length_score = _clamp01((_distance(p_mcp, p_tip)
                             / max(1.0e-5, _distance(p_mcp, p_pip)) - 1.30) / 1.00)
    return 0.50 * straight_score + 0.31 * reach_score + 0.19 * length_score


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
                                + _angle_degrees(thumb_mcp, thumb_ip, thumb_tip)) * 0.5 - 100.0) / 62.0)
    radial_score = _clamp01((_distance(thumb_tip, palm_center) / palm_width - 0.48) / 0.62)
    reach_score = _clamp01((_distance(thumb_tip, palm_center)
                            / max(1.0e-5, _distance(thumb_ip, palm_center)) - 0.98) / 0.42)
    return 0.40 * straight_score + 0.39 * radial_score + 0.21 * reach_score


def _pattern_confidence(scores: dict[str, float], expected: dict[str, bool]) -> float:
    confidence = [scores[name] if extended else 1.0 - scores[name]
                  for name, extended in expected.items()]
    return _clamp01(sum(confidence) / len(confidence))


def _palm_width(landmarks: list[list[float]]) -> float:
    return max(1.0e-5, _distance(_point(landmarks, 5), _point(landmarks, 17)))


def _shape_scores(scores: dict[str, float]) -> dict[str, float]:
    # Open Palm deliberately does NOT require a fully extended thumb. Natural
    # palms often have a relaxed/adducted thumb; four non-thumb fingers are the
    # primary shape evidence and thumb extension is only a small bonus.
    four = [scores["index"], scores["middle"], scores["ring"], scores["pinky"]]
    open_palm = _clamp01(0.72 * (sum(four) / 4.0)
                         + 0.22 * min(four)
                         + 0.06 * scores["thumb"])

    fist_base = _pattern_confidence(scores, {
        "index": False, "middle": False, "ring": False, "pinky": False,
    })
    # A thumb-only pose also has four folded fingers. Penalise Fist when the
    # thumb is clearly extended so Thumb Up/Down does not lose to a false fist.
    fist = _clamp01(fist_base * (1.0 - 0.55 * scores["thumb"]))
    victory = _pattern_confidence(scores, {
        "index": True, "middle": True, "ring": False, "pinky": False,
    })
    thumb_only = _pattern_confidence(scores, {
        "thumb": True, "index": False, "middle": False, "ring": False, "pinky": False,
    })
    # The thumb is intentionally omitted from PointOnly. Users naturally leave
    # it in several positions while pointing; requiring it folded caused misses.
    point_only = _pattern_confidence(scores, {
        "index": True, "middle": False, "ring": False, "pinky": False,
    })

    return {
        "Open_Palm": open_palm,
        "Closed_Fist": fist,
        "Victory": victory,
        "ThumbOnly": thumb_only,
        "PointOnly": point_only,
    }


def _direction_confidence(dx: float, dy: float, primary: str, palm_width: float) -> float:
    magnitude = math.hypot(dx, dy)
    if magnitude < 1.0e-6:
        return 0.0
    if primary == "horizontal":
        axis = abs(dx) / magnitude
        displacement = abs(dx) / palm_width
    else:
        axis = abs(dy) / magnitude
        displacement = abs(dy) / palm_width

    # The old 0.78/0.68 hard axis ratios rejected normal wrist rotation. The
    # shape head already proves this is a point/thumb pose; this head only decides
    # direction, so the directional gate can be softer.
    axis_score = _clamp01((axis - 0.50) / 0.40)
    displacement_score = _clamp01((displacement - 0.32) / 0.95)
    return _clamp01(0.66 * axis_score + 0.34 * displacement_score)


def _canned_boost(gesture: str, geometry: float,
                  canned_gesture: str, canned_confidence: float) -> float:
    canonical = CANNED_TO_CONTROL.get(canned_gesture, "None")
    if canonical != gesture:
        return geometry
    canned = _clamp01(canned_confidence)
    # Canned recognition is a strong independent signal for these poses, but
    # geometry still contributes so an occasional wrong canned result does not
    # automatically own the output.
    fused = 0.74 * canned + 0.26 * geometry
    return _clamp01(max(geometry, fused))


def classify_right_gesture(landmarks: list[list[float]], canned_gesture: str = "None",
                           canned_confidence: float = 0.0) -> RightGestureClassification:
    """Classify the seven physical-right control gestures.

    Shape and direction are separate heads. Open/Fist/Victory are shape classes;
    ThumbOnly and PointOnly are shape classes whose final control meaning is
    decided in image coordinates (Up/Down or Left/Right).
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
    shapes = _shape_scores(scores)

    candidates: dict[str, float] = {
        "Open_Palm": _canned_boost("Open_Palm", shapes["Open_Palm"],
                                     canned_gesture, canned_confidence),
        "Closed_Fist": _canned_boost("Closed_Fist", shapes["Closed_Fist"],
                                       canned_gesture, canned_confidence),
        "Victory": _canned_boost("Victory", shapes["Victory"],
                                   canned_gesture, canned_confidence),
    }

    palm_width = _palm_width(landmarks)

    # Thumb direction head. Do not require an almost perfectly vertical thumb;
    # users rotate the wrist naturally. Shape confidence carries most weight.
    thumb_mcp = _point(landmarks, 2)
    thumb_tip = _point(landmarks, 4)
    thumb_dx = thumb_tip[0] - thumb_mcp[0]
    thumb_dy = thumb_tip[1] - thumb_mcp[1]
    thumb_direction = _direction_confidence(thumb_dx, thumb_dy, "vertical", palm_width)
    thumb_shape = shapes["ThumbOnly"]
    if thumb_shape >= 0.50 and thumb_direction >= 0.20:
        thumb_score = _clamp01(0.74 * thumb_shape + 0.26 * thumb_direction)
        thumb_name = "Thumb_Up" if thumb_dy < 0.0 else "Thumb_Down"
        candidates[thumb_name] = _canned_boost(
            thumb_name, thumb_score, canned_gesture, canned_confidence)

    # Point direction is independent of MediaPipe Pointing_Up. Horizontal point
    # must not first be misclassified as Up in order to become Left/Right.
    index_mcp = _point(landmarks, 5)
    index_tip = _point(landmarks, 8)
    point_dx = index_tip[0] - index_mcp[0]
    point_dy = index_tip[1] - index_mcp[1]
    point_direction = _direction_confidence(point_dx, point_dy, "horizontal", palm_width)
    point_shape = shapes["PointOnly"]
    if point_shape >= 0.54 and point_direction >= 0.24:
        point_score = _clamp01(0.72 * point_shape + 0.28 * point_direction)
        candidates["Point_Right" if point_dx > 0.0 else "Point_Left"] = point_score

    ranked = sorted(candidates.items(), key=lambda item: item[1], reverse=True)
    if not ranked:
        return RightGestureClassification()

    gesture, confidence = ranked[0]
    runner_up = ranked[1][1] if len(ranked) > 1 else 0.0
    margin = confidence - runner_up

    # Ambiguous poses are safer as None. A very strong winner can pass even when
    # related score heads are numerically close.
    if margin < DEFAULT_TOP2_MARGIN and confidence < 0.90:
        return RightGestureClassification("None", confidence, runner_up, margin)

    return RightGestureClassification(gesture, _clamp01(confidence),
                                      _clamp01(runner_up), _clamp01(margin))
