#pragma once

#include <JuceHeader.h>

namespace gr
{
enum class ControlGesture : int
{
    unknown = 0,
    openPalm,
    closedFist,
    victory,
    thumbUp,
    thumbDown,
    pointRight,
    pointLeft
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
        case ControlGesture::pointRight: return "Point Right";
        case ControlGesture::pointLeft:  return "Point Left";
        default:                         return "Unknown";
    }
}

inline juce::String controlGestureToEmoji (ControlGesture gesture)
{
    switch (gesture)
    {
        case ControlGesture::openPalm:   return juce::CharPointer_UTF8 ("\xE2\x9C\x8B");            // ✋
        case ControlGesture::closedFist: return juce::CharPointer_UTF8 ("\xE2\x9C\x8A");            // ✊
        case ControlGesture::victory:    return juce::CharPointer_UTF8 ("\xE2\x9C\x8C\xEF\xB8\x8F"); // ✌️
        case ControlGesture::thumbUp:    return juce::CharPointer_UTF8 ("\xF0\x9F\x91\x8D");        // 👍
        case ControlGesture::thumbDown:  return juce::CharPointer_UTF8 ("\xF0\x9F\x91\x8E");        // 👎
        case ControlGesture::pointRight: return juce::CharPointer_UTF8 ("\xF0\x9F\x91\x89");        // 👉
        case ControlGesture::pointLeft:  return juce::CharPointer_UTF8 ("\xF0\x9F\x91\x88");        // 👈
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
    if (text.equalsIgnoreCase ("Point Right") || text.equalsIgnoreCase ("Point_Right"))
        return ControlGesture::pointRight;
    if (text.equalsIgnoreCase ("Point Left") || text.equalsIgnoreCase ("Point_Left"))
        return ControlGesture::pointLeft;
    return ControlGesture::unknown;
}
}
