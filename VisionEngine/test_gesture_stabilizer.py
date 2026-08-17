from __future__ import annotations

import unittest
from gesture_stabilizer import GestureStabilizer

ALL = {"Open_Palm", "Closed_Fist", "Victory", "Thumb_Up", "Thumb_Down", "Thumb_Left", "Thumb_Right"}


class GestureStabilizerTests(unittest.TestCase):
    def test_high_confidence_gesture_activates_after_two_callbacks(self) -> None:
        s = GestureStabilizer(allowed_gestures=ALL, hold_ms=50)
        self.assertEqual(s.update("Open_Palm", 0.96, 1000), "None")
        self.assertEqual(s.update("Open_Palm", 0.96, 1030), "Open_Palm")

    def test_fist_to_thumb_requires_extra_transition_evidence(self) -> None:
        s = GestureStabilizer(allowed_gestures=ALL, hold_ms=50)
        s.update("Closed_Fist", 0.96, 1000)
        s.update("Closed_Fist", 0.96, 1030)
        self.assertEqual(s.update("Thumb_Up", 0.96, 1060), "Closed_Fist")
        self.assertEqual(s.update("Thumb_Up", 0.96, 1100), "Closed_Fist")
        self.assertEqual(s.update("Thumb_Up", 0.96, 1121), "Thumb_Up")

    def test_thumb_boundary_none_does_not_immediately_release(self) -> None:
        s = GestureStabilizer(allowed_gestures=ALL, hold_ms=50, release_ms=50)
        s.update("Thumb_Up", 0.96, 1000)
        s.update("Thumb_Up", 0.96, 1030)
        self.assertEqual(s.update("None", 0.0, 1070), "Thumb_Up")
        self.assertEqual(s.update("None", 0.0, 1160), "Thumb_Up")
        self.assertEqual(s.update("None", 0.0, 1180), "None")

    def test_near_threshold_candidate_requires_more_evidence(self) -> None:
        s = GestureStabilizer(allowed_gestures=ALL, hold_ms=50)
        self.assertEqual(s.update("Thumb_Left", 0.67, 1000), "None")
        self.assertEqual(s.update("Thumb_Left", 0.67, 1060), "None")
        self.assertEqual(s.update("Thumb_Left", 0.67, 1076), "Thumb_Left")

    def test_brief_confidence_dip_keeps_candidate_clock(self) -> None:
        s = GestureStabilizer(allowed_gestures=ALL, hold_ms=50, candidate_grace_ms=75)
        self.assertEqual(s.update("Victory", 0.72, 1000), "None")
        self.assertEqual(s.update("None", 0.0, 1030), "None")
        self.assertEqual(s.update("Victory", 0.72, 1060), "None")
        self.assertEqual(s.update("Victory", 0.72, 1076), "Victory")


if __name__ == "__main__":
    unittest.main()
