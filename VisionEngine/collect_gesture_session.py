from __future__ import annotations

import argparse
import json
import socket
import struct
import time
import uuid
from pathlib import Path

from gesture_dataset import default_dataset_dir, gesture_label_to_family
from landmark_features import FEATURE_VERSION, extract_landmark_features


MULTICAST_ADDRESS = "239.255.71.77"
MULTICAST_PORT = 17777
SESSION_LABELS = (
    "None",
    "Open_Palm",
    "Closed_Fist",
    "Victory",
    "Thumb_Up",
    "Thumb_Down",
    "Thumb_Right",
    "Thumb_Left",
)

LABEL_INSTRUCTIONS = {
    "None": "Show hard negatives: relaxed, half-open, transitions, half-thumb, diagonal thumb, edge-of-frame and other non-command shapes. Keep the RIGHT hand visible.",
    "Open_Palm": "Open the RIGHT palm naturally. Vary wrist rotation and do not force the thumb straight.",
    "Closed_Fist": "Make a comfortable RIGHT fist. Vary whether the thumb rests over or beside the folded fingers.",
    "Victory": "Show a RIGHT-hand V / victory sign while varying wrist angle and distance.",
    "Thumb_Up": "Keep four fingers folded and show the RIGHT thumb clearly up. Include modest wrist rotation.",
    "Thumb_Down": "Keep four fingers folded and show the RIGHT thumb clearly down. Include modest wrist rotation.",
    "Thumb_Right": "Keep four fingers folded and show the RIGHT thumb clearly toward image-right. Do not use the index finger.",
    "Thumb_Left": "Keep four fingers folded and show the RIGHT thumb clearly toward image-left. Do not use the index finger.",
}


def open_multicast_socket() -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", MULTICAST_PORT))
    membership = struct.pack(
        "=4s4s",
        socket.inet_aton(MULTICAST_ADDRESS),
        socket.inet_aton("0.0.0.0"),
    )
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)
    sock.settimeout(0.5)
    return sock


def receive_packet(sock: socket.socket) -> dict | None:
    try:
        payload, _ = sock.recvfrom(65535)
    except socket.timeout:
        return None
    try:
        packet = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    if not isinstance(packet, dict) or int(packet.get("protocol", 0)) != 2:
        return None
    return packet


def role_is_ready(packet: dict) -> bool:
    role = packet.get("role_config", {})
    source = str(role.get("source", "")) if isinstance(role, dict) else ""
    return source.upper() not in {"", "UNCALIBRATED", "CALIBRATING"}


def make_record(packet: dict, label: str, recording_group: str) -> dict | None:
    right = packet.get("right")
    if not isinstance(right, dict) or not right.get("present", False):
        return None

    landmarks = right.get("landmarks", [])
    world_landmarks = right.get("world_landmarks", [])
    try:
        feature = extract_landmark_features(landmarks, world_landmarks)
    except ValueError:
        return None

    telemetry = packet.get("telemetry", {})
    if not isinstance(telemetry, dict):
        telemetry = {}
    role = packet.get("role_config", {})
    if not isinstance(role, dict):
        role = {}

    raw_gesture = str(right.get("raw_gesture", "None"))
    direction = raw_gesture.removeprefix("Thumb_") if raw_gesture.startswith("Thumb_") else "None"
    return {
        "schema_version": 2,
        "feature_version": FEATURE_VERSION,
        "timestamp_ms": int(packet.get("timestamp_ms", 0)),
        "label": label,
        "family_label": gesture_label_to_family(label),
        "physical_role": "right",
        "recording_group": recording_group,
        "session_id": str(packet.get("session_id", "")),
        "camera": {
            "index": int(telemetry.get("camera_index", 0) or 0),
            "backend": str(telemetry.get("backend", "")),
            "width": int(telemetry.get("width", 0) or 0),
            "height": int(telemetry.get("height", 0) or 0),
        },
        "handedness": {
            "label": "resolved-right",
            "confidence": float(right.get("handedness_confidence", 0.0)),
        },
        "canned": {
            "gesture": str(right.get("canned_gesture", "Unavailable")),
            "confidence": float(right.get("canned_confidence", 0.0)),
        },
        "heuristic": {
            "gesture": raw_gesture,
            "family": gesture_label_to_family(raw_gesture),
            "direction": direction,
            "confidence": float(right.get("confidence", 0.0)),
        },
        "used_world_landmarks": bool(feature.used_world_landmarks),
        "normalized_landmarks": landmarks,
        "world_landmarks": world_landmarks,
        "features": feature.values.tolist(),
        "role_config": role,
    }


def wait_for_ready_stream(sock: socket.socket, timeout_seconds: float = 12.0) -> str:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        packet = receive_packet(sock)
        if packet is None:
            continue
        if not role_is_ready(packet):
            raise RuntimeError(
                "Hand roles are not calibrated. Use CALIBRATE RIGHT in the plugin before recording."
            )
        session_id = str(packet.get("session_id", ""))
        if session_id:
            return session_id
    raise RuntimeError("No protocol-v2 VisionEngine stream received. Start GestureVisionEngine first.")


def capture_label(sock: socket.socket, handle, label: str, recording_group: str,
                  expected_session_id: str, seconds: float, interval_ms: int) -> int:
    deadline = time.monotonic() + seconds
    last_timestamp_ms = -1
    samples = 0
    while time.monotonic() < deadline:
        packet = receive_packet(sock)
        if packet is None:
            continue
        if not role_is_ready(packet):
            continue

        session_id = str(packet.get("session_id", ""))
        if expected_session_id and session_id and session_id != expected_session_id:
            raise RuntimeError(
                "VisionEngine restarted during this recording group. Restart this capture session so train/test grouping stays valid."
            )

        timestamp_ms = int(packet.get("timestamp_ms", 0))
        if timestamp_ms <= 0:
            continue
        if last_timestamp_ms >= 0 and timestamp_ms - last_timestamp_ms < interval_ms:
            continue

        record = make_record(packet, label, recording_group)
        if record is None:
            continue
        handle.write(json.dumps(record, separators=(",", ":")) + "\n")
        last_timestamp_ms = timestamp_ms
        samples += 1
    return samples


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Guided capture of all physical-right Gesture Rack classes from the already-running production VisionEngine stream. "
            "Each invocation is one independent recording group for session-held-out model evaluation."
        )
    )
    parser.add_argument("--seconds-per-label", type=float, default=8.0)
    parser.add_argument("--interval-ms", type=int, default=50)
    parser.add_argument("--settle-seconds", type=float, default=1.5)
    parser.add_argument("--output-dir", type=Path, default=default_dataset_dir())
    parser.add_argument(
        "--auto-start",
        action="store_true",
        help="Do not wait for Enter between labels; useful after you know the capture flow.",
    )
    args = parser.parse_args()

    if args.seconds_per_label < 2.0:
        parser.error("--seconds-per-label must be at least 2 seconds")
    if args.interval_ms < 20:
        parser.error("--interval-ms must be at least 20 ms")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    recording_group = f"right_{time.strftime('%Y%m%d_%H%M%S')}_{uuid.uuid4().hex[:8]}"
    output = args.output_dir / f"{recording_group}.jsonl"
    sock = open_multicast_socket()

    print("Gesture Rack guided RIGHT-hand dataset capture")
    print("This tool uses the production VisionEngine stream; it does not open a second camera.")
    print("First calibrate physical RIGHT in the plugin, then vary distance, position, wrist angle and lighting during every pose.")
    print(f"Recording group: {recording_group}")
    print(f"Output: {output}")

    try:
        expected_session_id = wait_for_ready_stream(sock)
        print(f"Vision session: {expected_session_id}")
        totals: dict[str, int] = {}

        with output.open("w", encoding="utf-8", buffering=1) as handle:
            for index, label in enumerate(SESSION_LABELS, start=1):
                print()
                print(f"[{index}/{len(SESSION_LABELS)}] {label}")
                print(LABEL_INSTRUCTIONS[label])
                if not args.auto_start:
                    input("Press Enter when the pose is ready...")
                if args.settle_seconds > 0.0:
                    print(f"Settling for {args.settle_seconds:.1f}s...")
                    time.sleep(args.settle_seconds)
                print(f"Recording {args.seconds_per_label:.1f}s...")
                samples = capture_label(
                    sock,
                    handle,
                    label,
                    recording_group,
                    expected_session_id,
                    args.seconds_per_label,
                    args.interval_ms,
                )
                totals[label] = samples
                print(f"Captured {samples} samples")

        print()
        print("Capture complete.")
        print(json.dumps({
            "recording_group": recording_group,
            "vision_session": expected_session_id,
            "output": str(output),
            "samples_per_label": totals,
            "minimum_samples": min(totals.values()) if totals else 0,
        }, indent=2))
        print("Record at least one more complete group under meaningfully different conditions before training.")
    except (KeyboardInterrupt, EOFError):
        print("\nCapture cancelled.")
    finally:
        sock.close()


if __name__ == "__main__":
    main()
