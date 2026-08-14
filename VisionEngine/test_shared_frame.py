from __future__ import annotations

import mmap
import sys
import unittest
import uuid

from shared_frame import HEADER_SIZE, SHM_SIZE, SharedFrameWriter, unpack_header

if not sys.platform.startswith("win"):
    from multiprocessing import shared_memory


class MappingReader:
    def __init__(self, name: str):
        self.mapping = None
        self.shared = None
        if sys.platform.startswith("win"):
            self.mapping = mmap.mmap(-1, SHM_SIZE, tagname=name, access=mmap.ACCESS_READ)
        else:
            self.shared = shared_memory.SharedMemory(name=name, create=False)

    @property
    def buffer(self):
        if self.mapping is not None:
            return self.mapping
        assert self.shared is not None
        return self.shared.buf

    def close(self) -> None:
        if self.mapping is not None:
            self.mapping.close()
            self.mapping = None
        if self.shared is not None:
            self.shared.close()
            self.shared = None


class SharedFrameTests(unittest.TestCase):
    def test_publish_writes_complete_even_sequence_and_rgb_payload(self) -> None:
        name = "GestureRackFrameTest_" + uuid.uuid4().hex
        writer = SharedFrameWriter(name)
        reader = None
        try:
            pixels = bytearray([
                255, 0, 0,   0, 255, 0,
                0, 0, 255,   255, 255, 255,
            ])
            self.assertTrue(writer.publish_rgb(pixels, 2, 2, 6, 12345))
            reader = MappingReader(name)
            header = unpack_header(reader.buffer)
            self.assertTrue(header.valid)
            self.assertEqual(header.width, 2)
            self.assertEqual(header.height, 2)
            self.assertEqual(header.stride, 6)
            self.assertEqual(header.timestamp_ms, 12345)
            self.assertEqual(header.payload_bytes, len(pixels))
            self.assertEqual(bytes(reader.buffer[HEADER_SIZE:HEADER_SIZE + len(pixels)]), bytes(pixels))
        finally:
            if reader is not None:
                reader.close()
            writer.close(unlink=True)

    def test_second_writer_attaches_to_existing_mapping(self) -> None:
        name = "GestureRackFrameRestartTest_" + uuid.uuid4().hex
        first = SharedFrameWriter(name)
        second = None
        reader = None
        try:
            self.assertTrue(first.publish_rgb(bytearray([1, 2, 3]), 1, 1, 3, 1))
            second = SharedFrameWriter(name)
            self.assertTrue(second.publish_rgb(bytearray([9, 8, 7]), 1, 1, 3, 2))
            reader = MappingReader(name)
            header = unpack_header(reader.buffer)
            self.assertTrue(header.valid)
            self.assertEqual(header.timestamp_ms, 2)
            self.assertEqual(bytes(reader.buffer[HEADER_SIZE:HEADER_SIZE + 3]), b"\x09\x08\x07")
        finally:
            if reader is not None:
                reader.close()
            if second is not None:
                second.close(unlink=False)
            first.close(unlink=True)


if __name__ == "__main__":
    unittest.main()
