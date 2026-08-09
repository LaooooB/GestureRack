from __future__ import annotations

import argparse
import json
import math
import socket
import threading
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path

import cv2
import mediapipe as mp
import numpy as np

MODEL_URL = (
    "https://storage.googleapis.com/mediapipe-models/gesture_recognizer/"
    "gesture_recognizer/float16/1/gesture_recognizer.task"
)
MULTICAST_ADDRESS = "239.255.71.77"
MULTICAST_PORT = 17777
PROTOCOL_VERSION = 1
ALLOWED_GESTURES = {"Open_Palm", "Closed_Fist"}


def ensure_model(model_path: Path) -> None:
    if model_path.exists():
        return
    model_path.parent.mkdir(parents=True, exist_ok=True)
    print(f"Downloading MediaPipe gesture model to: {model_path}")
    urllib.request.urlretrieve(MODEL_URL, model_path)


@dataclass
class Stabilizer:
    hold_ms: int = 120
    min_confidence: float = 0.80
    stable: str = "None"
    candidate: str = "None"
    candidate_since_ms: int = 0

    def update(self, raw: str, confidence: float, now_ms: int) -> str:
        valid = raw in ALLOWED_GESTURES and confidence >= self.min_confidence
        if not valid:
            self.candidate = "None"
            self.candidate_since_ms = 0
            return self.stable

        if raw != self.candidate:
            self.candidate = raw
            self.candidate_since_ms = now_ms
            return self.stable

        if self.candidate_since_ms and now_ms - self.candidate_since_ms >= self.hold_ms:
            self.stable = raw

        return self.stable


class GestureVisionEngine:
    def __init__(self, model_path: Path, camera_index: int, width: int, height: int,
                 confidence: float, hold_ms: int, preview: bool):
        self.model_path = model_path
        self.camera_index = camera_index
        self.width = width
        self.height = height
        self.preview = preview
        self.stabilizer = Stabilizer(hold_ms=hold_ms, min_confidence=confidence)
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

        options = GestureRecognizerOptions(
            base_options=BaseOptions(model_asset_path=str(model_path.resolve())),
            running_mode=RunningMode.LIVE_STREAM,
            num_hands=1,
            min_hand_detection_confidence=0.5,
            min_hand_presence_confidence=0.5,
            min_tracking_confidence=0.5,
            canned_gestures_classifier_options=ClassifierOptions(
                score_threshold=0.0,
                category_allowlist=["Open_Palm", "Closed_Fist"],
            ),
            result_callback=self._on_result,
        )
        self.recognizer = GestureRecognizer.create_from_options(options)

    def _on_result(self, result, _output_image, timestamp_ms: int) -> None:
        hand_present = bool(result.hand_landmarks)
        raw = "None"
        confidence = 0.0
        landmarks = []

        if hand_present:
            landmarks = [
                [float(lm.x), float(lm.y), float(lm.z)]
                for lm in result.hand_landmarks[0]
            ]

        if result.gestures and result.gestures[0]:
            top = result.gestures[0][0]
            raw = top.category_name or "None"
            confidence = float(top.score)

        stable = self.stabilizer.update(raw, confidence, timestamp_ms)
        self.sequence += 1

        packet = {
            "protocol": PROTOCOL_VERSION,
            "seq": self.sequence,
            "timestamp_ms": timestamp_ms,
            "hand": hand_present,
            "raw": raw,
            "stable": stable,
            "confidence": confidence,
            "landmarks": landmarks,
        }

        payload = json.dumps(packet, separators=(",", ":")).encode("utf-8")
        self.sock.sendto(payload, (MULTICAST_ADDRESS, MULTICAST_PORT))

        with self.lock:
            self.last_packet = packet

    def run(self) -> None:
        capture = cv2.VideoCapture(self.camera_index)
        if not capture.isOpened():
            raise RuntimeError(f"Could not open camera index {self.camera_index}")

        capture.set(cv2.CAP_PROP_FRAME_WIDTH, self.width)
        capture.set(cv2.CAP_PROP_FRAME_HEIGHT, self.height)
        capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)

        print("Gesture Vision Engine running")
        print("Open palm  -> ACTIVE")
        print("Closed fist -> BYPASS")
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
                        packet = self.last_packet.copy() if self.last_packet else None

                    if packet:
                        text = f"{packet['stable']}  {packet['confidence']:.2f}"
                        cv2.putText(frame, text, (18, 36), cv2.FONT_HERSHEY_SIMPLEX,
                                    0.8, (255, 255, 255), 2, cv2.LINE_AA)
                    cv2.imshow("Gesture Vision Engine", frame)
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
    parser.add_argument("--hold-ms", type=int, default=120)
    parser.add_argument("--preview", action="store_true")
    parser.add_argument(
        "--model",
        type=Path,
        default=Path(__file__).resolve().parent / "models" / "gesture_recognizer.task",
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
        preview=args.preview,
    )
    engine.run()


if __name__ == "__main__":
    main()
