from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

from gesture_dataset import RIGHT_DATASET_LABELS


OTHER_LABEL = "Other"


def _quantiles(values: list[float]) -> dict:
    if not values:
        return {"count": 0, "p10": 0.0, "p50": 0.0, "p90": 0.0, "mean": 0.0}
    array = np.asarray(values, dtype=np.float64)
    return {
        "count": int(array.size),
        "p10": float(np.percentile(array, 10)),
        "p50": float(np.percentile(array, 50)),
        "p90": float(np.percentile(array, 90)),
        "mean": float(np.mean(array)),
    }


def _canonical_prediction(value: str) -> str:
    text = str(value or "None")
    return text if text in RIGHT_DATASET_LABELS else OTHER_LABEL


def load_records(paths: list[Path]) -> list[dict]:
    records: list[dict] = []
    for path in paths:
        with path.open("r", encoding="utf-8") as handle:
            for line_number, line in enumerate(handle, start=1):
                if not line.strip():
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError as exc:
                    raise ValueError(f"{path}:{line_number}: invalid JSON: {exc}") from exc
                if not isinstance(record, dict):
                    raise ValueError(f"{path}:{line_number}: expected a JSON object")
                label = str(record.get("label", ""))
                if label not in RIGHT_DATASET_LABELS:
                    raise ValueError(f"{path}:{line_number}: invalid/missing ground-truth label {label!r}")
                record["_source_file"] = str(path)
                records.append(record)
    if not records:
        raise ValueError("dataset is empty")
    return records


def analyze_records(records: list[dict]) -> dict:
    labels = list(RIGHT_DATASET_LABELS)
    prediction_labels = labels + [OTHER_LABEL]
    label_index = {label: index for index, label in enumerate(labels)}
    prediction_index = {label: index for index, label in enumerate(prediction_labels)}
    confusion = np.zeros((len(labels), len(prediction_labels)), dtype=np.int64)

    truth_counts: Counter[str] = Counter()
    prediction_counts: Counter[str] = Counter()
    groups_by_label: dict[str, set[str]] = defaultdict(set)
    role_sources: Counter[str] = Counter()
    heuristic_confidence_by_truth: dict[str, list[float]] = defaultdict(list)
    handedness_confidence_by_truth: dict[str, list[float]] = defaultdict(list)
    canned_confidence_by_truth: dict[str, list[float]] = defaultdict(list)
    canned_prediction_by_truth: dict[str, Counter[str]] = defaultdict(Counter)
    world_landmark_samples = 0

    for record in records:
        truth = str(record["label"])
        heuristic = record.get("heuristic", {})
        if not isinstance(heuristic, dict):
            heuristic = {}
        predicted = _canonical_prediction(str(heuristic.get("gesture", "None")))
        confidence = max(0.0, min(1.0, float(heuristic.get("confidence", 0.0))))

        truth_counts[truth] += 1
        prediction_counts[predicted] += 1
        confusion[label_index[truth], prediction_index[predicted]] += 1
        heuristic_confidence_by_truth[truth].append(confidence)

        group = (str(record.get("recording_group", "")).strip()
                 or str(record.get("session_id", "")).strip()
                 or Path(str(record.get("_source_file", "dataset"))).stem)
        groups_by_label[truth].add(group)

        handedness = record.get("handedness", {})
        if isinstance(handedness, dict):
            handedness_confidence_by_truth[truth].append(
                max(0.0, min(1.0, float(handedness.get("confidence", 0.0)))))

        canned = record.get("canned", {})
        if isinstance(canned, dict):
            canned_gesture = str(canned.get("gesture", "Unavailable"))
            canned_prediction_by_truth[truth][canned_gesture] += 1
            canned_confidence_by_truth[truth].append(
                max(0.0, min(1.0, float(canned.get("confidence", 0.0)))))

        role = record.get("role_config", {})
        if isinstance(role, dict):
            role_sources[str(role.get("source", "")) or "UNKNOWN"] += 1

        world = record.get("world_landmarks", [])
        if isinstance(world, list) and len(world) >= 21:
            world_landmark_samples += 1

    total = len(records)
    correct = int(sum(confusion[index, prediction_index[label]]
                      for index, label in enumerate(labels)))

    per_class: dict[str, dict] = {}
    for row, label in enumerate(labels):
        tp = int(confusion[row, prediction_index[label]])
        fn = int(np.sum(confusion[row, :]) - tp)
        fp = int(np.sum(confusion[:, prediction_index[label]]) - tp)
        precision = tp / (tp + fp) if tp + fp > 0 else 0.0
        recall = tp / (tp + fn) if tp + fn > 0 else 0.0
        f1 = 2.0 * precision * recall / (precision + recall) if precision + recall > 0 else 0.0

        row_counts = {
            prediction_labels[col]: int(confusion[row, col])
            for col in range(len(prediction_labels))
            if confusion[row, col] > 0
        }
        mistakes = sorted(
            ((name, count) for name, count in row_counts.items() if name != label),
            key=lambda item: item[1],
            reverse=True,
        )
        per_class[label] = {
            "support": int(truth_counts[label]),
            "recording_groups": len(groups_by_label[label]),
            "precision": float(precision),
            "recall": float(recall),
            "f1": float(f1),
            "top_confusions": [
                {"predicted": name, "count": int(count),
                 "rate": float(count / max(1, truth_counts[label]))}
                for name, count in mistakes[:3]
            ],
            "heuristic_confidence": _quantiles(heuristic_confidence_by_truth[label]),
            "handedness_confidence": _quantiles(handedness_confidence_by_truth[label]),
            "canned_confidence": _quantiles(canned_confidence_by_truth[label]),
            "canned_top_outputs": canned_prediction_by_truth[label].most_common(5),
        }

    macro_f1 = float(np.mean([entry["f1"] for entry in per_class.values()]))
    macro_recall = float(np.mean([entry["recall"] for entry in per_class.values()]))
    weakest = sorted(
        ({"label": label, "recall": metrics["recall"], "f1": metrics["f1"]}
         for label, metrics in per_class.items()),
        key=lambda item: (item["recall"], item["f1"]),
    )

    return {
        "samples": total,
        "labels": labels,
        "prediction_labels": prediction_labels,
        "recording_groups": sorted({group for values in groups_by_label.values() for group in values}),
        "accuracy": float(correct / total),
        "macro_recall": macro_recall,
        "macro_f1": macro_f1,
        "world_landmark_coverage": float(world_landmark_samples / total),
        "role_sources": dict(role_sources),
        "truth_counts": {label: int(truth_counts[label]) for label in labels},
        "prediction_counts": {label: int(prediction_counts[label]) for label in prediction_labels},
        "per_class": per_class,
        "confusion_matrix": confusion.tolist(),
        "weakest_classes": weakest,
    }


def print_summary(report: dict) -> None:
    print(f"Samples: {report['samples']}  groups: {len(report['recording_groups'])}")
    print(f"Heuristic accuracy: {report['accuracy'] * 100.0:.1f}%  "
          f"macro recall: {report['macro_recall'] * 100.0:.1f}%  "
          f"macro F1: {report['macro_f1'] * 100.0:.1f}%")
    print(f"World-landmark coverage: {report['world_landmark_coverage'] * 100.0:.1f}%")
    print()
    print("Per-class baseline:")
    for item in report["weakest_classes"]:
        label = item["label"]
        metrics = report["per_class"][label]
        confusion = metrics["top_confusions"]
        confusion_text = (
            ", ".join(f"{entry['predicted']} {entry['rate'] * 100.0:.0f}%" for entry in confusion)
            if confusion else "none"
        )
        print(
            f"  {label:13s} recall={metrics['recall'] * 100.0:5.1f}% "
            f"F1={metrics['f1'] * 100.0:5.1f}% "
            f"n={metrics['support']:4d} groups={metrics['recording_groups']} "
            f"confused→ {confusion_text}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Analyze labeled Gesture Rack physical-right recordings. This reports the current "
            "heuristic baseline and its exact per-class confusions before any model promotion."
        )
    )
    parser.add_argument("datasets", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, default=None,
                        help="Optional JSON report path")
    args = parser.parse_args()

    report = analyze_records(load_records(args.datasets))
    print_summary(report)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
        print(f"\nJSON report: {args.output}")


if __name__ == "__main__":
    main()
