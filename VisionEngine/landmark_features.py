from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Sequence

import numpy as np


FEATURE_VERSION = 1
LANDMARK_COUNT = 21
PALM_INDICES = (0, 5, 9, 13, 17)
FINGERTIP_INDICES = (4, 8, 12, 16, 20)


@dataclass(frozen=True)
class LandmarkFeatureVector:
    values: np.ndarray
    used_world_landmarks: bool
    palm_scale: float


def _points(values: Sequence[Sequence[float]]) -> np.ndarray:
    array = np.asarray(values, dtype=np.float32)
    if array.shape != (LANDMARK_COUNT, 3) or not np.isfinite(array).all():
        raise ValueError("expected 21 finite xyz landmarks")
    return array


def _safe_unit(vector: np.ndarray, fallback: np.ndarray) -> np.ndarray:
    norm = float(np.linalg.norm(vector))
    if norm < 1.0e-6:
        vector = fallback
        norm = float(np.linalg.norm(vector))
    if norm < 1.0e-6:
        raise ValueError("degenerate hand geometry")
    return vector / norm


def _joint_straightness(points: np.ndarray, a: int, b: int, c: int) -> float:
    ba = points[a] - points[b]
    bc = points[c] - points[b]
    ba_norm = float(np.linalg.norm(ba))
    bc_norm = float(np.linalg.norm(bc))
    if ba_norm < 1.0e-6 or bc_norm < 1.0e-6:
        return 0.0
    cosine = float(np.clip(np.dot(ba, bc) / (ba_norm * bc_norm), -1.0, 1.0))
    # Opposing bone vectors are a straight finger: angle pi -> score 1.
    return float(math.acos(cosine) / math.pi)


def _finger_straightness(points: np.ndarray) -> np.ndarray:
    groups = (
        (1, 2, 3, 4),
        (5, 6, 7, 8),
        (9, 10, 11, 12),
        (13, 14, 15, 16),
        (17, 18, 19, 20),
    )
    result = []
    for mcp, pip, dip, tip in groups:
        first = _joint_straightness(points, mcp, pip, dip)
        second = _joint_straightness(points, pip, dip, tip)
        result.append(0.5 * (first + second))
    return np.asarray(result, dtype=np.float32)


def _palm_basis(points: np.ndarray) -> tuple[np.ndarray, np.ndarray, float]:
    palm_center = points[list(PALM_INDICES)].mean(axis=0)
    across = points[17] - points[5]
    palm_scale = float(np.linalg.norm(across))
    if palm_scale < 1.0e-6:
        palm_scale = float(np.linalg.norm(points[9] - points[0]))
    if palm_scale < 1.0e-6:
        raise ValueError("degenerate palm scale")

    x_axis = _safe_unit(across, np.asarray([1.0, 0.0, 0.0], dtype=np.float32))
    forward = points[9] - points[0]
    forward = forward - float(np.dot(forward, x_axis)) * x_axis
    y_axis = _safe_unit(forward, np.asarray([0.0, 1.0, 0.0], dtype=np.float32))
    z_axis = _safe_unit(np.cross(x_axis, y_axis), np.asarray([0.0, 0.0, 1.0], dtype=np.float32))
    # Recompute y to keep the basis orthonormal after fallbacks.
    y_axis = _safe_unit(np.cross(z_axis, x_axis), y_axis)
    basis = np.stack((x_axis, y_axis, z_axis), axis=0).astype(np.float32)
    return palm_center.astype(np.float32), basis, palm_scale


def _normalized_screen_direction(normalized: np.ndarray, tip: int, base: int,
                                 palm_width: float) -> tuple[float, float]:
    direction = normalized[tip, :2] - normalized[base, :2]
    scale = max(1.0e-5, palm_width)
    return float(direction[0] / scale), float(direction[1] / scale)


def extract_landmark_features(
    normalized_landmarks: Sequence[Sequence[float]],
    world_landmarks: Sequence[Sequence[float]] | None = None,
) -> LandmarkFeatureVector:
    """Create a compact feature vector for the future tiny gesture classifier.

    Shape features use a palm-local 3D coordinate frame. Translation, distance
    from the camera, and most hand rotation therefore do not force the model to
    relearn the same pose. A small set of image-space direction features is kept
    deliberately so Point_Left/Point_Right and Thumb_Up/Thumb_Down remain tied to
    the user's visible screen direction rather than becoming rotation invariant.

    World landmarks are preferred when MediaPipe supplies a complete 21x3 set;
    normalized landmarks remain a deterministic fallback for older runtimes.
    """
    normalized = _points(normalized_landmarks)
    source = normalized
    used_world = False
    if world_landmarks is not None:
        try:
            candidate = _points(world_landmarks)
            # Reject an all-collapsed world set even if its shape is technically valid.
            if float(np.linalg.norm(candidate[17] - candidate[5])) > 1.0e-6:
                source = candidate
                used_world = True
        except ValueError:
            pass

    palm_center, basis, palm_scale = _palm_basis(source)
    local = ((source - palm_center) @ basis.T) / palm_scale

    straightness = _finger_straightness(source)
    radial = np.asarray([
        float(np.linalg.norm(source[index] - palm_center) / palm_scale)
        for index in FINGERTIP_INDICES
    ], dtype=np.float32)

    screen_palm_width = float(np.linalg.norm(normalized[17, :2] - normalized[5, :2]))
    screen_vectors = np.asarray([
        *_normalized_screen_direction(normalized, 4, 2, screen_palm_width),
        *_normalized_screen_direction(normalized, 8, 5, screen_palm_width),
        *_normalized_screen_direction(normalized, 12, 9, screen_palm_width),
        *_normalized_screen_direction(normalized, 9, 0, screen_palm_width),
    ], dtype=np.float32)

    values = np.concatenate((
        local.reshape(-1).astype(np.float32),       # 63 palm-local xyz values
        straightness,                               # 5 finger shape values
        radial,                                     # 5 fingertip reach values
        screen_vectors,                             # 8 global direction values
    )).astype(np.float32)

    # 81 floats: tiny enough for a small dense model while retaining both
    # rotation-tolerant pose shape and explicit screen-direction information.
    if values.shape != (81,):
        raise RuntimeError(f"unexpected feature length: {values.shape}")

    return LandmarkFeatureVector(values=values,
                                 used_world_landmarks=used_world,
                                 palm_scale=palm_scale)
