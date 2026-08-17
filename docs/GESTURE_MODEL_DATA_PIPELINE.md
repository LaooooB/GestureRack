# Gesture Rack — Gesture Model Data Pipeline

> The runtime architecture is defined in `docs/GESTURE_RECOGNITION_V3.md`. This file defines only the learned-model data and promotion pipeline.

## Model task

The learned model is **not** a final gesture classifier anymore.

Its task marker is:

`right_hand_shape_family_v2`

It predicts exactly four broad right-hand shape families:

- `OpenPalm`
- `FoldedFour`
- `Victory`
- `Other`

`FoldedFour` deliberately combines Closed Fist and every Thumb direction. Fist-vs-Thumb is handled by the production thumb specialist. Thumb Up/Down/Left/Right is handled by deterministic landmark direction geometry.

Old NPZ files without this task marker or with the old final-gesture label space fail closed and are not loaded as valid shadow models.

## Feature contract

`VisionEngine/landmark_features.py` owns feature version 1.

Each sample contains 81 float32 features:

- 63 palm-local XYZ landmark coordinates
- 5 finger straightness values
- 5 fingertip reach values
- 8 image-space direction values

World landmarks are preferred when MediaPipe supplies a complete set. Normalized landmarks are the fallback.

Changing the feature layout requires a new `FEATURE_VERSION`.

## Ground-truth labels collected

Collectors still record the final physical-right labels because final-label data is required to measure Fist/Thumb and direction failures:

- `None`
- `Open_Palm`
- `Closed_Fist`
- `Victory`
- `Thumb_Up`
- `Thumb_Down`
- `Thumb_Left`
- `Thumb_Right`

Training projects these labels into family labels:

- Open_Palm -> OpenPalm
- Closed_Fist -> FoldedFour
- Thumb_* -> FoldedFour
- Victory -> Victory
- None -> Other

`None` means a visible right hand in a **non-command / ambiguous pose**, not an absent hand.

## Hard negatives are required

High-value `None` samples include:

- Fist -> Thumb transitions
- Thumb -> Fist transitions
- half-extended thumbs
- diagonal thumbs near direction boundaries
- relaxed and half-open hands
- edge-of-frame hands
- motion blur
- wrist rotation
- common non-command movements

Perfect static poses alone are not a sufficient training set.

## Preferred data collection

1. Start the normal `GestureVisionEngine` and Gesture Rack plugin.
2. Complete physical-right calibration.
3. Run:

```text
python collect_gesture_session.py
```

The Windows package also includes `GestureDataCollector.exe`.

One guided invocation is one explicit `recording_group` containing all required final labels. During each class vary:

- wrist rotation
- frame position
- camera distance
- hand size in frame
- natural finger style
- lighting
- background complexity

Record at least two complete groups under meaningfully different conditions. Three or more groups are preferred before production promotion decisions.

`collect_runtime_dataset.py <Label>` remains available for targeted failure-case capture.

## No random frame split

Adjacent video frames are near duplicates. Random frame train/test splits give falsely optimistic metrics.

The trainer uses explicit recording groups and `GroupKFold` out-of-fold evaluation. Every evaluated frame is predicted by a model that did not train on that frame's recording group.

After grouped evaluation, the exported shadow candidate is retrained on all approved data.

## Dataset quality gate

Training refuses promotion reporting unless:

- all seven final control gestures are present
- `None` hard negatives are present
- every final label meets the minimum sample count
- every final label occurs in at least two independent recording groups

Current minimum engineering defaults are 80 samples per final label and two groups per label.

## Training

Development dependencies:

```text
python -m pip install -r requirements-dev.txt
```

Train:

```text
python train_tiny_classifier.py <dataset1.jsonl> <dataset2.jsonl> ... \
  --output models/right_gesture_landmark_v1.npz
```

The trainer exports:

- model task marker
- feature version
- four family labels
- StandardScaler mean/scale
- one hidden dense layer weights/bias
- output dense layer weights/bias

Shipping inference remains pure NumPy and has no scikit-learn runtime dependency.

## Promotion gate

Grouped out-of-fold family metrics compare the tiny model against the production heuristic family result on the same samples.

Current minimum gate:

- tiny family macro-F1 >= heuristic family macro-F1 + 0.02
- every family recall >= 0.94

Passing the offline gate does **not** give the model control authority. It only makes the candidate eligible for live shadow testing.

Shadow testing measures disagreement and inference cost. Direction remains deterministic even if the family model is eventually promoted.

## Product metrics beyond family F1

Frame metrics alone cannot prove a musical controller is safe. Release evaluation should also measure:

- Fist/Thumb confusion
- Thumb direction confusion
- false ENTER events per minute
- missed ENTER events
- median and P95 activation latency
- repeated/chattering ENTER events
- continuous-motion latency separately

## Continuous control stays separate

`continuous_motion.py` applies a One Euro filter to right-hand height using actual callback timestamps.

Gesture family hold/release timing must never be inserted into this path. Discrete recognition can become more conservative without making continuous parameter motion feel delayed.
