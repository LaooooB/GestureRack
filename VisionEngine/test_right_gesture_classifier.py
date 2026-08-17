from __future__ import annotations

import unittest

from right_gesture_classifier import classify_right_gesture


def base_hand() -> list[list[float]]:
    landmarks = [[0.5, 0.8, 0.0] for _ in range(21)]
    landmarks[0] = [0.5, 0.86, 0.0]
    groups = [(5, 6, 7, 8), (9, 10, 11, 12), (13, 14, 15, 16), (17, 18, 19, 20)]
    for x, (mcp, pip, dip, tip) in zip([0.38, 0.46, 0.54, 0.62], groups):
        landmarks[mcp] = [x, 0.62, 0.0]
        landmarks[pip] = [x, 0.59, 0.0]
        landmarks[dip] = [x + 0.025, 0.64, 0.0]
        landmarks[tip] = [x + 0.04, 0.68, 0.0]
    landmarks[1] = [0.43, 0.73, 0.0]
    landmarks[2] = [0.40, 0.70, 0.0]
    landmarks[3] = [0.43, 0.68, 0.0]
    landmarks[4] = [0.46, 0.67, 0.0]
    return landmarks


def extend_finger(landmarks: list[list[float]], finger: int,
                  direction: tuple[float, float] = (0.0, -1.0)) -> None:
    groups = [(5, 6, 7, 8), (9, 10, 11, 12), (13, 14, 15, 16), (17, 18, 19, 20)]
    mcp, pip, dip, tip = groups[finger]
    x, y, _ = landmarks[mcp]
    dx, dy = direction
    landmarks[pip] = [x + dx * 0.10, y + dy * 0.11, 0.0]
    landmarks[dip] = [x + dx * 0.20, y + dy * 0.22, 0.0]
    landmarks[tip] = [x + dx * 0.31, y + dy * 0.34, 0.0]


def set_thumb(landmarks: list[list[float]], direction: tuple[float, float], scale: float = 1.0) -> None:
    x, y = 0.40, 0.70
    dx, dy = direction
    magnitude = max(1.0e-6, (dx * dx + dy * dy) ** 0.5)
    dx, dy = dx / magnitude, dy / magnitude
    landmarks[1] = [0.43, 0.73, 0.0]
    landmarks[2] = [x, y, 0.0]
    landmarks[3] = [x + dx * 0.12 * scale, y + dy * 0.14 * scale, 0.0]
    landmarks[4] = [x + dx * 0.24 * scale, y + dy * 0.28 * scale, 0.0]


class RightGestureClassifierTests(unittest.TestCase):
    def test_fist_and_thumb_share_folded_four_family(self) -> None:
        fist = classify_right_gesture(base_hand(), "Closed_Fist", 0.95)
        thumb_hand = base_hand()
        set_thumb(thumb_hand, (0.0, -1.0))
        thumb = classify_right_gesture(thumb_hand, "Thumb_Up", 0.95)
        self.assertEqual(fist.gesture, "Closed_Fist")
        self.assertEqual(thumb.gesture, "Thumb_Up")
        self.assertEqual(fist.family, "FoldedFour")
        self.assertEqual(thumb.family, "FoldedFour")
        self.assertEqual(fist.thumb_state, "Tucked")
        self.assertEqual(thumb.thumb_state, "Extended")

    def test_open_palm_does_not_require_extended_thumb(self) -> None:
        landmarks = base_hand()
        for finger in range(4):
            extend_finger(landmarks, finger)
        result = classify_right_gesture(landmarks, "Open_Palm", 0.72)
        self.assertEqual(result.gesture, "Open_Palm")
        self.assertEqual(result.family, "OpenPalm")

    def test_four_thumb_directions_are_geometry_not_point_gestures(self) -> None:
        specs = [
            ("Thumb_Up", (0.0, -1.0)),
            ("Thumb_Down", (0.0, 1.0)),
            ("Thumb_Left", (-1.0, 0.0)),
            ("Thumb_Right", (1.0, 0.0)),
        ]
        for expected, direction in specs:
            with self.subTest(expected=expected):
                landmarks = base_hand()
                set_thumb(landmarks, direction)
                result = classify_right_gesture(landmarks, "None", 0.0)
                self.assertEqual(result.gesture, expected)
                self.assertEqual(result.family, "FoldedFour")
                self.assertGreater(result.direction_confidence, 0.75)

    def test_diagonal_thumb_is_intentionally_ambiguous(self) -> None:
        landmarks = base_hand()
        set_thumb(landmarks, (1.0, -1.0))
        result = classify_right_gesture(landmarks)
        self.assertEqual(result.gesture, "None")
        self.assertEqual(result.direction, "Ambiguous")

    def test_half_formed_thumb_is_not_forced_to_fist(self) -> None:
        landmarks = base_hand()
        set_thumb(landmarks, (0.0, -1.0), scale=0.38)
        result = classify_right_gesture(landmarks)
        self.assertEqual(result.gesture, "None")
        self.assertEqual(result.thumb_state, "Ambiguous")

    def test_victory_remains_separate_family(self) -> None:
        landmarks = base_hand()
        extend_finger(landmarks, 0)
        extend_finger(landmarks, 1)
        result = classify_right_gesture(landmarks, "Victory", 0.95)
        self.assertEqual(result.gesture, "Victory")
        self.assertEqual(result.family, "Victory")

    def test_tiny_or_invalid_geometry_fails_closed(self) -> None:
        self.assertEqual(classify_right_gesture([]).gesture, "None")
        landmarks = base_hand()
        for point in landmarks:
            point[0] = 0.5 + (point[0] - 0.5) * 0.05
            point[1] = 0.5 + (point[1] - 0.5) * 0.05
        self.assertEqual(classify_right_gesture(landmarks).gesture, "None")


if __name__ == "__main__":
    unittest.main()
