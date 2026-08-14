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
    // session_id identifies a single VisionEngine process. It lets the plugin
    // distinguish a genuine out-of-order packet from a sidecar restart (which
    // resets seq to 1) instead of treating the restart's seq=1 as a rollback.
    juce::String sessionId;
    float captureFps = 0.0f;
    float visionFps = 0.0f;
    float captureToResultMs = 0.0f;
    float frameAgeAtSubmitMs = 0.0f;
    float inferenceMs = 0.0f;
    juce::String cameraBackend;
    HandSnapshot left;
    HandSnapshot right;
};
}
