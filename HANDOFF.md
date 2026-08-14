# Gesture Rack — Development Handoff

**Handoff date:** 2026-08-09  
**Current platform:** Windows 10/11 + Visual Studio 2022 + JUCE 8.0.15  
**Current status:** Source builds successfully in Windows Release after the JUCE 8 compatibility and low-memory build fixes. The current baseline includes the gesture pipeline, child VST3 hosting, visual hand skeleton, and latency-compensated gesture bypass.

---

## 1. Product Goal

Create a cross-DAW visual gesture-control audio plugin.

Current user-facing behavior is intentionally limited to:

- `Open_Palm` → hosted effect **ACTIVE**
- `Closed_Fist` → hosted effect **BYPASSED**
- no hand / unknown gesture → **hold the previous stable state**
- plugin UI shows a real-time 21-landmark hand skeleton and ACTIVE/BYPASSED visual state

Long-term architecture must support mapping hand signals such as X/Y position, pinch, rotation, hand openness, etc. to arbitrary hosted-plugin parameters without rewriting the gesture system.

The project deliberately uses a **rack/plugin-host architecture** rather than attempting to control arbitrary sibling plugins inside a DAW, because VST3 does not provide a universal API for one plugin to directly control another plugin instance owned by the DAW.

---

## 2. Chosen Architecture

```text
Webcam
  ↓
VisionEngine (one external Python process)
  ├─ OpenCV camera capture
  ├─ MediaPipe Gesture Recognizer
  ├─ Open_Palm / Closed_Fist classification
  ├─ 21 hand landmarks
  └─ temporal stabilizer
          ↓
UDP multicast 239.255.71.77:17777
          ↓
Gesture Rack VST3/AU instance(s)
  ├─ VisionReceiver
  ├─ gesture state → requested bypass state
  ├─ GestureBypassWrapper
  ├─ child VST3 host
  ├─ latency-compensated dry path
  └─ 15 ms wet/dry bypass crossfade
```

### Important architectural decisions

1. **Camera/ML does not run in each plugin instance.**
   - One `VisionEngine` process owns the webcam and MediaPipe.
   - All Gesture Rack instances receive the same state over local multicast.

2. **Audio thread never runs camera, Python, JSON generation, or ML inference.**
   - Vision packets are received on `VisionReceiver`'s JUCE thread.
   - Audio thread only reads the atomic `requestedBypass` state.

3. **Target plugin is hosted inside Gesture Rack.**
   - User loads Gesture Rack into the DAW.
   - Inside Gesture Rack, user chooses a third-party VST3 effect.
   - This is what makes future parameter control DAW-independent.

4. **Bypass is not a hard DSP disable.**
   - Hosted plugin continues processing.
   - Output crossfades between latency-matched dry signal and wet child output.
   - This avoids clicks and preserves reverb/delay tails.

---

## 3. Current Source Tree

```text
GestureRack/
├─ CMakeLists.txt
├─ README.md
├─ HANDOFF.md
├─ BuildWindows.bat
├─ BuildWindows-Clean.bat
├─ build_windows.ps1
├─ Source/
│  ├─ GestureTypes.h
│  ├─ VisionReceiver.h
│  ├─ VisionReceiver.cpp
│  ├─ GestureBypassWrapper.h
│  ├─ GestureBypassWrapper.cpp
│  ├─ PluginProcessor.h
│  ├─ PluginProcessor.cpp
│  ├─ PluginEditor.h
│  └─ PluginEditor.cpp
└─ VisionEngine/
   ├─ vision_engine.py
   └─ requirements.txt
```

---

## 4. Current Gesture / Vision Implementation

### MediaPipe

`VisionEngine/vision_engine.py`

Uses MediaPipe Tasks Gesture Recognizer in `LIVE_STREAM` mode.

Allowed gestures are restricted to:

```python
ALLOWED_GESTURES = {"Open_Palm", "Closed_Fist"}
```

Recognizer configuration:

- `num_hands = 1`
- hand detection confidence = `0.5`
- hand presence confidence = `0.5`
- tracking confidence = `0.5`
- canned gesture allowlist:
  - `Open_Palm`
  - `Closed_Fist`

### Stability filter

Defaults:

- minimum gesture confidence = `0.80`
- gesture must remain continuously valid for `120 ms`
- losing the hand or receiving an invalid gesture does **not** reset the stable state

This prevents rapid Active/Bypass chatter from frame-to-frame classification noise.

### Network protocol

Multicast:

```text
Address: 239.255.71.77
Port:    17777
TTL:     1
Protocol: 1
```

Packet shape:

```json
{
  "protocol": 1,
  "seq": 123,
  "timestamp_ms": 456789,
  "hand": true,
  "raw": "Open_Palm",
  "stable": "Open_Palm",
  "confidence": 0.97,
  "landmarks": [[0.5,0.5,0.0], ... 21 points]
}
```

`VisionReceiver` considers Vision Engine connected if a packet was received within the last `1500 ms`.

---

## 5. Gesture Mapping Currently Implemented

`PluginProcessor.cpp::timerCallback()` runs at 50 Hz.

Current mapping:

```text
stable Open_Palm
    → requestedBypass = false

stable Closed_Fist
    → requestedBypass = true

unknown / no hand / Vision disconnected
    → no state change
```

Each Gesture Rack instance has its own `GESTURE ON/OFF` toggle.

This is important because multiple Gesture Rack instances can exist in one DAW project while sharing one global camera stream.

---

## 6. Hosted Plugin System

### Supported child type right now

- VST3 audio **effects** only
- instruments are explicitly rejected
- current bus handling expects mono or stereo input/output with matching main layout

`loadVst3FromFile()`:

1. user chooses `.vst3`
2. prevents Gesture Rack from hosting itself
3. asks JUCE VST3 format to scan that file/bundle
4. uses `createPluginInstanceAsync()`
5. wraps resulting `AudioPluginInstance` inside `GestureBypassWrapper`
6. inserts wrapper into `AudioProcessorGraph`

### Hosted plugin editor

`OPEN PLUGIN` calls `createEditorIfNeeded()` and displays the child editor in a JUCE `DocumentWindow`.

### State persistence

Gesture Rack stores:

- `gestureEnabled`
- `requestedBypass`
- hosted `PluginDescription`
- child plugin's binary state encoded as Base64

On session restore it recreates the child asynchronously and restores its state.

---

## 7. Bypass / Audio Behavior

Implemented in `GestureBypassWrapper`.

### Signal flow

```text
Input
 ├─→ hosted child plugin ─────────────┐
 └─→ latency compensation DelayLine ─┤
                                     ↓
                               wet/dry crossfade
                                     ↓
                                  Output
```

### Current values

- bypass crossfade = `15 ms`
- maximum compensated latency = `524288 samples`
- dry path delay = child plugin's reported latency
- child stays running while audibly bypassed

Gesture Rack reports the child's latency back to the DAW with `setLatencySamples()`.

`processBlockBypassed()` also delays the host-level bypass path by the plugin's reported latency.

### Future caveat

Plugins that change latency dynamically need real-world validation. The current timer checks child latency and updates Gesture Rack's reported latency periodically.

---

## 8. Current UI

Default editor:

- initial size: `700 × 480`
- resizable
- min: `560 × 400`
- max: `1100 × 800`
- repaint timer: 60 Hz

Controls:

- `LOAD VST3`
- `OPEN PLUGIN`
- `GESTURE ON / OFF`

Visuals:

- 21 landmark hand skeleton
- hand connections
- animated palm glow/pulse
- green state = Active
- red state = Bypassed
- `VISION OFFLINE` if Vision Engine is not sending packets
- `SHOW YOUR HAND` when connected but no hand is visible
- target child plugin name
- current load error text

The UI currently renders landmarks only, not the actual camera image.

---

## 9. Windows Build Configuration

Current project is pinned to:

```text
JUCE 8.0.15
CMake >= 3.22
C++20
Visual Studio 2022
x64
```

Windows formats:

- VST3
- Standalone

macOS CMake configuration is prepared for:

- VST3
- AU
- Standalone

### CMake features

```text
JUCE_PLUGINHOST_VST3=1
JUCE_PLUGINHOST_AU=1 on macOS
JUCE_WEB_BROWSER=0
JUCE_USE_CURL=0
JUCE_VST3_CAN_REPLACE_VST2=0
```

### JUCE dependency strategy

Resolution order:

1. local `./JUCE`
2. `find_package(JUCE)`
3. auto-download JUCE 8.0.15 with CMake `FetchContent`

---

## 10. Build Problems Already Solved

### Problem A — CMake missing

User installed CMake.

### Problem B — Visual Studio installed but MSVC missing

User added Visual Studio workload:

```text
Desktop development with C++
```

### Problem C — JUCE 8 `XmlElement::createCopy()` compile error

Original broken code used:

```cpp
descXml->createCopy()
```

JUCE 8 has no such member.

Fixed to deep-copy construction:

```cpp
auto descClone = std::make_unique<juce::XmlElement> (*descXml);
```

This fix is already in current `PluginProcessor.cpp`.

### Problem D — Chinese Windows code page warning C4819

MSVC now compiles GestureRack target with:

```text
/utf-8
```

### Problem E — fatal error C1060: compiler heap space exhausted

This occurred while compiling large JUCE modules such as:

- `juce_dsp.cpp`
- `juce_audio_processors.cpp`

Current low-memory build solution:

```text
Visual Studio generator: VS 17 2022
Architecture:             x64
Toolset host:             host=x64
CMake parallel jobs:      1
MSVC:                     /MP1
LTO:                      disabled for bring-up build
```

Relevant configure command:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T host=x64 -DGESTURERACK_FETCH_JUCE=ON
```

Relevant build command:

```powershell
cmake --build build --config Release --target GestureRack_VST3 GestureRack_Standalone --parallel 1
```

**Result:** user reported the build finished successfully after this memory fix.

---

## 11. Current Windows Build Output

Expected source/build path from the latest package:

```text
C:\Users\DELL\Downloads\GestureRack-build-ready-memoryfix\GestureRack
```

Expected VST3 output:

```text
build\GestureRack_artefacts\Release\VST3\Gesture Rack.vst3
```

Expected Standalone output:

```text
build\GestureRack_artefacts\Release\Standalone\Gesture Rack.exe
```

Standard Windows VST3 install directory:

```text
C:\Program Files\Common Files\VST3\
```

Expected installed bundle:

```text
C:\Program Files\Common Files\VST3\Gesture Rack.vst3
```

A Windows VST3 bundle should contain approximately:

```text
Gesture Rack.vst3\
└─ Contents\
   └─ x86_64-win\
      └─ Gesture Rack.vst3
```

Do not accidentally create a doubled nesting such as:

```text
Gesture Rack.vst3\Gesture Rack.vst3\Contents\...
```

---


## 12. Vision Engine Runtime Setup — Not Yet Fully User-Tested

Python requirements are in:

```text
VisionEngine/requirements.txt
```

Typical Windows flow:

```powershell
cd VisionEngine
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python vision_engine.py --preview
```

First launch downloads MediaPipe's `gesture_recognizer.task` into:

```text
VisionEngine\models\gesture_recognizer.task
```

CLI options:

```text
--camera 0
--width 640
--height 480
--confidence 0.80
--hold-ms 120
--preview
--model <path>
```

Vision Engine's Python syntax had been checked previously, but the complete camera → MediaPipe → UDP → VST3 flow has **not yet been confirmed on the user's Windows machine**.

---

## 13. Known Limitations / Work Still Needed

These are intentional current limitations, not accidental missing code:

### Plugin hosting

- only one hosted child plugin
- child path is VST3 only
- effects only; instruments rejected
- mono/stereo main buses only
- no sidechain bus support yet
- no multi-output routing yet
- no external safe scanner process yet
- no plugin browser/database yet; current UI uses file chooser

### Gesture mapping

- only Open Palm / Closed Fist
- no parameter mapping UI yet
- no X/Y/Z control yet
- no pinch/open amount/rotation yet
- no left/right hand distinction yet
- only one hand tracked

### Vision process

- Python sidecar is not packaged as a background Windows executable/service yet
- plugin does not auto-launch Vision Engine yet
- no camera selector UI inside the plugin yet
- no automatic reconnect/config panel yet
- plugin UI draws landmarks, not camera frames

### Product/compatibility testing

Must eventually validate:

- FabFilter
- Valhalla
- Soundtoys
- Waves / shell-style products
- iLok/PACE plugins
- runtime latency changes
- offline rendering / bounce / freeze
- sample-rate changes
- multiple Gesture Rack instances
- hosted editor resizing
- OpenGL/Metal child UIs
- plugin crash isolation

---

## 14. Expansion Direction — Do Not Hard-Code Future Controls

Current public behavior is only bypass, but architecture should evolve around generic control signals.

Desired future conceptual layer:

```text
Gesture / Hand Input
        ↓
Control Signals
  ├─ gesture enum
  ├─ palm X
  ├─ palm Y
  ├─ palm depth
  ├─ pinch amount
  ├─ openness
  ├─ rotation
  └─ hand identity
        ↓
Mapping System
        ↓
Target
  ├─ rack bypass
  └─ child AudioProcessorParameter
```

Do **not** expand by adding chains of hard-coded `if (gesture == ...) parameter = ...` statements directly into `PluginProcessor`.

Instead, future parameter control should introduce a mapping abstraction that can target hosted `AudioProcessorParameter` IDs/indices and persist those mappings in project state.

---

## 15. Priority Order for Next Development Session

### P0 — Verify child hosting without gestures

Inside a DAW that can load the current Gesture Rack build:

1. load Gesture Rack
2. click `LOAD VST3`
3. load a simple known-good effect (FabFilter/Valhalla-class VST3)
4. confirm audio passes
5. confirm child UI opens
6. confirm project saves/reopens child plugin state
7. confirm host-level bypass and Gesture Rack internal bypass remain stable

### P1 — Verify Vision Engine

1. start `vision_engine.py --preview`
2. verify camera
3. verify Open Palm / Closed Fist stable labels
4. verify Gesture Rack shows Vision connected
5. verify 21-point skeleton

### P2 — Verify gesture-controlled audio bypass

1. Open Palm → Active
2. Closed Fist → Bypassed
3. hand disappears → hold state
4. quick noisy hand transitions do not chatter
5. no audible click on transition
6. delay/reverb tails remain continuous
7. verify dry-path latency alignment with a plugin that reports non-zero latency

### P3 — Harden hosted-plugin lifecycle

1. test project save/reload repeatedly
2. test sample-rate and block-size changes
3. test multiple Gesture Rack instances sharing one Vision Engine
4. test child editor open/close/resize behavior
5. test plugins with dynamic latency and larger reported latency

### P4 — Only after that, start generic parameter mapping

Do not jump directly into arbitrary parameter control before the basic hosted-plugin lifecycle and gesture bypass are reliable.

## 16. Current Package Lineage

Development packages created during the previous session:

```text
GestureRack.zip
GestureRack-build-ready.zip
GestureRack-build-ready-fixed.zip
GestureRack-build-ready-memoryfix.zip
```

The current baseline is:

```text
GestureRack-build-ready-memoryfix.zip
```

Important fixes included in that baseline:

- JUCE 8 `XmlElement` restore fix
- `/utf-8`
- x64-hosted MSVC
- serial low-memory build
- `/MP1`
- LTO disabled for bring-up

Use this baseline or a newer derivative, not the older zip files.

---

## 17. One-Sentence Continuation Prompt

Paste this into a new chat together with this handoff if needed:

> Continue Gesture Rack from HANDOFF.md using `GestureRack-build-ready-memoryfix.zip` as the current baseline. Preserve the existing architecture and continue validation from child-plugin hosting, Vision Engine, and gesture-controlled bypass before adding generic parameter mapping.
