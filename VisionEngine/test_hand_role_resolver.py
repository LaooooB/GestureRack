from __future__ import annotations

import unittest

from hand_role_resolver import DetectedHand, HandRoleResolver


def make_hand(x: float, label: str, confidence: float = 0.96) -> DetectedHand:
    landmarks = [[x, 0.6, 0.0] for _ in range(21)]
    landmarks[0] = [x, 0.8, 0.0]
    return DetectedHand(
        landmarks=landmarks,
        handedness_label=label,
        handedness_confidence=confidence,
    )


class HandRoleResolverTests(unittest.TestCase):
    def test_high_confidence_handedness_beats_stale_single_hand_track(self) -> None:
        resolver = HandRoleResolver()
        previous_left = make_hand(0.25, "Left", 0.99)
        left, right = resolver.resolve([previous_left], 1000)
        self.assertIs(left, previous_left)
        self.assertIsNone(right)

        # New hand appears close to the old left wrist, but handedness is strongly
        # Right. The resolver must correct immediately instead of inheriting role.
        new_right = make_hand(0.27, "Right", 0.99)
        left, right = resolver.resolve([new_right], 1100)
        self.assertIsNone(left)
        self.assertIs(right, new_right)

    def test_two_strong_handedness_labels_are_authoritative(self) -> None:
        resolver = HandRoleResolver()
        # Deliberately reverse screen positions. Physical role is not screen X.
        physical_left = make_hand(0.80, "Left", 0.98)
        physical_right = make_hand(0.20, "Right", 0.98)
        left, right = resolver.resolve([physical_right, physical_left], 2000)
        self.assertIs(left, physical_left)
        self.assertIs(right, physical_right)

    def test_swap_calibration_mapping_flips_labels(self) -> None:
        resolver = HandRoleResolver(swap_handedness=True)
        reported_left = make_hand(0.65, "Left", 0.99)
        left, right = resolver.resolve([reported_left], 3000)
        self.assertIsNone(left)
        self.assertIs(right, reported_left)

    def test_runtime_swap_resets_old_trajectory(self) -> None:
        resolver = HandRoleResolver()
        hand = make_hand(0.30, "Left", 0.99)
        resolver.resolve([hand], 4000)
        self.assertIsNotNone(resolver.left_track.wrist)
        resolver.set_swap_handedness(True)
        self.assertIsNone(resolver.left_track.wrist)
        self.assertIsNone(resolver.right_track.wrist)

    def test_low_confidence_can_fall_back_to_trajectory(self) -> None:
        resolver = HandRoleResolver(handedness_lock_confidence=0.75)
        left_seed = make_hand(0.25, "Left", 0.99)
        right_seed = make_hand(0.75, "Right", 0.99)
        resolver.resolve([left_seed, right_seed], 5000)

        low_a = make_hand(0.28, "Right", 0.40)
        low_b = make_hand(0.72, "Left", 0.40)
        left, right = resolver.resolve([low_a, low_b], 5100)
        self.assertIs(left, low_a)
        self.assertIs(right, low_b)


if __name__ == "__main__":
    unittest.main()
