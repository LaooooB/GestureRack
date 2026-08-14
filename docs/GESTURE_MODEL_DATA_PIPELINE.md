# Gesture Rack — Gesture Model Data Pipeline

This document is the implementation contract for the learned physical-right-hand classifier described in the optimization plan.

## Runtime rule

The shipping control path remains:

`MediaPipe landmarks/handedness -> physical role resolver -> role-specific classifier -> per-class hysteresis`

A learned landmark classifier must **not** replace the current heuristic/canned fusion merely because it trains successfully. It is promoted only after session-held-out measurements show a real accuracy improvement without adding material latency.

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

## Data collection

1. Start the normal VisionEngine and plugin.
2. Use `CALIBRATE RIGHT` in the plugin. Do not record with uncalibrated physical roles.
3. Install the runtime Python requirements plus NumPy.
4. From `VisionEngine/`, record each ground-truth class while the production sidecar is running:

```text
python collect_runtime_dataset.py Open_Palm --seconds 8
python collect_runtime_dataset.py Closed_Fist --seconds 8
python collect_runtime_dataset.py Victory --seconds 8
python collect_runtime_dataset.py Thumb_Up --seconds 8
python collect_runtime_dataset.py Thumb_Down --seconds 8
python collect_runtime_dataset.py Point_Right --seconds 8
python collect_runtime_dataset.py Point_Left --seconds 8
python collect_runtime_dataset.py None --seconds 8
```

`None` is the hard-negative class. Record realistic non-command poses, transitions, partially formed gestures, hands near the edge of frame, and confusing shapes.

During every class, deliberately vary:

- wrist rotation
- position in frame
- distance to camera
- hand size in frame
- moderate finger style differences
- bright/dim lighting
- background complexity

Repeat the full set across multiple independent VisionEngine sessions. A new sidecar session is a new evaluation group.

### No frame-random validation

Adjacent video frames are near duplicates. Randomly splitting individual frames between train and test produces misleadingly high accuracy. The trainer therefore holds out complete sidecar sessions with `GroupShuffleSplit` and refuses to train a promotion report when every class cannot appear on both sides.

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

The generated `.report.json` compares the tiny model against the current heuristic classifier on the **same held-out sessions**.

Default minimum gate:

- tiny-model macro F1 must exceed heuristic macro F1 by at least 0.02
- every held-out class recall must be at least 0.90

Passing this gate does not immediately replace production classification. The next phase is sidecar shadow mode: run both classifiers, transmit/log disagreements, and measure inference cost. Only after live shadow results are clean should the tiny classifier become authoritative.

## Continuous parameter control is separate

Discrete gesture classification and vertical parameter automation are intentionally separate pipelines.

`continuous_motion.py` applies a One Euro filter to right-hand height using actual callback timestamps. Gesture hold/release timing must never be inserted into this continuous path. This preserves fast parameter motion while keeping static hand jitter controlled.
