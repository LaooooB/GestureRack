from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Any, Optional


PROFILE_VERSION = 1


def default_profile_path() -> Path:
    """Return a per-user, writable profile path for vision calibration."""
    if sys.platform.startswith("win"):
        root = os.environ.get("APPDATA") or os.environ.get("LOCALAPPDATA")
        if root:
            return Path(root) / "GestureRack" / "vision_profile.json"
    xdg = os.environ.get("XDG_CONFIG_HOME")
    if xdg:
        return Path(xdg) / "GestureRack" / "vision_profile.json"
    return Path.home() / ".config" / "GestureRack" / "vision_profile.json"


def profile_key(camera_index: int, backend: str) -> str:
    normalized_backend = (backend or "unknown").strip().lower()
    return f"camera:{int(camera_index)}|backend:{normalized_backend}"


class VisionProfileStore:
    """Small JSON store for camera/backend-specific vision calibration.

    Corrupt or partially-written files are treated as empty instead of taking the
    realtime sidecar down. Writes use an atomic replace so a crash cannot leave
    half a JSON document behind.
    """

    def __init__(self, path: Optional[Path] = None):
        self.path = Path(path) if path is not None else default_profile_path()

    def _load_root(self) -> dict[str, Any]:
        try:
            if not self.path.exists():
                return {"version": PROFILE_VERSION, "profiles": {}}
            root = json.loads(self.path.read_text(encoding="utf-8"))
            if not isinstance(root, dict):
                raise ValueError("profile root is not an object")
            profiles = root.get("profiles")
            if not isinstance(profiles, dict):
                profiles = {}
            return {"version": PROFILE_VERSION, "profiles": profiles}
        except (OSError, ValueError, json.JSONDecodeError):
            return {"version": PROFILE_VERSION, "profiles": {}}

    def get(self, camera_index: int, backend: str) -> dict[str, Any]:
        root = self._load_root()
        value = root["profiles"].get(profile_key(camera_index, backend), {})
        return dict(value) if isinstance(value, dict) else {}

    def set(self, camera_index: int, backend: str, values: dict[str, Any]) -> None:
        root = self._load_root()
        key = profile_key(camera_index, backend)
        current = root["profiles"].get(key, {})
        if not isinstance(current, dict):
            current = {}
        current.update(values)
        root["profiles"][key] = current

        self.path.parent.mkdir(parents=True, exist_ok=True)
        temp = self.path.with_suffix(self.path.suffix + ".tmp")
        temp.write_text(json.dumps(root, indent=2, sort_keys=True), encoding="utf-8")
        os.replace(temp, self.path)

    def get_swap_handedness(self, camera_index: int, backend: str) -> Optional[bool]:
        value = self.get(camera_index, backend).get("swap_handedness")
        return value if isinstance(value, bool) else None

    def set_swap_handedness(self, camera_index: int, backend: str,
                            should_swap: bool, source: str) -> None:
        self.set(camera_index, backend, {
            "swap_handedness": bool(should_swap),
            "handedness_source": str(source),
        })
