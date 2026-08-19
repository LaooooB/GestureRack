from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def write(path, text):
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


# -----------------------------------------------------------------------------
# Face-only preview mosaic. Recognition keeps using the untouched MediaPipe frame.
# -----------------------------------------------------------------------------
face_mosaic = r'''from __future__ import annotations

from pathlib import Path

import cv2
import numpy as np


def _expanded_box(box: tuple[int, int, int, int], width: int, height: int,
                  margin_ratio: float) -> tuple[int, int, int, int]:
    x, y, w, h = [int(v) for v in box]
    margin_x = int(round(w * margin_ratio))
    margin_y = int(round(h * margin_ratio))
    x0 = max(0, x - margin_x)
    y0 = max(0, y - margin_y)
    x1 = min(width, x + w + margin_x)
    y1 = min(height, y + h + margin_y)
    return x0, y0, max(0, x1 - x0), max(0, y1 - y0)


def apply_face_mosaic(frame_rgb: np.ndarray,
                      boxes: list[tuple[int, int, int, int]],
                      block_size: int = 14,
                      margin_ratio: float = 0.18) -> np.ndarray:
    """Return a copy with pixelation applied only inside detected face regions."""
    if frame_rgb.ndim != 3 or frame_rgb.shape[2] < 3:
        return np.array(frame_rgb, copy=True)

    output = np.array(frame_rgb, copy=True)
    height, width = output.shape[:2]
    block_size = max(4, int(block_size))

    for box in boxes:
        x, y, w, h = _expanded_box(box, width, height, margin_ratio)
        if w < 2 or h < 2:
            continue
        roi = output[y:y + h, x:x + w]
        small_w = max(1, w // block_size)
        small_h = max(1, h // block_size)
        tiny = cv2.resize(roi, (small_w, small_h), interpolation=cv2.INTER_AREA)
        output[y:y + h, x:x + w] = cv2.resize(
            tiny, (w, h), interpolation=cv2.INTER_NEAREST)

    return output


class FaceMosaicProcessor:
    """Low-frequency face detector plus cheap per-frame ROI pixelation.

    Detection is deliberately decoupled from hand recognition. The processor only
    receives an already-recognised preview frame, and cached face boxes are reused
    between detector passes so privacy does not create a camera/gesture backlog.
    """

    def __init__(self, cascade_path: Path, detect_every_n: int = 4,
                 detection_scale: float = 0.5, block_size: int = 14,
                 margin_ratio: float = 0.18, detector=None):
        self.detect_every_n = max(1, int(detect_every_n))
        self.detection_scale = float(min(1.0, max(0.25, detection_scale)))
        self.block_size = max(4, int(block_size))
        self.margin_ratio = max(0.0, float(margin_ratio))
        self.frame_index = 0
        self.last_boxes: list[tuple[int, int, int, int]] = []
        self.detector = detector if detector is not None else cv2.CascadeClassifier(str(cascade_path))
        self.available = self.detector is not None and not self.detector.empty()

    def _detect(self, frame_rgb: np.ndarray) -> list[tuple[int, int, int, int]]:
        if not self.available:
            return []

        height, width = frame_rgb.shape[:2]
        scale = self.detection_scale if width >= 480 else 1.0
        if scale < 1.0:
            working = cv2.resize(frame_rgb, None, fx=scale, fy=scale,
                                 interpolation=cv2.INTER_AREA)
        else:
            working = frame_rgb

        gray = cv2.cvtColor(working, cv2.COLOR_RGB2GRAY)
        gray = cv2.equalizeHist(gray)
        minimum = max(24, int(round(min(working.shape[:2]) * 0.08)))
        detected = self.detector.detectMultiScale(
            gray,
            scaleFactor=1.12,
            minNeighbors=5,
            minSize=(minimum, minimum),
            flags=cv2.CASCADE_SCALE_IMAGE,
        )

        inverse = 1.0 / scale
        boxes: list[tuple[int, int, int, int]] = []
        for x, y, w, h in detected:
            boxes.append((
                int(round(x * inverse)), int(round(y * inverse)),
                int(round(w * inverse)), int(round(h * inverse)),
            ))
        return boxes

    def process_rgb(self, frame_rgb: np.ndarray) -> np.ndarray:
        if frame_rgb.ndim != 3 or frame_rgb.shape[2] < 3 or not self.available:
            return np.array(frame_rgb, copy=True)

        should_detect = self.frame_index % self.detect_every_n == 0
        self.frame_index += 1
        if should_detect:
            self.last_boxes = self._detect(frame_rgb)

        return apply_face_mosaic(
            frame_rgb,
            self.last_boxes,
            block_size=self.block_size,
            margin_ratio=self.margin_ratio,
        )
'''
write("VisionEngine/face_mosaic.py", face_mosaic)

face_test = r'''import unittest

import numpy as np

from face_mosaic import FaceMosaicProcessor, apply_face_mosaic


class _FakeCascade:
    def __init__(self, boxes):
        self.boxes = boxes
        self.calls = 0

    def empty(self):
        return False

    def detectMultiScale(self, *_args, **_kwargs):
        self.calls += 1
        return np.asarray(self.boxes, dtype=np.int32)


class FaceMosaicTests(unittest.TestCase):
    def test_mosaic_changes_only_face_roi(self):
        y, x = np.mgrid[0:72, 0:96]
        frame = np.stack(((x * 3) % 255, (y * 5) % 255, ((x + y) * 7) % 255), axis=-1).astype(np.uint8)
        output = apply_face_mosaic(frame, [(24, 18, 36, 30)], block_size=8, margin_ratio=0.0)

        outside = np.ones(frame.shape[:2], dtype=bool)
        outside[18:48, 24:60] = False
        self.assertTrue(np.array_equal(output[outside], frame[outside]))
        self.assertFalse(np.array_equal(output[18:48, 24:60], frame[18:48, 24:60]))
        self.assertTrue(np.array_equal(frame[0, 0], np.array([0, 0, 0], dtype=np.uint8)))

    def test_detector_is_low_frequency_and_reuses_boxes(self):
        detector = _FakeCascade([(10, 8, 28, 28)])
        processor = FaceMosaicProcessor(
            cascade_path=None,
            detect_every_n=4,
            detection_scale=1.0,
            block_size=7,
            margin_ratio=0.0,
            detector=detector,
        )
        frame = np.arange(80 * 80 * 3, dtype=np.uint8).reshape(80, 80, 3)

        first = processor.process_rgb(frame)
        second = processor.process_rgb(frame)
        third = processor.process_rgb(frame)
        fourth = processor.process_rgb(frame)

        self.assertEqual(detector.calls, 1)
        self.assertTrue(np.array_equal(first, second))
        self.assertTrue(np.array_equal(second, third))
        self.assertTrue(np.array_equal(third, fourth))
        processor.process_rgb(frame)
        self.assertEqual(detector.calls, 2)


if __name__ == "__main__":
    unittest.main()
'''
write("VisionEngine/test_face_mosaic.py", face_test)

vision = read("VisionEngine/vision_engine.py")
vision = replace_once(
    vision,
    "from vision_profile import VisionProfileStore\n",
    "from vision_profile import VisionProfileStore\nfrom face_mosaic import FaceMosaicProcessor\n",
    "vision import")
vision = replace_once(
    vision,
    "def default_shadow_model_path() -> Path:\n    return _resource_dir() / \"models\" / \"right_gesture_landmark_v1.npz\"\n",
    "def default_shadow_model_path() -> Path:\n    return _resource_dir() / \"models\" / \"right_gesture_landmark_v1.npz\"\n\n\ndef default_face_cascade_path() -> Path:\n    return _resource_dir() / \"models\" / \"haarcascade_frontalface_default.xml\"\n",
    "face cascade path")
vision = replace_once(
    vision,
    "        self.lock = threading.Lock()\n        self.last_packet = None\n\n        self.shadow_model_path",
    "        self.lock = threading.Lock()\n        self.last_packet = None\n\n        # Privacy is a preview-only post process. MediaPipe always receives the raw frame.\n        self.preview_config_lock = threading.Lock()\n        self.face_mosaic_enabled = False\n        self.face_mosaic = FaceMosaicProcessor(default_face_cascade_path())\n        if not self.face_mosaic.available:\n            print(\"Face mosaic detector unavailable; preview privacy will remain off.\")\n\n        self.shadow_model_path",
    "vision mosaic state")
vision = replace_once(
    vision,
    "            rgb = rgb[:, :, :3]\n            if not rgb.flags.c_contiguous:\n                rgb = np.ascontiguousarray(rgb)\n",
    "            rgb = rgb[:, :, :3]\n            with self.preview_config_lock:\n                mosaic_enabled = self.face_mosaic_enabled\n            if mosaic_enabled and self.face_mosaic.available:\n                rgb = self.face_mosaic.process_rgb(rgb)\n            if not rgb.flags.c_contiguous:\n                rgb = np.ascontiguousarray(rgb)\n",
    "preview mosaic")
vision = replace_once(
    vision,
    "    def _handle_control_command(self, command: dict) -> None:\n        name = str(command.get(\"command\", \"\")).strip().lower()\n        now_ms = int(time.monotonic() * 1000)\n",
    "    def _handle_control_command(self, command: dict) -> None:\n        name = str(command.get(\"command\", \"\")).strip().lower()\n        if name == \"set_face_mosaic\":\n            with self.preview_config_lock:\n                self.face_mosaic_enabled = bool(command.get(\"value\", False))\n            return\n\n        now_ms = int(time.monotonic() * 1000)\n",
    "control command")
write("VisionEngine/vision_engine.py", vision)

spec = read("VisionEngine/GestureVisionEngine.spec")
spec = replace_once(
    spec,
    "from PyInstaller.utils.hooks import collect_all\n\ndatas = [('models/gesture_recognizer.task', 'models')]\n",
    "from pathlib import Path\n\nimport cv2\nfrom PyInstaller.utils.hooks import collect_all\n\ndatas = [\n    ('models/gesture_recognizer.task', 'models'),\n    (str(Path(cv2.data.haarcascades) / 'haarcascade_frontalface_default.xml'), 'models'),\n]\n",
    "pyinstaller cascade")
write("VisionEngine/GestureVisionEngine.spec", spec)

# -----------------------------------------------------------------------------
# JUCE -> VisionEngine control command and persisted desired state.
# -----------------------------------------------------------------------------
header = read("Source/VisionReceiver.h")
header = replace_once(
    header,
    "    bool toggleSwapHandedness();\n",
    "    bool toggleSwapHandedness();\n    bool setFaceMosaicEnabled (bool enabled);\n",
    "vision receiver header")
write("Source/VisionReceiver.h", header)

receiver = read("Source/VisionReceiver.cpp")
receiver = replace_once(
    receiver,
    "bool VisionReceiver::toggleSwapHandedness()\n{\n    return sendControlCommand (makeCommand (\"toggle_swap_handedness\"));\n}\n\nvoid VisionReceiver::run()",
    "bool VisionReceiver::toggleSwapHandedness()\n{\n    return sendControlCommand (makeCommand (\"toggle_swap_handedness\"));\n}\n\nbool VisionReceiver::setFaceMosaicEnabled (bool enabled)\n{\n    auto command = makeCommand (\"set_face_mosaic\");\n    if (auto* object = command.getDynamicObject())\n        object->setProperty (\"value\", enabled);\n    return sendControlCommand (command);\n}\n\nvoid VisionReceiver::run()",
    "vision receiver command")
write("Source/VisionReceiver.cpp", receiver)

processor_h = read("Source/PluginProcessor.h")
processor_h = replace_once(
    processor_h,
    "    bool toggleSwapHandedness() { return vision.toggleSwapHandedness(); }\n\n    bool isGestureEnabled()",
    "    bool toggleSwapHandedness() { return vision.toggleSwapHandedness(); }\n    bool isFaceMosaicEnabled() const noexcept { return faceMosaicEnabled.load (std::memory_order_relaxed); }\n    void setFaceMosaicEnabled (bool enabled) noexcept;\n\n    bool isGestureEnabled()",
    "processor public mosaic")
processor_h = replace_once(
    processor_h,
    "    std::atomic<bool> gestureEnabled { true };\n    std::atomic<int> selectedSlot { 0 };",
    "    std::atomic<bool> gestureEnabled { true };\n    std::atomic<bool> faceMosaicEnabled { false };\n    bool lastVisionConnectedForCommands = false;\n    std::atomic<int> selectedSlot { 0 };",
    "processor mosaic state")
write("Source/PluginProcessor.h", processor_h)

processor = read("Source/PluginProcessor.cpp")
processor = replace_once(processor, "constexpr int currentStateVersion = 8;", "constexpr int currentStateVersion = 9;", "state version")
processor = replace_once(
    processor,
    "    const auto snapshot = vision.getDualHandSnapshot();\n    const auto connected = vision.isConnected();\n",
    "    const auto snapshot = vision.getDualHandSnapshot();\n    const auto connected = vision.isConnected();\n    if (connected && ! lastVisionConnectedForCommands)\n        vision.setFaceMosaicEnabled (faceMosaicEnabled.load (std::memory_order_relaxed));\n    lastVisionConnectedForCommands = connected;\n",
    "vision reconnect sync")
processor = replace_once(
    processor,
    "void GestureRackAudioProcessor::setGestureEnabled (bool enabled) noexcept\n{",
    "void GestureRackAudioProcessor::setFaceMosaicEnabled (bool enabled) noexcept\n{\n    faceMosaicEnabled.store (enabled, std::memory_order_relaxed);\n    vision.setFaceMosaicEnabled (enabled);\n}\n\nvoid GestureRackAudioProcessor::setGestureEnabled (bool enabled) noexcept\n{",
    "mosaic setter")
processor = replace_once(
    processor,
    "    root.setAttribute (\n        \"gestureEnabled\",\n        gestureEnabled.load (\n            std::memory_order_relaxed));\n\n    root.setAttribute (\n        \"selectedSlot\",",
    "    root.setAttribute (\n        \"gestureEnabled\",\n        gestureEnabled.load (\n            std::memory_order_relaxed));\n\n    root.setAttribute (\n        \"faceMosaicEnabled\",\n        faceMosaicEnabled.load (\n            std::memory_order_relaxed));\n\n    root.setAttribute (\n        \"selectedSlot\",",
    "state save mosaic")
processor = replace_once(
    processor,
    "            gestureEnabled.store (\n                stateCopy->getBoolAttribute (\n                    \"gestureEnabled\",\n                    true),\n                std::memory_order_relaxed);\n\n            const auto version =",
    "            gestureEnabled.store (\n                stateCopy->getBoolAttribute (\n                    \"gestureEnabled\",\n                    true),\n                std::memory_order_relaxed);\n            setFaceMosaicEnabled (\n                stateCopy->getBoolAttribute (\"faceMosaicEnabled\", false));\n\n            const auto version =",
    "state restore mosaic")
write("Source/PluginProcessor.cpp", processor)

# -----------------------------------------------------------------------------
# UI: Face Mosaic switch in CAMERA header + visible user-only PRESET menu.
# -----------------------------------------------------------------------------
editor_h = read("Source/PluginEditor.h")
editor_h = replace_once(
    editor_h,
    "    void showMainMenu();\n    void showPluginMoreMenu();",
    "    void showMainMenu();\n    void showPresetMenu();\n    void saveUserPreset();\n    void loadUserPreset();\n    void showPluginMoreMenu();",
    "editor preset methods")
editor_h = replace_once(
    editor_h,
    "    gr::ui::AnimatedTextButton swapHandsButton { \"SWAP L/R\" };\n\n    gr::ui::IconButton settingsButton",
    "    gr::ui::AnimatedTextButton swapHandsButton { \"SWAP L/R\" };\n    gr::ui::AnimatedTextButton faceMosaicButton { \"FACE MOSAIC\" };\n    gr::ui::AnimatedTextButton presetButton { \"PRESET\" };\n\n    gr::ui::IconButton settingsButton",
    "editor buttons")
editor_h = replace_once(
    editor_h,
    "    std::unique_ptr<PluginBrowserComponent> pluginBrowser;\n",
    "    std::unique_ptr<PluginBrowserComponent> pluginBrowser;\n    std::unique_ptr<juce::FileChooser> presetFileChooser;\n",
    "preset chooser")
write("Source/PluginEditor.h", editor_h)

editor = read("Source/PluginEditor.cpp")
editor = replace_once(
    editor,
    "    addAndMakeVisible (pluginMoreButton);\n    addAndMakeVisible (settingsButton);\n    addAndMakeVisible (menuButton);",
    "    addAndMakeVisible (pluginMoreButton);\n    addAndMakeVisible (faceMosaicButton);\n    addAndMakeVisible (presetButton);\n    addAndMakeVisible (settingsButton);\n    addAndMakeVisible (menuButton);",
    "editor add buttons")
editor = replace_once(
    editor,
    "    menuButton.onClick =\n        [this]\n        {\n            showMainMenu();\n        };\n\n    settingsButton.setAccentWhenOn (false);",
    "    menuButton.onClick =\n        [this]\n        {\n            showMainMenu();\n        };\n\n    presetButton.onClick =\n        [this]\n        {\n            showPresetMenu();\n        };\n\n    faceMosaicButton.setClickingTogglesState (true);\n    faceMosaicButton.setToggleState (processor.isFaceMosaicEnabled(), juce::dontSendNotification);\n    faceMosaicButton.onClick =\n        [this]\n        {\n            processor.setFaceMosaicEnabled (faceMosaicButton.getToggleState());\n        };\n\n    settingsButton.setAccentWhenOn (false);",
    "editor button handlers")
editor = replace_once(
    editor,
    "    const auto vision = processor.getDualHandVisionSnapshot();\n",
    "    const auto vision = processor.getDualHandVisionSnapshot();\n    faceMosaicButton.setToggleState (processor.isFaceMosaicEnabled(), juce::dontSendNotification);\n",
    "timer mosaic sync")
editor = replace_once(
    editor,
    "    topRight.removeFromRight (settingsButton.getWidth() + menuButton.getWidth() + 16);",
    "    topRight.removeFromRight (presetButton.getWidth() + settingsButton.getWidth() + menuButton.getWidth() + 24);",
    "top paint reservation")
editor = replace_once(
    editor,
    "    auto cameraContent = cameraPanelBounds.reduced (scaledMetric (uiScale, 14));\n    cameraContent.removeFromTop (scaledMetric (uiScale, 32));",
    "    auto cameraContent = cameraPanelBounds.reduced (scaledMetric (uiScale, 14));\n    auto cameraHeaderControls = cameraContent.removeFromTop (scaledMetric (uiScale, 28));\n    faceMosaicButton.setBounds (cameraHeaderControls.removeFromRight (scaledMetric (uiScale, 104)));\n    cameraContent.removeFromTop (scaledMetric (uiScale, 4));",
    "camera mosaic layout")
editor = replace_once(
    editor,
    "    settingsButton.setBounds (topRight.removeFromRight (scaledMetric (uiScale, 34)));\n\n    if (pluginBrowser != nullptr)",
    "    settingsButton.setBounds (topRight.removeFromRight (scaledMetric (uiScale, 34)));\n    topRight.removeFromRight (scaledMetric (uiScale, 8));\n    presetButton.setBounds (topRight.removeFromRight (scaledMetric (uiScale, 74)));\n\n    if (pluginBrowser != nullptr)",
    "preset top layout")

preset_impl = r'''
void GestureRackAudioProcessorEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&ui::themeLookAndFeel());
    menu.addItem (1, "SAVE USER PRESET...");
    menu.addItem (2, "LOAD USER PRESET...");

    juce::Component::SafePointer<GestureRackAudioProcessorEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton).withStandardItemHeight (27),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result == 0) return;
                            if (result == 1) safe->saveUserPreset();
                            else if (result == 2) safe->loadUserPreset();
                        });
}

void GestureRackAudioProcessorEditor::saveUserPreset()
{
    auto directory = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                         .getChildFile ("Gesture Rack")
                         .getChildFile ("Presets");
    directory.createDirectory();

    presetFileChooser = std::make_unique<juce::FileChooser> (
        "Save Gesture Rack user preset",
        directory.getChildFile ("Gesture Rack.grpreset"),
        "*.grpreset",
        true);

    juce::Component::SafePointer<GestureRackAudioProcessorEditor> safe (this);
    presetFileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            auto file = chooser.getResult();
            if (file == juce::File()) return;
            if (! file.hasFileExtension (".grpreset"))
                file = file.withFileExtension (".grpreset");

            juce::MemoryBlock state;
            safe->processor.getStateInformation (state);
            if (state.getSize() == 0 || ! file.replaceWithData (state.getData(), state.getSize()))
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "Preset Save Failed",
                    "Could not write the Gesture Rack user preset.");
            }
        });
}

void GestureRackAudioProcessorEditor::loadUserPreset()
{
    auto directory = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                         .getChildFile ("Gesture Rack")
                         .getChildFile ("Presets");
    directory.createDirectory();

    presetFileChooser = std::make_unique<juce::FileChooser> (
        "Load Gesture Rack user preset", directory, "*.grpreset", true);

    juce::Component::SafePointer<GestureRackAudioProcessorEditor> safe (this);
    presetFileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            const auto file = chooser.getResult();
            if (! file.existsAsFile()) return;

            juce::MemoryBlock state;
            if (! file.loadFileAsData (state) || state.getSize() == 0)
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "Preset Load Failed",
                    "Could not read the Gesture Rack user preset.");
                return;
            }

            safe->processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
            safe->displayedChildIdentity = 0;
            safe->displayedSlot = -1;
            safe->repaint();
        });
}

'''
marker = "void GestureRackAudioProcessorEditor::showPluginMoreMenu()\n"
if marker not in editor:
    raise RuntimeError("preset implementation insertion point missing")
editor = editor.replace(marker, preset_impl + marker, 1)
editor = editor.replace(
    '"Gesture Rack\\\nGesture-controlled VST3 effect rack."',
    '"Gesture Rack by Lao_B\\\nGesture-controlled VST3 effect rack."',
    1)
write("Source/PluginEditor.cpp", editor)

# -----------------------------------------------------------------------------
# Manufacturer/signature display. Keep manufacturer/plugin codes unchanged so DAWs
# continue to identify this as the same plug-in class.
# -----------------------------------------------------------------------------
cmake = read("CMakeLists.txt")
cmake = replace_once(cmake, '    COMPANY_NAME "Gesture Rack"', '    COMPANY_NAME "Lao_B"', "company name")
write("CMakeLists.txt", cmake)

# Ensure the official Windows production build exercises the new vision module.
workflow = read(".github/workflows/windows-vst3-build.yml")
workflow = replace_once(
    workflow,
    "test_slot_selector.py test_right_gesture_classifier.py",
    "test_face_mosaic.py test_slot_selector.py test_right_gesture_classifier.py",
    "official vision tests")
write(".github/workflows/windows-vst3-build.yml", workflow)

# Basic architecture invariants for the patch runner itself.
checks = {
    "VisionEngine/vision_engine.py": ["set_face_mosaic", "FaceMosaicProcessor", "mosaic_enabled"],
    "Source/PluginEditor.cpp": ["FACE MOSAIC", "SAVE USER PRESET", "LOAD USER PRESET"],
    "Source/PluginProcessor.cpp": ["faceMosaicEnabled", "currentStateVersion = 9"],
    "CMakeLists.txt": ['COMPANY_NAME "Lao_B"'],
}
for path, needles in checks.items():
    text = read(path)
    for needle in needles:
        if needle not in text:
            raise RuntimeError(f"{path}: missing invariant {needle!r}")

print("Face mosaic, user presets, and Lao_B signature patch applied.")
