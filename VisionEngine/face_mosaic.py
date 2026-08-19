from __future__ import annotations

from pathlib import Path

import cv2
import numpy as np


def _expanded_box(box: tuple[int, int, int, int], width: int, height: int,
                  margin_ratio: float) -> tuple[int, int, int, int]:
    x, y, w, h = [int(v) for v in box]
    margin_x = int(round(w * margin_ratio))
    margin_y = int(round(h * margin_ratio))
    x0 = max(0, x - margin_x)
    y0 = max(0, y - margin_y)
    x1 = min(width, x + w + margin_x)
    y1 = min(height, y + h + margin_y)
    return x0, y0, max(0, x1 - x0), max(0, y1 - y0)


def apply_face_mosaic(frame_rgb: np.ndarray,
                      boxes: list[tuple[int, int, int, int]],
                      block_size: int = 14,
                      margin_ratio: float = 0.18) -> np.ndarray:
    """Return a copy with pixelation applied only inside detected face regions."""
    if frame_rgb.ndim != 3 or frame_rgb.shape[2] < 3:
        return np.array(frame_rgb, copy=True)

    output = np.array(frame_rgb, copy=True)
    height, width = output.shape[:2]
    block_size = max(4, int(block_size))

    for box in boxes:
        x, y, w, h = _expanded_box(box, width, height, margin_ratio)
        if w < 2 or h < 2:
            continue
        roi = output[y:y + h, x:x + w]
        small_w = max(1, w // block_size)
        small_h = max(1, h // block_size)
        tiny = cv2.resize(roi, (small_w, small_h), interpolation=cv2.INTER_AREA)
        output[y:y + h, x:x + w] = cv2.resize(
            tiny, (w, h), interpolation=cv2.INTER_NEAREST)

    return output


class FaceMosaicProcessor:
    """Low-frequency face detector plus cheap per-frame ROI pixelation.

    Detection is deliberately decoupled from hand recognition. The processor only
    receives an already-recognised preview frame, and cached face boxes are reused
    between detector passes so privacy does not create a camera/gesture backlog.
    """

    def __init__(self, cascade_path: Path, detect_every_n: int = 4,
                 detection_scale: float = 0.5, block_size: int = 14,
                 margin_ratio: float = 0.18, detector=None):
        self.detect_every_n = max(1, int(detect_every_n))
        self.detection_scale = float(min(1.0, max(0.25, detection_scale)))
        self.block_size = max(4, int(block_size))
        self.margin_ratio = max(0.0, float(margin_ratio))
        self.frame_index = 0
        self.last_boxes: list[tuple[int, int, int, int]] = []
        self.detector = detector if detector is not None else cv2.CascadeClassifier(str(cascade_path))
        self.available = self.detector is not None and not self.detector.empty()

    def _detect(self, frame_rgb: np.ndarray) -> list[tuple[int, int, int, int]]:
        if not self.available:
            return []

        height, width = frame_rgb.shape[:2]
        scale = self.detection_scale if width >= 480 else 1.0
        if scale < 1.0:
            working = cv2.resize(frame_rgb, None, fx=scale, fy=scale,
                                 interpolation=cv2.INTER_AREA)
        else:
            working = frame_rgb

        gray = cv2.cvtColor(working, cv2.COLOR_RGB2GRAY)
        gray = cv2.equalizeHist(gray)
        minimum = max(24, int(round(min(working.shape[:2]) * 0.08)))
        detected = self.detector.detectMultiScale(
            gray,
            scaleFactor=1.12,
            minNeighbors=5,
            minSize=(minimum, minimum),
            flags=cv2.CASCADE_SCALE_IMAGE,
        )

        inverse = 1.0 / scale
        boxes: list[tuple[int, int, int, int]] = []
        for x, y, w, h in detected:
            boxes.append((
                int(round(x * inverse)), int(round(y * inverse)),
                int(round(w * inverse)), int(round(h * inverse)),
            ))
        return boxes

    def process_rgb(self, frame_rgb: np.ndarray) -> np.ndarray:
        if frame_rgb.ndim != 3 or frame_rgb.shape[2] < 3 or not self.available:
            return np.array(frame_rgb, copy=True)

        should_detect = self.frame_index % self.detect_every_n == 0
        self.frame_index += 1
        if should_detect:
            self.last_boxes = self._detect(frame_rgb)

        return apply_face_mosaic(
            frame_rgb,
            self.last_boxes,
            block_size=self.block_size,
            margin_ratio=self.margin_ratio,
        )
