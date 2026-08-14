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
    float rawHeight = 0.5f;
    // Height is the dedicated One Euro filtered continuous-control signal.
    float height = 0.5f;

    std::array<HandPoint, 21> landmarks {};
};

struct DualHandVisionSnapshot
{
    int protocol = 2;
    int64_t sequence = 0;
    int64_t timestampMs = 0;
    int64_t receivedAtMs = 0;
    juce::String sessionId;

    float captureFps = 0.0f;
    float visionFps = 0.0f;
    float captureToResultMs = 0.0f;
    float frameAgeAtSubmitMs = 0.0f;
    float inferenceMs = 0.0f;
    juce::String cameraBackend;

    bool swapHandedness = false;
    bool handCalibrationActive = false;
    int handCalibrationSamples = 0;
    float handCalibrationConfidence = 0.0f;
    juce::String handCalibrationStatus;
    juce::String handRoleSource;

    HandSnapshot left;
    HandSnapshot right;
};
}
