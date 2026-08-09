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


def set_thumb(landmarks: list[list[float]], direction: tuple[float, float]) -> None:
    x, y = 0.40, 0.70
    dx, dy = direction
    landmarks[1] = [0.43, 0.73, 0.0]
    landmarks[2] = [x, y, 0.0]
    landmarks[3] = [x + dx * 0.12, y + dy * 0.14, 0.0]
    landmarks[4] = [x + dx * 0.24, y + dy * 0.28, 0.0]


class RightGestureClassifierTests(unittest.TestCase):
    def test_canned_five(self) -> None:
        specs = [
            ("Open_Palm", [0, 1, 2, 3], (-1.0, -0.2)),
            ("Closed_Fist", [], None),
            ("Victory", [0, 1], None),
            ("Thumb_Up", [], (0.0, -1.0)),
            ("Thumb_Down", [], (0.0, 1.0)),
        ]
        for gesture, fingers, thumb in specs:
            with self.subTest(gesture=gesture):
                landmarks = base_hand()
                for finger in fingers:
                    extend_finger(landmarks, finger)
                if thumb is not None:
                    set_thumb(landmarks, thumb)
                result = classify_right_gesture(landmarks, gesture, 0.96)
                self.assertEqual(result.gesture, gesture)
                self.assertGreaterEqual(result.confidence, 0.72)

    def test_horizontal_pointing_direction(self) -> None:
        for expected, direction in [("Point_Right", (1.0, 0.0)), ("Point_Left", (-1.0, 0.0))]:
            with self.subTest(expected=expected):
                landmarks = base_hand()
                extend_finger(landmarks, 0, direction)
                result = classify_right_gesture(landmarks, "Pointing_Up", 0.92)
                self.assertEqual(result.gesture, expected)
                self.assertGreaterEqual(result.confidence, 0.72)

    def test_vertical_index_is_not_left_or_right(self) -> None:
        landmarks = base_hand()
        extend_finger(landmarks, 0, (0.0, -1.0))
        result = classify_right_gesture(landmarks, "Pointing_Up", 0.95)
        self.assertNotIn(result.gesture, {"Point_Left", "Point_Right"})

    def test_invalid_landmarks_are_unknown(self) -> None:
        self.assertEqual(classify_right_gesture([]).gesture, "None")


if __name__ == "__main__":
    unittest.main()
