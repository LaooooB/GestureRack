from __future__ import annotations

import argparse
import json
import socket
import threading
import time
import urllib.request
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
    import sys
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
                 slot_hold_ms: int):
        self.model_path = model_path
        self.camera_index = camera_index
        self.width = width
        self.height = height
        self.preview = preview
        self.right_stabilizer = GestureStabilizer(
            allowed_gestures=set(ALLOWED_RIGHT_GESTURES),
            hold_ms=hold_ms,
            release_ms=release_ms,
            min_confidence=confidence,
        )
        self.left_stabilizer = SlotStabilizer(hold_ms=slot_hold_ms, min_confidence=slot_confidence)
        self.role_resolver = HandRoleResolver(swap_handedness=swap_handedness)
        self.sequence = 0
        self.lock = threading.Lock()
        self.last_packet = None

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
            "seq": self.sequence,
            "timestamp_ms": timestamp_ms,
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

    def run(self) -> None:
        capture = cv2.VideoCapture(self.camera_index)
        if not capture.isOpened():
            raise RuntimeError(f"Could not open camera index {self.camera_index}")

        capture.set(cv2.CAP_PROP_FRAME_WIDTH, self.width)
        capture.set(cv2.CAP_PROP_FRAME_HEIGHT, self.height)
        capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)

        print("Gesture Vision Engine running - protocol v2")
        print("Tracking physical left and right hands independently")
        print("Left hand classifies Slot 1-5 only; Slot 6-9 remain mouse-selectable")
        print("Right hand: Open Palm / Closed Fist / Victory / Thumb Up / Thumb Down / Point Right / Point Left")
        print(f"Sending multicast {MULTICAST_ADDRESS}:{MULTICAST_PORT}")
        if self.preview:
            print("Press ESC in the preview window to quit.")
        else:
            print("Press Ctrl+C to quit.")

        last_submit_ms = -1

        try:
            while True:
                ok, frame = capture.read()
                if not ok:
                    time.sleep(0.01)
                    continue

                frame = cv2.flip(frame, 1)
                rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                rgb = np.ascontiguousarray(rgb)
                timestamp_ms = int(time.monotonic() * 1000)

                if timestamp_ms <= last_submit_ms:
                    timestamp_ms = last_submit_ms + 1
                last_submit_ms = timestamp_ms

                mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
                self.recognizer.recognize_async(mp_image, timestamp_ms)

                if self.preview:
                    with self.lock:
                        packet = json.loads(json.dumps(self.last_packet)) if self.last_packet else None

                    if packet:
                        self._draw_landmarks(frame, packet["left"], (255, 190, 80), "PHYSICAL LEFT")
                        self._draw_landmarks(frame, packet["right"], (80, 220, 140), "PHYSICAL RIGHT")
                        left = packet["left"]
                        right = packet["right"]
                        left_status = (f"L: raw {left['raw_slot']} stable {left['stable_slot']} "
                                       f"{left['confidence']:.2f}")
                        right_status = (f"R: raw {right['raw_gesture']} stable {right['stable_gesture']} "
                                        f"{right['confidence']:.2f} H:{right['height']:.2f}")
                        cv2.putText(frame, left_status, (18, 34), cv2.FONT_HERSHEY_SIMPLEX,
                                    0.60, (255, 255, 255), 2, cv2.LINE_AA)
                        cv2.putText(frame, right_status, (18, 64), cv2.FONT_HERSHEY_SIMPLEX,
                                    0.60, (255, 255, 255), 2, cv2.LINE_AA)

                    cv2.imshow("Gesture Vision Engine v2", frame)
                    if cv2.waitKey(1) & 0xFF == 27:
                        break
        finally:
            capture.release()
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
    )
    engine.run()


if __name__ == "__main__":
    main()
