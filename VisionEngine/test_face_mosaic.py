import unittest

import numpy as np

from face_mosaic import FaceMosaicProcessor, apply_face_mosaic


class _FakeCascade:
    def __init__(self, boxes):
        self.boxes = boxes
        self.calls = 0

    def empty(self):
        return False

    def detectMultiScale(self, *_args, **_kwargs):
        self.calls += 1
        return np.asarray(self.boxes, dtype=np.int32)


class FaceMosaicTests(unittest.TestCase):
    def test_mosaic_changes_only_face_roi(self):
        y, x = np.mgrid[0:72, 0:96]
        frame = np.stack(((x * 3) % 255, (y * 5) % 255, ((x + y) * 7) % 255), axis=-1).astype(np.uint8)
        output = apply_face_mosaic(frame, [(24, 18, 36, 30)], block_size=8, margin_ratio=0.0)

        outside = np.ones(frame.shape[:2], dtype=bool)
        outside[18:48, 24:60] = False
        self.assertTrue(np.array_equal(output[outside], frame[outside]))
        self.assertFalse(np.array_equal(output[18:48, 24:60], frame[18:48, 24:60]))
        self.assertTrue(np.array_equal(frame[0, 0], np.array([0, 0, 0], dtype=np.uint8)))

    def test_detector_is_low_frequency_and_reuses_boxes(self):
        detector = _FakeCascade([(10, 8, 28, 28)])
        processor = FaceMosaicProcessor(
            cascade_path=None,
            detect_every_n=4,
            detection_scale=1.0,
            block_size=7,
            margin_ratio=0.0,
            detector=detector,
        )
        frame = np.arange(80 * 80 * 3, dtype=np.uint8).reshape(80, 80, 3)

        first = processor.process_rgb(frame)
        second = processor.process_rgb(frame)
        third = processor.process_rgb(frame)
        fourth = processor.process_rgb(frame)

        self.assertEqual(detector.calls, 1)
        self.assertTrue(np.array_equal(first, second))
        self.assertTrue(np.array_equal(second, third))
        self.assertTrue(np.array_equal(third, fourth))
        processor.process_rgb(frame)
        self.assertEqual(detector.calls, 2)


if __name__ == "__main__":
    unittest.main()
