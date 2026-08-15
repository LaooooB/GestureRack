# Gesture Rack — Gesture Model Data Pipeline

This document is the implementation contract for the learned physical-right-hand classifier described in the optimization plan.

## Runtime rule

The shipping control path remains:

`MediaPipe landmarks/handedness -> physical role resolver -> role-specific classifier -> per-class hysteresis`

A learned landmark classifier must **not** replace the current heuristic/canned fusion merely because it trains successfully. It is promoted only after grouped out-of-fold measurements show a real accuracy improvement without adding material latency.

Physical role is resolved **before** gesture classification. Gesture shape is never allowed to decide whether a detection is the selector hand or the controller hand. Therefore the same two-finger pose is `Slot 2` on physical LEFT and `Victory` on physical RIGHT.

## Feature contract

`VisionEngine/landmark_features.py` owns feature version 1.

Each sample contains 81 float32 features:

- 63 palm-local XYZ landmark coordinates
- 5 finger straightness values
- 5 fingertip reach values
- 8 image-space direction values

Palm-local coordinates remove translation and scale and tolerate hand rotation. Image-space direction remains explicit so Point Left/Right and Thumb Up/Down retain the user's visible screen direction. MediaPipe world landmarks are preferred when available; normalized landmarks are the fallback.

Changing feature layout requires a new `FEATURE_VERSION`. Old models must fail closed instead of silently consuming incompatible features.

## Preferred data collection

1. Start the normal `GestureVisionEngine` and plugin.
2. Use `CALIBRATE RIGHT` in the plugin. Do not record with uncalibrated physical roles.
3. Run the guided collector:

```text
python collect_gesture_session.py
```

The Windows release package also contains `GestureDataCollector.exe`, so target-machine data can be captured without installing Python.

One collector run records all required physical-right classes into one explicit `recording_group`:

- `None` hard negatives
- `Open_Palm`
- `Closed_Fist`
- `Victory`
- `Thumb_Up`
- `Thumb_Down`
- `Point_Right`
- `Point_Left`

`None` is not "no hand". Keep the right hand visible and record realistic non-command poses, transitions, half-formed gestures, relaxed hands, edge-of-frame poses, and shapes that are easy to confuse with a real command.

During every class, deliberately vary:

- wrist rotation
- position in frame
- distance to camera
- hand size in frame
- moderate finger style differences
- bright/dim lighting
- background complexity

Run at least two complete guided capture groups under meaningfully different conditions. Three or more groups are preferred before considering production promotion.

### Legacy single-class collection

`collect_runtime_dataset.py <Label>` remains available for targeted hard-negative or failure-case capture, but full guided groups are preferred for promotion evaluation because each group contains all eight classes.

## No frame-random validation

Adjacent video frames are near duplicates. Randomly splitting individual frames between train and test produces misleadingly high accuracy.

The trainer therefore uses explicit recording groups and `GroupKFold` out-of-fold evaluation. Every sample is predicted by a model that was trained without that sample's recording group. The promotion metrics are computed from those out-of-fold predictions across the complete dataset.

After evaluation, the exported shadow candidate is retrained on all approved data. This means no data is wasted in the final candidate, while the reported promotion metrics remain unbiased by group leakage.

## Dataset quality gate

Training refuses to produce a promotion report unless:

- all seven control gestures are present
- the `None` hard-negative class is present
- every class meets the minimum sample count
- every class appears in at least two independent recording groups

Current defaults are 80 samples per class and two groups per class. These are minimum engineering gates, not a claim that two groups are enough for a commercial production model.

## Offline training

Training dependencies are development-only:

```text
python -m pip install -r requirements-dev.txt
```

Then:

```text
python train_tiny_classifier.py <dataset1.jsonl> <dataset2.jsonl> ... \
  --output models/right_gesture_landmark_v1.npz
```

The trainer uses a small one-hidden-layer MLP and exports only:

- StandardScaler mean/scale
- first dense layer weights/bias
- second dense layer weights/bias
- class names
- feature version

The shipping inference implementation in `tiny_landmark_classifier.py` is pure NumPy and does not depend on scikit-learn.

## Promotion gate

The generated `.report.json` compares the tiny model against the current heuristic classifier on the **same grouped out-of-fold samples**.

Default minimum gate:

- tiny-model macro F1 must exceed heuristic macro F1 by at least 0.02
- every out-of-fold class recall must be at least 0.90

The report also contains per-fold train/test recording groups, confusion matrices, per-class precision/recall/F1, and the aggregate heuristic baseline.

Passing this gate does not immediately replace production classification. The next phase is sidecar shadow mode: run both classifiers, transmit/log disagreements, and measure inference cost. Only after live shadow results are clean should the tiny classifier become authoritative.

## Continuous parameter control is separate

Discrete gesture classification and vertical parameter automation are intentionally separate pipelines.

`continuous_motion.py` applies a One Euro filter to right-hand height using actual callback timestamps. Gesture hold/release timing must never be inserted into this continuous path. This preserves fast parameter motion while keeping static hand jitter controlled.
