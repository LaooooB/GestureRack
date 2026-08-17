from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

from gesture_dataset import (
    RIGHT_DATASET_LABELS,
    RIGHT_FAMILY_LABELS,
    gesture_label_to_family,
)
from landmark_features import FEATURE_VERSION
from tiny_landmark_classifier import MODEL_TASK


def load_records(paths: list[Path]):
    features, gesture_labels, groups, heuristic_families = [], [], [], []
    for path in paths:
        with path.open("r", encoding="utf-8") as handle:
            for line_number, line in enumerate(handle, start=1):
                if not line.strip():
                    continue
                record = json.loads(line)
                if int(record.get("feature_version", -1)) != FEATURE_VERSION:
                    raise ValueError(f"{path}:{line_number}: feature version does not match runtime")
                values = record.get("features")
                if not isinstance(values, list) or len(values) != 81:
                    raise ValueError(f"{path}:{line_number}: expected 81 features")
                label = str(record.get("label", "")).strip()
                if label not in RIGHT_DATASET_LABELS:
                    raise ValueError(f"{path}:{line_number}: unsupported label {label!r}")
                recording_group = str(record.get("recording_group", "")).strip()
                session_id = str(record.get("session_id", "")).strip()
                group = recording_group or session_id or path.stem
                heuristic = record.get("heuristic", {})
                if isinstance(heuristic, dict):
                    family = str(heuristic.get("family", "")).strip()
                    if family not in RIGHT_FAMILY_LABELS:
                        family = gesture_label_to_family(str(heuristic.get("gesture", "None")))
                else:
                    family = "Other"
                features.append([float(v) for v in values])
                gesture_labels.append(label)
                groups.append(group)
                heuristic_families.append(family)
    if not features:
        raise ValueError("dataset is empty")
    x = np.asarray(features, dtype=np.float32)
    gestures = np.asarray(gesture_labels, dtype=str)
    families = np.asarray([gesture_label_to_family(v) for v in gesture_labels], dtype=str)
    return x, gestures, families, np.asarray(groups, dtype=str), np.asarray(heuristic_families, dtype=str)


def dataset_quality_summary(gestures: np.ndarray, groups: np.ndarray) -> dict:
    counts = Counter(gestures.tolist())
    groups_by_label: dict[str, set[str]] = defaultdict(set)
    for label, group in zip(gestures.tolist(), groups.tolist()):
        groups_by_label[str(label)].add(str(group))
    return {
        "label_counts": {label: int(counts.get(label, 0)) for label in RIGHT_DATASET_LABELS},
        "groups_per_label": {label: len(groups_by_label.get(label, set())) for label in RIGHT_DATASET_LABELS},
        "groups": sorted(set(groups.tolist())),
        "missing_labels": [label for label in RIGHT_DATASET_LABELS if counts.get(label, 0) <= 0],
    }


def validate_dataset_quality(gestures, groups, min_samples_per_class, min_groups_per_class):
    summary = dataset_quality_summary(gestures, groups)
    if summary["missing_labels"]:
        raise ValueError("dataset is missing required classes: " + ", ".join(summary["missing_labels"]))
    low_samples = [k for k, v in summary["label_counts"].items() if v < min_samples_per_class]
    if low_samples:
        raise ValueError("not enough samples per gesture: " + ", ".join(
            f"{k}={summary['label_counts'][k]}" for k in low_samples))
    low_groups = [k for k, v in summary["groups_per_label"].items() if v < min_groups_per_class]
    if low_groups:
        raise ValueError("not enough independent groups: " + ", ".join(
            f"{k}={summary['groups_per_label'][k]}" for k in low_groups))
    summary["passed"] = True
    return summary


def balance_training_set(x, y, random_state):
    rng = np.random.default_rng(random_state)
    labels, counts = np.unique(y, return_counts=True)
    target = int(np.max(counts))
    selected = []
    for label in labels:
        indices = np.flatnonzero(y == label)
        if len(indices) < target:
            indices = np.concatenate((indices, rng.choice(indices, size=target-len(indices), replace=True)))
        selected.append(indices)
    balanced = np.concatenate(selected)
    rng.shuffle(balanced)
    return x[balanced], y[balanced]


def metrics_dict(y_true, y_pred, labels):
    from sklearn.metrics import accuracy_score, balanced_accuracy_score, classification_report, confusion_matrix
    report = classification_report(y_true, y_pred, labels=labels, output_dict=True, zero_division=0)
    return {
        "accuracy": float(accuracy_score(y_true, y_pred)),
        "balanced_accuracy": float(balanced_accuracy_score(y_true, y_pred)),
        "macro_f1": float(report["macro avg"]["f1-score"]),
        "per_class": {label: {
            "precision": float(report[label]["precision"]),
            "recall": float(report[label]["recall"]),
            "f1": float(report[label]["f1-score"]),
            "support": int(report[label]["support"]),
        } for label in labels},
        "confusion_matrix": confusion_matrix(y_true, y_pred, labels=labels).tolist(),
    }


def fit_model(x, y, args, random_state):
    from sklearn.neural_network import MLPClassifier
    from sklearn.preprocessing import StandardScaler
    bx, by = balance_training_set(x, y, random_state)
    scaler = StandardScaler()
    scaled = scaler.fit_transform(bx)
    classifier = MLPClassifier(
        hidden_layer_sizes=(args.hidden,), activation="relu", solver="adam",
        alpha=args.alpha, batch_size=min(args.batch_size, len(scaled)),
        learning_rate_init=args.learning_rate, max_iter=args.max_iter,
        random_state=random_state, early_stopping=False,
    )
    classifier.fit(scaled, by)
    return scaler, classifier


def grouped_out_of_fold_evaluation(x, y, groups, heuristic_predictions, labels, args):
    from sklearn.model_selection import GroupKFold
    unique_groups = np.unique(groups)
    if len(unique_groups) < 2:
        raise ValueError("need at least two independent recording groups")
    n_splits = min(int(args.max_folds), len(unique_groups))
    splitter = GroupKFold(n_splits=n_splits)
    oof = np.empty(len(y), dtype=object)
    covered = np.zeros(len(y), dtype=bool)
    fold_reports = []
    for fold_index, (train_idx, test_idx) in enumerate(splitter.split(x, y, groups), start=1):
        if set(y[train_idx].tolist()) != set(labels):
            missing = sorted(set(labels) - set(y[train_idx].tolist()))
            raise ValueError(f"fold {fold_index} training side missing families: {', '.join(missing)}")
        scaler, clf = fit_model(x[train_idx], y[train_idx], args, args.random_state + fold_index)
        pred = clf.predict(scaler.transform(x[test_idx]))
        oof[test_idx] = pred
        covered[test_idx] = True
        fold_reports.append({
            "fold": fold_index,
            "train_groups": sorted(np.unique(groups[train_idx]).tolist()),
            "test_groups": sorted(np.unique(groups[test_idx]).tolist()),
            "tiny_family_model": metrics_dict(y[test_idx], pred, labels),
            "heuristic_family_baseline": metrics_dict(y[test_idx], heuristic_predictions[test_idx], labels),
        })
    if not bool(np.all(covered)):
        raise RuntimeError("grouped evaluation did not cover every sample")
    oof = np.asarray(oof, dtype=str)
    return {
        "method": "GroupKFold out-of-fold family classification",
        "fold_count": n_splits,
        "folds": fold_reports,
        "tiny_model": metrics_dict(y, oof, labels),
        "heuristic_baseline": metrics_dict(y, heuristic_predictions, labels),
    }


def train(args) -> dict:
    x, gestures, families, groups, heuristic_families = load_records(args.datasets)
    quality = validate_dataset_quality(gestures, groups, args.min_samples_per_class, args.min_groups_per_class)
    labels = list(RIGHT_FAMILY_LABELS)
    evaluation = grouped_out_of_fold_evaluation(x, families, groups, heuristic_families, labels, args)
    tiny = evaluation["tiny_model"]
    baseline = evaluation["heuristic_baseline"]
    min_recall = min(tiny["per_class"][label]["recall"] for label in labels)
    eligible = tiny["macro_f1"] >= baseline["macro_f1"] + args.min_macro_improvement and min_recall >= args.min_class_recall

    scaler, clf = fit_model(x, families, args, args.random_state)
    if len(clf.coefs_) != 2 or len(clf.intercepts_) != 2:
        raise RuntimeError("trainer expected exactly one hidden layer")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    np.savez(args.output,
        labels=np.asarray(clf.classes_), feature_version=np.asarray([FEATURE_VERSION], dtype=np.int32),
        task=np.asarray([MODEL_TASK]),
        mean=np.asarray(scaler.mean_, dtype=np.float32), scale=np.asarray(scaler.scale_, dtype=np.float32),
        w1=np.asarray(clf.coefs_[0], dtype=np.float32), b1=np.asarray(clf.intercepts_[0], dtype=np.float32),
        w2=np.asarray(clf.coefs_[1], dtype=np.float32), b2=np.asarray(clf.intercepts_[1], dtype=np.float32))

    report = {
        "feature_version": FEATURE_VERSION,
        "model": str(args.output),
        "task": MODEL_TASK,
        "labels": labels,
        "samples": int(len(x)),
        "gesture_label_counts": dict(Counter(gestures.tolist())),
        "family_label_counts": dict(Counter(families.tolist())),
        "dataset_quality": quality,
        "recording_groups": sorted(np.unique(groups).tolist()),
        "evaluation": evaluation,
        "tiny_model": tiny,
        "heuristic_baseline": baseline,
        "promotion_gate": {
            "eligible": bool(eligible),
            "required_macro_f1_improvement": float(args.min_macro_improvement),
            "required_min_class_recall": float(args.min_class_recall),
            "observed_macro_f1_improvement": float(tiny["macro_f1"] - baseline["macro_f1"]),
            "observed_min_class_recall": float(min_recall),
        },
    }
    args.output.with_suffix(".report.json").write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description="Train Gesture Rack broad right-hand family classifier")
    parser.add_argument("datasets", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, default=Path("models/right_gesture_landmark_v1.npz"))
    parser.add_argument("--hidden", type=int, default=32)
    parser.add_argument("--alpha", type=float, default=0.001)
    parser.add_argument("--batch-size", type=int, default=64)
    parser.add_argument("--learning-rate", type=float, default=0.001)
    parser.add_argument("--max-iter", type=int, default=500)
    parser.add_argument("--random-state", type=int, default=41)
    parser.add_argument("--max-folds", type=int, default=5)
    parser.add_argument("--min-samples-per-class", type=int, default=80)
    parser.add_argument("--min-groups-per-class", type=int, default=2)
    parser.add_argument("--min-macro-improvement", type=float, default=0.02)
    parser.add_argument("--min-class-recall", type=float, default=0.94)
    args = parser.parse_args()
    if not 1 <= args.hidden <= 128:
        parser.error("--hidden must be in 1..128")
    if args.max_folds < 2 or args.min_groups_per_class < 2:
        parser.error("grouped evaluation requires at least two groups/folds")
    report = train(args)
    gate = report["promotion_gate"]
    print(json.dumps({
        "model": report["model"], "task": report["task"], "samples": report["samples"],
        "groups": len(report["recording_groups"]),
        "tiny_macro_f1": report["tiny_model"]["macro_f1"],
        "heuristic_macro_f1": report["heuristic_baseline"]["macro_f1"],
        "promotion_eligible": gate["eligible"],
    }, indent=2))


if __name__ == "__main__":
    main()
