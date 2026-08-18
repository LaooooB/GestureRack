#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include "UiMetrics.h"

namespace gr::ui
{
inline const juce::Colour canvas       { 0xff0a0b0c };
inline const juce::Colour workspace    { 0xff101114 };
inline const juce::Colour surface      { 0xff151619 };
inline const juce::Colour surfaceHigh  { 0xff1b1c1f };
inline const juce::Colour control      { 0xff222428 };
inline const juce::Colour controlHigh  { 0xff27292d };
inline const juce::Colour border       { 0xff34363a };
inline const juce::Colour gray         { 0xff777b82 };
inline const juce::Colour text         { 0xfff0f1f2 };
inline const juce::Colour textMuted    { 0xff9a9da2 };
inline const juce::Colour accent       { 0xffeedf05 };
inline const juce::Colour statusGreen  { 0xff39d353 };
inline const juce::Colour danger       { 0xffa84c4c };
inline const juce::Colour viewport     { 0xff070809 };
inline const juce::Colour shadow       { 0x26000000 };

constexpr int micro = 4;
constexpr int small = 8;
constexpr int normal = 12;
constexpr int large = 16;
constexpr int major = 24;
constexpr float panelRadius = metrics::panelRadius;
constexpr float controlRadius = metrics::controlRadius;

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
    return juce::Font (juce::FontOptions (fontFamily(), juce::jmax (9.0f, height), style));
}

inline juce::Font appTitleFont() { return font (19.0f, juce::Font::bold); }
inline juce::Font titleFont()    { return font (18.0f, juce::Font::bold); }
inline juce::Font sectionFont()  { return font (14.0f, juce::Font::bold); }
inline juce::Font rowFont()      { return font (13.0f, juce::Font::plain); }
inline juce::Font controlFont()  { return font (11.5f, juce::Font::plain); }
inline juce::Font metaFont()     { return font (10.0f, juce::Font::plain); }

inline juce::Colour blend (juce::Colour a, juce::Colour b, float amount)
{
    return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, amount));
}

inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, bool = false)
{
    if (bounds.isEmpty()) return;
    g.setColour (surface);
    g.fillRoundedRectangle (bounds, metrics::panelRadius);
    g.setColour (border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), metrics::panelRadius, metrics::borderThickness);
}

inline void drawSectionTitle (juce::Graphics& g, const juce::String& title,
                              juce::Rectangle<int> bounds,
                              juce::Justification justification = juce::Justification::centredLeft)
{
    g.setColour (text);
    g.setFont (sectionFont());
    g.drawFittedText (title, bounds, justification, 1);
}

inline void drawDashedRoundedRect (juce::Graphics& g, juce::Rectangle<float> bounds,
                                   float radius, juce::Colour colour,
                                   float thickness = 1.0f,
                                   float dash = 5.0f, float gap = 4.0f)
{
    juce::Path source;
    source.addRoundedRectangle (bounds, radius);
    const float pattern[] { dash, gap };
    juce::Path dashed;
    juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded)
        .createDashedStroke (dashed, source, pattern, 2);
    g.setColour (colour);
    g.fillPath (dashed);
}

enum class Icon : int
{
    hand,
    search,
    power,
    trash,
    more,
    undo,
    settings,
    menu,
    camera,
    folder,
    calibrate,
    swap,
    bypass,
    sliders,
    palm,
    fist,
    victory,
    thumbUp,
    arrowUp,
    arrowDown,
    arrowLeft,
    arrowRight,
    toggle,
    choice,
    stepped,
    continuous,
    cross,
    count
};

inline std::array<juce::Path, static_cast<size_t> (Icon::count)> buildIconPaths()
{
    std::array<juce::Path, static_cast<size_t> (Icon::count)> paths;
    auto& hand = paths[static_cast<size_t> (Icon::hand)];
    hand.startNewSubPath (7, 20); hand.lineTo (6, 11); hand.lineTo (8, 10); hand.lineTo (9, 15);
    hand.startNewSubPath (9, 15); hand.lineTo (9, 6); hand.lineTo (11, 6); hand.lineTo (11, 14);
    hand.startNewSubPath (11, 14); hand.lineTo (12, 4); hand.lineTo (14, 4); hand.lineTo (14, 14);
    hand.startNewSubPath (14, 14); hand.lineTo (16, 6); hand.lineTo (18, 7); hand.lineTo (17, 15);
    hand.startNewSubPath (17, 15); hand.lineTo (20, 12); hand.lineTo (22, 14); hand.lineTo (18, 20); hand.lineTo (11, 22); hand.closeSubPath();

    auto& search = paths[static_cast<size_t> (Icon::search)];
    search.addEllipse (4, 4, 11, 11); search.startNewSubPath (14, 14); search.lineTo (21, 21);

    auto& power = paths[static_cast<size_t> (Icon::power)];
    power.startNewSubPath (12, 3); power.lineTo (12, 12);
    power.addCentredArc (12, 12, 8, 8, 0.0f, -2.35f, 2.35f, true);

    auto& trash = paths[static_cast<size_t> (Icon::trash)];
    trash.addRectangle (7, 8, 10, 12); trash.startNewSubPath (5, 6); trash.lineTo (19, 6);
    trash.startNewSubPath (9, 6); trash.lineTo (10, 3); trash.lineTo (14, 3); trash.lineTo (15, 6);
    trash.startNewSubPath (10, 10); trash.lineTo (10, 17); trash.startNewSubPath (14, 10); trash.lineTo (14, 17);

    auto& more = paths[static_cast<size_t> (Icon::more)];
    more.addEllipse (5, 10, 3, 3); more.addEllipse (10.5f, 10, 3, 3); more.addEllipse (16, 10, 3, 3);

    auto& undo = paths[static_cast<size_t> (Icon::undo)];
    undo.startNewSubPath (8, 7); undo.lineTo (3, 12); undo.lineTo (8, 17);
    undo.startNewSubPath (4, 12); undo.lineTo (13, 12);
    undo.addCentredArc (13, 12, 7, 7, 0.0f, -1.57f, 1.57f, true);

    auto& settings = paths[static_cast<size_t> (Icon::settings)];
    settings.addEllipse (8, 8, 8, 8); settings.addEllipse (10.5f, 10.5f, 3, 3);
    for (int i = 0; i < 8; ++i)
    {
        const auto a = juce::MathConstants<float>::twoPi * static_cast<float> (i) / 8.0f;
        const juce::Point<float> p1 { 12.0f + std::cos (a) * 6.0f, 12.0f + std::sin (a) * 6.0f };
        const juce::Point<float> p2 { 12.0f + std::cos (a) * 9.0f, 12.0f + std::sin (a) * 9.0f };
        settings.startNewSubPath (p1); settings.lineTo (p2);
    }

    auto& menu = paths[static_cast<size_t> (Icon::menu)];
    menu.startNewSubPath (4, 7); menu.lineTo (20, 7); menu.startNewSubPath (4, 12); menu.lineTo (20, 12); menu.startNewSubPath (4, 17); menu.lineTo (20, 17);

    auto& camera = paths[static_cast<size_t> (Icon::camera)];
    camera.addRoundedRectangle (4, 7, 16, 11, 2); camera.addEllipse (9, 9, 6, 6);
    camera.startNewSubPath (8, 7); camera.lineTo (10, 4); camera.lineTo (14, 4); camera.lineTo (16, 7);

    auto& folder = paths[static_cast<size_t> (Icon::folder)];
    folder.startNewSubPath (3, 7); folder.lineTo (9, 7); folder.lineTo (11, 9); folder.lineTo (21, 9); folder.lineTo (19, 19); folder.lineTo (4, 19); folder.closeSubPath();

    auto& calibrate = paths[static_cast<size_t> (Icon::calibrate)];
    calibrate.addEllipse (7, 7, 10, 10); calibrate.startNewSubPath (12, 2); calibrate.lineTo (12, 7);
    calibrate.startNewSubPath (12, 17); calibrate.lineTo (12, 22); calibrate.startNewSubPath (2, 12); calibrate.lineTo (7, 12); calibrate.startNewSubPath (17, 12); calibrate.lineTo (22, 12);

    auto& swap = paths[static_cast<size_t> (Icon::swap)];
    swap.startNewSubPath (3, 8); swap.lineTo (18, 8); swap.lineTo (15, 5); swap.startNewSubPath (18, 8); swap.lineTo (15, 11);
    swap.startNewSubPath (21, 16); swap.lineTo (6, 16); swap.lineTo (9, 13); swap.startNewSubPath (6, 16); swap.lineTo (9, 19);

    auto& bypass = paths[static_cast<size_t> (Icon::bypass)];
    bypass.addEllipse (4, 4, 16, 16); bypass.startNewSubPath (6.5f, 17.5f); bypass.lineTo (17.5f, 6.5f);

    auto& sliders = paths[static_cast<size_t> (Icon::sliders)];
    sliders.startNewSubPath (4, 6); sliders.lineTo (20, 6); sliders.addEllipse (7, 4, 4, 4);
    sliders.startNewSubPath (4, 12); sliders.lineTo (20, 12); sliders.addEllipse (14, 10, 4, 4);
    sliders.startNewSubPath (4, 18); sliders.lineTo (20, 18); sliders.addEllipse (10, 16, 4, 4);

    auto& palm = paths[static_cast<size_t> (Icon::palm)];
    palm.startNewSubPath (8, 20); palm.lineTo (7, 10); palm.lineTo (9, 9); palm.lineTo (10, 15);
    palm.startNewSubPath (10, 15); palm.lineTo (10, 5); palm.lineTo (12, 5); palm.lineTo (12, 14);
    palm.startNewSubPath (12, 14); palm.lineTo (13, 4); palm.lineTo (15, 4); palm.lineTo (15, 14);
    palm.startNewSubPath (15, 14); palm.lineTo (17, 6); palm.lineTo (19, 7); palm.lineTo (18, 15);
    palm.startNewSubPath (18, 15); palm.lineTo (20, 12); palm.lineTo (22, 14); palm.lineTo (18, 20); palm.lineTo (11, 21); palm.closeSubPath();

    auto& fist = paths[static_cast<size_t> (Icon::fist)];
    fist.addRoundedRectangle (5, 9, 14, 10, 3); fist.startNewSubPath (7, 9); fist.lineTo (8, 5); fist.lineTo (11, 5); fist.lineTo (11, 9);
    fist.startNewSubPath (11, 9); fist.lineTo (12, 4); fist.lineTo (15, 4); fist.lineTo (15, 9);
    fist.startNewSubPath (15, 9); fist.lineTo (16, 6); fist.lineTo (19, 7); fist.lineTo (18, 10);

    auto& victory = paths[static_cast<size_t> (Icon::victory)];
    victory.startNewSubPath (10, 20); victory.lineTo (9, 12); victory.lineTo (7, 6); victory.lineTo (9, 5); victory.lineTo (12, 11);
    victory.startNewSubPath (12, 11); victory.lineTo (15, 4); victory.lineTo (17, 5); victory.lineTo (14, 13);
    victory.startNewSubPath (14, 13); victory.lineTo (19, 12); victory.lineTo (20, 15); victory.lineTo (16, 20); victory.closeSubPath();

    auto& thumbUp = paths[static_cast<size_t> (Icon::thumbUp)];
    thumbUp.startNewSubPath (7, 20); thumbUp.lineTo (7, 11); thumbUp.lineTo (11, 11); thumbUp.lineTo (13, 4); thumbUp.lineTo (16, 5); thumbUp.lineTo (15, 10); thumbUp.lineTo (20, 10); thumbUp.lineTo (19, 19); thumbUp.closeSubPath();

    auto addArrow = [&paths] (Icon icon, juce::Point<float> a, juce::Point<float> b,
                              juce::Point<float> h1, juce::Point<float> h2)
    {
        auto& p = paths[static_cast<size_t> (icon)];
        p.startNewSubPath (a); p.lineTo (b); p.startNewSubPath (b); p.lineTo (h1); p.startNewSubPath (b); p.lineTo (h2);
    };
    addArrow (Icon::arrowUp,    {12, 20}, {12, 4},  {7, 9},  {17, 9});
    addArrow (Icon::arrowDown,  {12, 4},  {12, 20}, {7, 15}, {17, 15});
    addArrow (Icon::arrowLeft,  {20, 12}, {4, 12},  {9, 7},  {9, 17});
    addArrow (Icon::arrowRight, {4, 12},  {20, 12}, {15, 7}, {15, 17});

    auto& toggle = paths[static_cast<size_t> (Icon::toggle)];
    toggle.addRoundedRectangle (3, 8, 18, 8, 4); toggle.addEllipse (5, 9, 6, 6);
    auto& choice = paths[static_cast<size_t> (Icon::choice)];
    choice.addRoundedRectangle (4, 5, 16, 14, 2); choice.startNewSubPath (8, 10); choice.lineTo (12, 14); choice.lineTo (16, 10);
    auto& stepped = paths[static_cast<size_t> (Icon::stepped)];
    stepped.startNewSubPath (4, 18); stepped.lineTo (9, 18); stepped.lineTo (9, 13); stepped.lineTo (14, 13); stepped.lineTo (14, 8); stepped.lineTo (20, 8);
    auto& continuous = paths[static_cast<size_t> (Icon::continuous)];
    continuous.startNewSubPath (3, 15); continuous.cubicTo (7, 5, 10, 21, 14, 10); continuous.cubicTo (17, 2, 19, 12, 21, 7);
    auto& cross = paths[static_cast<size_t> (Icon::cross)];
    cross.startNewSubPath (6, 6); cross.lineTo (18, 18); cross.startNewSubPath (18, 6); cross.lineTo (6, 18);
    return paths;
}

inline const juce::Path& iconPath (Icon icon)
{
    static const auto paths = buildIconPaths();
    return paths[static_cast<size_t> (icon)];
}

inline void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> bounds,
                      juce::Colour colour, float stroke = 1.65f, bool fill = false)
{
    if (bounds.isEmpty()) return;
    auto path = iconPath (icon);
    const auto source = juce::Rectangle<float> (0.0f, 0.0f, 24.0f, 24.0f);
    const auto scale = juce::jmin (bounds.getWidth() / source.getWidth(), bounds.getHeight() / source.getHeight());
    const auto targetW = source.getWidth() * scale;
    const auto targetH = source.getHeight() * scale;
    const auto tx = bounds.getCentreX() - targetW * 0.5f;
    const auto ty = bounds.getCentreY() - targetH * 0.5f;
    path.applyTransform (juce::AffineTransform::scale (scale).translated (tx, ty));
    g.setColour (colour);
    if (fill)
        g.fillPath (path);
    else
        g.strokePath (path, juce::PathStrokeType (stroke, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
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
        auto fill = blend (control, controlHigh, hoverAmount * 0.75f);
        if (selected) fill = blend (fill, accent, 0.055f);
        if (down) fill = blend (fill, canvas, 0.22f);
        g.setColour (isEnabled() ? fill : surfaceHigh);
        g.fillRoundedRectangle (b, metrics::controlRadius);
        g.setColour (isEnabled() ? (selected ? accent : blend (border, accent, hoverAmount * 0.8f))
                                 : border.withAlpha (0.45f));
        g.drawRoundedRectangle (b, metrics::controlRadius, 1.0f);
        g.setColour (isEnabled() ? (selected ? accent : gr::ui::text) : textMuted.withAlpha (0.55f));
        g.setFont (controlFont());
        g.drawFittedText (getButtonText(), getLocalBounds().reduced (7, 2), juce::Justification::centred, 1);
    }

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

class IconButton : public juce::Button,
                   private juce::Timer
{
public:
    explicit IconButton (Icon iconToUse, const juce::String& name = {})
        : juce::Button (name), icon (iconToUse)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    ~IconButton() override { stopTimer(); }

    void setIcon (Icon iconToUse) { icon = iconToUse; repaint(); }
    void setAccentWhenOn (bool shouldUse) { accentWhenOn = shouldUse; repaint(); }

    void mouseEnter (const juce::MouseEvent& e) override
    {
        juce::Button::mouseEnter (e);
        hoverTarget = 1.0f;
        startTimerHz (60);
    }

    void mouseExit (const juce::MouseEvent& e) override
    {
        juce::Button::mouseExit (e);
        hoverTarget = 0.0f;
        startTimerHz (60);
    }

    void paintButton (juce::Graphics& g, bool, bool down) override
    {
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        const auto on = getToggleState();
        auto fill = blend (surfaceHigh, control, hoverAmount * 0.65f);
        if (on && accentWhenOn) fill = blend (fill, accent, 0.06f);
        if (down) fill = blend (fill, canvas, 0.18f);
        g.setColour (isEnabled() ? fill : surfaceHigh.withAlpha (0.65f));
        g.fillRoundedRectangle (b, metrics::controlRadius);
        const auto edge = on && accentWhenOn ? accent : blend (border, accent, hoverAmount * 0.72f);
        g.setColour (isEnabled() ? edge : border.withAlpha (0.35f));
        g.drawRoundedRectangle (b, metrics::controlRadius, 1.0f);
        auto iconBounds = b.reduced (juce::jmax (5.0f, juce::jmin (b.getWidth(), b.getHeight()) * 0.23f));
        drawIcon (g, icon, iconBounds,
                  isEnabled() ? ((on && accentWhenOn) ? accent : gr::ui::text) : textMuted.withAlpha (0.45f));
    }

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

    Icon icon;
    float hoverAmount = 0.0f;
    float hoverTarget = 0.0f;
    bool accentWhenOn = true;
};
}
