# Gesture Rack

A JUCE audio effect that hosts a child VST3 and controls its audible bypass state from webcam hand gestures.

> **不想编译？直接用预编译版（Windows，下载即用，无需装 Python）：**
> 到 [Releases](../../releases) 下载 `GestureRack-v0.1.0-Windows.zip`，解压后按包内 `使用说明-快速开始.txt` 三步即可使用（装 VST3 → 双击 `GestureVisionEngine.exe` → DAW 加载）。`GestureVisionEngine.exe` 已内置 Python 运行时、MediaPipe、OpenCV 和手势模型，无需另装环境。

Current gesture mapping:

- **Open_Palm** -> child effect ACTIVE
- **Closed_Fist** -> child effect BYPASSED
- no hand / unknown gesture -> hold the previous stable state

The visualizer renders MediaPipe's 21 hand landmarks inside the plugin UI. The camera and ML model do **not** run on the audio thread or once per plugin instance. One `VisionEngine` process broadcasts the same normalized hand state to all loaded Gesture Rack instances over local multicast.

## Architecture

```text
Webcam
  -> VisionEngine (Python + MediaPipe, one process)
      -> Open_Palm / Closed_Fist + 21 landmarks
      -> local multicast UDP 239.255.71.77:17777
          -> Gesture Rack VST3/AU instance(s)
              -> 120 ms stable gesture state
              -> 15 ms audio crossfade
              -> hosted VST3 effect
```

The hosted plugin remains processing while audibly bypassed. This is intentional: it preserves reverb/delay tails and avoids reopening/resetting DSP every time the user closes a fist. CPU therefore does not drop to zero while bypassed.

## Vision Engine

Python 3.9+ is supported by the current MediaPipe Tasks desktop setup.

```bash
cd VisionEngine
python -m venv .venv
```

Windows:

```powershell
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python vision_engine.py --preview
```

macOS/Linux:

```bash
source .venv/bin/activate
python -m pip install -r requirements.txt
python vision_engine.py --preview
```

On first launch the script downloads Google's official `gesture_recognizer.task` model into `VisionEngine/models/`.

Once verified, omit `--preview` and let the Gesture Rack plugin be the visual display.

## Build Gesture Rack

Requirements:

- CMake 3.22+
- a current JUCE checkout/install
- Visual Studio on Windows, Xcode on macOS

Simplest source-tree setup:

```text
GestureRack/
  JUCE/
  Source/
  VisionEngine/
  CMakeLists.txt
```

Configure and build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Formats:

- Windows: VST3 + Standalone test host
- macOS: VST3 + AU + Standalone test host

The **outer Gesture Rack** is therefore loadable by common VST3 DAWs, plus Logic/GarageBand through AU on macOS. The hosted target in this code path is VST3.

## Use

1. Start `VisionEngine/vision_engine.py`.
2. Insert **Gesture Rack** in a DAW effect slot.
3. Click **LOAD VST3** and choose the effect to host.
4. Click **OPEN PLUGIN** when you need the hosted plugin's own GUI.
5. Open your palm: ACTIVE.
6. Close your fist: BYPASSED.

The **GESTURE ON/OFF** button lets one Rack ignore the global camera state. This matters if the session contains several Gesture Rack instances.

## Important implementation choices

- MediaPipe only allows `Open_Palm` and `Closed_Fist` through the built-in canned classifier.
- Confidence threshold defaults to 0.80.
- A gesture must remain valid for 120 ms before becoming stable.
- Losing the hand does not change the last stable state.
- Bypass uses a 15 ms wet/dry crossfade rather than abruptly dropping the child processor.
- The dry path is delayed by the child plugin's reported latency before crossfading.
- Gesture Rack reports the hosted plugin latency back to the DAW.
- Project state stores the hosted plugin description and its state blob, then recreates it asynchronously when the DAW session is restored.

## Known product-level constraints

This source deliberately does **not** pretend every third-party plugin is perfectly nestable. Before shipping commercially, test at least:

- Waves/Shell-style plugins
- iLok/PACE plugins
- plugins with multiple output buses
- sidechain effects
- plugins that change latency at runtime
- resizable/OpenGL/Metal hosted editors
- offline bounce and freeze
- sample-rate changes
- duplicate Gesture Rack instances

Full plugin scanning should also be moved into an external scanner process before commercial release, because a broken third-party plugin can crash the process that scans it. The current code loads one user-selected VST3 bundle rather than scanning an entire machine.

## Windows one-click build

For the packaged project on Windows, the root directory now contains:

- `BuildWindows.bat` — normal Release build
- `BuildWindows-Clean.bat` — deletes the old build tree, then rebuilds
- `build_windows.ps1` — the script used by the batch files

Requirements are CMake 3.22+ and Visual Studio with the **Desktop development with C++** workload. If no `JUCE/` directory and no installed JUCE CMake package are found, CMake automatically downloads the pinned JUCE 8.0.15 source archive during configuration.

Double-click `BuildWindows.bat`, or run:

```powershell
cd C:\path\to\GestureRack
.\build_windows.ps1
```

The expected VST3 result is:

```text
build\GestureRack_artefacts\Release\VST3\Gesture Rack.vst3
```

The build deliberately does not auto-copy into `C:\Program Files\Common Files\VST3`, because that directory may require elevation. Test the bundle from the build output first; copy/install it only after the build is confirmed working.


## Windows low-memory build

The Windows build scripts force the x64-hosted MSVC toolchain and serialize compilation (`--parallel 1`, `/MP1`). LTO is disabled during bring-up to reduce peak memory usage. Run `BuildWindows-Clean.bat` after switching to this package so CMake can regenerate the build tree with `host=x64`.
