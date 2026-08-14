from __future__ import annotations

import argparse
import json
import os
import socket
import struct
import sys
import time
from pathlib import Path

from shadow_evaluator import ShadowGestureEvaluator
from tiny_landmark_classifier import DEFAULT_MODEL_NAME, TinyLandmarkClassifier


MULTICAST_ADDRESS = "239.255.71.77"
MULTICAST_PORT = 17777


def default_shadow_dir() -> Path:
    if sys.platform.startswith("win"):
        root = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA")
        if root:
            return Path(root) / "GestureRack" / "shadow"
    xdg = os.environ.get("XDG_DATA_HOME")
    if xdg:
        return Path(xdg) / "GestureRack" / "shadow"
    return Path.home() / ".local" / "share" / "GestureRack" / "shadow"


def default_model_path() -> Path:
    return Path(__file__).resolve().parent / "models" / DEFAULT_MODEL_NAME


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


def compact_summary(session_id: str, evaluator: ShadowGestureEvaluator) -> str:
    summary = evaluator.stats.summary()
    return (
        f"session={session_id or '--'} samples={summary['samples']} "
        f"agree={summary['agreement_rate'] * 100.0:.1f}% "
        f"disagree={summary['disagreement_rate'] * 100.0:.1f}% "
        f"model={summary['mean_inference_ms']:.3f}ms mean/"
        f"{summary['p95_inference_ms']:.3f}ms p95"
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Run the tiny landmark classifier in shadow mode against the already-running "
            "production VisionEngine stream. Shadow predictions never control the rack."
        ))
    parser.add_argument("--model", type=Path, default=default_model_path())
    parser.add_argument("--seconds", type=float, default=0.0,
                        help="0 means run until Ctrl+C")
    parser.add_argument("--report-seconds", type=float, default=2.0)
    parser.add_argument("--output", type=Path, default=None,
                        help="Optional JSONL path; defaults to the GestureRack per-user shadow folder")
    args = parser.parse_args()

    model = TinyLandmarkClassifier.load_optional(args.model)
    if model is None:
        raise SystemExit(
            f"Could not load tiny landmark model: {args.model}\n"
            "Train and export a promotion candidate before running shadow mode."
        )

    output = args.output
    if output is None:
        directory = default_shadow_dir()
        directory.mkdir(parents=True, exist_ok=True)
        output = directory / f"shadow_{time.strftime('%Y%m%d_%H%M%S')}.jsonl"
    else:
        output.parent.mkdir(parents=True, exist_ok=True)

    evaluator = ShadowGestureEvaluator(model)
    sock = open_multicast_socket()
    started = time.monotonic()
    deadline = started + args.seconds if args.seconds > 0.0 else None
    next_report = started + max(0.25, args.report_seconds)
    current_session = ""
    all_session_summaries: list[dict] = []

    print(f"Shadow model: {args.model}")
    print(f"Listening to production stream {MULTICAST_ADDRESS}:{MULTICAST_PORT}")
    print(f"JSONL: {output}")
    print("Shadow mode has zero control authority.")

    try:
        with output.open("w", encoding="utf-8", buffering=1) as handle:
            while deadline is None or time.monotonic() < deadline:
                try:
                    payload, _ = sock.recvfrom(65535)
                except socket.timeout:
                    if time.monotonic() >= next_report:
                        print(compact_summary(current_session, evaluator))
                        next_report = time.monotonic() + max(0.25, args.report_seconds)
                    continue

                try:
                    packet = json.loads(payload.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    continue
                if not isinstance(packet, dict) or int(packet.get("protocol", 0)) != 2:
                    continue

                session_id = str(packet.get("session_id", ""))
                if current_session and session_id and session_id != current_session:
                    previous = evaluator.stats.summary()
                    previous["session_id"] = current_session
                    all_session_summaries.append(previous)
                    print("completed " + compact_summary(current_session, evaluator))
                    evaluator.reset_stats()
                if session_id:
                    current_session = session_id

                right = packet.get("right")
                if not isinstance(right, dict) or not right.get("present", False):
                    continue
                landmarks = right.get("landmarks", [])
                heuristic = str(right.get("raw_gesture", "None"))
                observation = evaluator.evaluate(landmarks, heuristic)
                if not observation.available:
                    continue

                record = {
                    "schema_version": 1,
                    "session_id": session_id,
                    "seq": int(packet.get("seq", 0)),
                    "timestamp_ms": int(packet.get("timestamp_ms", 0)),
                    "heuristic": {
                        "gesture": observation.heuristic_gesture,
                        "confidence": float(right.get("confidence", 0.0)),
                    },
                    "tiny": {
                        "gesture": observation.model_gesture,
                        "confidence": observation.confidence,
                        "margin": observation.margin,
                        "inference_ms": observation.inference_ms,
                    },
                    "agrees": observation.agrees,
                    "stable_gesture": str(right.get("stable_gesture", "None")),
                    "role_source": (
                        str(packet.get("role_config", {}).get("source", ""))
                        if isinstance(packet.get("role_config"), dict) else ""
                    ),
                }
                handle.write(json.dumps(record, separators=(",", ":")) + "\n")

                now = time.monotonic()
                if now >= next_report:
                    print(compact_summary(current_session, evaluator))
                    next_report = now + max(0.25, args.report_seconds)
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()

    final = evaluator.stats.summary()
    final["session_id"] = current_session
    if final["samples"] > 0:
        all_session_summaries.append(final)
    report = {
        "model": str(args.model),
        "output": str(output),
        "sessions": all_session_summaries,
    }
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
