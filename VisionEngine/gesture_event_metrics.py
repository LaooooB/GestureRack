from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Iterable

from gesture_stabilizer import GestureStabilizer
from right_gesture_classifier import RIGHT_GESTURES, classify_right_gesture


def _percentile(values: list[float], quantile: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(float(v) for v in values)
    position = max(0.0, min(1.0, float(quantile))) * (len(ordered) - 1)
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def build_timeline(records: Iterable[dict]) -> list[dict]:
    """Re-run the production classifier on recorded landmark samples."""
    timeline: list[dict] = []
    for record in records:
        truth = str(record.get("label", "None"))
        timestamp_ms = int(record.get("timestamp_ms", 0) or 0)
        if timestamp_ms <= 0:
            continue
        landmarks = record.get("normalized_landmarks", [])
        canned = record.get("canned", {})
        if not isinstance(canned, dict):
            canned = {}
        classification = classify_right_gesture(
            landmarks,
            str(canned.get("gesture", "None")),
            float(canned.get("confidence", 0.0) or 0.0),
        )
        timeline.append({
            "recording_group": str(record.get("recording_group", "")
                                   or record.get("session_id", "") or "ungrouped"),
            "timestamp_ms": timestamp_ms,
            "truth": truth,
            "raw": classification.gesture,
            "confidence": float(classification.confidence),
        })
    return timeline


def evaluate_timeline(timeline: Iterable[dict], *, hold_ms: int = 50,
                      release_ms: int = 50) -> dict:
    """Measure control-event behavior, not just per-frame classification.

    A segment is a contiguous ground-truth label inside one recording group.
    Non-None segments expect one correct ENTER. Additional correct ENTERs in the
    same segment are chatter. Any ENTER for the wrong gesture is a false ENTER.
    """
    frames = list(timeline)
    allowed = set(RIGHT_GESTURES)
    stabilizer = GestureStabilizer(
        allowed_gestures=allowed,
        hold_ms=hold_ms,
        release_ms=release_ms,
    )

    previous_group = None
    previous_stable = "None"
    segment_truth = None
    segment_started_ms = 0
    segment_correct_enters = 0

    total_frames = 0
    frame_correct = 0
    fist_thumb_frame_confusions = 0
    thumb_direction_frame_confusions = 0
    false_enters = 0
    correct_enters = 0
    chatter_enters = 0
    expected_segments = 0
    missed_enters = 0
    activation_latencies: list[float] = []

    def finish_segment() -> None:
        nonlocal expected_segments, missed_enters
        if segment_truth is None or segment_truth == "None":
            return
        expected_segments += 1
        if segment_correct_enters <= 0:
            missed_enters += 1

    for frame in frames:
        group = str(frame.get("recording_group", "ungrouped"))
        timestamp_ms = int(frame.get("timestamp_ms", 0) or 0)
        truth = str(frame.get("truth", "None"))
        raw = str(frame.get("raw", "None"))
        confidence = float(frame.get("confidence", 0.0) or 0.0)
        if timestamp_ms <= 0:
            continue

        if group != previous_group:
            if previous_group is not None:
                finish_segment()
            stabilizer.reset()
            previous_stable = "None"
            segment_truth = truth
            segment_started_ms = timestamp_ms
            segment_correct_enters = 0
            previous_group = group
        elif truth != segment_truth:
            finish_segment()
            segment_truth = truth
            segment_started_ms = timestamp_ms
            segment_correct_enters = 0

        total_frames += 1
        if raw == truth:
            frame_correct += 1

        truth_is_thumb = truth.startswith("Thumb_")
        raw_is_thumb = raw.startswith("Thumb_")
        if ((truth == "Closed_Fist" and raw_is_thumb)
                or (truth_is_thumb and raw == "Closed_Fist")):
            fist_thumb_frame_confusions += 1
        if truth_is_thumb and raw_is_thumb and truth != raw:
            thumb_direction_frame_confusions += 1

        stable = stabilizer.update(raw, confidence, timestamp_ms)
        entered = stable != "None" and stable != previous_stable
        if entered:
            if stable == truth:
                correct_enters += 1
                segment_correct_enters += 1
                if segment_correct_enters == 1:
                    activation_latencies.append(max(0.0, float(timestamp_ms - segment_started_ms)))
                else:
                    chatter_enters += 1
            else:
                false_enters += 1
        previous_stable = stable

    if previous_group is not None:
        finish_segment()

    duration_ms = 0
    if frames:
        by_group: dict[str, list[int]] = {}
        for frame in frames:
            timestamp = int(frame.get("timestamp_ms", 0) or 0)
            if timestamp > 0:
                by_group.setdefault(str(frame.get("recording_group", "ungrouped")), []).append(timestamp)
        duration_ms = sum(max(values) - min(values) for values in by_group.values() if values)
    duration_minutes = duration_ms / 60000.0 if duration_ms > 0 else 0.0

    return {
        "frames": total_frames,
        "frame_accuracy": (frame_correct / total_frames) if total_frames else 0.0,
        "fist_thumb_frame_confusions": fist_thumb_frame_confusions,
        "thumb_direction_frame_confusions": thumb_direction_frame_confusions,
        "expected_command_segments": expected_segments,
        "correct_enters": correct_enters,
        "false_enters": false_enters,
        "missed_enters": missed_enters,
        "chatter_enters": chatter_enters,
        "false_enters_per_minute": (false_enters / duration_minutes) if duration_minutes > 0.0 else 0.0,
        "activation_latency_ms": {
            "samples": len(activation_latencies),
            "median": _percentile(activation_latencies, 0.50),
            "p95": _percentile(activation_latencies, 0.95),
            "max": max(activation_latencies) if activation_latencies else 0.0,
        },
    }


def load_jsonl(paths: Iterable[Path]) -> list[dict]:
    records: list[dict] = []
    for path in paths:
        with Path(path).open("r", encoding="utf-8") as handle:
            for line in handle:
                if line.strip():
                    item = json.loads(line)
                    if isinstance(item, dict):
                        records.append(item)
    return records


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Measure Gesture Rack false ENTER, missed ENTER, confusion and activation latency from captured datasets")
    parser.add_argument("datasets", nargs="+", type=Path)
    parser.add_argument("--hold-ms", type=int, default=50)
    parser.add_argument("--release-ms", type=int, default=50)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()

    report = evaluate_timeline(
        build_timeline(load_jsonl(args.datasets)),
        hold_ms=args.hold_ms,
        release_ms=args.release_ms,
    )
    text = json.dumps(report, indent=2, sort_keys=True)
    print(text)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
