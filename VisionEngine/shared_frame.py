from __future__ import annotations

from dataclasses import dataclass
from multiprocessing import shared_memory
import struct
from typing import Optional


SHM_NAME = "GestureRackVisionFrameV1"
MAGIC = 0x47525646  # ASCII GRVF
VERSION = 1
MAX_WIDTH = 1920
MAX_HEIGHT = 1080
CHANNELS = 3
HEADER_FORMAT = "<IIQIIIIQII"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
MAX_PAYLOAD_BYTES = MAX_WIDTH * MAX_HEIGHT * CHANNELS
SHM_SIZE = HEADER_SIZE + MAX_PAYLOAD_BYTES
_SEQUENCE_OFFSET = 8


@dataclass(frozen=True)
class FrameHeader:
    magic: int
    version: int
    sequence: int
    width: int
    height: int
    stride: int
    channels: int
    timestamp_ms: int
    payload_bytes: int
    flags: int

    @property
    def valid(self) -> bool:
        return (
            self.magic == MAGIC
            and self.version == VERSION
            and self.sequence > 0
            and self.sequence % 2 == 0
            and 0 < self.width <= MAX_WIDTH
            and 0 < self.height <= MAX_HEIGHT
            and self.channels == CHANNELS
            and self.stride >= self.width * self.channels
            and self.payload_bytes == self.stride * self.height
            and self.payload_bytes <= MAX_PAYLOAD_BYTES
        )


def unpack_header(buffer) -> FrameHeader:
    return FrameHeader(*struct.unpack_from(HEADER_FORMAT, buffer, 0))


class SharedFrameWriter:
    """Publish the exact MediaPipe callback image through named shared memory.

    A simple sequence lock is used: odd sequence means write in progress, even
    means complete. Readers copy the payload only when the sequence is even and
    unchanged before/after the copy, so the VST3 never renders a torn frame.
    """

    def __init__(self, name: str = SHM_NAME):
        self.name = name
        self.shm = self._create_or_replace(name)
        self.sequence = 0
        self._write_header(0, 0, 0, 0, 0, 0)

    @staticmethod
    def _create_or_replace(name: str) -> shared_memory.SharedMemory:
        try:
            return shared_memory.SharedMemory(name=name, create=True, size=SHM_SIZE)
        except FileExistsError:
            stale: Optional[shared_memory.SharedMemory] = None
            try:
                stale = shared_memory.SharedMemory(name=name, create=False)
                stale.close()
                stale.unlink()
            except FileNotFoundError:
                pass
            finally:
                if stale is not None:
                    try:
                        stale.close()
                    except Exception:
                        pass
            return shared_memory.SharedMemory(name=name, create=True, size=SHM_SIZE)

    def _write_header(self, sequence: int, width: int, height: int, stride: int,
                      timestamp_ms: int, payload_bytes: int) -> None:
        struct.pack_into(
            HEADER_FORMAT,
            self.shm.buf,
            0,
            MAGIC,
            VERSION,
            int(sequence),
            int(width),
            int(height),
            int(stride),
            CHANNELS,
            int(timestamp_ms),
            int(payload_bytes),
            0,
        )

    def publish_rgb(self, rgb, width: int, height: int, stride: int,
                    timestamp_ms: int) -> bool:
        width = int(width)
        height = int(height)
        stride = int(stride)
        payload_bytes = stride * height
        if (width <= 0 or height <= 0 or width > MAX_WIDTH or height > MAX_HEIGHT
                or stride < width * CHANNELS or payload_bytes > MAX_PAYLOAD_BYTES):
            return False

        view = memoryview(rgb)
        if view.nbytes < payload_bytes:
            return False

        begin_sequence = self.sequence + 1
        if begin_sequence % 2 == 0:
            begin_sequence += 1
        complete_sequence = begin_sequence + 1

        self._write_header(begin_sequence, width, height, stride, timestamp_ms, payload_bytes)
        self.shm.buf[HEADER_SIZE:HEADER_SIZE + payload_bytes] = view[:payload_bytes]
        self._write_header(complete_sequence, width, height, stride, timestamp_ms, payload_bytes)
        self.sequence = complete_sequence
        return True

    def close(self, unlink: bool = True) -> None:
        try:
            self.shm.close()
        finally:
            if unlink:
                try:
                    self.shm.unlink()
                except FileNotFoundError:
                    pass
