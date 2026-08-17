#pragma once

#include <JuceHeader.h>

namespace gr
{
enum class ControlGesture : int
{
    unknown = 0,
    openPalm = 1,
    closedFist = 2,
    victory = 3,
    thumbUp = 4,
    thumbDown = 5,
    thumbRight = 6,
    thumbLeft = 7,

    // Source compatibility for the old palette/runtime. Numeric values are kept
    // so old Point Right/Left mappings migrate to the new thumb directions.
    pointRight = thumbRight,
    pointLeft = thumbLeft
};

inline juce::String controlGestureToString (ControlGesture gesture)
{
    switch (gesture)
    {
        case ControlGesture::openPalm:   return "Open Palm";
        case ControlGesture::closedFist: return "Closed Fist";
        case ControlGesture::victory:    return "Victory";
        case ControlGesture::thumbUp:    return "Thumb Up";
        case ControlGesture::thumbDown:  return "Thumb Down";
        case ControlGesture::thumbRight: return "Thumb Right";
        case ControlGesture::thumbLeft:  return "Thumb Left";
        default:                         return "Unknown";
    }
}

inline juce::String controlGestureToShortLabel (ControlGesture gesture)
{
    switch (gesture)
    {
        case ControlGesture::openPalm:   return "PALM";
        case ControlGesture::closedFist: return "FIST";
        case ControlGesture::victory:    return "VICTORY";
        case ControlGesture::thumbUp:    return "UP";
        case ControlGesture::thumbDown:  return "DOWN";
        case ControlGesture::thumbRight: return "RIGHT";
        case ControlGesture::thumbLeft:  return "LEFT";
        default:                         return "?";
    }
}

inline juce::String controlGestureToEmoji (ControlGesture gesture)
{
    switch (gesture)
    {
        case ControlGesture::openPalm:   return juce::CharPointer_UTF8 ("\xE2\x9C\x8B");
        case ControlGesture::closedFist: return juce::CharPointer_UTF8 ("\xE2\x9C\x8A");
        case ControlGesture::victory:    return juce::CharPointer_UTF8 ("\xE2\x9C\x8C\xEF\xB8\x8F");
        case ControlGesture::thumbUp:    return juce::CharPointer_UTF8 ("\xF0\x9F\x91\x8D");
        case ControlGesture::thumbDown:  return juce::CharPointer_UTF8 ("\xF0\x9F\x91\x8E");
        case ControlGesture::thumbRight: return juce::CharPointer_UTF8 ("\xE2\x86\x92");
        case ControlGesture::thumbLeft:  return juce::CharPointer_UTF8 ("\xE2\x86\x90");
        default:                         return juce::CharPointer_UTF8 ("?");
    }
}

inline ControlGesture controlGestureFromString (const juce::String& text)
{
    if (text.equalsIgnoreCase ("Open Palm") || text.equalsIgnoreCase ("Open_Palm"))
        return ControlGesture::openPalm;
    if (text.equalsIgnoreCase ("Closed Fist") || text.equalsIgnoreCase ("Closed_Fist"))
        return ControlGesture::closedFist;
    if (text.equalsIgnoreCase ("Victory"))
        return ControlGesture::victory;
    if (text.equalsIgnoreCase ("Thumb Up") || text.equalsIgnoreCase ("Thumb_Up"))
        return ControlGesture::thumbUp;
    if (text.equalsIgnoreCase ("Thumb Down") || text.equalsIgnoreCase ("Thumb_Down"))
        return ControlGesture::thumbDown;
    if (text.equalsIgnoreCase ("Thumb Right") || text.equalsIgnoreCase ("Thumb_Right")
        || text.equalsIgnoreCase ("Point Right") || text.equalsIgnoreCase ("Point_Right"))
        return ControlGesture::thumbRight;
    if (text.equalsIgnoreCase ("Thumb Left") || text.equalsIgnoreCase ("Thumb_Left")
        || text.equalsIgnoreCase ("Point Left") || text.equalsIgnoreCase ("Point_Left"))
        return ControlGesture::thumbLeft;
    return ControlGesture::unknown;
}
}
