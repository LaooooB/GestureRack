from __future__ import annotations

import unittest

from gesture_stabilizer import GestureStabilizer


class GestureStabilizerTests(unittest.TestCase):
    def make(self) -> GestureStabilizer:
        return GestureStabilizer(
            allowed_gestures={"Closed_Fist", "Victory"},
            hold_ms=120,
            release_ms=100,
            min_confidence=0.80,
        )

    def test_requires_hold_before_entering(self) -> None:
        stabilizer = self.make()
        self.assertEqual(stabilizer.update("Closed_Fist", 0.95, 1000), "None")
        self.assertEqual(stabilizer.update("Closed_Fist", 0.95, 1119), "None")
        self.assertEqual(stabilizer.update("Closed_Fist", 0.95, 1120), "Closed_Fist")

    def test_invalid_frames_release_stable_gesture(self) -> None:
        stabilizer = self.make()
        stabilizer.update("Closed_Fist", 0.95, 1000)
        stabilizer.update("Closed_Fist", 0.95, 1120)
        self.assertEqual(stabilizer.update("None", 0.0, 1200), "Closed_Fist")
        self.assertEqual(stabilizer.update("None", 0.0, 1299), "Closed_Fist")
        self.assertEqual(stabilizer.update("None", 0.0, 1300), "None")

    def test_change_to_new_gesture_requires_new_hold(self) -> None:
        stabilizer = self.make()
        stabilizer.update("Closed_Fist", 0.95, 1000)
        stabilizer.update("Closed_Fist", 0.95, 1120)
        self.assertEqual(stabilizer.update("Victory", 0.95, 1200), "Closed_Fist")
        self.assertEqual(stabilizer.update("Victory", 0.95, 1320), "Victory")


if __name__ == "__main__":
    unittest.main()
