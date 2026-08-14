from __future__ import annotations

from dataclasses import dataclass
import mmap
import struct
import sys

if not sys.platform.startswith("win"):
    from multiprocessing import shared_memory


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

    Windows uses stdlib mmap(tagname=...) directly, which maps to a Win32 named
    file mapping and avoids multiprocessing/resource-tracker subprocesses in the
    frozen sidecar. Other platforms use multiprocessing.shared_memory as a
    development fallback; the current VST3 reader is Windows-first.
    """

    def __init__(self, name: str = SHM_NAME):
        self.name = name
        self.sequence = 0
        self._windows_mapping = None
        self._shared_memory = None

        if sys.platform.startswith("win"):
            # If the VST3 still holds a mapping from a previous sidecar process,
            # the same tag opens that mapping. This makes sidecar restart safe.
            self._windows_mapping = mmap.mmap(
                -1, SHM_SIZE, tagname=name, access=mmap.ACCESS_WRITE)
        else:
            try:
                self._shared_memory = shared_memory.SharedMemory(
                    name=name, create=True, size=SHM_SIZE)
            except FileExistsError:
                existing = shared_memory.SharedMemory(name=name, create=False)
                if existing.size < SHM_SIZE:
                    existing.close()
                    try:
                        existing.unlink()
                    except FileNotFoundError:
                        pass
                    existing = shared_memory.SharedMemory(
                        name=name, create=True, size=SHM_SIZE)
                self._shared_memory = existing

        self._write_header(0, 0, 0, 0, 0, 0)

    def _buffer(self):
        if self._windows_mapping is not None:
            return self._windows_mapping
        assert self._shared_memory is not None
        return self._shared_memory.buf

    def _write_header(self, sequence: int, width: int, height: int, stride: int,
                      timestamp_ms: int, payload_bytes: int) -> None:
        struct.pack_into(
            HEADER_FORMAT,
            self._buffer(),
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
        if view.ndim != 1 or view.format != "B":
            view = view.cast("B")
        if view.nbytes < payload_bytes:
            return False

        begin_sequence = self.sequence + 1
        if begin_sequence % 2 == 0:
            begin_sequence += 1
        complete_sequence = begin_sequence + 1

        target = self._buffer()
        self._write_header(begin_sequence, width, height, stride, timestamp_ms, payload_bytes)
        target[HEADER_SIZE:HEADER_SIZE + payload_bytes] = view[:payload_bytes]
        self._write_header(complete_sequence, width, height, stride, timestamp_ms, payload_bytes)
        self.sequence = complete_sequence
        return True

    def close(self, unlink: bool = True) -> None:
        if self._windows_mapping is not None:
            self._windows_mapping.close()
            self._windows_mapping = None
            return

        if self._shared_memory is None:
            return

        try:
            self._shared_memory.close()
        finally:
            if unlink:
                try:
                    self._shared_memory.unlink()
                except FileNotFoundError:
                    pass
            self._shared_memory = None
