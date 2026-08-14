from __future__ import annotations

import unittest

from continuous_motion import HeightMotionFilter, OneEuroFilter


class ContinuousMotionTests(unittest.TestCase):
    def test_first_sample_has_zero_startup_lag(self) -> None:
        filt = OneEuroFilter()
        self.assertAlmostEqual(filt.update(0.73, 1000), 0.73, places=6)

    def test_constant_input_converges_and_stays_stable(self) -> None:
        filt = HeightMotionFilter()
        value = filt.update(0.2, 1000)
        for i in range(1, 31):
            value = filt.update(0.8, 1000 + i * 33)
        self.assertGreater(value, 0.79)
        steady = filt.update(0.8, 2050)
        self.assertAlmostEqual(steady, value, delta=0.01)

    def test_fast_motion_tracks_faster_than_slow_filter(self) -> None:
        adaptive = OneEuroFilter(min_cutoff_hz=2.5, beta=1.2)
        fixed = OneEuroFilter(min_cutoff_hz=2.5, beta=0.0)
        adaptive.update(0.1, 1000)
        fixed.update(0.1, 1000)
        adaptive_value = adaptive.update(0.9, 1033)
        fixed_value = fixed.update(0.9, 1033)
        self.assertGreater(adaptive_value, fixed_value)

    def test_reset_drops_previous_motion_history(self) -> None:
        filt = HeightMotionFilter()
        filt.update(0.1, 1000)
        filt.update(0.9, 1033)
        filt.reset()
        self.assertAlmostEqual(filt.update(0.4, 2000), 0.4, places=6)

    def test_height_is_clamped(self) -> None:
        filt = HeightMotionFilter()
        self.assertEqual(filt.update(-1.0, 1000), 0.0)
        filt.reset()
        self.assertEqual(filt.update(2.0, 2000), 1.0)


if __name__ == "__main__":
    unittest.main()
