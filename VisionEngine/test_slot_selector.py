from __future__ import annotations

import unittest

from slot_selector import SlotStabilizer, classify_slot_1_to_5, classify_slot_1_to_10


def synthetic_hand(slot: int) -> list[list[float]]:
    landmarks = [[0.5, 0.8, 0.0] for _ in range(21)]
    landmarks[0] = [0.5, 0.85, 0.0]
    groups = [(5, 6, 7, 8), (9, 10, 11, 12), (13, 14, 15, 16), (17, 18, 19, 20)]
    xs = [0.38, 0.46, 0.54, 0.62]

    # Start from a compact folded hand.
    for finger_index, (mcp, pip, dip, tip) in enumerate(groups):
        x = xs[finger_index]
        landmarks[mcp] = [x, 0.62, 0.0]
        landmarks[pip] = [x, 0.58, 0.0]
        landmarks[dip] = [x + 0.025, 0.63, 0.0]
        landmarks[tip] = [x + 0.04, 0.67, 0.0]

    landmarks[1] = [0.43, 0.72, 0.0]
    landmarks[2] = [0.39, 0.68, 0.0]
    landmarks[3] = [0.42, 0.66, 0.0]
    landmarks[4] = [0.46, 0.65, 0.0]

    def extend_finger(finger_index: int) -> None:
        mcp, pip, dip, tip = groups[finger_index]
        x = xs[finger_index]
        landmarks[pip] = [x, 0.50, 0.0]
        landmarks[dip] = [x, 0.38, 0.0]
        landmarks[tip] = [x, 0.24, 0.0]

    def extend_thumb() -> None:
        landmarks[1] = [0.43, 0.72, 0.0]
        landmarks[2] = [0.35, 0.66, 0.0]
        landmarks[3] = [0.27, 0.60, 0.0]
        landmarks[4] = [0.17, 0.54, 0.0]

    if 1 <= slot <= 5:
        for finger_index in range(min(slot, 4)):
            extend_finger(finger_index)
        if slot == 5:
            extend_thumb()
    elif slot == 6:
        extend_finger(3)
        extend_thumb()
    elif slot == 7:
        # Thumb/index/middle converge into the characteristic Chinese-seven pinch.
        landmarks[1] = [0.43, 0.72, 0.0]
        landmarks[2] = [0.37, 0.63, 0.0]
        landmarks[3] = [0.34, 0.52, 0.0]
        landmarks[4] = [0.41, 0.43, 0.0]

        landmarks[5] = [0.38, 0.62, 0.0]
        landmarks[6] = [0.36, 0.51, 0.0]
        landmarks[7] = [0.37, 0.44, 0.0]
        landmarks[8] = [0.42, 0.41, 0.0]

        landmarks[9] = [0.46, 0.62, 0.0]
        landmarks[10] = [0.47, 0.51, 0.0]
        landmarks[11] = [0.46, 0.44, 0.0]
        landmarks[12] = [0.43, 0.41, 0.0]
    elif slot == 8:
        extend_finger(0)
        extend_thumb()
    elif slot == 9:
        # Raised index with a pronounced hook; the other digits stay folded.
        landmarks[5] = [0.38, 0.62, 0.0]
        landmarks[6] = [0.36, 0.49, 0.0]
        landmarks[7] = [0.42, 0.41, 0.0]
        landmarks[8] = [0.50, 0.47, 0.0]
    elif slot == 10:
        pass
    else:
        raise ValueError(f"Unsupported synthetic slot {slot}")

    return landmarks


class SlotSelectorTests(unittest.TestCase):
    def test_clean_1_to_10_shapes(self) -> None:
        for expected in range(1, 11):
            with self.subTest(slot=expected):
                result = classify_slot_1_to_10(synthetic_hand(expected))
                self.assertEqual(result.slot, expected)
                self.assertGreaterEqual(result.confidence, 0.80)

    def test_8_is_not_confused_with_1(self) -> None:
        result = classify_slot_1_to_10(synthetic_hand(8))
        self.assertEqual(result.slot, 8)
        self.assertGreaterEqual(result.confidence, 0.80)

    def test_9_is_not_confused_with_fist_10(self) -> None:
        nine = classify_slot_1_to_10(synthetic_hand(9))
        ten = classify_slot_1_to_10(synthetic_hand(10))
        self.assertEqual(nine.slot, 9)
        self.assertEqual(ten.slot, 10)
        self.assertGreaterEqual(nine.confidence, 0.80)
        self.assertGreaterEqual(ten.confidence, 0.80)

    def test_6_is_not_confused_with_fist(self) -> None:
        result = classify_slot_1_to_10(synthetic_hand(6))
        self.assertEqual(result.slot, 6)
        self.assertGreaterEqual(result.confidence, 0.80)

    def test_invalid_landmarks_return_unknown(self) -> None:
        result = classify_slot_1_to_10([])
        self.assertEqual(result.slot, 0)
        self.assertEqual(result.confidence, 0.0)

    def test_non_finite_landmarks_return_unknown(self) -> None:
        hand = synthetic_hand(5)
        hand[8][0] = float("nan")
        result = classify_slot_1_to_10(hand)
        self.assertEqual(result.slot, 0)
        self.assertEqual(result.confidence, 0.0)

    def test_legacy_entry_point_uses_full_classifier(self) -> None:
        # VisionEngine currently imports this compatibility name. It must not drop
        # the newly-supported slot IDs while mixed-version components still exist.
        result = classify_slot_1_to_5(synthetic_hand(10))
        self.assertEqual(result.slot, 10)

    def test_stabilizer_requires_hold_and_keeps_last_slot(self) -> None:
        stabilizer = SlotStabilizer(hold_ms=150, min_confidence=0.80)
        self.assertEqual(stabilizer.update(3, 0.95, 1000), 0)
        self.assertEqual(stabilizer.update(3, 0.95, 1149), 0)
        self.assertEqual(stabilizer.update(3, 0.95, 1150), 3)
        self.assertEqual(stabilizer.update(0, 0.0, 1200), 3)
        self.assertEqual(stabilizer.update(10, 0.95, 1210), 3)
        self.assertEqual(stabilizer.update(10, 0.95, 1360), 10)

    def test_stabilizer_rejects_out_of_range_slot(self) -> None:
        stabilizer = SlotStabilizer(hold_ms=0, min_confidence=0.80)
        self.assertEqual(stabilizer.update(11, 1.0, 1000), 0)


if __name__ == "__main__":
    unittest.main()
