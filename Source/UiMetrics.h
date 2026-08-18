#pragma once

#include <JuceHeader.h>

namespace gr::ui::metrics
{
constexpr int designWidth = 1672;
constexpr int designHeight = 941;

constexpr int outerMargin = 14;
constexpr int gutter = 12;
constexpr int topBarHeight = 42;
constexpr int footerHeight = 64;
constexpr int panelPadding = 12;
constexpr int hostChromeHeight = 42;
constexpr int minimumParameterHeight = 290;
constexpr int minimumUpperHeight = 280;
constexpr int minimumEditorWidth = 1240;
constexpr int minimumEditorHeight = 760;
constexpr int defaultEditorWidth = designWidth;
constexpr int defaultEditorHeight = designHeight;

inline int railWidthFor (int editorWidth)
{
    return juce::jlimit (200, 260, juce::roundToInt (static_cast<float> (editorWidth) * 0.138f));
}

inline int rightColumnWidthFor (int editorWidth)
{
    return juce::jlimit (320, 380, juce::roundToInt (static_cast<float> (editorWidth) * 0.203f));
}

inline int upperRowHeightFor (int workspaceHeight, int desiredNativeHeight)
{
    const auto proportional = juce::roundToInt (static_cast<float> (workspaceHeight) * 0.55f);
    const auto desired = desiredNativeHeight > 0
        ? desiredNativeHeight + hostChromeHeight + panelPadding * 2
        : proportional;
    const auto maxUpper = juce::jmax (minimumUpperHeight,
                                      workspaceHeight - gutter - minimumParameterHeight);
    return juce::jlimit (minimumUpperHeight, maxUpper, juce::jmax (proportional, desired));
}

// PRISIM theme geometry. Layout proportions deliberately stay unchanged; only the
// surface language is tightened to the softer 9/5/6 px package/control/card rhythm.
constexpr float panelRadius = 9.0f;
constexpr float controlRadius = 5.0f;
constexpr float cardRadius = 6.0f;
constexpr float borderThickness = 0.8f;
}
