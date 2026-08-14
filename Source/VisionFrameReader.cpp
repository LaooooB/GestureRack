#include "VisionFrameReader.h"

#include <cstring>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX 1
 #endif
 #include <windows.h>
#endif

namespace gr
{
namespace
{
constexpr uint32_t frameMagic = 0x47525646u; // GRVF
constexpr uint32_t frameVersion = 1u;
constexpr uint32_t maxWidth = 1920u;
constexpr uint32_t maxHeight = 1080u;
constexpr uint32_t rgbChannels = 3u;
constexpr size_t maxPayloadBytes = static_cast<size_t> (maxWidth) * maxHeight * rgbChannels;

#pragma pack(push, 1)
struct SharedFrameHeader
{
    uint32_t magic = 0;
    uint32_t version = 0;
    uint64_t sequence = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t channels = 0;
    uint64_t timestampMs = 0;
    uint32_t payloadBytes = 0;
    uint32_t flags = 0;
};
#pragma pack(pop)

static_assert (sizeof (SharedFrameHeader) == 48, "Shared camera frame header must match Python struct");

bool headerIsValid (const SharedFrameHeader& header) noexcept
{
    if (header.magic != frameMagic || header.version != frameVersion)
        return false;
    if (header.sequence == 0 || (header.sequence & 1u) != 0u)
        return false;
    if (header.width == 0 || header.height == 0
        || header.width > maxWidth || header.height > maxHeight)
        return false;
    if (header.channels != rgbChannels || header.stride < header.width * rgbChannels)
        return false;

    const auto expected = static_cast<uint64_t> (header.stride) * header.height;
    return expected == header.payloadBytes && expected <= maxPayloadBytes;
}
}

VisionFrameReader::~VisionFrameReader()
{
    close();
}

bool VisionFrameReader::ensureOpen()
{
#if JUCE_WINDOWS
    if (mappedBytes != nullptr)
        return true;

    auto handle = ::OpenFileMappingA (FILE_MAP_READ, FALSE, sharedMemoryName);
    if (handle == nullptr)
        return false;

    auto* view = static_cast<const uint8_t*> (
        ::MapViewOfFile (handle, FILE_MAP_READ, 0, 0, 0));
    if (view == nullptr)
    {
        ::CloseHandle (handle);
        return false;
    }

    mappingHandle = handle;
    mappedBytes = view;
    lastSequence = 0;
    return true;
#else
    return false;
#endif
}

bool VisionFrameReader::readLatest (VisionCameraFrame& destination)
{
    if (! ensureOpen())
        return false;

#if JUCE_WINDOWS
    SharedFrameHeader first {};
    std::memcpy (&first, mappedBytes, sizeof (first));
    ::MemoryBarrier();

    if (! headerIsValid (first) || first.sequence == lastSequence)
        return false;

    scratch.resize (first.payloadBytes);
    std::memcpy (scratch.data(), mappedBytes + sizeof (SharedFrameHeader), first.payloadBytes);
    ::MemoryBarrier();

    SharedFrameHeader second {};
    std::memcpy (&second, mappedBytes, sizeof (second));
    ::MemoryBarrier();

    // Sequence equality is the cross-process seqlock. Re-check all geometry too,
    // because Python may already have started a newer frame while we copied.
    if (! headerIsValid (second)
        || first.sequence != second.sequence
        || first.width != second.width
        || first.height != second.height
        || first.stride != second.stride
        || first.payloadBytes != second.payloadBytes)
        return false;

    juce::Image image (juce::Image::RGB,
                       static_cast<int> (second.width),
                       static_cast<int> (second.height),
                       false);
    juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::writeOnly);

    for (uint32_t y = 0; y < second.height; ++y)
    {
        const auto* source = scratch.data() + static_cast<size_t> (y) * second.stride;
        auto* destinationLine = bitmap.getLinePointer (static_cast<int> (y));

        for (uint32_t x = 0; x < second.width; ++x)
        {
            const auto* rgb = source + static_cast<size_t> (x) * rgbChannels;
            auto* pixel = reinterpret_cast<juce::PixelRGB*> (
                destinationLine + static_cast<size_t> (x) * bitmap.pixelStride);
            pixel->setARGB (0xff, rgb[0], rgb[1], rgb[2]);
        }
    }

    destination.image = std::move (image);
    destination.sequence = second.sequence;
    destination.timestampMs = static_cast<int64_t> (second.timestampMs);
    lastSequence = second.sequence;
    return true;
#else
    juce::ignoreUnused (destination);
    return false;
#endif
}

void VisionFrameReader::close()
{
#if JUCE_WINDOWS
    if (mappedBytes != nullptr)
    {
        ::UnmapViewOfFile (mappedBytes);
        mappedBytes = nullptr;
    }

    if (mappingHandle != nullptr)
    {
        ::CloseHandle (static_cast<HANDLE> (mappingHandle));
        mappingHandle = nullptr;
    }
#endif

    lastSequence = 0;
    scratch.clear();
}
}
