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


from gesture_stabilizer import GestureStabilizer
from hand_role_resolver import DetectedHand, HandRoleResolver
from right_gesture_classifier import RIGHT_GESTURES, classify_right_gesture
from slot_selector import SlotStabilizer, classify_slot_1_to_5

MODEL_URL = (
    "https://storage.googleapis.com/mediapipe-models/gesture_recognizer/"
    "gesture_recognizer/float16/1/gesture_recognizer.task"
)
MULTICAST_ADDRESS = "239.255.71.77"
MULTICAST_PORT = 17777
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
        "raw_gesture": "None",
        "stable_gesture": stable_gesture,
        "confidence": 0.0,
        "palm_x": 0.5,
        "palm_y": 0.5,
        "palm_z": 0.0,
        "height": 0.5,
        "landmarks": [],
    }


class GestureVisionEngine:
    def __init__(self, model_path: Path, camera_index: int, width: int, height: int,
                 confidence: float, hold_ms: int, release_ms: int, preview: bool,
                 swap_handedness: bool, slot_confidence: float,
                 slot_hold_ms: int, backend: str = "auto"):
        self.model_path = model_path
        self.camera_index = camera_index
        self.width = width
        self.height = height
        self.preview = preview
        self.preferred_backend = backend
        self.right_stabilizer = GestureStabilizer(
            allowed_gestures=set(ALLOWED_RIGHT_GESTURES),
            hold_ms=hold_ms,
            release_ms=release_ms,
            min_confidence=confidence,
        )
        self.left_stabilizer = SlotStabilizer(hold_ms=slot_hold_ms, min_confidence=slot_confidence)
        self.role_resolver = HandRoleResolver(swap_handedness=swap_handedness)
        self.sequence = 0
        self.session_id = uuid.uuid4().hex[:12]
        self.lock = threading.Lock()
        self.last_packet = None

        # Telemetry for latency / fps observability. The pending table keeps the
        # capture + submit timestamps keyed by the MediaPipe timestamp_ms so the
        # result callback can split frame_age_at_submit / inference / total.
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

        # one-in-flight gate: keeps the MediaPipe async queue at <= 1 frame so
        # frames can never pile up and produce multi-second stale results.
        # Note: GestureRecognizer LIVE_STREAM recognize_async returns
        # immediately and may drop inputs when busy to lower overall latency;
        # this gate enforces freshness explicitly rather than relying on a
        # per-frame queue guarantee.
        self._idle = threading.Event()
        self._idle.set()

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)

        BaseOptions = mp.tasks.BaseOptions
        GestureRecognizer = mp.tasks.vision.GestureRecognizer
        GestureRecognizerOptions = mp.tasks.vision.GestureRecognizerOptions
        RunningMode = mp.tasks.vision.RunningMode
        ClassifierOptions = mp.tasks.components.processors.ClassifierOptions

        # The canned-gesture classifier option field was renamed across
        # MediaPipe versions: older wheels use the singular
        # ``canned_gesture_classifier_options`` while newer ones use the
        # plural ``canned_gestures_classifier_options``. Detect the real field
        # name so the engine works on either wheel without a source edit.
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
            ))

        return hands

    def _on_result(self, result, _output_image, timestamp_ms: int) -> None:
        # Previous inference finished; allow the next submission.
        self._idle.set()
        result_ms = int(time.monotonic() * 1000)
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
                # frame_age_at_submit: how old the captured image was when it
                # entered MediaPipe. High values mean we are feeding stale frames.
                "frame_age_at_submit_ms": max(0.0, float(submit_ms - capture_ms)),
                # inference_ms is now pure MediaPipe time (result - submit),
                # excluding the one-in-flight gate wait.
                "inference_ms": max(0.0, float(result_ms - submit_ms)),
                "capture_to_result_ms": max(0.0, float(result_ms - capture_ms)),
                "backend": self.camera_backend,
                "width": self.camera_width,
                "height": self.camera_height,
                "fps": self.camera_fps,
                "fourcc": self.camera_fourcc,
            }

        detections = self._extract_hands(result)
        left_hand, right_hand = self.role_resolver.resolve(detections, timestamp_ms)

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
            stable = self.right_stabilizer.update(
                classification.gesture, classification.confidence, timestamp_ms)
            palm_x, palm_y, palm_z, height = palm_metrics(right_hand.landmarks)
            right_packet = {
                "present": True,
                "handedness_confidence": right_hand.handedness_confidence,
                "raw_gesture": classification.gesture,
                "stable_gesture": stable,
                "confidence": classification.confidence,
                "palm_x": palm_x,
                "palm_y": palm_y,
                "palm_z": palm_z,
                "height": height,
                "landmarks": right_hand.landmarks,
            }
        else:
            stable = self.right_stabilizer.update("None", 0.0, timestamp_ms)
            right_packet = empty_right_packet(stable)

        self.sequence += 1
        packet = {
            "protocol": PROTOCOL_VERSION,
            "session_id": self.session_id,
            "seq": self.sequence,
            "timestamp_ms": timestamp_ms,
            "telemetry": telemetry,
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
            # DirectShow has a smaller driver buffer than Media Foundation on
            # Windows, so try it first for the lowest capture latency.
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

        # MJPG halves the USB bandwidth vs the default YUYV on external
        # webcams, leaving headroom for the CPU/GPU inference instead of
        # saturating the bus with raw frames.
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

        # Dedicated capture thread that always overwrites the latest frame.
        # Windows MSMF ignores CAP_PROP_BUFFERSIZE, so without a thread that
        # keeps reading, the driver buffer fills with stale frames. This loop
        # drains them so the inference always sees the newest image.
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

        print("Gesture Vision Engine running - low-latency protocol v2")
        print(f"Session: {self.session_id}")
        print(f"Camera: {self.camera_backend} {self.camera_width}x{self.camera_height} "
              f"{self.camera_fps:.1f} FPS {self.camera_fourcc}")
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
                # one-in-flight gate (wait-first): block until the previous
                # inference has finished BEFORE snapshotting the latest frame.
                # The previous order (snapshot -> wait -> submit) let the frame
                # age by up to one extra inference period while we waited idle,
                # so the submitted image was already stale. Waiting first means
                # we always grab the newest frame right at submit time.
                # Event.wait() returns False on timeout; in that case the model
                # is stuck, so skip this iteration instead of queuing more work.
                if not self._idle.wait(timeout=2.0):
                    continue
                self._idle.clear()

                with frame_lock:
                    frame = latest_frame
                    sequence = latest_sequence
                    capture_ms = latest_capture_ms
                if frame is None or sequence == last_consumed_sequence:
                    # No fresh frame yet; re-arm the gate so we wait for a real
                    # result callback next time instead of busy-spinning.
                    self._idle.set()
                    time.sleep(0.001)
                    continue
                last_consumed_sequence = sequence

                frame = cv2.flip(frame, 1)
                rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                rgb = np.ascontiguousarray(rgb)

                # timestamp_ms is the MediaPipe per-frame monotonically-
                # increasing id (used to match result callbacks to submissions).
                # It MUST be generated after the idle wait so that inference_ms
                # = result_ms - submit_ms measures pure MediaPipe time, not the
                # gate wait. capture_to_result stays the true end-to-end figure.
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
                    # Re-arm the gate so the loop is not deadlocked if a
                    # submission fails without producing a result callback.
                    self._idle.set()

                if self.preview:
                    with self.lock:
                        packet = json.loads(json.dumps(self.last_packet)) if self.last_packet else None

                    if packet:
                        self._draw_landmarks(frame, packet["left"], (255, 190, 80), "PHYSICAL LEFT")
                        self._draw_landmarks(frame, packet["right"], (80, 220, 140), "PHYSICAL RIGHT")
                        telemetry = packet.get("telemetry", {})
                        cv2.putText(frame,
                                    f"CAM {telemetry.get('capture_fps', 0.0):.1f}  "
                                    f"VISION {telemetry.get('vision_fps', 0.0):.1f}  "
                                    f"FA {telemetry.get('frame_age_at_submit_ms', 0.0):.0f}ms  "
                                    f"LAT {telemetry.get('capture_to_result_ms', 0.0):.0f}ms",
                                    (18, 34), cv2.FONT_HERSHEY_SIMPLEX,
                                    0.60, (255, 255, 255), 2, cv2.LINE_AA)

                    cv2.imshow("Gesture Vision Engine v2", frame)
                    if cv2.waitKey(1) & 0xFF == 27:
                        break
        finally:
            stop_capture.set()
            capture.release()
            capture_thread.join(timeout=2.0)
            if self.preview:
                cv2.destroyAllWindows()
            self.recognizer.close()
            self.sock.close()


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
    parser.add_argument(
        "--swap-handedness",
        action="store_true",
        help="Swap MediaPipe Left/Right labels if the mirrored camera pipeline reports physical roles reversed.",
    )
    parser.add_argument(
        "--model",
        type=Path,
        default=default_model_path(),
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
    )
    engine.run()


if __name__ == "__main__":
    main()
