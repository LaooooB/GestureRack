from __future__ import annotations

import argparse
import json
import socket
import struct
import time
from pathlib import Path

from gesture_dataset import RIGHT_DATASET_LABELS, default_dataset_dir
from landmark_features import FEATURE_VERSION, extract_landmark_features
from vision_engine import MULTICAST_ADDRESS, MULTICAST_PORT


def open_multicast_socket() -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", MULTICAST_PORT))
    membership = struct.pack("=4s4s",
                             socket.inet_aton(MULTICAST_ADDRESS),
                             socket.inet_aton("0.0.0.0"))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)
    sock.settimeout(0.5)
    return sock


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Collect labeled physical-right samples from the already-running "
            "production VisionEngine multicast stream. This does not open a second camera."
        ))
    parser.add_argument("label", choices=RIGHT_DATASET_LABELS,
                        help="Ground-truth pose being held; use None for hard negatives")
    parser.add_argument("--seconds", type=float, default=8.0)
    parser.add_argument("--interval-ms", type=int, default=50)
    parser.add_argument("--output-dir", type=Path, default=default_dataset_dir())
    parser.add_argument("--allow-uncalibrated", action="store_true",
                        help="Allow recording before physical L/R calibration (not recommended)")
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    started_wall = time.strftime("%Y%m%d_%H%M%S")
    safe_label = args.label.replace("/", "_")
    output = args.output_dir / f"runtime_{started_wall}_{safe_label}.jsonl"

    sock = open_multicast_socket()
    deadline = time.monotonic() + max(0.5, args.seconds)
    last_sample_ms = -1
    samples = 0
    session_seen = ""
    warned_uncalibrated = False

    print(f"Recording physical RIGHT = {args.label}")
    print("Keep the requested pose visible and vary position, distance, wrist angle, and lighting.")
    print(f"Output: {output}")

    try:
        with output.open("w", encoding="utf-8", buffering=1) as handle:
            while time.monotonic() < deadline:
                try:
                    payload, _ = sock.recvfrom(65535)
                except socket.timeout:
                    continue
                try:
                    packet = json.loads(payload.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    continue

                if not isinstance(packet, dict) or int(packet.get("protocol", 0)) != 2:
                    continue
                right = packet.get("right")
                if not isinstance(right, dict) or not right.get("present", False):
                    continue

                role = packet.get("role_config", {})
                role_source = str(role.get("source", "")) if isinstance(role, dict) else ""
                if (not args.allow_uncalibrated
                        and role_source.upper() in {"", "UNCALIBRATED", "CALIBRATING"}):
                    if not warned_uncalibrated:
                        print("Waiting for calibrated hand roles. Use CALIBRATE RIGHT in the plugin first.")
                        warned_uncalibrated = True
                    continue

                timestamp_ms = int(packet.get("timestamp_ms", 0))
                if timestamp_ms <= 0:
                    continue
                if last_sample_ms >= 0 and timestamp_ms - last_sample_ms < args.interval_ms:
                    continue

                landmarks = right.get("landmarks", [])
                try:
                    feature = extract_landmark_features(landmarks)
                except ValueError:
                    continue

                telemetry = packet.get("telemetry", {})
                if not isinstance(telemetry, dict):
                    telemetry = {}
                session_id = str(packet.get("session_id", ""))
                session_seen = session_seen or session_id

                record = {
                    "schema_version": 1,
                    "feature_version": FEATURE_VERSION,
                    "timestamp_ms": timestamp_ms,
                    "label": args.label,
                    "physical_role": "right",
                    "session_id": session_id,
                    "camera": {
                        "index": 0,
                        "backend": telemetry.get("backend", ""),
                        "width": telemetry.get("width", 0),
                        "height": telemetry.get("height", 0),
                    },
                    "handedness": {
                        "label": "resolved-right",
                        "confidence": float(right.get("handedness_confidence", 0.0)),
                    },
                    # The multicast stream contains the production heuristic result,
                    # not the original MediaPipe canned head. Integrated sidecar
                    # recordings can preserve the canned value later; training only
                    # requires features/label and uses heuristic here as the baseline.
                    "canned": {"gesture": "Unavailable", "confidence": 0.0},
                    "heuristic": {
                        "gesture": str(right.get("raw_gesture", "None")),
                        "confidence": float(right.get("confidence", 0.0)),
                    },
                    "used_world_landmarks": False,
                    "normalized_landmarks": landmarks,
                    "world_landmarks": [],
                    "features": feature.values.tolist(),
                    "role_config": role,
                }
                handle.write(json.dumps(record, separators=(",", ":")) + "\n")
                samples += 1
                last_sample_ms = timestamp_ms
    finally:
        sock.close()

    print(f"Saved {samples} samples from sidecar session {session_seen or '--'}")
    if samples == 0:
        print("No samples were captured. Confirm VisionEngine is running and hand roles are calibrated.")


if __name__ == "__main__":
    main()
