from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

from gesture_dataset import RIGHT_DATASET_LABELS
from landmark_features import FEATURE_VERSION


def load_records(paths: list[Path]) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    features: list[list[float]] = []
    labels: list[str] = []
    groups: list[str] = []
    heuristic: list[str] = []

    for path in paths:
        with path.open("r", encoding="utf-8") as handle:
            for line_number, line in enumerate(handle, start=1):
                if not line.strip():
                    continue
                record = json.loads(line)
                if int(record.get("feature_version", -1)) != FEATURE_VERSION:
                    raise ValueError(
                        f"{path}:{line_number}: feature version does not match runtime")
                values = record.get("features")
                if not isinstance(values, list) or len(values) != 81:
                    raise ValueError(f"{path}:{line_number}: expected 81 features")
                label = str(record.get("label", ""))
                if not label:
                    raise ValueError(f"{path}:{line_number}: missing label")

                # Prefer an explicit guided-capture group. This lets a user record
                # all eight classes in one controlled pass without restarting the
                # production sidecar between labels. Older datasets fall back to
                # sidecar session_id, then file stem.
                recording_group = str(record.get("recording_group", "")).strip()
                session_id = str(record.get("session_id", "")).strip()
                group = recording_group or session_id or path.stem
                heuristic_record = record.get("heuristic", {})
                heuristic_label = (str(heuristic_record.get("gesture", "None"))
                                   if isinstance(heuristic_record, dict) else "None")

                features.append([float(value) for value in values])
                labels.append(label)
                groups.append(group)
                heuristic.append(heuristic_label)

    if not features:
        raise ValueError("dataset is empty")
    return (
        np.asarray(features, dtype=np.float32),
        np.asarray(labels, dtype=str),
        np.asarray(groups, dtype=str),
        np.asarray(heuristic, dtype=str),
    )


def dataset_quality_summary(y: np.ndarray, groups: np.ndarray) -> dict:
    counts = Counter(y.tolist())
    groups_by_label: dict[str, set[str]] = defaultdict(set)
    for label, group in zip(y.tolist(), groups.tolist()):
        groups_by_label[str(label)].add(str(group))

    return {
        "label_counts": {label: int(counts.get(label, 0)) for label in RIGHT_DATASET_LABELS},
        "groups_per_label": {
            label: len(groups_by_label.get(label, set())) for label in RIGHT_DATASET_LABELS
        },
        "groups": sorted(set(groups.tolist())),
        "missing_labels": [label for label in RIGHT_DATASET_LABELS if counts.get(label, 0) <= 0],
    }


def validate_dataset_quality(y: np.ndarray, groups: np.ndarray,
                             min_samples_per_class: int,
                             min_groups_per_class: int) -> dict:
    summary = dataset_quality_summary(y, groups)
    missing = summary["missing_labels"]
    if missing:
        raise ValueError(
            "dataset is missing required classes: " + ", ".join(missing)
            + ". Include None hard negatives plus all seven control gestures."
        )

    low_samples = [
        label for label, count in summary["label_counts"].items()
        if count < min_samples_per_class
    ]
    if low_samples:
        details = ", ".join(
            f"{label}={summary['label_counts'][label]}" for label in low_samples)
        raise ValueError(
            f"not enough samples per class ({details}); need at least {min_samples_per_class} each"
        )

    low_groups = [
        label for label, count in summary["groups_per_label"].items()
        if count < min_groups_per_class
    ]
    if low_groups:
        details = ", ".join(
            f"{label}={summary['groups_per_label'][label]}" for label in low_groups)
        raise ValueError(
            f"not enough independent recording groups ({details}); need at least {min_groups_per_class} per class. "
            "Run collect_gesture_session.py again under different position/lighting/distance conditions."
        )

    summary["min_samples_per_class_required"] = int(min_samples_per_class)
    summary["min_groups_per_class_required"] = int(min_groups_per_class)
    summary["passed"] = True
    return summary


def balance_training_set(x: np.ndarray, y: np.ndarray, random_state: int):
    rng = np.random.default_rng(random_state)
    labels, counts = np.unique(y, return_counts=True)
    target = int(np.max(counts))
    selected: list[np.ndarray] = []
    for label in labels:
        indices = np.flatnonzero(y == label)
        if len(indices) < target:
            extra = rng.choice(indices, size=target - len(indices), replace=True)
            indices = np.concatenate((indices, extra))
        selected.append(indices)
    balanced = np.concatenate(selected)
    rng.shuffle(balanced)
    return x[balanced], y[balanced]


def metrics_dict(y_true: np.ndarray, y_pred: np.ndarray, labels: list[str]) -> dict:
    from sklearn.metrics import (
        accuracy_score,
        balanced_accuracy_score,
        classification_report,
        confusion_matrix,
    )

    report = classification_report(
        y_true,
        y_pred,
        labels=labels,
        output_dict=True,
        zero_division=0,
    )
    return {
        "accuracy": float(accuracy_score(y_true, y_pred)),
        "balanced_accuracy": float(balanced_accuracy_score(y_true, y_pred)),
        "macro_f1": float(report["macro avg"]["f1-score"]),
        "per_class": {
            label: {
                "precision": float(report[label]["precision"]),
                "recall": float(report[label]["recall"]),
                "f1": float(report[label]["f1-score"]),
                "support": int(report[label]["support"]),
            }
            for label in labels
        },
        "confusion_matrix": confusion_matrix(y_true, y_pred, labels=labels).tolist(),
    }


def fit_model(x: np.ndarray, y: np.ndarray, args, random_state: int):
    from sklearn.neural_network import MLPClassifier
    from sklearn.preprocessing import StandardScaler

    balanced_x, balanced_y = balance_training_set(x, y, random_state)
    scaler = StandardScaler()
    scaled = scaler.fit_transform(balanced_x)
    classifier = MLPClassifier(
        hidden_layer_sizes=(args.hidden,),
        activation="relu",
        solver="adam",
        alpha=args.alpha,
        batch_size=min(args.batch_size, len(scaled)),
        learning_rate_init=args.learning_rate,
        max_iter=args.max_iter,
        random_state=random_state,
        early_stopping=False,
    )
    classifier.fit(scaled, balanced_y)
    return scaler, classifier


def grouped_out_of_fold_evaluation(x: np.ndarray, y: np.ndarray, groups: np.ndarray,
                                   heuristic_predictions: np.ndarray, labels: list[str], args):
    from sklearn.model_selection import GroupKFold

    unique_groups = np.unique(groups)
    if len(unique_groups) < 2:
        raise ValueError("need at least two independent recording groups")

    n_splits = min(int(args.max_folds), len(unique_groups))
    if n_splits < 2:
        raise ValueError("grouped evaluation requires at least two folds")

    splitter = GroupKFold(n_splits=n_splits)
    oof_predictions = np.empty(len(y), dtype=object)
    covered = np.zeros(len(y), dtype=bool)
    fold_reports: list[dict] = []

    for fold_index, (train_indices, test_indices) in enumerate(
            splitter.split(x, y, groups), start=1):
        train_labels = set(y[train_indices].tolist())
        if train_labels != set(labels):
            missing = sorted(set(labels) - train_labels)
            raise ValueError(
                f"fold {fold_index} training side is missing classes: {', '.join(missing)}. "
                "Record complete guided capture groups so every fold can learn every gesture."
            )

        scaler, classifier = fit_model(
            x[train_indices], y[train_indices], args, args.random_state + fold_index)
        predictions = classifier.predict(scaler.transform(x[test_indices]))
        oof_predictions[test_indices] = predictions
        covered[test_indices] = True

        fold_reports.append({
            "fold": fold_index,
            "train_groups": sorted(np.unique(groups[train_indices]).tolist()),
            "test_groups": sorted(np.unique(groups[test_indices]).tolist()),
            "tiny_model": metrics_dict(y[test_indices], predictions, labels),
            "heuristic_baseline": metrics_dict(
                y[test_indices], heuristic_predictions[test_indices], labels),
        })

    if not bool(np.all(covered)):
        raise RuntimeError("grouped evaluation did not produce an out-of-fold prediction for every sample")

    oof = np.asarray(oof_predictions, dtype=str)
    return {
        "method": "GroupKFold out-of-fold",
        "fold_count": n_splits,
        "folds": fold_reports,
        "tiny_model": metrics_dict(y, oof, labels),
        "heuristic_baseline": metrics_dict(y, heuristic_predictions, labels),
    }


def train(args) -> dict:
    x, y, groups, heuristic_predictions = load_records(args.datasets)
    quality = validate_dataset_quality(
        y,
        groups,
        min_samples_per_class=args.min_samples_per_class,
        min_groups_per_class=args.min_groups_per_class,
    )
    labels = list(RIGHT_DATASET_LABELS)

    evaluation = grouped_out_of_fold_evaluation(
        x, y, groups, heuristic_predictions, labels, args)
    tiny_metrics = evaluation["tiny_model"]
    baseline_metrics = evaluation["heuristic_baseline"]

    min_tiny_recall = min(
        tiny_metrics["per_class"][label]["recall"] for label in labels
    )
    promotion_eligible = (
        tiny_metrics["macro_f1"] >= baseline_metrics["macro_f1"] + args.min_macro_improvement
        and min_tiny_recall >= args.min_class_recall
    )

    # After unbiased grouped out-of-fold evaluation, fit the shipped shadow
    # candidate on all approved data. This avoids wasting the held-out frames in
    # the final candidate while keeping promotion metrics strictly out-of-fold.
    final_scaler, final_classifier = fit_model(x, y, args, args.random_state)

    if len(final_classifier.coefs_) != 2 or len(final_classifier.intercepts_) != 2:
        raise RuntimeError("trainer expected exactly one hidden layer")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    np.savez(
        args.output,
        labels=np.asarray(final_classifier.classes_),
        feature_version=np.asarray([FEATURE_VERSION], dtype=np.int32),
        mean=np.asarray(final_scaler.mean_, dtype=np.float32),
        scale=np.asarray(final_scaler.scale_, dtype=np.float32),
        w1=np.asarray(final_classifier.coefs_[0], dtype=np.float32),
        b1=np.asarray(final_classifier.intercepts_[0], dtype=np.float32),
        w2=np.asarray(final_classifier.coefs_[1], dtype=np.float32),
        b2=np.asarray(final_classifier.intercepts_[1], dtype=np.float32),
    )

    report = {
        "feature_version": FEATURE_VERSION,
        "model": str(args.output),
        "labels": labels,
        "samples": int(len(x)),
        "label_counts": dict(Counter(y.tolist())),
        "dataset_quality": quality,
        "recording_groups": sorted(np.unique(groups).tolist()),
        "evaluation": evaluation,
        "tiny_model": tiny_metrics,
        "heuristic_baseline": baseline_metrics,
        "promotion_gate": {
            "eligible": bool(promotion_eligible),
            "required_macro_f1_improvement": float(args.min_macro_improvement),
            "required_min_class_recall": float(args.min_class_recall),
            "observed_macro_f1_improvement": float(
                tiny_metrics["macro_f1"] - baseline_metrics["macro_f1"]),
            "observed_min_class_recall": float(min_tiny_recall),
        },
    }

    report_path = args.output.with_suffix(".report.json")
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Train the Gesture Rack physical-right tiny landmark classifier")
    parser.add_argument("datasets", nargs="+", type=Path,
                        help="JSONL files recorded by GestureDatasetRecorder or collect_gesture_session.py")
    parser.add_argument("--output", type=Path,
                        default=Path("models/right_gesture_landmark_v1.npz"))
    parser.add_argument("--hidden", type=int, default=32)
    parser.add_argument("--alpha", type=float, default=0.001)
    parser.add_argument("--batch-size", type=int, default=64)
    parser.add_argument("--learning-rate", type=float, default=0.001)
    parser.add_argument("--max-iter", type=int, default=500)
    parser.add_argument("--random-state", type=int, default=41)
    parser.add_argument("--max-folds", type=int, default=5,
                        help="Maximum GroupKFold count for out-of-fold promotion metrics")
    parser.add_argument("--min-samples-per-class", type=int, default=80,
                        help="Refuse training when any gesture has fewer independent frame samples")
    parser.add_argument("--min-groups-per-class", type=int, default=2,
                        help="Refuse training unless every class appears in this many independent guided capture groups")
    parser.add_argument("--min-macro-improvement", type=float, default=0.02,
                        help="Shadow model must beat held-out heuristic macro-F1 by this amount")
    parser.add_argument("--min-class-recall", type=float, default=0.90,
                        help="Every out-of-fold class must meet this recall before promotion")
    args = parser.parse_args()

    if not 1 <= args.hidden <= 128:
        parser.error("--hidden must be in 1..128")
    if args.max_folds < 2:
        parser.error("--max-folds must be at least 2")
    if args.min_samples_per_class < 1:
        parser.error("--min-samples-per-class must be positive")
    if args.min_groups_per_class < 2:
        parser.error("--min-groups-per-class must be at least 2")

    report = train(args)
    gate = report["promotion_gate"]
    print(json.dumps({
        "model": report["model"],
        "samples": report["samples"],
        "groups": len(report["recording_groups"]),
        "evaluation": report["evaluation"]["method"],
        "folds": report["evaluation"]["fold_count"],
        "tiny_macro_f1": report["tiny_model"]["macro_f1"],
        "heuristic_macro_f1": report["heuristic_baseline"]["macro_f1"],
        "promotion_eligible": gate["eligible"],
        "report": str(Path(report["model"]).with_suffix(".report.json")),
    }, indent=2))


if __name__ == "__main__":
    main()
