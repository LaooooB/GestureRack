# Gesture Rack — Gesture Recognition V3

This document describes the production recognition architecture after the August 2026 hierarchy refactor.

## Product rule

Gesture Rack must prefer **no control event** over a wrong control event. A missed frame is tolerable; an accidental bypass, mode switch, or parameter jump is not.

The recognition stack is therefore hierarchical instead of a flat final-gesture classifier:

`camera -> MediaPipe landmarks -> physical hand role -> hand quality -> shape family -> family specialist -> thumb direction -> temporal state -> ENTER/HOLD/EXIT`

Continuous hand height is a separate path and must never wait for discrete gesture hold/release timing.

## Right-hand control vocabulary

Production controls are:

- Open Palm
- Closed Fist
- Victory
- Thumb Up
- Thumb Down
- Thumb Left
- Thumb Right

Old `Point Left` / `Point Right` saved mappings are accepted by the C++ parser and migrate to `Thumb Left` / `Thumb Right`. The numeric enum positions are preserved for project-state compatibility.

## Shape families

The first classifier does not ask whether a folded hand is Fist or Thumb Up/Down/Left/Right. It asks only for a broad shape:

- `OpenPalm`
- `FoldedFour`
- `Victory`
- `Other`

`FoldedFour` means index, middle, ring, and pinky are folded. The thumb is deliberately excluded from this family decision.

This removes the original structural error where Closed Fist and thumb gestures competed as peer classes even though they share the same four folded fingers.

## FoldedFour specialist

After `FoldedFour` wins, a thumb specialist evaluates multiple cues:

- distal thumb straightness
- MCP-to-tip reach
- fingertip distance from palm center
- fingertip separation from index MCP
- thumb base opening

The result is one of:

- `Tucked` -> Closed Fist
- `Extended` -> continue to direction resolver
- `Ambiguous` -> no gesture

There is an explicit ambiguity band between tucked and extended. Half-formed thumbs are not forced into either Fist or Thumb.

## Direction resolver

Thumb direction is deterministic geometry, not a learned final class.

The resolver blends the stable long `MCP -> Tip` vector with the distal `IP -> Tip` vector, then resolves:

- Up
- Down
- Left
- Right
- Ambiguous

Diagonal boundaries are dead zones. A thumb close to 45 degrees does not alternate between adjacent directions; it becomes `Ambiguous` until the direction is clear again.

MediaPipe canned gesture output does not decide direction.

## MediaPipe canned classifier

MediaPipe canned results are retained only as weak corroborating evidence for broad families. Geometry remains authoritative.

A matching canned family can slightly strengthen a family score. A conflicting high-confidence canned result can slightly reduce it. Canned recognition cannot force a final control gesture.

This avoids the old behavior where canned output carried enough weight to pull a FoldedFour pose toward the wrong final class.

## Hand quality gate

Every right-hand observation receives a lightweight quality score before it can produce a gesture. The current gate checks:

- finite/complete 21 landmarks
- palm size in frame
- distance from frame edges
- non-collapsed palm geometry

Very poor geometry fails closed. Lower-but-usable quality reduces classification confidence so the temporal layer naturally requires stronger evidence.

The quality gate is intentionally cheap and stateless; temporal dropout protection belongs to the stabilizer.

## Temporal state and low latency

The temporal stabilizer is adaptive rather than using one hold time for every transition.

### Clean high-confidence gesture

A very clear candidate can reduce the normal hold and become stable after roughly two inference callbacks. This keeps obvious Palm/Fist/Victory/Thumb commands responsive.

### Near-threshold gesture

A weak candidate needs extra evidence before activation.

### Fist <-> Thumb

This is a known dangerous confusion pair. Switching between Closed Fist and any Thumb direction requires additional hold evidence.

### Thumb direction changes

Changing directly between Thumb Up/Down/Left/Right has an additional transition barrier. A short diagonal/ambiguous period does not immediately release the current thumb command.

### Release

Thumb gestures use a slightly longer release grace than ordinary gestures because direction geometry naturally crosses an ambiguous zone during a physical rotation.

## Event model

The C++ runtime exposes three useful phases:

- `ENTER`: a new stable gesture begins
- `HOLD`: the gesture remains active (`continuousActive`)
- `EXIT`: the previous stable gesture ends

This is the basis for later parameter behavior:

- Toggle / button -> act only on `ENTER`
- Step / cycle -> act only on `ENTER`
- Continuous parameter -> use `HOLD` plus the independent continuous motion signal

A brief tracking loss does not immediately manufacture a second ENTER.

## Continuous motion path

Continuous height remains:

`right-hand palm height -> One Euro filter -> normalized continuous source`

It does not pass through shape-family activation holds. This prevents accuracy improvements for discrete commands from making knobs feel delayed.

## Learned family model

The tiny NumPy MLP is no longer trained on final gestures. Its task marker is:

`right_hand_shape_family_v2`

It predicts exactly:

- OpenPalm
- FoldedFour
- Victory
- Other

Old final-gesture NPZ models fail closed because they do not contain the new task marker / label contract.

The model remains in shadow mode until held-out data proves it is better than the production family heuristic without material latency cost.

## Dataset contract

Collectors still record final labels so we can measure the failures that matter to the product:

- None
- Open_Palm
- Closed_Fist
- Victory
- Thumb_Up
- Thumb_Down
- Thumb_Left
- Thumb_Right

Training derives the broad family label from those records.

`None` must include hard negatives, not an absent hand. Important hard negatives include:

- Fist -> Thumb transitions
- Thumb -> Fist transitions
- half-extended thumbs
- diagonal thumbs near direction boundaries
- relaxed / half-open hands
- edge-of-frame hands
- wrist rotation
- distance and hand-size changes
- bright/dim lighting
- motion blur
- natural non-command hand activity

Guided recording groups remain the unit for held-out evaluation; adjacent frames must not be randomly split across train and test.

## Promotion metrics

Frame accuracy alone is not a sufficient product metric. Evaluation should track:

- broad-family macro F1
- per-family recall
- Fist/Thumb confusion rate
- thumb direction confusion rate
- false ENTER count / minute
- missed ENTER count
- median activation latency
- P95 activation latency
- gesture chatter / repeated unintended ENTER
- continuous-control latency separately

The family-model training gate currently requires at least +0.02 macro-F1 over the heuristic family baseline and >= 0.94 recall for every family before a candidate is even eligible for promotion to live shadow testing.

## Latency architecture that must remain

The camera pipeline already follows the intended low-latency structure and should not be regressed:

- camera buffer requested at 1
- capture thread keeps only the newest frame
- one MediaPipe inference in flight
- stale captured frames are replaced instead of queued
- UDP receiver drains old datagrams and keeps the newest result
- continuous motion uses callback timestamps rather than assuming a fixed frame rate

Accuracy work should stay downstream of landmark extraction unless profiling proves the backend itself is the bottleneck.

## Compatibility

- Protocol v2 final `raw_gesture` / `stable_gesture` fields remain available.
- Existing Point Left/Right saved mappings parse as Thumb Left/Right.
- Existing continuous-height mappings are unaffected.
- Slot-selection logic for the physical left hand is unaffected by this refactor.
