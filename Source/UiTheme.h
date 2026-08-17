#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace gr::ui
{
// Pantone 17-5104 Ultimate Gray / Pantone 13-0647 Illuminating inspired UI palette.
inline const juce::Colour canvas      { 0xff101111 };
inline const juce::Colour workspace   { 0xff151616 };
inline const juce::Colour surface     { 0xff1c1d1e };
inline const juce::Colour surfaceHigh { 0xff252627 };
inline const juce::Colour control     { 0xff2e3031 };
inline const juce::Colour border      { 0xff4b4d4e };
inline const juce::Colour gray        { 0xff939597 };
inline const juce::Colour text        { 0xffe8e8e4 };
inline const juce::Colour textMuted   { 0xff9a9b99 };
inline const juce::Colour accent      { 0xfff5df4d };
inline const juce::Colour shadow      { 0x52000000 };
inline const juce::Colour viewport    { 0xff0c0d0d };

constexpr int micro = 4;
constexpr int small = 8;
constexpr int normal = 12;
constexpr int large = 16;
constexpr int major = 24;
constexpr float panelRadius = 11.0f;
constexpr float controlRadius = 6.0f;

inline juce::String fontFamily()
{
   #if JUCE_WINDOWS
    return "Segoe UI";
   #else
    return juce::Font::getDefaultSansSerifFontName();
   #endif
}

inline juce::Font font (float height, int style = juce::Font::plain)
{
    return juce::Font (juce::FontOptions (fontFamily(), height, style));
}

inline juce::Font titleFont()   { return font (21.0f, juce::Font::bold); }
inline juce::Font sectionFont() { return font (13.5f, juce::Font::bold); }
inline juce::Font controlFont() { return font (10.5f, juce::Font::bold); }
inline juce::Font metaFont()    { return font (9.0f, juce::Font::plain); }

inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, bool elevated = false)
{
    if (bounds.isEmpty()) return;
    if (elevated)
    {
        auto shadowBounds = bounds.translated (0.0f, 2.0f).expanded (2.0f);
        g.setColour (shadow);
        g.fillRoundedRectangle (shadowBounds, panelRadius + 1.0f);
    }
    g.setColour (surface);
    g.fillRoundedRectangle (bounds, panelRadius);
    g.setColour (border.withAlpha (0.62f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), panelRadius, 1.0f);
}

inline void drawSectionTitle (juce::Graphics& g, const juce::String& title,
                              juce::Rectangle<int> bounds, juce::Justification justification = juce::Justification::centredLeft)
{
    g.setColour (text);
    g.setFont (sectionFont());
    g.drawFittedText (title, bounds, justification, 1);
}

inline juce::Colour blend (juce::Colour a, juce::Colour b, float amount)
{
    return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, amount));
}

class AnimatedTextButton : public juce::TextButton,
                           private juce::Timer
{
public:
    explicit AnimatedTextButton (const juce::String& textToUse = {}) : juce::TextButton (textToUse)
    {
        setColour (juce::TextButton::buttonColourId, control);
        setColour (juce::TextButton::buttonOnColourId, control);
        setColour (juce::TextButton::textColourOffId, gr::ui::text);
        setColour (juce::TextButton::textColourOnId, gr::ui::text);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    ~AnimatedTextButton() override { stopTimer(); }

    float getHoverAmount() const noexcept { return hoverAmount; }

    void mouseEnter (const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseEnter (e);
        hoverTarget = 1.0f;
        startTimerHz (60);
    }

    void mouseExit (const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseExit (e);
        hoverTarget = 0.0f;
        startTimerHz (60);
    }

    void paintButton (juce::Graphics& g, bool, bool down) override
    {
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        const auto selected = getToggleState();
        const auto focus = juce::jmax (hoverAmount, selected ? 1.0f : 0.0f);
        auto base = findColour (juce::TextButton::buttonColourId);
        auto fill = blend (base, canvas, hoverAmount * 0.38f);
        if (selected) fill = blend (fill, accent.withAlpha (1.0f), 0.08f);
        if (down) fill = blend (fill, canvas, 0.30f);

        g.setColour (isEnabled() ? fill : surfaceHigh.withAlpha (0.45f));
        g.fillRoundedRectangle (b, controlRadius);
        g.setColour (isEnabled() ? blend (border, accent, focus) : border.withAlpha (0.28f));
        g.drawRoundedRectangle (b, controlRadius, 1.0f);

        g.setColour (isEnabled() ? (selected ? accent : findColour (juce::TextButton::textColourOffId))
                                 : textMuted.withAlpha (0.45f));
        g.setFont (controlFont());
        g.drawFittedText (getButtonText(), getLocalBounds().reduced (7, 2), juce::Justification::centred, 1);
    }

protected:
    void setHoverAmountForTesting (float v) noexcept { hoverAmount = v; }

private:
    void timerCallback() override
    {
        const auto speed = hoverTarget > hoverAmount ? 0.22f : 0.16f;
        hoverAmount += (hoverTarget - hoverAmount) * speed;
        if (std::abs (hoverTarget - hoverAmount) < 0.01f)
        {
            hoverAmount = hoverTarget;
            stopTimer();
        }
        repaint();
    }

    float hoverAmount = 0.0f;
    float hoverTarget = 0.0f;
};
}
