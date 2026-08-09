# GestureRack Phase A — 9 Slot Rack Core

Local implementation bundle. GitHub was read-only and was not modified.

Implemented in this pass:

- `PluginSlot` abstraction for 9 fixed slots.
- `RackGraphManager` with serial routing through loaded slots in slot order.
- Per-slot asynchronous VST3 Load / Replace using generation tokens to reject stale callbacks.
- Per-slot Remove / Open Editor / Active-Bypass state.
- Per-slot child plugin state persistence.
- State format v2 with all nine slots plus legacy v1 single-slot restore support.
- Rack latency reports the sum of all loaded child latencies.
- Host-bypass delay capacity expanded to the rack-level latency budget.
- 9-slot UI, mouse slot selection, selected-slot actions, and slot state feedback.
- Existing Open Palm / Closed Fist behavior preserved and routed to the selected slot.
- Slot selection does not reset `lastAppliedGesture`, preventing a held gesture from firing merely because the selected slot changed.

Intentionally not implemented yet:

- Dual-hand Vision protocol v2.
- Left-hand digit classification.
- 6/7/8/9 hand-shape recognition.
- Parameter inspector / GestureBinding / Parameter Learn (Phase B).

Validation still required on the target Windows/JUCE environment:

- Release compile with Visual Studio 2022 + JUCE 8.0.15.
- Real VST3 load/replace/remove for multiple plugin vendors.
- DAW project save/reopen with 2+ loaded slots.
- Dynamic-latency plugin behavior while multiple slots are loaded.

## Validation performed here

Static structure checks passed for:

- fixed slot count = 9
- removal of the old single `childNode` / `loadedDescription` architecture
- serial graph iteration across loaded slots
- state format v2 and 9-slot serialization loop
- stale async load rejection via per-slot generation token
- legacy single-slot state restore path
- CMake registration of the new source files
- no implementation of left-hand digit 6/7/8/9 recognition

JUCE API calls used by the new graph manager were cross-checked against current JUCE documentation (`addNode`, `removeNode`, `disconnectNode`, `addConnection`, `UpdateKind`, `rebuild`).

A real JUCE compile was not possible in this container because JUCE is not installed and outbound DNS/network access for cloning dependencies is unavailable. The target Windows Visual Studio/JUCE build remains the required compile gate before Phase A should be considered production-validated.
