from __future__ import annotations

import unittest

from gesture_stabilizer import GestureStabilizer


class GestureThresholdTests(unittest.TestCase):
    def test_open_palm_uses_class_specific_activation_threshold(self) -> None:
        stabilizer = GestureStabilizer(
            allowed_gestures={"Open_Palm"}, hold_ms=50, release_ms=50,
            min_confidence=0.80,
        )
        self.assertEqual(stabilizer.update("Open_Palm", 0.63, 1000), "None")
        # Near-threshold input deliberately requires extra evidence.
        self.assertEqual(stabilizer.update("Open_Palm", 0.63, 1055), "None")
        self.assertEqual(stabilizer.update("Open_Palm", 0.63, 1076), "Open_Palm")

    def test_release_hysteresis_keeps_stable_thumb(self) -> None:
        stabilizer = GestureStabilizer(
            allowed_gestures={"Thumb_Up"}, hold_ms=20, release_ms=50,
            min_confidence=0.80,
        )
        stabilizer.update("Thumb_Up", 0.92, 100)
        self.assertEqual(stabilizer.update("Thumb_Up", 0.92, 120), "Thumb_Up")
        self.assertEqual(stabilizer.update("Thumb_Up", 0.53, 140), "Thumb_Up")

    def test_direction_boundary_grace_is_longer_than_normal_release(self) -> None:
        stabilizer = GestureStabilizer(
            allowed_gestures={"Thumb_Right"}, hold_ms=20, release_ms=40,
        )
        stabilizer.update("Thumb_Right", 0.92, 100)
        stabilizer.update("Thumb_Right", 0.92, 120)
        self.assertEqual(stabilizer.update("None", 0.0, 140), "Thumb_Right")
        self.assertEqual(stabilizer.update("None", 0.0, 230), "Thumb_Right")
        self.assertEqual(stabilizer.update("None", 0.0, 250), "None")


if __name__ == "__main__":
    unittest.main()
