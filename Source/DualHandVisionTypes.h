#pragma once

#include <JuceHeader.h>
#include <array>
#include <cstdint>
#include "GestureTypes.h"
#include "ControlGesture.h"

namespace gr
{
struct HandSnapshot
{
    bool present = false;
    float handednessConfidence = 0.0f;

    int rawSlot = 0;
    int stableSlot = 0;

    ControlGesture rawGesture = ControlGesture::unknown;
    ControlGesture stableGesture = ControlGesture::unknown;
    float confidence = 0.0f;

    float palmX = 0.5f;
    float palmY = 0.5f;
    float palmZ = 0.0f;
    float height = 0.5f;

    std::array<HandPoint, 21> landmarks {};
};

struct DualHandVisionSnapshot
{
    int protocol = 2;
    int64_t sequence = 0;
    int64_t timestampMs = 0;
    int64_t receivedAtMs = 0;
    HandSnapshot left;
    HandSnapshot right;
};
}
