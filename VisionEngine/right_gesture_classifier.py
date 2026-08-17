from __future__ import annotations

from dataclasses import dataclass
import math

# Public control gestures. Left/right now use the thumb family as well; the C++
# side keeps backward parsing for old Point_Left/Point_Right saved mappings.
RIGHT_GESTURES = {
    "Open_Palm",
    "Closed_Fist",
    "Victory",
    "Thumb_Up",
    "Thumb_Down",
    "Thumb_Left",
    "Thumb_Right",
}

SHAPE_FAMILIES = {"OpenPalm", "FoldedFour", "Victory", "Other"}
THUMB_DIRECTIONS = {"Up", "Down", "Left", "Right"}

# Geometry is authoritative. MediaPipe's canned recognizer is only corroborating
# evidence for broad shape families; it never decides thumb direction.
CANNED_TO_FAMILY = {
    "Open_Palm": "OpenPalm",
    "Closed_Fist": "FoldedFour",
    "Thumb_Up": "FoldedFour",
    "Thumb_Down": "FoldedFour",
    "Victory": "Victory",
}

DEFAULT_FAMILY_MARGIN = 0.085
DEFAULT_FINAL_MARGIN = 0.060


@dataclass(frozen=True)
class RightGestureClassification:
    gesture: str = "None"
    confidence: float = 0.0
    runner_up_confidence: float = 0.0
    margin: float = 0.0
    family: str = "Other"
    family_confidence: float = 0.0
    thumb_state: str = "None"       # Tucked / Extended / Ambiguous / None
    direction: str = "None"         # Up / Down / Left / Right / Ambiguous / None
    direction_confidence: float = 0.0
    quality: float = 0.0


def _clamp01(value: float) -> float:
    return max(0.0, min(1.0, float(value)))


def _point(landmarks: list[list[float]], index: int) -> tuple[float, float]:
    return float(landmarks[index][0]), float(landmarks[index][1])


def _point3(landmarks: list[list[float]], index: int) -> tuple[float, float, float]:
    point = landmarks[index]
    return float(point[0]), float(point[1]), float(point[2] if len(point) > 2 else 0.0)


def _distance(a: tuple[float, float], b: tuple[float, float]) -> float:
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _distance3(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def _angle_degrees(a: tuple[float, float], b: tuple[float, float], c: tuple[float, float]) -> float:
    ba = (a[0] - b[0], a[1] - b[1])
    bc = (c[0] - b[0], c[1] - b[1])
    ba_len = math.hypot(*ba)
    bc_len = math.hypot(*bc)
    if ba_len < 1.0e-6 or bc_len < 1.0e-6:
        return 0.0
    cosine = max(-1.0, min(1.0, (ba[0] * bc[0] + ba[1] * bc[1]) / (ba_len * bc_len)))
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
    return _clamp01(0.50 * straight_score + 0.31 * reach_score + 0.19 * length_score)


def _palm_width(landmarks: list[list[float]]) -> float:
    return max(1.0e-5, _distance(_point(landmarks, 5), _point(landmarks, 17)))


def _palm_center(landmarks: list[list[float]]) -> tuple[float, float]:
    indices = (0, 5, 9, 13, 17)
    return (
        sum(float(landmarks[i][0]) for i in indices) / len(indices),
        sum(float(landmarks[i][1]) for i in indices) / len(indices),
    )


def _hand_quality(landmarks: list[list[float]]) -> float:
    """Stateless quality gate for obvious geometry that should not create ENTER.

    It deliberately stays soft: low-quality observations lose confidence rather
    than being rejected until geometry is genuinely too small/cropped. Temporal
    dropout protection is handled by GestureStabilizer.
    """
    if len(landmarks) < 21:
        return 0.0
    try:
        for point in landmarks[:21]:
            if len(point) < 2 or not all(math.isfinite(float(v)) for v in point[:3]):
                return 0.0
    except (TypeError, ValueError):
        return 0.0

    width = _palm_width(landmarks)
    if width < 0.035:
        return 0.0
    size_score = _clamp01((width - 0.035) / 0.065)

    cx, cy = _palm_center(landmarks)
    center_margin = min(cx, 1.0 - cx, cy, 1.0 - cy)
    center_score = _clamp01((center_margin - 0.015) / 0.11)

    # Palm anchors should not collapse into one point. Compare wrist-middle MCP
    # depth/length to palm width using all three coordinates when available.
    palm_length = _distance3(_point3(landmarks, 0), _point3(landmarks, 9))
    ratio = palm_length / max(width, 1.0e-5)
    geometry_score = _clamp01((ratio - 0.28) / 0.65)

    return _clamp01(0.48 * size_score + 0.34 * center_score + 0.18 * geometry_score)


def _thumb_extension_score(landmarks: list[list[float]]) -> float:
    """Measure thumb extension using several partially independent cues."""
    thumb_cmc = _point(landmarks, 1)
    thumb_mcp = _point(landmarks, 2)
    thumb_ip = _point(landmarks, 3)
    thumb_tip = _point(landmarks, 4)
    index_mcp = _point(landmarks, 5)
    palm_center = _palm_center(landmarks)
    palm_width = _palm_width(landmarks)

    # Distal IP straightness and MCP->tip reach are the most direction-invariant
    # cues. CMC orientation changes heavily when the user rotates the hand to
    # point the thumb left/right/up/down, so it is only weak supporting evidence.
    distal_straight = _clamp01((_angle_degrees(thumb_mcp, thumb_ip, thumb_tip) - 120.0) / 50.0)
    base_open = _clamp01((_angle_degrees(thumb_cmc, thumb_mcp, thumb_ip) - 55.0) / 100.0)
    radial_score = _clamp01((_distance(thumb_tip, palm_center) / palm_width - 0.48) / 0.62)
    segment_score = _clamp01((_distance(thumb_mcp, thumb_tip) / palm_width - 0.34) / 0.78)
    separation_score = _clamp01((_distance(thumb_tip, index_mcp) / palm_width - 0.30) / 0.90)

    return _clamp01(0.34 * distal_straight
                    + 0.33 * segment_score
                    + 0.13 * radial_score
                    + 0.12 * separation_score
                    + 0.08 * base_open)


def _pattern_confidence(scores: dict[str, float], expected: dict[str, bool]) -> float:
    terms = [scores[name] if extended else 1.0 - scores[name]
             for name, extended in expected.items()]
    return _clamp01(sum(terms) / max(1, len(terms)))


def _family_scores(scores: dict[str, float]) -> dict[str, float]:
    four = [scores["index"], scores["middle"], scores["ring"], scores["pinky"]]

    # Thumb is intentionally excluded from FoldedFour. This is the core design
    # change: Fist and all thumb directions first share one family, then a small
    # specialist decides tucked vs extended.
    folded_four = _pattern_confidence(scores, {
        "index": False, "middle": False, "ring": False, "pinky": False,
    })
    open_palm = _clamp01(0.75 * (sum(four) / 4.0) + 0.25 * min(four))
    victory = _pattern_confidence(scores, {
        "index": True, "middle": True, "ring": False, "pinky": False,
    })

    return {
        "OpenPalm": open_palm,
        "FoldedFour": folded_four,
        "Victory": victory,
    }


def _corroborate_family(family: str, geometry: float,
                        canned_gesture: str, canned_confidence: float) -> float:
    """Use canned recognition as weak corroboration, never as authority."""
    canned_family = CANNED_TO_FAMILY.get(canned_gesture)
    canned = _clamp01(canned_confidence)
    value = geometry
    if canned_family == family:
        value += 0.08 * canned * (1.0 - value)
    elif canned_family is not None and canned >= 0.80:
        value *= 0.94
    return _clamp01(value)


def _resolve_thumb_state(extension: float) -> tuple[str, float]:
    # Explicit no-man's-land is intentional. Hard Fist/Thumb errors are worse
    # than withholding a control event for a half-formed thumb.
    if extension <= 0.40:
        confidence = _clamp01((0.40 - extension) / 0.40)
        return "Tucked", 0.55 + 0.45 * confidence
    if extension >= 0.62:
        confidence = _clamp01((extension - 0.62) / 0.38)
        return "Extended", 0.55 + 0.45 * confidence
    return "Ambiguous", 0.0


def _resolve_thumb_direction(landmarks: list[list[float]]) -> tuple[str, float]:
    mcp = _point(landmarks, 2)
    ip = _point(landmarks, 3)
    tip = _point(landmarks, 4)
    palm_width = _palm_width(landmarks)

    # Blend MCP->Tip with IP->Tip. The long vector is stable; the distal vector
    # helps when the thumb base rotates with the wrist.
    long_dx, long_dy = tip[0] - mcp[0], tip[1] - mcp[1]
    distal_dx, distal_dy = tip[0] - ip[0], tip[1] - ip[1]
    dx = 0.78 * long_dx + 0.22 * distal_dx
    dy = 0.78 * long_dy + 0.22 * distal_dy
    magnitude = math.hypot(dx, dy)
    if magnitude < 1.0e-6:
        return "Ambiguous", 0.0

    horizontal = abs(dx) / magnitude
    vertical = abs(dy) / magnitude
    dominance = max(horizontal, vertical)

    # 45-degree boundaries are deliberately a dead zone. Clear directions can
    # still be noticeably off-axis, but diagonals will not chatter between two
    # commands.
    axis_score = _clamp01((dominance - 0.74) / 0.22)
    displacement = magnitude / palm_width
    reach_score = _clamp01((displacement - 0.30) / 1.00)
    confidence = _clamp01(0.72 * axis_score + 0.28 * reach_score)

    if dominance < 0.78 or confidence < 0.24:
        return "Ambiguous", confidence
    if horizontal > vertical:
        return ("Right" if dx > 0.0 else "Left"), confidence
    return ("Down" if dy > 0.0 else "Up"), confidence


def _make_none(*, confidence: float = 0.0, runner_up: float = 0.0,
               margin: float = 0.0, family: str = "Other",
               family_confidence: float = 0.0, thumb_state: str = "None",
               direction: str = "None", direction_confidence: float = 0.0,
               quality: float = 0.0) -> RightGestureClassification:
    return RightGestureClassification(
        "None", _clamp01(confidence), _clamp01(runner_up), _clamp01(margin),
        family, _clamp01(family_confidence), thumb_state, direction,
        _clamp01(direction_confidence), _clamp01(quality),
    )


def classify_right_gesture(landmarks: list[list[float]], canned_gesture: str = "None",
                           canned_confidence: float = 0.0) -> RightGestureClassification:
    """Hierarchical physical-right-hand classifier.

    Pipeline: quality -> broad shape family -> family specialist -> direction.
    Fist and thumb commands never compete as peer classes. Thumb direction is
    deterministic geometry, not a learned/canned class.
    """
    if len(landmarks) < 21:
        return RightGestureClassification()

    quality = _hand_quality(landmarks)
    if quality < 0.22:
        return _make_none(quality=quality)

    scores = {
        "index": _finger_extension_score(landmarks, 5, 6, 7, 8),
        "middle": _finger_extension_score(landmarks, 9, 10, 11, 12),
        "ring": _finger_extension_score(landmarks, 13, 14, 15, 16),
        "pinky": _finger_extension_score(landmarks, 17, 18, 19, 20),
    }
    family_scores = _family_scores(scores)
    family_scores = {
        family: _corroborate_family(family, value, canned_gesture, canned_confidence)
        for family, value in family_scores.items()
    }
    ranked_families = sorted(family_scores.items(), key=lambda item: item[1], reverse=True)
    family, family_confidence = ranked_families[0]
    family_runner = ranked_families[1][1] if len(ranked_families) > 1 else 0.0
    family_margin = family_confidence - family_runner

    quality_factor = 0.72 + 0.28 * quality
    family_confidence *= quality_factor

    if family_margin < DEFAULT_FAMILY_MARGIN and family_confidence < 0.90:
        return _make_none(
            confidence=family_confidence,
            runner_up=family_runner,
            margin=max(0.0, family_margin),
            family="Other",
            family_confidence=family_confidence,
            quality=quality,
        )

    if family == "OpenPalm":
        return RightGestureClassification(
            "Open_Palm", _clamp01(family_confidence), _clamp01(family_runner),
            _clamp01(family_margin), family, _clamp01(family_confidence),
            "None", "None", 0.0, quality,
        )

    if family == "Victory":
        return RightGestureClassification(
            "Victory", _clamp01(family_confidence), _clamp01(family_runner),
            _clamp01(family_margin), family, _clamp01(family_confidence),
            "None", "None", 0.0, quality,
        )

    thumb_extension = _thumb_extension_score(landmarks)
    thumb_state, thumb_confidence = _resolve_thumb_state(thumb_extension)
    if thumb_state == "Ambiguous":
        return _make_none(
            confidence=0.70 * family_confidence,
            runner_up=family_runner,
            margin=max(0.0, family_margin),
            family="FoldedFour",
            family_confidence=family_confidence,
            thumb_state="Ambiguous",
            quality=quality,
        )

    if thumb_state == "Tucked":
        confidence = _clamp01(0.72 * family_confidence + 0.28 * thumb_confidence)
        return RightGestureClassification(
            "Closed_Fist", confidence, _clamp01(family_runner),
            _clamp01(max(0.0, confidence - family_runner)),
            "FoldedFour", _clamp01(family_confidence), "Tucked",
            "None", 0.0, quality,
        )

    direction, direction_confidence = _resolve_thumb_direction(landmarks)
    if direction == "Ambiguous":
        return _make_none(
            confidence=0.65 * family_confidence,
            runner_up=family_runner,
            margin=max(0.0, family_margin),
            family="FoldedFour",
            family_confidence=family_confidence,
            thumb_state="Extended",
            direction="Ambiguous",
            direction_confidence=direction_confidence,
            quality=quality,
        )

    gesture = f"Thumb_{direction}"
    confidence = _clamp01(0.58 * family_confidence
                          + 0.24 * thumb_confidence
                          + 0.18 * direction_confidence)
    runner_up = max(family_runner, confidence - max(DEFAULT_FINAL_MARGIN, direction_confidence * 0.12))
    margin = max(0.0, confidence - runner_up)

    return RightGestureClassification(
        gesture, confidence, _clamp01(runner_up), _clamp01(margin),
        "FoldedFour", _clamp01(family_confidence), "Extended", direction,
        _clamp01(direction_confidence), quality,
    )
