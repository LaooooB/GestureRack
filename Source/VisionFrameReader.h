#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <vector>

namespace gr
{
struct VisionCameraFrame
{
    juce::Image image;
    uint64_t sequence = 0;
    int64_t timestampMs = 0;

    bool isValid() const noexcept { return image.isValid() && sequence > 0; }
};

class VisionFrameReader final
{
public:
    static constexpr const char* sharedMemoryName = "GestureRackVisionFrameV1";

    VisionFrameReader() = default;
    ~VisionFrameReader();

    // Returns true only when a newer complete frame was copied.
    bool readLatest (VisionCameraFrame& destination);
    void close();

private:
    bool ensureOpen();

    void* mappingHandle = nullptr;
    const uint8_t* mappedBytes = nullptr;
    uint64_t lastSequence = 0;
    std::vector<uint8_t> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisionFrameReader)
};
}
