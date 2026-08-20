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


def _near_score(value: float, full_at: float, zero_at: float) -> float:
    if value <= full_at:
        return 1.0
    if value >= zero_at:
        return 0.0
    return 1.0 - (value - full_at) / max(1.0e-6, zero_at - full_at)


def _far_score(value: float, zero_at: float, full_at: float) -> float:
    if value <= zero_at:
        return 0.0
    if value >= full_at:
        return 1.0
    return (value - zero_at) / max(1.0e-6, full_at - zero_at)


def _band_score(value: float,
                outer_low: float,
                inner_low: float,
                inner_high: float,
                outer_high: float) -> float:
    if value <= outer_low or value >= outer_high:
        return 0.0
    if inner_low <= value <= inner_high:
        return 1.0
    if value < inner_low:
        return (value - outer_low) / max(1.0e-6, inner_low - outer_low)
    return (outer_high - value) / max(1.0e-6, outer_high - inner_high)


def _landmarks_valid(landmarks: list[list[float]]) -> bool:
    if len(landmarks) < 21:
        return False
    for point in landmarks[:21]:
        if len(point) < 2:
            return False
        if not math.isfinite(float(point[0])) or not math.isfinite(float(point[1])):
            return False
    return True


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


def classify_slot_1_to_10(landmarks: list[list[float]]) -> SlotClassification:
    """Classify the physical-left hand using the common Chinese 1..10 hand signs.

    Shapes:
      1  = index
      2  = index + middle
      3  = index + middle + ring
      4  = four non-thumb fingers, thumb tucked
      5  = open palm
      6  = thumb + pinky
      7  = thumb/index/middle tips gathered into a pinch cluster
      8  = thumb + index
      9  = hooked index, remaining fingers folded
      10 = closed fist

    The classifier deliberately returns unknown for close/ambiguous candidates. Slot
    selection is a discrete command, so a short miss is safer than selecting the
    wrong hosted plug-in while the hand is transitioning between shapes.
    """
    if not _landmarks_valid(landmarks):
        return SlotClassification()

    finger_indices = {
        "index": (5, 6, 7, 8),
        "middle": (9, 10, 11, 12),
        "ring": (13, 14, 15, 16),
        "pinky": (17, 18, 19, 20),
    }
    scores = {
        name: _finger_extension_score(landmarks, *indices)
        for name, indices in finger_indices.items()
    }
    scores["thumb"] = _thumb_extension_score(landmarks)

    wrist = _point(landmarks, 0)
    index_mcp = _point(landmarks, 5)
    middle_mcp = _point(landmarks, 9)
    pinky_mcp = _point(landmarks, 17)
    palm_center = ((wrist[0] + index_mcp[0] + middle_mcp[0] + pinky_mcp[0]) / 4.0,
                   (wrist[1] + index_mcp[1] + middle_mcp[1] + pinky_mcp[1]) / 4.0)
    palm_width = max(1.0e-5, _distance(index_mcp, pinky_mcp))

    tips = {
        name: _point(landmarks, indices[3])
        for name, indices in finger_indices.items()
    }
    tips["thumb"] = _point(landmarks, 4)
    tip_radius = {
        name: _distance(point, palm_center) / palm_width
        for name, point in tips.items()
    }

    def pattern_score(expected: dict[str, bool]) -> float:
        values = [scores[name] if should_extend else 1.0 - scores[name]
                  for name, should_extend in expected.items()]
        return sum(values) / len(values)

    # Extension-pattern digits. Including the thumb state for 1..4 is important:
    # without it the Chinese 8 sign can look like a high-confidence 1.
    patterns = {
        1: {"index": True, "middle": False, "ring": False, "pinky": False, "thumb": False},
        2: {"index": True, "middle": True, "ring": False, "pinky": False, "thumb": False},
        3: {"index": True, "middle": True, "ring": True, "pinky": False, "thumb": False},
        4: {"index": True, "middle": True, "ring": True, "pinky": True, "thumb": False},
        5: {"index": True, "middle": True, "ring": True, "pinky": True, "thumb": True},
        6: {"index": False, "middle": False, "ring": False, "pinky": True, "thumb": True},
        8: {"index": True, "middle": False, "ring": False, "pinky": False, "thumb": True},
    }
    candidates = {slot: pattern_score(expected) for slot, expected in patterns.items()}

    # 6 and 8 both use two extended digits. A normalized tip-separation term keeps
    # partially-open transitions from becoming confident slot changes.
    thumb_pinky_separation = _distance(tips["thumb"], tips["pinky"]) / palm_width
    candidates[6] = (0.85 * candidates[6]
                     + 0.15 * _far_score(thumb_pinky_separation, 0.55, 1.25))

    thumb_index_separation = _distance(tips["thumb"], tips["index"]) / palm_width
    candidates[8] = (0.85 * candidates[8]
                     + 0.15 * _far_score(thumb_index_separation, 0.25, 0.80))

    # Chinese 7 is a pinch-like shape. Counted-finger logic is unreliable here;
    # instead require the thumb/index/middle tips to form a tight cluster away from
    # the palm while ring and pinky remain folded.
    cluster_points = [tips["thumb"], tips["index"], tips["middle"]]
    cluster_span = max(
        _distance(cluster_points[i], cluster_points[j])
        for i in range(3)
        for j in range(i + 1, 3)
    ) / palm_width
    cluster_centroid = (
        sum(point[0] for point in cluster_points) / 3.0,
        sum(point[1] for point in cluster_points) / 3.0,
    )
    cluster_score = _near_score(cluster_span, 0.18, 0.58)
    cluster_away_from_palm = _far_score(
        _distance(cluster_centroid, palm_center) / palm_width, 0.32, 0.72)
    ring_pinky_folded = ((1.0 - scores["ring"]) + (1.0 - scores["pinky"])) * 0.5
    triad_reach = _far_score(
        max(tip_radius["thumb"], tip_radius["index"], tip_radius["middle"]), 0.45, 0.90)
    candidates[7] = (0.45 * cluster_score
                     + 0.25 * cluster_away_from_palm
                     + 0.20 * ring_pinky_folded
                     + 0.10 * triad_reach)

    # Chinese 9 is defined by a raised hooked index rather than a simple extended
    # finger. Joint bend + tip distance separates it from the closed fist (10).
    index_pip_angle = _angle_degrees(_point(landmarks, 5), _point(landmarks, 6), _point(landmarks, 7))
    index_dip_angle = _angle_degrees(_point(landmarks, 6), _point(landmarks, 7), _point(landmarks, 8))
    hook_bend = (
        _band_score(index_pip_angle, 45.0, 70.0, 145.0, 172.0)
        + _band_score(index_dip_angle, 35.0, 60.0, 145.0, 172.0)
    ) * 0.5
    partial_index = _band_score(scores["index"], 0.0, 0.08, 0.58, 0.82)
    other_digits_folded = (
        (1.0 - scores["middle"])
        + (1.0 - scores["ring"])
        + (1.0 - scores["pinky"])
        + (1.0 - scores["thumb"])
    ) * 0.25
    index_away_from_palm = _far_score(tip_radius["index"], 0.42, 0.85)
    hook_evidence = hook_bend * index_away_from_palm * other_digits_folded
    candidates[9] = (0.40 * hook_bend
                     + 0.10 * partial_index
                     + 0.25 * other_digits_folded
                     + 0.25 * index_away_from_palm)

    # 10 is a compact fist. Penalise it when the index geometry already supplies
    # strong hook evidence, which is the main 9-vs-10 failure mode.
    fist_pattern = pattern_score(
        {"index": False, "middle": False, "ring": False, "pinky": False, "thumb": False})
    compact_radius = sum(tip_radius[name] for name in ("index", "middle", "ring", "pinky")) / 4.0
    fist_compactness = _near_score(compact_radius, 0.62, 1.00)
    candidates[10] = (0.82 * fist_pattern + 0.18 * fist_compactness) * (1.0 - 0.65 * hook_evidence)

    ranked = sorted(candidates.items(), key=lambda item: item[1], reverse=True)
    best_slot, best_score = ranked[0]
    second_score = ranked[1][1]

    # Candidate confidence and separation are both required. The stabilizer applies
    # the final temporal confidence threshold on top of this geometric gate.
    if best_score < 0.68 or best_score - second_score < 0.07:
        return SlotClassification()

    return SlotClassification(slot=best_slot, confidence=_clamp01(best_score))


def classify_slot_1_to_5(landmarks: list[list[float]]) -> SlotClassification:
    """Compatibility entry point used by older VisionEngine builds.

    It intentionally delegates to the full 1..10 classifier so a mixed-version
    sidecar/runtime does not silently lose slots 6..10.
    """
    return classify_slot_1_to_10(landmarks)


@dataclass
class SlotStabilizer:
    hold_ms: int = 150
    min_confidence: float = 0.80
    stable: int = 0
    candidate: int = 0
    candidate_since_ms: int = 0

    def update(self, raw_slot: int, confidence: float, now_ms: int) -> int:
        valid = 1 <= raw_slot <= 10 and confidence >= self.min_confidence
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
