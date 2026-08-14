from __future__ import annotations

import unittest

from slot_selector import SlotStabilizer, classify_slot_1_to_5


def synthetic_hand(slot: int) -> list[list[float]]:
    landmarks = [[0.5, 0.8, 0.0] for _ in range(21)]
    landmarks[0] = [0.5, 0.85, 0.0]
    groups = [(5, 6, 7, 8), (9, 10, 11, 12), (13, 14, 15, 16), (17, 18, 19, 20)]
    xs = [0.38, 0.46, 0.54, 0.62]
    extended_count = min(slot, 4)

    for finger_index, (mcp, pip, dip, tip) in enumerate(groups):
        x = xs[finger_index]
        landmarks[mcp] = [x, 0.62, 0.0]
        if finger_index < extended_count:
            landmarks[pip] = [x, 0.50, 0.0]
            landmarks[dip] = [x, 0.38, 0.0]
            landmarks[tip] = [x, 0.24, 0.0]
        else:
            landmarks[pip] = [x, 0.58, 0.0]
            landmarks[dip] = [x + 0.025, 0.63, 0.0]
            landmarks[tip] = [x + 0.04, 0.67, 0.0]

    landmarks[1] = [0.43, 0.72, 0.0]
    landmarks[2] = [0.39, 0.68, 0.0]
    landmarks[3] = [0.42, 0.66, 0.0]
    landmarks[4] = [0.46, 0.65, 0.0]

    if slot == 5:
        landmarks[1] = [0.43, 0.72, 0.0]
        landmarks[2] = [0.35, 0.66, 0.0]
        landmarks[3] = [0.27, 0.60, 0.0]
        landmarks[4] = [0.17, 0.54, 0.0]

    return landmarks


class SlotSelectorTests(unittest.TestCase):
    def test_clean_1_to_5_shapes(self) -> None:
        for expected in range(1, 6):
            with self.subTest(slot=expected):
                result = classify_slot_1_to_5(synthetic_hand(expected))
                self.assertEqual(result.slot, expected)
                self.assertGreaterEqual(result.confidence, 0.80)

    def test_invalid_landmarks_return_unknown(self) -> None:
        result = classify_slot_1_to_5([])
        self.assertEqual(result.slot, 0)
        self.assertEqual(result.confidence, 0.0)

    def test_stabilizer_requires_hold_and_keeps_last_slot(self) -> None:
        stabilizer = SlotStabilizer(hold_ms=150, min_confidence=0.80)
        self.assertEqual(stabilizer.update(3, 0.95, 1000), 0)
        self.assertEqual(stabilizer.update(3, 0.95, 1149), 0)
        self.assertEqual(stabilizer.update(3, 0.95, 1150), 3)
        self.assertEqual(stabilizer.update(0, 0.0, 1200), 3)
        self.assertEqual(stabilizer.update(4, 0.95, 1210), 3)
        self.assertEqual(stabilizer.update(4, 0.95, 1360), 4)


if __name__ == "__main__":
    unittest.main()
