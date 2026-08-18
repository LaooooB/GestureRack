#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include "BinaryData.h"
#include "UiMetrics.h"

namespace gr::ui
{
// PRISIM-derived neutral charcoal ladder. Yellow is reserved for actual focus/
// selection/state, rather than being used as a generic hover colour.
inline const juce::Colour canvas       { 0xff1e1e1e }; // window / seams
inline const juce::Colour workspace    { 0xff2a2a2a }; // page body
inline const juce::Colour surface      { 0xff353535 }; // package surface
inline const juce::Colour surfaceHigh  { 0xff3a3a3a }; // quiet raised surface
inline const juce::Colour control      { 0xff434343 }; // control field
inline const juce::Colour controlHigh  { 0xff4e4e4e }; // hover
inline const juce::Colour border       { 0xff585858 }; // hairline
inline const juce::Colour borderSoft   { 0x55585858 };
inline const juce::Colour gray         { 0xffb0b0b0 };
inline const juce::Colour text         { 0xffeaeaea };
inline const juce::Colour textMuted    { 0xff888888 };
inline const juce::Colour textFaint    { 0xff5f5f5f };
inline const juce::Colour accent       { 0xfff5df4d };
inline const juce::Colour accentBright { 0xfffff07a };
inline const juce::Colour accentDim    { 0xffb99a34 };
inline const juce::Colour tertiary     { 0xff62b8c4 };
inline const juce::Colour tertiaryDim  { 0xff3f7f88 };
inline const juce::Colour statusGreen  { 0xff78b57b };
inline const juce::Colour danger       { 0xffe06c75 };
inline const juce::Colour viewport     { 0xff181818 };
inline const juce::Colour labelChip    { 0xff181818 };
inline const juce::Colour panelLow     { 0xff141414 };
inline const juce::Colour shadow       { 0x33000000 };
inline const juce::Colour onAccent     { 0xff1a1a12 };

constexpr int micro = 4;
constexpr int small = 8;
constexpr int normal = 12;
constexpr int large = 16;
constexpr int major = 24;
constexpr float panelRadius = metrics::panelRadius;
constexpr float controlRadius = metrics::controlRadius;

namespace embeddedType
{
enum class Weight
{
    regular,
    semibold,
    bold,
    display
};

inline juce::Typeface::Ptr load (const void* data, int size)
{
    jassert (data != nullptr && size > 0);
    return juce::Typeface::createSystemTypefaceFor (data, static_cast<size_t> (size));
}

inline juce::Typeface::Ptr get (Weight weight)
{
    // These are the exact hinted static TTF faces used by the PRISIM handoff.
    // They live inside the VST3 binary, so no installed system font can change
    // Gesture Rack's metrics, weight selection or glyph outlines.
    static juce::Typeface::Ptr regularTypeface = load (BinaryData::InterRegular_ttf,
                                                        BinaryData::InterRegular_ttfSize);
    static juce::Typeface::Ptr semiboldTypeface = load (BinaryData::InterSemiBold_ttf,
                                                         BinaryData::InterSemiBold_ttfSize);
    static juce::Typeface::Ptr boldTypeface = load (BinaryData::InterBold_ttf,
                                                     BinaryData::InterBold_ttfSize);
    static juce::Typeface::Ptr displayTypeface = load (BinaryData::InterDisplayExtraBold_ttf,
                                                        BinaryData::InterDisplayExtraBold_ttfSize);

    switch (weight)
    {
        case Weight::semibold: return semiboldTypeface;
        case Weight::bold:     return boldTypeface;
        case Weight::display:  return displayTypeface;
        case Weight::regular:  break;
    }
    return regularTypeface;
}
}

inline juce::String fontFamily()
{
    const auto typeface = embeddedType::get (embeddedType::Weight::regular);
    return typeface != nullptr ? typeface->getName() : juce::String();
}

inline juce::Font font (float height, embeddedType::Weight weight, float tracking = 0.0f)
{
    const auto typeface = embeddedType::get (weight);
    jassert (typeface != nullptr);
    auto result = juce::Font (juce::FontOptions (typeface).withHeight (juce::jmax (8.0f, height)));
    if (tracking != 0.0f) result = result.withExtraKerningFactor (tracking);
    return result;
}

// Compatibility overload for existing call sites. PRISIM's handoff maps the old
// boolean/bold path to SemiBold rather than asking the OS for a synthetic bold face.
inline juce::Font font (float height, int style = juce::Font::plain, float tracking = 0.0f)
{
    const auto weight = (style & juce::Font::bold) != 0
        ? embeddedType::Weight::semibold
        : embeddedType::Weight::regular;
    auto result = font (height, weight, tracking);
    if ((style & juce::Font::underlined) != 0) result.setUnderline (true);
    // No UI call site should request synthetic italic; keeping it out avoids a hidden
    // platform typeface lookup that would break deterministic typography.
    jassert ((style & juce::Font::italic) == 0);
    return result;
}

// Keep the current Gesture Rack layout scale, but use PRISIM's exact embedded faces.
inline juce::Font appTitleFont() { return font (17.0f, embeddedType::Weight::display, 0.045f); }
inline juce::Font titleFont()    { return font (15.5f, embeddedType::Weight::semibold, 0.025f); }
inline juce::Font sectionFont()  { return font (10.0f, embeddedType::Weight::semibold, 0.12f); }
inline juce::Font rowFont()      { return font (12.0f, embeddedType::Weight::regular); }
inline juce::Font controlFont()  { return font (11.0f, embeddedType::Weight::regular); }
inline juce::Font comboFont()    { return font (12.0f, embeddedType::Weight::semibold); }
inline juce::Font popupFont()    { return font (11.5f, embeddedType::Weight::regular); }
inline juce::Font metaFont()     { return font (9.0f, embeddedType::Weight::regular); }

inline juce::Colour blend (juce::Colour a, juce::Colour b, float amount)
{
    return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, amount));
}

inline float approachFast (float current, float target)
{
    const auto speed = target > current ? 0.42f : 0.34f;
    current += (target - current) * speed;
    if (std::abs (target - current) < 0.008f) current = target;
    return current;
}

inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, bool withHeader = false)
{
    if (bounds.isEmpty()) return;

    // One soft depth cue only. No plastic gradient/highlight treatment.
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 3.0f).expanded (0.5f, 0.0f),
                            metrics::panelRadius + 0.5f);

    g.setColour (surface);
    g.fillRoundedRectangle (bounds, metrics::panelRadius);
    g.setColour (border.withAlpha (0.52f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), metrics::panelRadius, metrics::borderThickness);

    if (withHeader && bounds.getHeight() > 40.0f)
    {
        auto rail = juce::Rectangle<float> (bounds.getX() + 1.0f, bounds.getY() + 1.0f,
                                            bounds.getWidth() - 2.0f, 34.0f);
        g.setColour (panelLow.withAlpha (0.20f));
        g.fillRoundedRectangle (rail, metrics::panelRadius - 1.0f);
        g.setColour (border.withAlpha (0.24f));
        g.drawLine (bounds.getX() + 10.0f, bounds.getY() + 35.0f,
                    bounds.getRight() - 10.0f, bounds.getY() + 35.0f, 0.7f);
    }
}

inline void drawSectionTitle (juce::Graphics& g, const juce::String& title,
                              juce::Rectangle<int> bounds,
                              juce::Justification justification = juce::Justification::centredLeft)
{
    g.setColour (text.withAlpha (0.94f));
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
    addArrow (Icon::arrowRight, {4, 12}, {20, 12}, {15, 7}, {15, 17});

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

class ThemeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ThemeLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, surface);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::headerTextColourId, textMuted);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.14f));
        setColour (juce::PopupMenu::highlightedTextColourId, text);

        setColour (juce::ComboBox::backgroundColourId, control);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::outlineColourId, border);
        setColour (juce::ComboBox::arrowColourId, accent);

        setColour (juce::TextButton::buttonColourId, control);
        setColour (juce::TextButton::buttonOnColourId, control);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, onAccent);

        setColour (juce::Label::textColourId, text);
        setColour (juce::TextEditor::backgroundColourId, control);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::outlineColourId, border);
        setColour (juce::TextEditor::focusedOutlineColourId, accentDim);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return controlFont(); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return comboFont(); }
    juce::Font getPopupMenuFont() override { return popupFont(); }
    juce::Font getLabelFont (juce::Label&) override { return controlFont(); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool highlighted, bool down) override
    {
        const auto b = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto selected = button.getToggleState();
        auto fill = highlighted ? controlHigh : control;
        if (selected) fill = blend (fill, accent, 0.10f);
        if (down) fill = blend (fill, panelLow, 0.22f);
        g.setColour (button.isEnabled() ? fill : surfaceHigh);
        g.fillRoundedRectangle (b, metrics::controlRadius);
        g.setColour (button.isEnabled() ? (selected ? accent : (highlighted ? gray.withAlpha (0.72f) : border.withAlpha (0.72f)))
                                         : border.withAlpha (0.32f));
        g.drawRoundedRectangle (b, metrics::controlRadius, 0.8f);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool down,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<float> (0.5f, 0.5f,
                                                     static_cast<float> (width - 1),
                                                     static_cast<float> (height - 1));
        auto fill = box.isEnabled() ? control : surfaceHigh;
        if (down) fill = controlHigh;
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, metrics::controlRadius);
        g.setColour (box.isEnabled() ? border.withAlpha (0.86f) : border.withAlpha (0.35f));
        g.drawRoundedRectangle (bounds, metrics::controlRadius, 0.8f);

        auto arrowArea = juce::Rectangle<float> (static_cast<float> (buttonX), static_cast<float> (buttonY),
                                                 static_cast<float> (buttonW), static_cast<float> (buttonH))
                         .reduced (juce::jmax (5.0f, static_cast<float> (buttonW) * 0.28f),
                                   juce::jmax (5.0f, static_cast<float> (buttonH) * 0.32f));
        juce::Path chevron;
        chevron.startNewSubPath (arrowArea.getX(), arrowArea.getCentreY() - 2.0f);
        chevron.lineTo (arrowArea.getCentreX(), arrowArea.getCentreY() + 2.0f);
        chevron.lineTo (arrowArea.getRight(), arrowArea.getCentreY() - 2.0f);
        g.setColour (box.isEnabled() ? accent : textFaint);
        g.strokePath (chevron, juce::PathStrokeType (1.45f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (9, 1, juce::jmax (1, box.getWidth() - 36), juce::jmax (1, box.getHeight() - 2));
        label.setFont (comboFont());
    }

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        g.fillAll (surface);
        auto bounds = juce::Rectangle<float> (0.5f, 0.5f,
                                               static_cast<float> (width - 1),
                                               static_cast<float> (height - 1));
        g.setColour (border.withAlpha (0.82f));
        g.drawRoundedRectangle (bounds, 6.0f, 0.8f);
    }

    void drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area,
                                     const juce::String& sectionName) override
    {
        auto rail = area.reduced (4, 1);
        g.setColour (panelLow.withAlpha (0.44f));
        g.fillRoundedRectangle (rail.toFloat(), 4.0f);
        g.setColour (textMuted);
        g.setFont (sectionFont());
        g.drawFittedText (sectionName.toUpperCase(), rail.reduced (8, 0),
                          juce::Justification::centredLeft, 1);
    }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& itemText,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override
    {
        if (isSeparator)
        {
            g.setColour (border.withAlpha (0.40f));
            g.drawHorizontalLine (area.getCentreY(), static_cast<float> (area.getX() + 9),
                                  static_cast<float> (area.getRight() - 9));
            return;
        }

        auto row = area.reduced (4, 2);
        if (isHighlighted && isActive)
        {
            g.setColour (accent.withAlpha (0.14f));
            g.fillRoundedRectangle (row.toFloat(), 4.0f);
            g.setColour (accent.withAlpha (0.52f));
            g.drawRoundedRectangle (row.toFloat().reduced (0.5f), 4.0f, 0.8f);
        }

        auto content = row.reduced (8, 0);
        auto markArea = content.removeFromLeft (18);
        content.removeFromLeft (3);
        auto subArea = content.removeFromRight (hasSubMenu ? 16 : 0);
        auto shortcutArea = content.removeFromRight (shortcutKeyText.isNotEmpty() ? 72 : 0);

        const auto itemColour = textColour != nullptr ? *textColour
            : (isActive ? (isHighlighted ? text : text.withAlpha (0.94f)) : textFaint);

        if (icon != nullptr)
        {
            icon->drawWithin (g, markArea.toFloat().reduced (2.0f),
                              juce::RectanglePlacement::centred, 1.0f);
        }
        else if (isTicked)
        {
            g.setColour (accent);
            g.fillEllipse (markArea.withSizeKeepingCentre (6, 6).toFloat());
        }

        g.setColour (itemColour);
        g.setFont (popupFont());
        g.drawFittedText (itemText, content, juce::Justification::centredLeft, 1);

        if (shortcutKeyText.isNotEmpty())
        {
            g.setColour (isActive ? textMuted : textFaint);
            g.setFont (metaFont());
            g.drawFittedText (shortcutKeyText, shortcutArea, juce::Justification::centredRight, 1);
        }

        if (hasSubMenu)
        {
            const auto cx = static_cast<float> (subArea.getCentreX());
            const auto cy = static_cast<float> (subArea.getCentreY());
            juce::Path arrow;
            arrow.startNewSubPath (cx - 2.0f, cy - 4.0f);
            arrow.lineTo (cx + 2.0f, cy);
            arrow.lineTo (cx - 2.0f, cy + 4.0f);
            g.setColour (isHighlighted ? accent : textMuted);
            g.strokePath (arrow, juce::PathStrokeType (1.2f));
        }
    }
};

inline ThemeLookAndFeel& themeLookAndFeel()
{
    static ThemeLookAndFeel instance;
    return instance;
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
        auto fill = blend (control, controlHigh, hoverAmount);
        if (selected) fill = blend (fill, accent, 0.10f);
        if (down) fill = blend (fill, panelLow, 0.20f);
        g.setColour (isEnabled() ? fill : surfaceHigh);
        g.fillRoundedRectangle (b, metrics::controlRadius);
        const auto edge = selected ? accent
            : blend (border.withAlpha (0.76f), gray.withAlpha (0.72f), hoverAmount);
        g.setColour (isEnabled() ? edge : border.withAlpha (0.35f));
        g.drawRoundedRectangle (b, metrics::controlRadius, 0.8f);
        g.setColour (isEnabled() ? (selected ? accent : gr::ui::text) : textFaint);
        g.setFont (controlFont());
        g.drawFittedText (getButtonText(), getLocalBounds().reduced (7, 2), juce::Justification::centred, 1);
    }

private:
    void timerCallback() override
    {
        hoverAmount = approachFast (hoverAmount, hoverTarget);
        if (hoverAmount == hoverTarget) stopTimer();
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
        auto fill = blend (surfaceHigh, controlHigh, hoverAmount * 0.82f);
        if (on && accentWhenOn) fill = blend (fill, accent, 0.08f);
        if (down) fill = blend (fill, panelLow, 0.20f);
        g.setColour (isEnabled() ? fill : surfaceHigh.withAlpha (0.65f));
        g.fillRoundedRectangle (b, metrics::controlRadius);
        const auto edge = on && accentWhenOn ? accent
            : blend (border.withAlpha (0.72f), gray.withAlpha (0.68f), hoverAmount);
        g.setColour (isEnabled() ? edge : border.withAlpha (0.32f));
        g.drawRoundedRectangle (b, metrics::controlRadius, 0.8f);
        auto iconBounds = b.reduced (juce::jmax (5.0f, juce::jmin (b.getWidth(), b.getHeight()) * 0.23f));
        drawIcon (g, icon, iconBounds,
                  isEnabled() ? ((on && accentWhenOn) ? accent : gr::ui::text) : textFaint, 1.55f);
    }

private:
    void timerCallback() override
    {
        hoverAmount = approachFast (hoverAmount, hoverTarget);
        if (hoverAmount == hoverTarget) stopTimer();
        repaint();
    }

    Icon icon;
    float hoverAmount = 0.0f;
    float hoverTarget = 0.0f;
    bool accentWhenOn = true;
};
}
