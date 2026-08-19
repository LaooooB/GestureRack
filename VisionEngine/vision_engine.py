from __future__ import annotations

import argparse
import json
import socket
import sys
import threading
import time
import urllib.request
import uuid
from pathlib import Path

import cv2
import mediapipe as mp
import numpy as np


def _resource_dir() -> Path:
    """Return the directory that ships bundled resources.

    When frozen with PyInstaller (``--onefile``) this is the temporary
    extraction directory (``sys._MEIPASS``); otherwise it is the directory
    containing this source file. Bundled assets such as the gesture model
    live here.
    """
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        return Path(meipass)
    return Path(__file__).resolve().parent


def default_model_path() -> Path:
    return _resource_dir() / "models" / "gesture_recognizer.task"


def default_shadow_model_path() -> Path:
    return _resource_dir() / "models" / "right_gesture_landmark_v1.npz"


def default_face_cascade_path() -> Path:
    return _resource_dir() / "models" / "haarcascade_frontalface_default.xml"


from continuous_motion import HeightMotionFilter
from gesture_stabilizer import GestureStabilizer
from hand_role_calibration import RightHandCalibration
from hand_role_resolver import DetectedHand, HandRoleResolver
from right_gesture_classifier import RIGHT_GESTURES, classify_right_gesture
from shadow_evaluator import ShadowGestureEvaluator
from shared_frame import SHM_NAME, SharedFrameWriter
from slot_selector import SlotStabilizer, classify_slot_1_to_5
from tiny_landmark_classifier import TinyLandmarkClassifier
from vision_profile import VisionProfileStore
from face_mosaic import FaceMosaicProcessor

MODEL_URL = (
    "https://storage.googleapis.com/mediapipe-models/gesture_recognizer/"
    "gesture_recognizer/float16/1/gesture_recognizer.task"
)
MULTICAST_ADDRESS = "239.255.71.77"
MULTICAST_PORT = 17777
CONTROL_ADDRESS = "127.0.0.1"
CONTROL_PORT = 17778
PROTOCOL_VERSION = 2
ALLOWED_RIGHT_GESTURES = RIGHT_GESTURES


def ensure_model(model_path: Path) -> None:
    if model_path.exists():
        return
    model_path.parent.mkdir(parents=True, exist_ok=True)
    print(f"Downloading MediaPipe gesture model to: {model_path}")
    urllib.request.urlretrieve(MODEL_URL, model_path)


def palm_metrics(landmarks: list[list[float]]) -> tuple[float, float, float, float]:
    if len(landmarks) < 18:
        return 0.5, 0.5, 0.0, 0.5

    indices = (0, 5, 9, 13, 17)
    palm_x = sum(landmarks[i][0] for i in indices) / len(indices)
    palm_y = sum(landmarks[i][1] for i in indices) / len(indices)
    palm_z = sum(landmarks[i][2] for i in indices) / len(indices)

    top_y = 0.15
    bottom_y = 0.85
    normalized = (bottom_y - palm_y) / (bottom_y - top_y)
    height = max(0.0, min(1.0, normalized))
    return float(palm_x), float(palm_y), float(palm_z), float(height)


def empty_left_packet(stable_slot: int = 0) -> dict:
    return {
        "present": False,
        "handedness_confidence": 0.0,
        "raw_slot": 0,
        "stable_slot": stable_slot,
        "confidence": 0.0,
        "landmarks": [],
    }


def empty_right_packet(stable_gesture: str = "None") -> dict:
    return {
        "present": False,
        "handedness_confidence": 0.0,
        "canned_gesture": "None",
        "canned_confidence": 0.0,
        "raw_gesture": "None",
        "stable_gesture": stable_gesture,
        "confidence": 0.0,
        "palm_x": 0.5,
        "palm_y": 0.5,
        "palm_z": 0.0,
        "height_raw": 0.5,
        "height": 0.5,
        "landmarks": [],
        "world_landmarks": [],
        "shadow": {
            "available": False,
            "gesture": "None",
            "confidence": 0.0,
            "margin": 0.0,
            "inference_ms": 0.0,
            "agrees": False,
        },
    }


class GestureVisionEngine:
    def __init__(self, model_path: Path, camera_index: int, width: int, height: int,
                 confidence: float, hold_ms: int, release_ms: int, preview: bool,
                 swap_handedness: bool | None, slot_confidence: float,
                 slot_hold_ms: int, backend: str = "auto", profile_path: Path | None = None,
                 calibrate_right: bool = False, shadow_model_path: Path | None = None):
        self.model_path = model_path
        self.camera_index = camera_index
        self.width = width
        self.height = height
        self.preview = preview
        self.preferred_backend = backend
        self.swap_handedness_override = swap_handedness
        self.calibrate_right_on_start = calibrate_right
        self.profile_store = VisionProfileStore(profile_path)
        self.profile_backend = "UNKNOWN"
        self.role_config_source = "DEFAULT"
        self.role_lock = threading.Lock()
        self.hand_calibration = RightHandCalibration()
        self._pre_calibration_role_source = "DEFAULT"
        self._pre_calibration_swap = bool(swap_handedness)

        self.right_stabilizer = GestureStabilizer(
            allowed_gestures=set(ALLOWED_RIGHT_GESTURES),
            hold_ms=hold_ms,
            release_ms=release_ms,
            min_confidence=confidence,
        )
        self.left_stabilizer = SlotStabilizer(hold_ms=slot_hold_ms, min_confidence=slot_confidence)
        self.role_resolver = HandRoleResolver(swap_handedness=bool(swap_handedness))
        self.height_motion_filter = HeightMotionFilter()
        self.sequence = 0
        self.session_id = uuid.uuid4().hex[:12]
        self.lock = threading.Lock()
        self.last_packet = None

        # Privacy is a preview-only post process. MediaPipe always receives the raw frame.
        self.preview_config_lock = threading.Lock()
        self.face_mosaic_enabled = False
        self.face_mosaic = FaceMosaicProcessor(default_face_cascade_path())
        if not self.face_mosaic.available:
            print("Face mosaic detector unavailable; preview privacy will remain off.")

        self.shadow_model_path = Path(shadow_model_path) if shadow_model_path is not None else None
        shadow_model = (
            TinyLandmarkClassifier.load_optional(self.shadow_model_path)
            if self.shadow_model_path is not None else None
        )
        self.shadow_evaluator = ShadowGestureEvaluator(shadow_model)

        self.telemetry_lock = threading.Lock()
        self._pending: dict[int, tuple[int, int]] = {}
        self.capture_fps = 0.0
        self.vision_fps = 0.0
        self.capture_window_started = time.monotonic()
        self.capture_window_frames = 0
        self.vision_window_started = time.monotonic()
        self.vision_window_results = 0
        self.camera_backend = "UNKNOWN"
        self.camera_width = width
        self.camera_height = height
        self.camera_fps = 0.0
        self.camera_fourcc = ""

        self.frame_writer: SharedFrameWriter | None = None
        try:
            self.frame_writer = SharedFrameWriter()
        except (OSError, FileExistsError) as exc:
            print(f"Shared camera frame transport unavailable: {exc}")

        self._idle = threading.Event()
        self._idle.set()

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
        self.control_socket: socket.socket | None = None
        self.stop_control = threading.Event()

        BaseOptions = mp.tasks.BaseOptions
        GestureRecognizer = mp.tasks.vision.GestureRecognizer
        GestureRecognizerOptions = mp.tasks.vision.GestureRecognizerOptions
        RunningMode = mp.tasks.vision.RunningMode
        ClassifierOptions = mp.tasks.components.processors.ClassifierOptions

        import dataclasses as _dataclasses
        _option_fields = {
            f.name for f in _dataclasses.fields(GestureRecognizerOptions)
        } if _dataclasses.is_dataclass(GestureRecognizerOptions) else set()
        _canned_field_name = (
            "canned_gestures_classifier_options"
            if "canned_gestures_classifier_options" in _option_fields
            else "canned_gesture_classifier_options"
        )

        options_kwargs = dict(
            base_options=BaseOptions(model_asset_path=str(model_path.resolve())),
            running_mode=RunningMode.LIVE_STREAM,
            num_hands=2,
            min_hand_detection_confidence=0.5,
            min_hand_presence_confidence=0.5,
            min_tracking_confidence=0.5,
            result_callback=self._on_result,
        )
        options_kwargs[_canned_field_name] = ClassifierOptions(
            score_threshold=0.0,
            category_allowlist=[
                "Open_Palm",
                "Closed_Fist",
                "Victory",
                "Thumb_Up",
                "Thumb_Down",
                "Pointing_Up",
            ],
        )

        options = GestureRecognizerOptions(**options_kwargs)
        self.recognizer = GestureRecognizer.create_from_options(options)

    @staticmethod
    def _extract_hands(result) -> list[DetectedHand]:
        hands: list[DetectedHand] = []
        count = min(2, len(result.hand_landmarks or []))

        for index in range(count):
            landmarks = [
                [float(lm.x), float(lm.y), float(lm.z)]
                for lm in result.hand_landmarks[index]
            ]
            world_landmarks: list[list[float]] = []
            if (getattr(result, "hand_world_landmarks", None)
                    and index < len(result.hand_world_landmarks)
                    and result.hand_world_landmarks[index]):
                world_landmarks = [
                    [float(lm.x), float(lm.y), float(lm.z)]
                    for lm in result.hand_world_landmarks[index]
                ]

            raw_gesture = "None"
            gesture_confidence = 0.0
            if result.gestures and index < len(result.gestures) and result.gestures[index]:
                top = result.gestures[index][0]
                raw_gesture = top.category_name or "None"
                gesture_confidence = float(top.score)

            handedness_label = "Unknown"
            handedness_confidence = 0.0
            if result.handedness and index < len(result.handedness) and result.handedness[index]:
                top_hand = result.handedness[index][0]
                handedness_label = top_hand.category_name or "Unknown"
                handedness_confidence = float(top_hand.score)

            hands.append(DetectedHand(
                landmarks=landmarks,
                raw_gesture=raw_gesture,
                gesture_confidence=gesture_confidence,
                handedness_label=handedness_label,
                handedness_confidence=handedness_confidence,
                world_landmarks=world_landmarks,
            ))

        return hands

    def _publish_callback_frame(self, output_image, timestamp_ms: int) -> bool:
        """Publish the exact image MediaPipe used for this callback.

        Landmarks in the same callback are normalized against this image, so the
        VST3 can draw them directly over the frame without temporal or geometric
        drift. This is intentionally not the newest capture-thread frame.
        """
        if self.frame_writer is None or output_image is None:
            return False
        try:
            rgb = np.asarray(output_image.numpy_view())
            if rgb.ndim != 3 or rgb.shape[2] < 3:
                return False
            rgb = rgb[:, :, :3]
            with self.preview_config_lock:
                mosaic_enabled = self.face_mosaic_enabled
            if mosaic_enabled and self.face_mosaic.available:
                rgb = self.face_mosaic.process_rgb(rgb)
            if not rgb.flags.c_contiguous:
                rgb = np.ascontiguousarray(rgb)
            height, width = rgb.shape[:2]
            stride = int(rgb.strides[0])
            return self.frame_writer.publish_rgb(
                memoryview(rgb).cast("B"), width, height, stride, timestamp_ms)
        except (BufferError, ValueError, TypeError, OSError):
            return False

    def _reset_role_dependent_state_locked(self) -> None:
        self.role_resolver.reset()
        self.right_stabilizer.reset()
        self.height_motion_filter.reset()
        self.left_stabilizer.stable = 0
        self.left_stabilizer.candidate = 0
        self.left_stabilizer.candidate_since_ms = 0

    def _set_swap_handedness_locked(self, should_swap: bool, source: str,
                                    persist: bool) -> None:
        changed = self.role_resolver.swap_handedness != bool(should_swap)
        self.role_resolver.set_swap_handedness(bool(should_swap))
        self.role_config_source = str(source)
        if changed:
            self._reset_role_dependent_state_locked()
        if persist and self.profile_backend:
            try:
                self.profile_store.set_swap_handedness(
                    self.camera_index, self.profile_backend, bool(should_swap), source)
            except OSError as exc:
                print(f"Could not persist handedness profile: {exc}")

    def _begin_hand_calibration_locked(self, now_ms: int) -> None:
        if not self.hand_calibration.snapshot().active:
            self._pre_calibration_role_source = self.role_config_source
            self._pre_calibration_swap = bool(self.role_resolver.swap_handedness)
        self.hand_calibration.start(now_ms)
        self.role_config_source = "CALIBRATING"
        self._reset_role_dependent_state_locked()

    def _restore_pre_calibration_role_locked(self) -> None:
        self.role_resolver.set_swap_handedness(self._pre_calibration_swap)
        self.role_config_source = self._pre_calibration_role_source
        self._reset_role_dependent_state_locked()

    def _load_role_profile(self) -> None:
        with self.role_lock:
            if self.swap_handedness_override is not None:
                self._set_swap_handedness_locked(
                    bool(self.swap_handedness_override), "CLI OVERRIDE", persist=False)
                return

            stored = self.profile_store.get_swap_handedness(self.camera_index, self.profile_backend)
            if stored is not None:
                self._set_swap_handedness_locked(stored, "PROFILE", persist=False)
            else:
                self._set_swap_handedness_locked(False, "UNCALIBRATED", persist=False)

    def _role_config_packet_locked(self) -> dict:
        calibration = self.hand_calibration.snapshot()
        return {
            "swap_handedness": bool(self.role_resolver.swap_handedness),
            "source": self.role_config_source,
            "calibration_active": bool(calibration.active),
            "calibration_status": calibration.status,
            "calibration_samples": int(calibration.sample_count),
            "calibration_confidence": float(calibration.confidence),
        }

    def _handle_control_command(self, command: dict) -> None:
        name = str(command.get("command", "")).strip().lower()
        if name == "set_face_mosaic":
            with self.preview_config_lock:
                self.face_mosaic_enabled = bool(command.get("value", False))
            return

        now_ms = int(time.monotonic() * 1000)
        with self.role_lock:
            if name == "begin_hand_calibration":
                self._begin_hand_calibration_locked(now_ms)
            elif name == "cancel_hand_calibration":
                was_active = self.hand_calibration.snapshot().active
                self.hand_calibration.cancel()
                if was_active:
                    self._restore_pre_calibration_role_locked()
            elif name == "set_swap_handedness":
                self.hand_calibration.cancel()
                self._set_swap_handedness_locked(
                    bool(command.get("value", False)), "MANUAL", persist=True)
            elif name == "toggle_swap_handedness":
                self.hand_calibration.cancel()
                self._set_swap_handedness_locked(
                    not self.role_resolver.swap_handedness, "MANUAL", persist=True)

    def _control_loop(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.control_socket = sock
        try:
            sock.bind((CONTROL_ADDRESS, CONTROL_PORT))
            sock.settimeout(0.25)
            print(f"Vision control listening on {CONTROL_ADDRESS}:{CONTROL_PORT}")
            while not self.stop_control.is_set():
                try:
                    payload, _ = sock.recvfrom(4096)
                except socket.timeout:
                    continue
                except OSError:
                    break
                try:
                    command = json.loads(payload.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    continue
                if isinstance(command, dict):
                    self._handle_control_command(command)
        except OSError as exc:
            print(f"Vision control channel unavailable: {exc}")
        finally:
            try:
                sock.close()
            except OSError:
                pass
            self.control_socket = None

    def _on_result(self, result, output_image, timestamp_ms: int) -> None:
        self._idle.set()
        result_ms = int(time.monotonic() * 1000)
        frame_published = self._publish_callback_frame(output_image, timestamp_ms)
        with self.telemetry_lock:
            capture_ms, submit_ms = self._pending.pop(timestamp_ms, (timestamp_ms, timestamp_ms))
            self.vision_window_results += 1
            elapsed = time.monotonic() - self.vision_window_started
            if elapsed >= 1.0:
                self.vision_fps = self.vision_window_results / elapsed
                self.vision_window_results = 0
                self.vision_window_started = time.monotonic()
            telemetry = {
                "capture_fps": self.capture_fps,
                "vision_fps": self.vision_fps,
                "frame_age_at_submit_ms": max(0.0, float(submit_ms - capture_ms)),
                "inference_ms": max(0.0, float(result_ms - submit_ms)),
                "capture_to_result_ms": max(0.0, float(result_ms - capture_ms)),
                "camera_index": self.camera_index,
                "backend": self.camera_backend,
                "width": self.camera_width,
                "height": self.camera_height,
                "fps": self.camera_fps,
                "fourcc": self.camera_fourcc,
                "shared_frame": frame_published,
                "shared_frame_name": SHM_NAME,
            }

        detections = self._extract_hands(result)
        with self.role_lock:
            calibration_result = self.hand_calibration.observe(detections, timestamp_ms)
            if calibration_result is not None:
                if calibration_result.swap_handedness is not None:
                    self._set_swap_handedness_locked(
                        calibration_result.swap_handedness, "CALIBRATION", persist=True)
                    self._pre_calibration_role_source = "CALIBRATION"
                    self._pre_calibration_swap = bool(calibration_result.swap_handedness)
                else:
                    self._restore_pre_calibration_role_locked()
            left_hand, right_hand = self.role_resolver.resolve(detections, timestamp_ms)
            role_config = self._role_config_packet_locked()

        left_packet = empty_left_packet(self.left_stabilizer.stable)
        if left_hand is not None:
            classification = classify_slot_1_to_5(left_hand.landmarks)
            stable_slot = self.left_stabilizer.update(
                classification.slot, classification.confidence, timestamp_ms)
            left_packet = {
                "present": True,
                "handedness_confidence": left_hand.handedness_confidence,
                "raw_slot": classification.slot,
                "stable_slot": stable_slot,
                "confidence": classification.confidence,
                "landmarks": left_hand.landmarks,
            }
        else:
            self.left_stabilizer.update(0, 0.0, timestamp_ms)

        if right_hand is not None:
            classification = classify_right_gesture(
                right_hand.landmarks,
                right_hand.raw_gesture,
                right_hand.gesture_confidence,
            )
            shadow = self.shadow_evaluator.evaluate(
                right_hand.landmarks,
                classification.gesture,
                right_hand.world_landmarks,
            )
            stable = self.right_stabilizer.update(
                classification.gesture, classification.confidence, timestamp_ms)
            palm_x, palm_y, palm_z, raw_height = palm_metrics(right_hand.landmarks)
            filtered_height = self.height_motion_filter.update(raw_height, timestamp_ms)
            right_packet = {
                "present": True,
                "handedness_confidence": right_hand.handedness_confidence,
                "canned_gesture": right_hand.raw_gesture,
                "canned_confidence": right_hand.gesture_confidence,
                "raw_gesture": classification.gesture,
                "stable_gesture": stable,
                "confidence": classification.confidence,
                "palm_x": palm_x,
                "palm_y": palm_y,
                "palm_z": palm_z,
                "height_raw": raw_height,
                "height": filtered_height,
                "landmarks": right_hand.landmarks,
                "world_landmarks": right_hand.world_landmarks,
                "shadow": {
                    "available": shadow.available,
                    "gesture": shadow.model_gesture,
                    "confidence": shadow.confidence,
                    "margin": shadow.margin,
                    "inference_ms": shadow.inference_ms,
                    "agrees": shadow.agrees,
                },
            }
        else:
            self.height_motion_filter.reset()
            stable = self.right_stabilizer.update("None", 0.0, timestamp_ms)
            right_packet = empty_right_packet(stable)

        shadow_summary = self.shadow_evaluator.stats.summary()
        telemetry.update({
            "shadow_model_loaded": self.shadow_evaluator.available,
            "shadow_samples": shadow_summary["samples"],
            "shadow_agreement_rate": shadow_summary["agreement_rate"],
            "shadow_disagreement_rate": shadow_summary["disagreement_rate"],
            "shadow_mean_inference_ms": shadow_summary["mean_inference_ms"],
            "shadow_p95_inference_ms": shadow_summary["p95_inference_ms"],
        })

        self.sequence += 1
        packet = {
            "protocol": PROTOCOL_VERSION,
            "session_id": self.session_id,
            "seq": self.sequence,
            "timestamp_ms": timestamp_ms,
            "telemetry": telemetry,
            "role_config": role_config,
            "left": left_packet,
            "right": right_packet,
        }

        payload = json.dumps(packet, separators=(",", ":")).encode("utf-8")
        self.sock.sendto(payload, (MULTICAST_ADDRESS, MULTICAST_PORT))

        with self.lock:
            self.last_packet = packet

    @staticmethod
    def _draw_landmarks(frame, hand_packet: dict, colour: tuple[int, int, int], label: str) -> None:
        if not hand_packet.get("present"):
            return
        height, width = frame.shape[:2]
        landmarks = hand_packet.get("landmarks", [])
        for point in landmarks:
            x = int(max(0.0, min(1.0, point[0])) * width)
            y = int(max(0.0, min(1.0, point[1])) * height)
            cv2.circle(frame, (x, y), 3, colour, -1, cv2.LINE_AA)
        if landmarks:
            wrist_x = int(landmarks[0][0] * width)
            wrist_y = int(landmarks[0][1] * height)
            cv2.putText(frame, label, (wrist_x + 8, wrist_y - 8),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.55, colour, 2, cv2.LINE_AA)

    @staticmethod
    def _fourcc_text(value: float) -> str:
        code = int(value)
        return "".join(chr((code >> (8 * i)) & 0xFF) for i in range(4))

    def _open_camera(self):
        backend_map = {
            "any": cv2.CAP_ANY,
            "dshow": cv2.CAP_DSHOW,
            "msmf": cv2.CAP_MSMF,
        }

        if self.preferred_backend and self.preferred_backend.lower() != "auto":
            key = self.preferred_backend.lower()
            ordered = [backend_map[key]] if key in backend_map else [cv2.CAP_ANY]
        else:
            ordered = [cv2.CAP_ANY]
            if sys.platform.startswith("win"):
                ordered = [cv2.CAP_DSHOW, cv2.CAP_MSMF, cv2.CAP_ANY]

        for backend in ordered:
            capture = cv2.VideoCapture(self.camera_index, backend)
            if capture.isOpened():
                return capture
            capture.release()

        raise RuntimeError(f"Could not open camera index {self.camera_index}")

    def run(self) -> None:
        capture = self._open_camera()

        capture.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        capture.set(cv2.CAP_PROP_FRAME_WIDTH, self.width)
        capture.set(cv2.CAP_PROP_FRAME_HEIGHT, self.height)
        capture.set(cv2.CAP_PROP_FPS, 30.0)
        capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        try:
            self.camera_backend = capture.getBackendName()
        except Exception:
            self.camera_backend = "UNKNOWN"
        self.camera_width = int(capture.get(cv2.CAP_PROP_FRAME_WIDTH))
        self.camera_height = int(capture.get(cv2.CAP_PROP_FRAME_HEIGHT))
        self.camera_fps = float(capture.get(cv2.CAP_PROP_FPS))
        self.camera_fourcc = self._fourcc_text(capture.get(cv2.CAP_PROP_FOURCC))
        self.profile_backend = self.camera_backend or self.preferred_backend or "UNKNOWN"
        self._load_role_profile()

        if self.calibrate_right_on_start:
            with self.role_lock:
                self._begin_hand_calibration_locked(int(time.monotonic() * 1000))

        self.stop_control.clear()
        control_thread = threading.Thread(
            target=self._control_loop,
            name="GestureRackVisionControl",
            daemon=True,
        )
        control_thread.start()

        frame_lock = threading.Lock()
        stop_capture = threading.Event()
        latest_frame = None
        latest_sequence = 0
        latest_capture_ms = 0

        def capture_loop() -> None:
            nonlocal latest_frame, latest_sequence, latest_capture_ms
            while not stop_capture.is_set():
                ok, frame = capture.read()
                now = time.monotonic()
                if not ok:
                    if stop_capture.is_set():
                        break
                    time.sleep(0.002)
                    continue
                with frame_lock:
                    latest_frame = frame
                    latest_sequence += 1
                    latest_capture_ms = int(now * 1000)
                with self.telemetry_lock:
                    self.capture_window_frames += 1
                    elapsed = now - self.capture_window_started
                    if elapsed >= 1.0:
                        self.capture_fps = self.capture_window_frames / elapsed
                        self.capture_window_frames = 0
                        self.capture_window_started = now

        capture_thread = threading.Thread(
            target=capture_loop,
            name="GestureRackLatestFrameCapture",
            daemon=True,
        )
        capture_thread.start()

        with self.role_lock:
            role_text = "SWAPPED" if self.role_resolver.swap_handedness else "NORMAL"
            role_source = self.role_config_source
        print("Gesture Vision Engine running - low-latency protocol v2")
        print(f"Session: {self.session_id}")
        print(f"Camera: {self.camera_backend} {self.camera_width}x{self.camera_height} "
              f"{self.camera_fps:.1f} FPS {self.camera_fourcc}")
        print(f"Hand roles: {role_text} ({role_source})")
        print(f"Plugin camera transport: {SHM_NAME}")
        if self.shadow_evaluator.available:
            print(f"Tiny classifier: SHADOW ONLY ({self.shadow_model_path})")
        elif self.shadow_model_path is not None:
            print(f"Tiny classifier: shadow model unavailable ({self.shadow_model_path})")
        else:
            print("Tiny classifier: shadow disabled")
        print("Capture keeps only the newest frame; inference is one-in-flight")
        print("Tracking physical left and right hands independently")
        print("Left hand classifies Slot 1-5 only; Slot 6-9 remain mouse-selectable")
        print("Right hand: Open Palm / Closed Fist / Victory / Thumb Up / Thumb Down / Point Right / Point Left")
        print(f"Sending multicast {MULTICAST_ADDRESS}:{MULTICAST_PORT}")
        if self.preview:
            print("Press ESC in the preview window to quit.")
        else:
            print("Press Ctrl+C to quit.")

        last_submit_ms = -1
        last_consumed_sequence = 0

        try:
            while True:
                if not self._idle.wait(timeout=2.0):
                    continue
                self._idle.clear()

                with frame_lock:
                    frame = latest_frame
                    sequence = latest_sequence
                    capture_ms = latest_capture_ms
                if frame is None or sequence == last_consumed_sequence:
                    self._idle.set()
                    time.sleep(0.001)
                    continue
                last_consumed_sequence = sequence

                frame = cv2.flip(frame, 1)
                rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                rgb = np.ascontiguousarray(rgb)

                submit_ms = int(time.monotonic() * 1000)
                timestamp_ms = submit_ms
                if timestamp_ms <= last_submit_ms:
                    timestamp_ms = last_submit_ms + 1
                last_submit_ms = timestamp_ms
                with self.telemetry_lock:
                    self._pending[timestamp_ms] = (capture_ms, submit_ms)
                    while len(self._pending) > 16:
                        oldest = next(iter(self._pending))
                        self._pending.pop(oldest, None)

                mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
                try:
                    self.recognizer.recognize_async(mp_image, timestamp_ms)
                except Exception:
                    self._idle.set()

                if self.preview:
                    with self.lock:
                        packet = json.loads(json.dumps(self.last_packet)) if self.last_packet else None

                    if packet:
                        self._draw_landmarks(frame, packet["left"], (255, 190, 80), "PHYSICAL LEFT")
                        self._draw_landmarks(frame, packet["right"], (80, 220, 140), "PHYSICAL RIGHT")
                        telemetry = packet.get("telemetry", {})
                        role = packet.get("role_config", {})
                        cv2.putText(frame,
                                    f"CAM {telemetry.get('capture_fps', 0.0):.1f}  "
                                    f"VISION {telemetry.get('vision_fps', 0.0):.1f}  "
                                    f"FA {telemetry.get('frame_age_at_submit_ms', 0.0):.0f}ms  "
                                    f"LAT {telemetry.get('capture_to_result_ms', 0.0):.0f}ms",
                                    (18, 34), cv2.FONT_HERSHEY_SIMPLEX,
                                    0.60, (255, 255, 255), 2, cv2.LINE_AA)
                        cv2.putText(frame,
                                    f"ROLES {'SWAP' if role.get('swap_handedness') else 'NORMAL'}  "
                                    f"{role.get('calibration_status', '')}",
                                    (18, 60), cv2.FONT_HERSHEY_SIMPLEX,
                                    0.52, (255, 255, 255), 1, cv2.LINE_AA)
                        if telemetry.get("shadow_model_loaded", False):
                            cv2.putText(
                                frame,
                                f"SHADOW {telemetry.get('shadow_agreement_rate', 0.0) * 100.0:.1f}% agree  "
                                f"p95 {telemetry.get('shadow_p95_inference_ms', 0.0):.3f}ms",
                                (18, 84), cv2.FONT_HERSHEY_SIMPLEX,
                                0.50, (255, 255, 255), 1, cv2.LINE_AA,
                            )

                    cv2.imshow("Gesture Vision Engine v2", frame)
                    if cv2.waitKey(1) & 0xFF == 27:
                        break
        finally:
            stop_capture.set()
            capture.release()
            capture_thread.join(timeout=2.0)
            self.stop_control.set()
            if self.control_socket is not None:
                try:
                    self.control_socket.close()
                except OSError:
                    pass
            control_thread.join(timeout=1.0)
            if self.preview:
                cv2.destroyAllWindows()
            self.recognizer.close()
            self.sock.close()
            if self.frame_writer is not None:
                try:
                    self.frame_writer.close(unlink=True)
                except (BufferError, OSError):
                    pass


def main() -> None:
    parser = argparse.ArgumentParser(description="Gesture Rack vision sidecar")
    parser.add_argument("--camera", type=int, default=0)
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=480)
    parser.add_argument("--confidence", type=float, default=0.80)
    parser.add_argument("--hold-ms", type=int, default=50)
    parser.add_argument("--release-ms", type=int, default=50)
    parser.add_argument("--slot-confidence", type=float, default=0.80)
    parser.add_argument("--slot-hold-ms", type=int, default=80)
    parser.add_argument("--preview", action="store_true")
    parser.add_argument(
        "--backend",
        type=str,
        default="auto",
        choices=["auto", "any", "dshow", "msmf"],
        help="Camera backend: auto (default), any, dshow (DirectShow), msmf (Media Foundation).",
    )
    handedness = parser.add_mutually_exclusive_group()
    handedness.add_argument(
        "--swap-handedness",
        dest="swap_handedness",
        action="store_true",
        help="Force swapped MediaPipe Left/Right labels for this run.",
    )
    handedness.add_argument(
        "--no-swap-handedness",
        dest="swap_handedness",
        action="store_false",
        help="Force normal MediaPipe Left/Right labels for this run.",
    )
    parser.set_defaults(swap_handedness=None)
    parser.add_argument(
        "--calibrate-right",
        action="store_true",
        help="Start physical-right-hand calibration immediately after opening the camera.",
    )
    parser.add_argument(
        "--profile",
        type=Path,
        default=None,
        help="Optional vision profile path (default is the per-user GestureRack config directory).",
    )
    parser.add_argument(
        "--model",
        type=Path,
        default=default_model_path(),
    )
    parser.add_argument(
        "--shadow-model",
        type=Path,
        default=None,
        help=(
            "Optional tiny landmark model. Runs in shadow mode only: predictions are logged/telemetried "
            "but never drive stable gestures or rack control."
        ),
    )
    args = parser.parse_args()

    ensure_model(args.model)
    engine = GestureVisionEngine(
        model_path=args.model,
        camera_index=args.camera,
        width=args.width,
        height=args.height,
        confidence=args.confidence,
        hold_ms=args.hold_ms,
        release_ms=args.release_ms,
        preview=args.preview,
        swap_handedness=args.swap_handedness,
        slot_confidence=args.slot_confidence,
        slot_hold_ms=args.slot_hold_ms,
        backend=args.backend,
        profile_path=args.profile,
        calibrate_right=args.calibrate_right,
        shadow_model_path=args.shadow_model,
    )
    engine.run()


if __name__ == "__main__":
    main()
