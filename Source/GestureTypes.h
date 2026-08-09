#pragma once
#include <JuceHeader.h>
#include <array>

namespace gr
{
enum class Gesture : int
{
    unknown = 0,
    openPalm,
    closedFist
};

inline Gesture gestureFromString (const juce::String& s)
{
    if (s == "Open_Palm")   return Gesture::openPalm;
    if (s == "Closed_Fist") return Gesture::closedFist;
    return Gesture::unknown;
}

inline juce::String gestureToString (Gesture g)
{
    switch (g)
    {
        case Gesture::openPalm:   return "OPEN PALM";
        case Gesture::closedFist: return "CLOSED FIST";
        default:                  return "NO GESTURE";
    }
}

struct HandPoint
{
    float x = 0.5f;
    float y = 0.5f;
    float z = 0.0f;
};

struct VisionSnapshot
{
    int protocol = 1;
    int64_t sequence = 0;
    int64_t timestampMs = 0;
    int64_t receivedAtMs = 0;
    bool handPresent = false;
    Gesture rawGesture = Gesture::unknown;
    Gesture stableGesture = Gesture::unknown;
    float confidence = 0.0f;
    std::array<HandPoint, 21> landmarks {};
};
}
