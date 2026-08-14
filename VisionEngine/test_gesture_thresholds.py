from __future__ import annotations

import unittest

from gesture_stabilizer import GestureStabilizer


class GestureThresholdTests(unittest.TestCase):
    def test_open_palm_uses_lower_class_specific_activation_threshold(self) -> None:
        stabilizer = GestureStabilizer(
            allowed_gestures={"Open_Palm"}, hold_ms=50, release_ms=50,
            min_confidence=0.80,
        )
        self.assertEqual(stabilizer.update("Open_Palm", 0.63, 1000), "None")
        self.assertEqual(stabilizer.update("Open_Palm", 0.63, 1055), "Open_Palm")

    def test_release_hysteresis_keeps_stable_gesture(self) -> None:
        stabilizer = GestureStabilizer(
            allowed_gestures={"Thumb_Up"}, hold_ms=20, release_ms=50,
            min_confidence=0.80,
        )
        stabilizer.update("Thumb_Up", 0.70, 100)
        self.assertEqual(stabilizer.update("Thumb_Up", 0.70, 125), "Thumb_Up")
        # Below activation (0.62) but above release (0.48): remain stable.
        self.assertEqual(stabilizer.update("Thumb_Up", 0.53, 140), "Thumb_Up")

    def test_weak_gesture_releases_after_release_hold(self) -> None:
        stabilizer = GestureStabilizer(
            allowed_gestures={"Victory"}, hold_ms=20, release_ms=40,
        )
        stabilizer.update("Victory", 0.80, 100)
        stabilizer.update("Victory", 0.80, 125)
        self.assertEqual(stabilizer.update("None", 0.0, 140), "Victory")
        self.assertEqual(stabilizer.update("None", 0.0, 185), "None")


if __name__ == "__main__":
    unittest.main()
