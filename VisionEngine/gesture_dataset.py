from __future__ import annotations

import json
import os
import re
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from hand_role_resolver import DetectedHand
from landmark_features import FEATURE_VERSION, extract_landmark_features
from right_gesture_classifier import RIGHT_GESTURES, RightGestureClassification


DATASET_SCHEMA_VERSION = 2
RIGHT_DATASET_LABELS = tuple(sorted(RIGHT_GESTURES)) + ("None",)
RIGHT_FAMILY_LABELS = ("OpenPalm", "FoldedFour", "Victory", "Other")
_SAFE_LABEL = re.compile(r"[^A-Za-z0-9_-]+")


def gesture_label_to_family(label: str) -> str:
    canonical = str(label).strip()
    if canonical == "Open_Palm":
        return "OpenPalm"
    if canonical == "Victory":
        return "Victory"
    if canonical == "Closed_Fist" or canonical.startswith("Thumb_"):
        return "FoldedFour"
    return "Other"


def default_dataset_dir() -> Path:
    if sys.platform.startswith("win"):
        root = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA")
        if root:
            return Path(root) / "GestureRack" / "datasets"
    xdg = os.environ.get("XDG_DATA_HOME")
    if xdg:
        return Path(xdg) / "GestureRack" / "datasets"
    return Path.home() / ".local" / "share" / "GestureRack" / "datasets"


@dataclass(frozen=True)
class DatasetRecorderStatus:
    active: bool = False
    label: str = ""
    samples: int = 0
    path: str = ""


class GestureDatasetRecorder:
    """Record exact production physical-right samples for offline training.

    Labels remain user-facing final gestures so the dataset can measure direction
    and Fist/Thumb confusion. The learned tiny model deliberately trains on the
    derived broad family label instead of final direction classes.
    """

    def __init__(self, directory: Optional[Path] = None, min_interval_ms: int = 50):
        self.directory = Path(directory) if directory is not None else default_dataset_dir()
        self.min_interval_ms = int(min_interval_ms)
        self.lock = threading.Lock()
        self.file = None
        self.path: Optional[Path] = None
        self.label = ""
        self.samples = 0
        self.last_sample_ms = -1
        self.metadata: dict = {}

    def status(self) -> DatasetRecorderStatus:
        with self.lock:
            return DatasetRecorderStatus(
                active=self.file is not None,
                label=self.label,
                samples=self.samples,
                path=str(self.path) if self.path is not None else "",
            )

    def start(self, label: str, metadata: dict) -> DatasetRecorderStatus:
        canonical = str(label).strip()
        if canonical not in RIGHT_DATASET_LABELS:
            raise ValueError(f"unsupported right-hand dataset label: {canonical}")

        with self.lock:
            self._stop_locked()
            self.directory.mkdir(parents=True, exist_ok=True)
            timestamp = time.strftime("%Y%m%d_%H%M%S")
            safe = _SAFE_LABEL.sub("_", canonical).strip("_") or "None"
            path = self.directory / f"right_{timestamp}_{safe}.jsonl"
            suffix = 1
            while path.exists():
                path = self.directory / f"right_{timestamp}_{safe}_{suffix}.jsonl"
                suffix += 1

            self.file = path.open("a", encoding="utf-8", buffering=1)
            self.path = path
            self.label = canonical
            self.samples = 0
            self.last_sample_ms = -1
            self.metadata = dict(metadata)
            return DatasetRecorderStatus(True, canonical, 0, str(path))

    def stop(self) -> DatasetRecorderStatus:
        with self.lock:
            final = DatasetRecorderStatus(
                active=False,
                label=self.label,
                samples=self.samples,
                path=str(self.path) if self.path is not None else "",
            )
            self._stop_locked()
            return final

    def _stop_locked(self) -> None:
        if self.file is not None:
            self.file.flush()
            self.file.close()
        self.file = None
        self.label = ""
        self.metadata = {}
        self.last_sample_ms = -1

    def observe(self, hand: DetectedHand, classification: RightGestureClassification,
                timestamp_ms: int) -> bool:
        with self.lock:
            if self.file is None:
                return False
            now = int(timestamp_ms)
            if self.last_sample_ms >= 0 and now - self.last_sample_ms < self.min_interval_ms:
                return False

            try:
                feature = extract_landmark_features(hand.landmarks, hand.world_landmarks)
            except ValueError:
                return False

            record = {
                "schema_version": DATASET_SCHEMA_VERSION,
                "feature_version": FEATURE_VERSION,
                "timestamp_ms": now,
                "label": self.label,
                "family_label": gesture_label_to_family(self.label),
                "physical_role": "right",
                "session_id": self.metadata.get("session_id", ""),
                "recording_group": self.metadata.get("recording_group", ""),
                "camera": {
                    "index": self.metadata.get("camera_index", 0),
                    "backend": self.metadata.get("backend", ""),
                    "width": self.metadata.get("width", 0),
                    "height": self.metadata.get("height", 0),
                },
                "handedness": {
                    "label": hand.handedness_label,
                    "confidence": float(hand.handedness_confidence),
                },
                "canned": {
                    "gesture": hand.raw_gesture,
                    "confidence": float(hand.gesture_confidence),
                },
                "heuristic": {
                    "gesture": classification.gesture,
                    "family": classification.family,
                    "confidence": float(classification.confidence),
                    "family_confidence": float(classification.family_confidence),
                    "thumb_state": classification.thumb_state,
                    "direction": classification.direction,
                    "direction_confidence": float(classification.direction_confidence),
                    "quality": float(classification.quality),
                },
                "used_world_landmarks": bool(feature.used_world_landmarks),
                "normalized_landmarks": hand.landmarks,
                "world_landmarks": hand.world_landmarks,
                "features": feature.values.tolist(),
            }
            self.file.write(json.dumps(record, separators=(",", ":")) + "\n")
            self.samples += 1
            self.last_sample_ms = now
            return True

    def close(self) -> None:
        self.stop()
