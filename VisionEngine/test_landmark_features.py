from __future__ import annotations

import unittest

import numpy as np

from landmark_features import extract_landmark_features


def sample_hand() -> list[list[float]]:
    points = [[0.5, 0.8, 0.0] for _ in range(21)]
    points[0] = [0.50, 0.84, 0.00]
    points[1] = [0.43, 0.75, -0.01]
    points[2] = [0.39, 0.69, -0.01]
    points[3] = [0.34, 0.64, -0.02]
    points[4] = [0.29, 0.60, -0.02]
    for x, group in zip([0.40, 0.47, 0.54, 0.61],
                        [(5, 6, 7, 8), (9, 10, 11, 12),
                         (13, 14, 15, 16), (17, 18, 19, 20)]):
        mcp, pip, dip, tip = group
        points[mcp] = [x, 0.63, 0.00]
        points[pip] = [x, 0.51, -0.01]
        points[dip] = [x, 0.40, -0.02]
        points[tip] = [x, 0.29, -0.03]
    return points


class LandmarkFeatureTests(unittest.TestCase):
    def test_feature_shape_is_fixed(self) -> None:
        features = extract_landmark_features(sample_hand())
        self.assertEqual(features.values.shape, (81,))
        self.assertEqual(features.values.dtype, np.float32)
        self.assertFalse(features.used_world_landmarks)

    def test_world_translation_and_scale_do_not_change_features(self) -> None:
        normalized = np.asarray(sample_hand(), dtype=np.float32)
        world = (normalized - np.asarray([0.5, 0.5, 0.0], dtype=np.float32)) * 0.18
        transformed = world * 3.7 + np.asarray([4.0, -2.0, 7.0], dtype=np.float32)

        first = extract_landmark_features(normalized, world)
        second = extract_landmark_features(normalized, transformed)
        self.assertTrue(first.used_world_landmarks)
        self.assertTrue(second.used_world_landmarks)
        # MediaPipe and the shipping model use float32. Large synthetic offsets
        # deliberately lose a few 1e-5 in angle calculations; 1e-4 is still far
        # below any meaningful hand-motion variation and verifies the invariant.
        np.testing.assert_allclose(first.values, second.values, atol=1.0e-4, rtol=1.0e-4)

    def test_bad_world_landmarks_fall_back_to_normalized(self) -> None:
        normalized = sample_hand()
        collapsed_world = [[0.0, 0.0, 0.0] for _ in range(21)]
        fallback = extract_landmark_features(normalized, collapsed_world)
        baseline = extract_landmark_features(normalized)
        self.assertFalse(fallback.used_world_landmarks)
        np.testing.assert_allclose(fallback.values, baseline.values, atol=1.0e-6)

    def test_screen_direction_features_preserve_pointing_direction(self) -> None:
        right = np.asarray(sample_hand(), dtype=np.float32)
        left = right.copy()
        right[8, 0] = right[5, 0] + 0.24
        right[8, 1] = right[5, 1]
        left[8, 0] = left[5, 0] - 0.24
        left[8, 1] = left[5, 1]

        right_features = extract_landmark_features(right).values
        left_features = extract_landmark_features(left).values
        # Layout: 63 local + 5 straightness + 5 radial + thumb dx/dy + index dx/dy ...
        index_dx = 75
        self.assertGreater(right_features[index_dx], 0.0)
        self.assertLess(left_features[index_dx], 0.0)

    def test_invalid_landmarks_are_rejected(self) -> None:
        with self.assertRaises(ValueError):
            extract_landmark_features([[0.0, 0.0, 0.0] for _ in range(20)])


if __name__ == "__main__":
    unittest.main()
