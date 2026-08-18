#include "PluginEditor.h"
#include "PluginBrowser.h"
#include <cmath>
#include <limits>

namespace
{
using namespace gr;
namespace ui = gr::ui;
namespace metrics = gr::ui::metrics;

constexpr std::array<std::pair<int, int>, 21> handConnections {{
    {0,1}, {1,2}, {2,3}, {3,4}, {0,5}, {5,6}, {6,7}, {7,8}, {5,9}, {9,10}, {10,11}, {11,12},
    {9,13}, {13,14}, {14,15}, {15,16}, {13,17}, {17,18}, {18,19}, {19,20}, {17,0}
}};

struct GestureCardDescriptor
{
    gr::ControlGesture gesture = gr::ControlGesture::unknown;
    const char* label = "";
    ui::Icon icon = ui::Icon::palm;
    bool enabled = false;
};

const std::array<GestureCardDescriptor, 8>& gestureCards()
{
    // The reference has eight visual slots. The current vision protocol only exposes seven
    // source IDs, so the extra UP cell deliberately remains an inert/empty panel instead of
    // aliasing THUMB UP and creating two cards for one backend gesture.
    static const std::array<GestureCardDescriptor, 8> cards {{
        { gr::ControlGesture::openPalm,   "PALM",      ui::Icon::palm,       true },
        { gr::ControlGesture::closedFist, "FIST",      ui::Icon::fist,       true },
        { gr::ControlGesture::victory,    "VICTORY",   ui::Icon::victory,    true },
        { gr::ControlGesture::thumbUp,    "THUMB UP",  ui::Icon::thumbUp,    true },
        { gr::ControlGesture::unknown,    "",          ui::Icon::arrowUp,    false },
        { gr::ControlGesture::thumbDown,  "DOWN",      ui::Icon::arrowDown,  true },
        { gr::ControlGesture::thumbLeft,  "LEFT",      ui::Icon::arrowLeft,  true },
        { gr::ControlGesture::thumbRight, "RIGHT",     ui::Icon::arrowRight, true }
    }};
    return cards;
}

bool handRolesTrusted (const gr::DualHandVisionSnapshot& snapshot)
{
    if (snapshot.protocol < 2) return true;
    if (snapshot.handCalibrationActive) return false;
    const auto source = snapshot.handRoleSource.trim().toUpperCase();
    return source.isNotEmpty() && source != "UNCALIBRATED" && source != "CALIBRATING" && source != "DEFAULT";
}

juce::Colour cameraStatusColour (bool connected, const gr::DualHandVisionSnapshot& snapshot)
{
    if (! connected) return ui::textMuted.withAlpha (0.62f);
    const auto age = snapshot.receivedAtMs > 0
        ? juce::Time::currentTimeMillis() - snapshot.receivedAtMs : int64_t { 1000000 };
    if (snapshot.handCalibrationActive || age < 0 || age > 300) return ui::accent;
    return ui::statusGreen;
}

int scaledMetric (float scale, int value)
{
    return juce::jmax (1, juce::roundToInt (static_cast<float> (value) * scale));
}
}

class GestureRackAudioProcessorEditor::EmbeddedEditorCanvas final : public juce::Component
{
public:
    void attach (juce::AudioProcessorEditor* editor, juce::String pluginName)
    {
        if (nativeEditor.getComponent() == editor)
        {
            name = std::move (pluginName);
            syncNativeSize();
            return;
        }
        detach();
        name = std::move (pluginName);
        nativeEditor = editor;
        if (editor != nullptr) addAndMakeVisible (*editor);
        syncNativeSize();
        repaint();
    }

    void detach()
    {
        if (auto* editor = nativeEditor.getComponent()) removeChildComponent (editor);
        nativeEditor = nullptr;
        name.clear();
        nativeSize = {};
        syncCanvasSize();
        repaint();
    }

    juce::Point<int> getNativeEditorSize() const noexcept { return nativeSize; }

    bool syncNativeSize()
    {
        auto next = juce::Point<int>();
        if (auto* editor = nativeEditor.getComponent())
            next = { juce::jmax (1, editor->getWidth()), juce::jmax (1, editor->getHeight()) };
        const auto changed = next != nativeSize;
        nativeSize = next;
        syncCanvasSize();
        return changed;
    }

    void setMinimumCanvasSize (juce::Point<int> size)
    {
        size.x = juce::jmax (1, size.x);
        size.y = juce::jmax (1, size.y);
        if (size == minimumCanvasSize) return;
        minimumCanvasSize = size;
        syncCanvasSize();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (ui::viewport);
        if (nativeEditor == nullptr)
        {
            g.setColour (ui::textMuted);
            g.setFont (ui::controlFont());
            g.drawText (name.isNotEmpty() ? "NO NATIVE EDITOR" : "NO DEVICE",
                        getLocalBounds().reduced (24), juce::Justification::centred);
        }
    }

    void resized() override
    {
        if (auto* editor = nativeEditor.getComponent())
        {
            const auto x = juce::jmax (12, (getWidth() - editor->getWidth()) / 2);
            const auto y = juce::jmax (12, (getHeight() - editor->getHeight()) / 2);
            editor->setTopLeftPosition (x, y);
        }
    }

private:
    void syncCanvasSize()
    {
        auto desired = minimumCanvasSize;
        if (nativeSize.x > 0 && nativeSize.y > 0)
        {
            desired.x = juce::jmax (desired.x, nativeSize.x + 24);
            desired.y = juce::jmax (desired.y, nativeSize.y + 24);
        }
        else
        {
            desired.x = juce::jmax (desired.x, 560);
            desired.y = juce::jmax (desired.y, 260);
        }
        if (getWidth() != desired.x || getHeight() != desired.y) setSize (desired.x, desired.y);
        else resized();
    }

    juce::Component::SafePointer<juce::AudioProcessorEditor> nativeEditor;
    juce::String name;
    juce::Point<int> nativeSize;
    juce::Point<int> minimumCanvasSize { 560, 260 };
};

class GestureRackAudioProcessorEditor::GesturePanel final : public juce::Component,
                                                             private juce::Timer
{
public:
    explicit GesturePanel (GestureRackAudioProcessorEditor& ownerToUse) : owner (ownerToUse)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible (owner.swapHandsButton);
        startTimerHz (60);
    }

    ~GesturePanel() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        ui::drawPanel (g, getLocalBounds().toFloat(), true);
        ui::drawSectionTitle (g, "GESTURES", { 14, 9, juce::jmax (80, getWidth() - 170), 22 });

        const auto gestureEnabled = owner.processor.isGestureEnabled();
        const auto powerHot = enableRect.contains (pointer);
        g.setColour (powerHot ? ui::controlHigh : ui::control);
        g.fillRoundedRectangle (enableRect.toFloat(), ui::metrics::controlRadius);
        g.setColour (gestureEnabled ? ui::accent : (powerHot ? ui::gray.withAlpha (0.72f) : ui::border));
        g.drawRoundedRectangle (enableRect.toFloat().reduced (0.5f), ui::metrics::controlRadius, 0.8f);
        ui::drawIcon (g, ui::Icon::power, enableRect.reduced (9).toFloat(),
                      gestureEnabled ? ui::accent : ui::textMuted, 1.6f);

        const auto live = owner.processor.getLiveRightGesture();
        const auto& cards = gestureCards();
        for (int i = 0; i < static_cast<int> (cards.size()); ++i)
        {
            const auto& descriptor = cards[static_cast<size_t> (i)];
            const auto rect = chipRects[static_cast<size_t> (i)];
            if (rect.isEmpty()) continue;
            const auto liveCard = descriptor.enabled && descriptor.gesture == live;
            const auto hot = descriptor.enabled && rect.contains (pointer);
            const auto dragSource = dragging && descriptor.gesture == draggedGesture;
            auto fill = hot ? ui::controlHigh : ui::control;
            if (liveCard || dragSource) fill = ui::blend (fill, ui::accent, 0.075f);
            g.setColour (fill);
            g.fillRoundedRectangle (rect.toFloat(), ui::metrics::cardRadius);
            g.setColour (liveCard || dragSource ? ui::accent
                                               : (descriptor.enabled ? (hot ? ui::gray.withAlpha (0.70f) : ui::border)
                                                                     : ui::border.withAlpha (0.52f)));
            g.drawRoundedRectangle (rect.toFloat().reduced (0.5f), ui::metrics::cardRadius, 0.8f);

            if (! descriptor.enabled) continue;
            auto iconArea = rect.reduced (10, 8);
            auto labelArea = iconArea.removeFromBottom (22);
            iconArea.removeFromBottom (2);
            ui::drawIcon (g, descriptor.icon, iconArea.withSizeKeepingCentre (34, 34).toFloat(),
                          liveCard || dragSource ? ui::accent : ui::text, 1.7f);
            g.setColour (liveCard || dragSource ? ui::accent : ui::text);
            g.setFont (ui::controlFont());
            g.drawFittedText (descriptor.label, labelArea, juce::Justification::centred, 1);
        }

        const auto drawBottomButton = [&] (juce::Rectangle<int> rect, ui::Icon icon,
                                           const juce::String& label, bool accentState)
        {
            const auto hot = rect.contains (pointer) || (dragging && rect.contains (dragPoint));
            g.setColour (hot ? ui::controlHigh : ui::control);
            g.fillRoundedRectangle (rect.toFloat(), ui::metrics::controlRadius);
            g.setColour (accentState || (dragging && rect.contains (dragPoint)) ? ui::accent
                                                                               : (hot ? ui::gray.withAlpha (0.72f) : ui::border));
            g.drawRoundedRectangle (rect.toFloat().reduced (0.5f), ui::metrics::controlRadius, 0.8f);
            auto content = rect.reduced (12, 6);
            auto iconArea = content.removeFromLeft (32).withSizeKeepingCentre (22, 22);
            ui::drawIcon (g, icon, iconArea.toFloat(), accentState ? ui::accent : ui::text, 1.55f);
            g.setColour (ui::text);
            g.setFont (ui::controlFont());
            g.drawFittedText (label, content, juce::Justification::centred, 1);
        };
        drawBottomButton (bypassRect, ui::Icon::bypass, "BYPASS", owner.processor.isSlotBypassed (owner.processor.getSelectedSlot()));
        drawBottomButton (calibrateRect, ui::Icon::calibrate,
                          owner.processor.getDualHandVisionSnapshot().handCalibrationActive ? "CALIBRATING" : "CALIBRATE",
                          owner.processor.getDualHandVisionSnapshot().handCalibrationActive);

        if (dragging)
        {
            auto bubble = juce::Rectangle<float> (86.0f, 28.0f).withCentre (dragPoint.toFloat());
            bubble = bubble.constrainedWithin (getLocalBounds().toFloat().reduced (2.0f));
            g.setColour (ui::panelLow.withAlpha (0.96f));
            g.fillRoundedRectangle (bubble, 5.0f);
            g.setColour (ui::accent);
            g.drawRoundedRectangle (bubble, 5.0f, 0.8f);
            g.setColour (ui::accent);
            g.setFont (ui::controlFont());
            juce::String label;
            for (const auto& card : cards)
                if (card.gesture == draggedGesture && card.enabled) label = card.label;
            g.drawFittedText (label, bubble.toNearestInt().reduced (4, 0), juce::Justification::centred, 1);
        }
    }

    void resized() override
    {
        auto content = getLocalBounds().reduced (14);
        auto header = content.removeFromTop (28);
        enableRect = header.removeFromRight (42);
        header.removeFromRight (8);
        owner.swapHandsButton.setBounds (header.removeFromRight (88));
        content.removeFromTop (8);

        auto bottom = content.removeFromBottom (56);
        content.removeFromBottom (10);
        const auto rowGap = 8;
        auto firstRow = content.removeFromTop ((content.getHeight() - rowGap) / 2);
        content.removeFromTop (rowGap);
        auto secondRow = content;

        const auto layoutRow = [this] (juce::Rectangle<int> row, int start)
        {
            constexpr int gap = 8;
            const auto width = juce::jmax (1, (row.getWidth() - gap * 3) / 4);
            for (int i = 0; i < 4; ++i)
            {
                chipRects[static_cast<size_t> (start + i)] = row.removeFromLeft (width);
                if (i < 3) row.removeFromLeft (gap);
            }
        };
        layoutRow (firstRow, 0);
        layoutRow (secondRow, 4);

        const auto gap = 10;
        bypassRect = bottom.removeFromLeft ((bottom.getWidth() - gap) / 2);
        bottom.removeFromLeft (gap);
        calibrateRect = bottom;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        pointer = e.getPosition();
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        pointer = { -9999, -9999 };
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pointer = e.getPosition();
        if (enableRect.contains (pointer))
        {
            owner.processor.setGestureEnabled (! owner.processor.isGestureEnabled());
            repaint();
            return;
        }
        if (bypassRect.contains (pointer))
        {
            const auto slot = owner.processor.getSelectedSlot();
            if (owner.processor.isSlotLoaded (slot))
                owner.processor.setSlotBypassed (slot, ! owner.processor.isSlotBypassed (slot));
            owner.updateSlotButtons();
            owner.repaint();
            repaint();
            return;
        }
        if (calibrateRect.contains (pointer))
        {
            const auto vision = owner.processor.getDualHandVisionSnapshot();
            if (vision.handCalibrationActive) owner.processor.cancelHandCalibration();
            else owner.processor.beginHandCalibration();
            repaint();
            return;
        }

        const auto& cards = gestureCards();
        for (int i = 0; i < static_cast<int> (cards.size()); ++i)
        {
            const auto& card = cards[static_cast<size_t> (i)];
            if (! card.enabled || card.gesture == gr::ControlGesture::unknown) continue;
            if (chipRects[static_cast<size_t> (i)].contains (pointer))
            {
                draggedGesture = card.gesture;
                dragging = true;
                dragPoint = pointer;
                repaint();
                return;
            }
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        dragPoint = e.getPosition();
        const auto ownerPoint = owner.getLocalPoint (this, dragPoint);
        if (owner.parameterInspector.getBounds().contains (ownerPoint))
            owner.parameterInspector.setGestureDragPreview (
                draggedGesture, owner.parameterInspector.getLocalPoint (&owner, ownerPoint));
        else
            owner.parameterInspector.clearGestureDragPreview();
        repaint();
        owner.repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        dragPoint = e.getPosition();
        const auto ownerPoint = owner.getLocalPoint (this, dragPoint);

        if (bypassRect.contains (dragPoint))
        {
            owner.parameterInspector.assignSlotActionGesture (draggedGesture, gr::MappingMode::triggerSetBypassed);
        }
        else if (owner.bypassButton.getBounds().contains (ownerPoint))
        {
            // Preserve main's one-way Set Active behavior. The reference no longer needs a
            // second visible slot-action target because ACTIVE itself is the drop target.
            owner.parameterInspector.assignSlotActionGesture (draggedGesture, gr::MappingMode::triggerSetActive);
        }
        else if (owner.parameterInspector.getBounds().contains (ownerPoint))
        {
            owner.parameterInspector.dropGestureAt (
                draggedGesture, owner.parameterInspector.getLocalPoint (&owner, ownerPoint));
        }

        owner.parameterInspector.clearGestureDragPreview();
        dragging = false;
        draggedGesture = gr::ControlGesture::unknown;
        repaint();
        owner.repaint();
    }

private:
    void timerCallback() override { repaint(); }

    GestureRackAudioProcessorEditor& owner;
    std::array<juce::Rectangle<int>, 8> chipRects {};
    juce::Rectangle<int> enableRect;
    juce::Rectangle<int> bypassRect;
    juce::Rectangle<int> calibrateRect;
    juce::Point<int> pointer { -9999, -9999 };
    juce::Point<int> dragPoint;
    bool dragging = false;
    gr::ControlGesture draggedGesture = gr::ControlGesture::unknown;
};

GestureRackAudioProcessorEditor::RackSlotButton::RackSlotButton()
    : juce::Button ("Rack slot")
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void GestureRackAudioProcessorEditor::RackSlotButton::setSlotVisualState (
    bool loadedToUse, bool bypassedToUse, juce::String pluginNameToUse)
{
    loaded = loadedToUse;
    bypassed = bypassedToUse;
    pluginName = std::move (pluginNameToUse);
    repaint();
}

void GestureRackAudioProcessorEditor::RackSlotButton::resized()
{
    powerRect = getLocalBounds().removeFromRight (42).reduced (8);
}

void GestureRackAudioProcessorEditor::RackSlotButton::mouseEnter (const juce::MouseEvent& e)
{
    juce::Button::mouseEnter (e);
    hoverTarget = 1.0f;
    startTimerHz (60);
}

void GestureRackAudioProcessorEditor::RackSlotButton::mouseExit (const juce::MouseEvent& e)
{
    juce::Button::mouseExit (e);
    hoverTarget = 0.0f;
    startTimerHz (60);
}

void GestureRackAudioProcessorEditor::RackSlotButton::timerCallback()
{
    hoverAmount = ui::approachFast (hoverAmount, hoverTarget);
    if (hoverAmount == hoverTarget) stopTimer();
    repaint();
}

void GestureRackAudioProcessorEditor::RackSlotButton::paintButton (juce::Graphics& g, bool, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto selected = getToggleState();
    auto fill = ui::blend (ui::surfaceHigh, ui::controlHigh, hoverAmount * 0.82f);
    if (selected) fill = ui::blend (fill, ui::accent, 0.075f);
    if (down) fill = ui::blend (fill, ui::panelLow, 0.20f);
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, ui::metrics::cardRadius);

    const auto hoverEdge = ui::blend (ui::border.withAlpha (0.74f), ui::gray.withAlpha (0.72f), hoverAmount);
    const auto edge = selected ? ui::accent : hoverEdge;
    if (loaded)
    {
        g.setColour (edge);
        g.drawRoundedRectangle (bounds, ui::metrics::cardRadius, 0.8f);
        auto content = getLocalBounds().reduced (12, 5);
        auto power = content.removeFromRight (32);
        content.removeFromRight (7);
        g.setColour (bypassed ? ui::textMuted : ui::text);
        g.setFont (ui::rowFont());
        g.drawFittedText (pluginName, content, juce::Justification::centredLeft, 1);
        ui::drawIcon (g, ui::Icon::power, power.withSizeKeepingCentre (18, 18).toFloat(),
                      bypassed ? ui::textMuted : ui::accent, 1.55f);
    }
    else
    {
        ui::drawDashedRoundedRect (g, bounds.reduced (0.5f), ui::metrics::cardRadius,
                                   selected ? ui::accent : hoverEdge, 0.9f, 5.0f, 4.0f);
        g.setColour (selected ? ui::accent : ui::blend (ui::textMuted, ui::text, hoverAmount * 0.62f));
        g.setFont (ui::font (22.0f));
        g.drawText ("+", getLocalBounds(), juce::Justification::centred);
    }
}

void GestureRackAudioProcessorEditor::RackSlotButton::mouseDown (const juce::MouseEvent& e)
{
    juce::Button::mouseDown (e);
    powerPressed = loaded && powerRect.contains (e.getPosition());
    bodyPressed = ! powerPressed;
    if (powerPressed) return;
    if (bodyClick) bodyClick();
    if (loaded && dragDown) dragDown (e);
}

void GestureRackAudioProcessorEditor::RackSlotButton::mouseDrag (const juce::MouseEvent& e)
{
    juce::Button::mouseDrag (e);
    if (bodyPressed && loaded && dragMove) dragMove (e);
}

void GestureRackAudioProcessorEditor::RackSlotButton::mouseUp (const juce::MouseEvent& e)
{
    if (powerPressed && powerRect.contains (e.getPosition()) && powerClick) powerClick();
    else if (bodyPressed && loaded && dragUp) dragUp (e);
    bodyPressed = false;
    powerPressed = false;
    juce::Button::mouseUp (e);
}

GestureRackAudioProcessorEditor::GestureRackAudioProcessorEditor (GestureRackAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p), parameterInspector (p)
{
    setWantsKeyboardFocus (true);
    setSize (metrics::defaultEditorWidth, metrics::defaultEditorHeight);
    setResizable (true, true);
    setResizeLimits (metrics::minimumEditorWidth, metrics::minimumEditorHeight, 3200, 2100);

    for (int i = 0; i < GestureRackAudioProcessor::slotCount; ++i)
    {
        auto& button = slotButtons[static_cast<size_t> (i)];
        addAndMakeVisible (button);
        button.bodyClick = [this, i]
        {
            processor.setSelectedSlot (i);
            updateSlotButtons();
            updateEmbeddedEditor();
            if (! processor.isSlotLoaded (i)) showPluginBrowserForSlot (i);
            repaint();
        };
        button.powerClick = [this, i]
        {
            if (! processor.isSlotLoaded (i)) return;
            processor.setSlotBypassed (i, ! processor.isSlotBypassed (i));
            updateSlotButtons();
            repaint();
        };
        button.dragDown = [this, i] (const juce::MouseEvent& e) { handleSlotMouseDown (i, e); };
        button.dragMove = [this] (const juce::MouseEvent& e) { handleSlotMouseDrag (e); };
        button.dragUp = [this] (const juce::MouseEvent& e) { handleSlotMouseUp (e); };
    }

    addAndMakeVisible (railSearchButton);
    addAndMakeVisible (removeButton);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (pluginMoreButton);
    addAndMakeVisible (settingsButton);
    addAndMakeVisible (menuButton);

    railSearchButton.onClick = [this] { showPluginBrowser(); };
    removeButton.onClick = [this] { removeSelectedPlugin(); };
    bypassButton.onClick = [this]
    {
        const auto slot = processor.getSelectedSlot();
        if (! processor.isSlotLoaded (slot)) return;
        processor.setSlotBypassed (slot, ! processor.isSlotBypassed (slot));
        updateSlotButtons();
        repaint();
    };
    pluginMoreButton.onClick = [this] { showPluginMoreMenu(); };
    swapHandsButton.onClick = [this] { processor.toggleSwapHandedness(); };
    settingsButton.onClick = [this] { showSettingsMenu(); };
    menuButton.onClick = [this] { showMainMenu(); };

    settingsButton.setAccentWhenOn (false);
    menuButton.setAccentWhenOn (false);
    railSearchButton.setAccentWhenOn (false);
    removeButton.setAccentWhenOn (false);
    pluginMoreButton.setAccentWhenOn (false);

    addAndMakeVisible (parameterInspector);

    embeddedCanvas = std::make_unique<EmbeddedEditorCanvas>();
    addAndMakeVisible (pluginViewport);
    pluginViewport.setViewedComponent (embeddedCanvas.get(), false);
    pluginViewport.setScrollBarsShown (false, false);
    pluginViewport.setScrollBarThickness (8);
    pluginViewport.setColour (juce::ScrollBar::thumbColourId, ui::gray.withAlpha (0.34f));
    pluginViewport.setColour (juce::ScrollBar::backgroundColourId, ui::viewport);

    gesturePanel = std::make_unique<GesturePanel> (*this);
    addAndMakeVisible (*gesturePanel);

    pluginBrowser = std::make_unique<PluginBrowserComponent> (
        [this] (int slotIndex, const juce::PluginDescription& description)
        {
            if (embeddedCanvas != nullptr) embeddedCanvas->detach();
            displayedChildIdentity = 0;
            displayedSlot = -1;
            lastNativeEditorSize = {};
            processor.setSelectedSlot (slotIndex);
            processor.loadPluginDescription (slotIndex, description);
            updateSlotButtons();
            updateEmbeddedEditor();
            repaint();
        },
        [this] { hidePluginBrowser(); });
    addAndMakeVisible (*pluginBrowser);
    pluginBrowser->setVisible (false);

    updateSlotButtons();
    updateEmbeddedEditor();
    startTimerHz (30);
}

GestureRackAudioProcessorEditor::~GestureRackAudioProcessorEditor()
{
    stopTimer();
    pluginViewport.setViewedComponent (nullptr, false);
    if (embeddedCanvas != nullptr) embeddedCanvas->detach();
    gesturePanel.reset();
    embeddedCanvas.reset();
    pluginBrowser.reset();
}

void GestureRackAudioProcessorEditor::removeSelectedPlugin()
{
    if (embeddedCanvas != nullptr) embeddedCanvas->detach();
    displayedChildIdentity = 0;
    displayedSlot = -1;
    lastNativeEditorSize = {};
    processor.removeSlotPlugin (processor.getSelectedSlot());
    updateSlotButtons();
    repaint();
}

void GestureRackAudioProcessorEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (pluginBrowser != nullptr && pluginBrowser->isVisible()) return;
    int targetSlot = processor.getSelectedSlot();
    for (int i = 0; i < GestureRackAudioProcessor::slotCount; ++i)
        if (slotButtons[static_cast<size_t> (i)].getBounds().contains (x, y))
        {
            targetSlot = i;
            break;
        }

    for (const auto& path : files)
        if (path.endsWithIgnoreCase (".vst3"))
        {
            if (embeddedCanvas != nullptr) embeddedCanvas->detach();
            displayedChildIdentity = 0;
            displayedSlot = -1;
            lastNativeEditorSize = {};
            processor.setSelectedSlot (targetSlot);
            processor.loadVst3FromFile (targetSlot, juce::File (path));
            updateSlotButtons();
            updateEmbeddedEditor();
            repaint();
            break;
        }
}

void GestureRackAudioProcessorEditor::timerCallback()
{
    const auto selected = processor.getSelectedSlot();
    const auto loaded = processor.isSlotLoaded (selected);
    const auto bypassed = processor.isSlotBypassed (selected);
    const auto vision = processor.getDualHandVisionSnapshot();
    const auto connected = processor.isVisionConnected();
    frameReader.readLatest (cameraFrame);
    refreshCameraDisplay();

    bypassButton.setButtonText (bypassed ? "BYPASS" : "ACTIVE");
    bypassButton.setToggleState (loaded && ! bypassed, juce::dontSendNotification);
    removeButton.setEnabled (loaded);
    bypassButton.setEnabled (loaded);
    swapHandsButton.setButtonText ("SWAP L/R");
    swapHandsButton.setToggleState (vision.swapHandedness, juce::dontSendNotification);
    swapHandsButton.setEnabled (connected && ! vision.handCalibrationActive);

    updateSlotButtons();
    updateEmbeddedEditor();
    repaint();
}

void GestureRackAudioProcessorEditor::refreshCameraDisplay()
{
    if (! cameraFrame.isValid() || cameraFrame.sequence == cameraDisplaySequence) return;
    cameraDisplaySequence = cameraFrame.sequence;
    const auto sourceW = cameraFrame.image.getWidth();
    const auto sourceH = cameraFrame.image.getHeight();
    const auto previewW = juce::jmax (320, cameraPreviewBounds.getWidth() * 2);
    const auto previewH = juce::jmax (220, cameraPreviewBounds.getHeight() * 2);
    const auto scale = juce::jmin (1.0f, juce::jmin (static_cast<float> (previewW) / static_cast<float> (sourceW),
                                                    static_cast<float> (previewH) / static_cast<float> (sourceH)));
    const auto targetW = juce::jmax (1, juce::roundToInt (sourceW * scale));
    const auto targetH = juce::jmax (1, juce::roundToInt (sourceH * scale));
    cameraDisplayImage = (targetW == sourceW && targetH == sourceH)
        ? cameraFrame.image.createCopy()
        : cameraFrame.image.rescaled (targetW, targetH, juce::Graphics::ResamplingQuality::mediumResamplingQuality);
}

void GestureRackAudioProcessorEditor::updateEmbeddedEditor()
{
    if (embeddedCanvas == nullptr) return;
    const auto slot = processor.getSelectedSlot();
    const auto identity = processor.getSlotChildIdentity (slot);
    const auto identityChanged = slot != displayedSlot || identity != displayedChildIdentity;

    if (identityChanged)
    {
        embeddedCanvas->detach();
        displayedSlot = slot;
        displayedChildIdentity = identity;
        if (identity != 0)
            embeddedCanvas->attach (processor.getOrCreateSlotEditor (slot), processor.getSlotPluginName (slot));
        else
            embeddedCanvas->attach (nullptr, {});
    }

    const auto nativeChanged = embeddedCanvas->syncNativeSize();
    const auto nativeSize = embeddedCanvas->getNativeEditorSize();
    if (identityChanged || nativeChanged || nativeSize != lastNativeEditorSize)
    {
        lastNativeEditorSize = nativeSize;
        if (nativeSize.x > 0) largestNativeEditorSize.x = juce::jmax (largestNativeEditorSize.x, nativeSize.x);
        if (nativeSize.y > 0) largestNativeEditorSize.y = juce::jmax (largestNativeEditorSize.y, nativeSize.y);
        adaptEditorToNativeSize (true);
    }
    updateViewportScrollbars();
}

void GestureRackAudioProcessorEditor::adaptEditorToNativeSize (bool force)
{
    if (adaptiveResizeInProgress || embeddedCanvas == nullptr) return;
    if (! force && largestNativeEditorSize.x <= 0 && largestNativeEditorSize.y <= 0) return;

    auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect (getScreenBounds());
    if (display == nullptr) display = displays.getPrimaryDisplay();
    int maxWidth = 3200;
    int maxHeight = 2100;
    if (display != nullptr)
    {
        const auto user = display->userBounds.toNearestInt();
        maxWidth = juce::jmax (metrics::minimumEditorWidth, juce::roundToInt (user.getWidth() * 0.985f));
        maxHeight = juce::jmax (metrics::minimumEditorHeight, juce::roundToInt (user.getHeight() * 0.965f));
    }

    const auto centerWidth = largestNativeEditorSize.x > 0 ? juce::jmax (900, largestNativeEditorSize.x + 30) : 980;
    const auto upperHeight = largestNativeEditorSize.y > 0 ? juce::jmax (360, largestNativeEditorSize.y + 70) : 420;
    const auto desiredWidth = scaledMetric (uiScale, metrics::outerMargin * 2 + 230 + metrics::gutter * 2 + 340) + centerWidth;
    const auto desiredHeight = scaledMetric (uiScale, metrics::outerMargin * 2 + metrics::topBarHeight
                                                   + metrics::gutter * 2)
                             + upperHeight + scaledMetric (uiScale, 350);

    setResizeLimits (metrics::minimumEditorWidth, metrics::minimumEditorHeight, maxWidth, maxHeight);
    const auto nextWidth = juce::jlimit (metrics::minimumEditorWidth, maxWidth, juce::jmax (getWidth(), desiredWidth));
    const auto nextHeight = juce::jlimit (metrics::minimumEditorHeight, maxHeight, juce::jmax (getHeight(), desiredHeight));
    if (nextWidth == getWidth() && nextHeight == getHeight()) return;
    adaptiveResizeInProgress = true;
    setSize (nextWidth, nextHeight);
    adaptiveResizeInProgress = false;
}

void GestureRackAudioProcessorEditor::updateViewportScrollbars()
{
    if (embeddedCanvas == nullptr || pluginViewport.getWidth() <= 0 || pluginViewport.getHeight() <= 0) return;
    const auto visible = juce::Point<int> (pluginViewport.getWidth(), pluginViewport.getHeight());
    embeddedCanvas->setMinimumCanvasSize (visible);
    const auto native = embeddedCanvas->getNativeEditorSize();
    const auto needHorizontal = native.x > 0 && native.x + 24 > visible.x;
    const auto needVertical = native.y > 0 && native.y + 24 > visible.y;
    pluginViewport.setScrollBarsShown (needVertical, needHorizontal, false, false);
}

void GestureRackAudioProcessorEditor::showPluginBrowserForSlot (int slotIndex)
{
    if (pluginBrowser == nullptr) return;
    processor.setSelectedSlot (juce::jlimit (0, GestureRackAudioProcessor::slotCount - 1, slotIndex));
    updateSlotButtons();
    updateEmbeddedEditor();
    parameterInspector.clearGestureDragPreview();
    slotDragging = false;
    slotDragSource = slotDragTarget = -1;
    pluginBrowser->showForSlot (processor.getSelectedSlot());
    repaint();
}

void GestureRackAudioProcessorEditor::showPluginBrowser()
{
    showPluginBrowserForSlot (processor.getSelectedSlot());
}

void GestureRackAudioProcessorEditor::hidePluginBrowser()
{
    if (pluginBrowser != nullptr) pluginBrowser->setVisible (false);
    grabKeyboardFocus();
    repaint();
}

void GestureRackAudioProcessorEditor::showSettingsMenu()
{
    const auto snapshot = processor.getDualHandVisionSnapshot();
    const auto connected = processor.isVisionConnected();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&ui::themeLookAndFeel());
    menu.addSectionHeader ("Diagnostics");
    menu.addItem (101, connected ? "CAMERA  ONLINE" : "CAMERA  OFFLINE", false);
    menu.addItem (102, "BACKEND  " + (snapshot.cameraBackend.isNotEmpty() ? snapshot.cameraBackend : juce::String ("--")), false);
    menu.addItem (103, "CAPTURE  " + juce::String (snapshot.captureFps, 1) + " FPS", false);
    menu.addItem (104, "VISION  " + juce::String (snapshot.visionFps, 1) + " FPS", false);
    menu.addItem (105, "CAPTURE -> RESULT  " + juce::String (snapshot.captureToResultMs, 1) + " ms", false);
    const auto sr = processor.getHostSampleRateForUi();
    const auto bs = processor.getHostBlockSizeForUi();
    menu.addItem (106, "AUDIO  " + (sr > 0.0 ? juce::String (sr / 1000.0, 1) + " kHz" : juce::String ("--"))
                        + " / " + (bs > 0 ? juce::String (bs) : juce::String ("--")), false);
    juce::String handState;
    if (! connected) handState = "OFFLINE";
    else if (snapshot.handCalibrationActive) handState = "CALIBRATING";
    else handState = handRolesTrusted (snapshot) ? "READY" : "CALIBRATE";
    menu.addItem (107, "HAND  " + handState, false);
    menu.addItem (108, "GESTURE  " + (snapshot.right.present
        ? gr::controlGestureToShortLabel (snapshot.right.stableGesture) : juce::String ("--")), false);
    menu.addItem (109, "SELECT  SLOT " + (snapshot.left.present && snapshot.left.stableSlot > 0
        ? juce::String (snapshot.left.stableSlot) : juce::String ("--")), false);
    menu.addSeparator();
    menu.addSectionHeader ("UI Scale");
    menu.addItem (90, "90%", true, std::abs (uiScale - 0.90f) < 0.01f);
    menu.addItem (100, "100%", true, std::abs (uiScale - 1.00f) < 0.01f);
    menu.addItem (110, "110%", true, std::abs (uiScale - 1.10f) < 0.01f);
    menu.addItem (125, "125%", true, std::abs (uiScale - 1.25f) < 0.01f);

    juce::Component::SafePointer<GestureRackAudioProcessorEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton).withStandardItemHeight (27),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result == 0) return;
                            if (result == 90 || result == 100 || result == 110 || result == 125)
                                safe->setUiScale (static_cast<float> (result) / 100.0f);
                        });
}

void GestureRackAudioProcessorEditor::showMainMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&ui::themeLookAndFeel());
    menu.addItem (1, "Plugin Browser");
    menu.addItem (2, "Scan / Paths...");
    menu.addSeparator();
    menu.addItem (3, "About Gesture Rack");
    juce::Component::SafePointer<GestureRackAudioProcessorEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton).withStandardItemHeight (27),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result == 0) return;
                            if (result == 1 || result == 2) safe->showPluginBrowser();
                            else if (result == 3)
                                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                                        "Gesture Rack",
                                                                        "Gesture Rack\nGesture-controlled VST3 effect rack.");
                        });
}

void GestureRackAudioProcessorEditor::showPluginMoreMenu()
{
    const auto slot = processor.getSelectedSlot();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&ui::themeLookAndFeel());
    menu.addSectionHeader (processor.isSlotLoaded (slot) ? processor.getSlotPluginName (slot) : "EMPTY SLOT");
    menu.addItem (1, "Replace / Load Plugin...");
    menu.addItem (2, "Remove Plugin", processor.isSlotLoaded (slot));
    menu.addSeparator();
    menu.addItem (3, "Diagnostics...");
    juce::Component::SafePointer<GestureRackAudioProcessorEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pluginMoreButton).withStandardItemHeight (27),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result == 0) return;
                            if (result == 1) safe->showPluginBrowser();
                            else if (result == 2) safe->removeSelectedPlugin();
                            else if (result == 3) safe->showSettingsMenu();
                        });
}

void GestureRackAudioProcessorEditor::setUiScale (float newScale)
{
    newScale = juce::jlimit (0.90f, 1.25f, newScale);
    if (std::abs (newScale - uiScale) < 0.001f) return;
    const auto ratio = newScale / uiScale;
    uiScale = newScale;
    auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect (getScreenBounds());
    if (display == nullptr) display = displays.getPrimaryDisplay();
    auto maxW = 3200;
    auto maxH = 2100;
    if (display != nullptr)
    {
        maxW = juce::roundToInt (display->userBounds.getWidth() * 0.985f);
        maxH = juce::roundToInt (display->userBounds.getHeight() * 0.965f);
    }
    setSize (juce::jlimit (metrics::minimumEditorWidth, maxW, juce::roundToInt (getWidth() * ratio)),
             juce::jlimit (metrics::minimumEditorHeight, maxH, juce::roundToInt (getHeight() * ratio)));
}

void GestureRackAudioProcessorEditor::updateSlotButtons()
{
    const auto selected = processor.getSelectedSlot();
    for (int i = 0; i < GestureRackAudioProcessor::slotCount; ++i)
    {
        auto& button = slotButtons[static_cast<size_t> (i)];
        auto name = processor.getSlotPluginName (i);
        if (name.length() > 22) name = name.substring (0, 20) + "..";
        button.setSlotVisualState (processor.isSlotLoaded (i), processor.isSlotBypassed (i), name);
        button.setToggleState (i == selected, juce::dontSendNotification);
        button.setTooltip (processor.isSlotLoaded (i) ? processor.getSlotPluginName (i) : "Load plugin");
    }
}

void GestureRackAudioProcessorEditor::handleSlotMouseDown (int slotIndex, const juce::MouseEvent& e)
{
    if (pluginBrowser != nullptr && pluginBrowser->isVisible()) return;
    if (! juce::isPositiveAndBelow (slotIndex, GestureRackAudioProcessor::slotCount)) return;
    slotDragSource = slotDragTarget = slotIndex;
    slotDragging = false;
    slotDragOrigin = e.getEventRelativeTo (this).getPosition();
}

void GestureRackAudioProcessorEditor::handleSlotMouseDrag (const juce::MouseEvent& e)
{
    if (pluginBrowser != nullptr && pluginBrowser->isVisible()) return;
    if (! juce::isPositiveAndBelow (slotDragSource, GestureRackAudioProcessor::slotCount)) return;
    const auto point = e.getEventRelativeTo (this).getPosition();
    const auto dx = point.x - slotDragOrigin.x;
    const auto dy = point.y - slotDragOrigin.y;
    if (! slotDragging && dx * dx + dy * dy < 36) return;
    slotDragging = true;
    slotDragTarget = findNearestSlot (point);
    repaint();
}

void GestureRackAudioProcessorEditor::handleSlotMouseUp (const juce::MouseEvent& e)
{
    if (pluginBrowser != nullptr && pluginBrowser->isVisible()) return;
    if (! juce::isPositiveAndBelow (slotDragSource, GestureRackAudioProcessor::slotCount)) return;
    if (slotDragging)
    {
        const auto point = e.getEventRelativeTo (this).getPosition();
        slotDragTarget = findNearestSlot (point);
        if (slotDragTarget >= 0 && slotDragTarget != slotDragSource)
        {
            if (embeddedCanvas != nullptr) embeddedCanvas->detach();
            displayedChildIdentity = 0;
            displayedSlot = -1;
            lastNativeEditorSize = {};
            processor.moveSlot (slotDragSource, slotDragTarget);
            updateEmbeddedEditor();
        }
    }
    slotDragging = false;
    slotDragSource = slotDragTarget = -1;
    updateSlotButtons();
    repaint();
}

int GestureRackAudioProcessorEditor::findNearestSlot (juce::Point<int> editorPoint) const
{
    auto rail = pluginRailBounds.expanded (18, 20);
    if (! rail.contains (editorPoint)) return slotDragSource;
    auto best = 0;
    auto bestDistance = std::numeric_limits<int>::max();
    for (int i = 0; i < GestureRackAudioProcessor::slotCount; ++i)
    {
        const auto distance = std::abs (editorPoint.y - slotButtons[static_cast<size_t> (i)].getBounds().getCentreY());
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

void GestureRackAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    if (pluginBrowser != nullptr && pluginBrowser->isVisible()) return;
    if (! slotDragging || ! juce::isPositiveAndBelow (slotDragTarget, GestureRackAudioProcessor::slotCount)) return;
    const auto target = slotButtons[static_cast<size_t> (slotDragTarget)].getBounds();
    auto line = juce::Rectangle<float> (static_cast<float> (target.getX() + 8),
                                        static_cast<float> (target.getY() - 3),
                                        static_cast<float> (target.getWidth() - 16), 2.0f);
    if (slotDragTarget > slotDragSource) line.setY (static_cast<float> (target.getBottom() + 1));
    g.setColour (ui::accent);
    g.fillRoundedRectangle (line, 1.0f);
}

void GestureRackAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::canvas);

    auto brand = topBarBounds;
    auto logo = brand.removeFromLeft (36).withSizeKeepingCentre (26, 26);
    ui::drawIcon (g, ui::Icon::hand, logo.toFloat(), ui::accent, 1.75f);
    brand.removeFromLeft (2);
    g.setColour (ui::text);
    g.setFont (ui::appTitleFont());
    g.drawText ("GESTURE RACK", brand.removeFromLeft (240), juce::Justification::centredLeft);

    const auto snapshot = processor.getDualHandVisionSnapshot();
    const auto connected = processor.isVisionConnected();
    auto statusArea = topBarBounds;
    statusArea.removeFromRight (settingsButton.getWidth() + menuButton.getWidth() + 16);
    statusArea = statusArea.removeFromRight (150);
    auto cameraIcon = statusArea.removeFromLeft (24).withSizeKeepingCentre (18, 18);
    ui::drawIcon (g, ui::Icon::camera, cameraIcon.toFloat(), ui::textMuted, 1.45f);
    auto dotArea = statusArea.removeFromLeft (18).withSizeKeepingCentre (8, 8).toFloat();
    g.setColour (cameraStatusColour (connected, snapshot));
    g.fillEllipse (dotArea);
    g.setColour (connected ? ui::textMuted : ui::textMuted.withAlpha (0.58f));
    g.setFont (ui::metaFont());
    g.drawText (connected ? juce::String (snapshot.captureFps, 0) + " FPS" : "-- FPS",
                statusArea, juce::Justification::centredLeft);

    ui::drawPanel (g, pluginRailBounds.toFloat(), true);
    ui::drawPanel (g, pluginPanelBounds.toFloat(), true);
    ui::drawPanel (g, cameraPanelBounds.toFloat(), true);

    auto railHeader = pluginRailBounds.reduced (14).removeFromTop (26);
    ui::drawSectionTitle (g, "PLUGINS", railHeader);

    if (const auto error = processor.getSlotLastError (processor.getSelectedSlot()); error.isNotEmpty())
    {
        auto errorArea = pluginPanelBounds.reduced (14).removeFromBottom (18);
        g.setColour (ui::accentDim);
        g.setFont (ui::metaFont());
        g.drawFittedText (error, errorArea, juce::Justification::centredLeft, 1);
    }

    auto cameraHeader = cameraPanelBounds.reduced (14).removeFromTop (24);
    ui::drawSectionTitle (g, "CAMERA", cameraHeader);
    auto dot = juce::Rectangle<float> (static_cast<float> (cameraHeader.getX() + 66),
                                       static_cast<float> (cameraHeader.getCentreY() - 3), 6.0f, 6.0f);
    g.setColour (cameraStatusColour (connected, snapshot));
    g.fillEllipse (dot);

    g.setColour (ui::viewport);
    g.fillRoundedRectangle (cameraPreviewBounds.toFloat(), 6.0f);
    g.setColour (ui::border.withAlpha (0.64f));
    g.drawRoundedRectangle (cameraPreviewBounds.toFloat().reduced (0.5f), 6.0f, 0.8f);

    juce::Rectangle<int> imageRect;
    if (connected && cameraDisplayImage.isValid())
    {
        const auto iw = cameraDisplayImage.getWidth();
        const auto ih = cameraDisplayImage.getHeight();
        const auto scale = juce::jmin (static_cast<float> (cameraPreviewBounds.getWidth()) / static_cast<float> (iw),
                                      static_cast<float> (cameraPreviewBounds.getHeight()) / static_cast<float> (ih));
        const auto dw = juce::jmax (1, juce::roundToInt (iw * scale));
        const auto dh = juce::jmax (1, juce::roundToInt (ih * scale));
        imageRect = juce::Rectangle<int> (dw, dh).withCentre (cameraPreviewBounds.getCentre());
        g.saveState();
        g.reduceClipRegion (cameraPreviewBounds);
        g.drawImage (cameraDisplayImage, imageRect.getX(), imageRect.getY(), imageRect.getWidth(), imageRect.getHeight(),
                     0, 0, iw, ih, false);
        drawCameraReticle (g, imageRect.toFloat());
        // Tracking is part of the live camera surface, not a diagnostics option. Both
        // physical hands use the same yellow point/line language so the user can read
        // tracking quality immediately without decoding colour roles.
        drawHandOverlay (g, imageRect.toFloat(), snapshot.left, ui::accent);
        drawHandOverlay (g, imageRect.toFloat(), snapshot.right, ui::accent);
        g.restoreState();
    }
    else
    {
        g.setColour (ui::textMuted);
        g.setFont (ui::controlFont());
        g.drawText (connected ? "WAITING FOR CAMERA" : "VISION OFFLINE",
                    cameraPreviewBounds, juce::Justification::centred);
        drawCameraReticle (g, cameraPreviewBounds.toFloat().reduced (12.0f));
    }

    drawCameraMotionTelemetry (g, snapshot);
}

void GestureRackAudioProcessorEditor::drawCameraReticle (juce::Graphics& g, juce::Rectangle<float> imageArea)
{
    if (imageArea.isEmpty()) return;
    auto area = imageArea.reduced (juce::jmin (22.0f, imageArea.getWidth() * 0.08f),
                                   juce::jmin (18.0f, imageArea.getHeight() * 0.08f));
    const auto arm = juce::jlimit (10.0f, 20.0f, juce::jmin (area.getWidth(), area.getHeight()) * 0.08f);
    juce::Path p;
    p.startNewSubPath (area.getX(), area.getY() + arm); p.lineTo (area.getX(), area.getY()); p.lineTo (area.getX() + arm, area.getY());
    p.startNewSubPath (area.getRight() - arm, area.getY()); p.lineTo (area.getRight(), area.getY()); p.lineTo (area.getRight(), area.getY() + arm);
    p.startNewSubPath (area.getX(), area.getBottom() - arm); p.lineTo (area.getX(), area.getBottom()); p.lineTo (area.getX() + arm, area.getBottom());
    p.startNewSubPath (area.getRight() - arm, area.getBottom()); p.lineTo (area.getRight(), area.getBottom()); p.lineTo (area.getRight(), area.getBottom() - arm);
    const auto cx = area.getCentreX();
    const auto cy = area.getCentreY();
    p.startNewSubPath (cx - 6.0f, cy); p.lineTo (cx + 6.0f, cy);
    p.startNewSubPath (cx, cy - 6.0f); p.lineTo (cx, cy + 6.0f);
    g.setColour (ui::accent);
    g.strokePath (p, juce::PathStrokeType (1.45f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
}

void GestureRackAudioProcessorEditor::drawHandOverlay (juce::Graphics& g, juce::Rectangle<float> imageArea,
                                                        const gr::HandSnapshot& hand, juce::Colour colour)
{
    if (! hand.present || imageArea.isEmpty()) return;
    std::array<juce::Point<float>, 21> points {};
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto x = juce::jlimit (0.0f, 1.0f, hand.landmarks[i].x);
        const auto y = juce::jlimit (0.0f, 1.0f, hand.landmarks[i].y);
        points[i] = { imageArea.getX() + x * imageArea.getWidth(), imageArea.getY() + y * imageArea.getHeight() };
    }
    g.setColour (colour.withAlpha (0.92f));
    for (const auto [a, b] : handConnections)
        g.drawLine ({ points[static_cast<size_t> (a)], points[static_cast<size_t> (b)] }, 1.55f);
    for (const auto& point : points)
    {
        g.setColour (ui::canvas.withAlpha (0.66f));
        g.fillEllipse (point.x - 3.0f, point.y - 3.0f, 6.0f, 6.0f);
        g.setColour (colour);
        g.fillEllipse (point.x - 1.7f, point.y - 1.7f, 3.4f, 3.4f);
    }
}

void GestureRackAudioProcessorEditor::drawCameraMotionTelemetry (juce::Graphics& g,
                                                                  const gr::DualHandVisionSnapshot& snapshot)
{
    if (cameraTelemetryBounds.isEmpty()) return;

    auto panel = cameraTelemetryBounds.toFloat();
    g.setColour (ui::viewport);
    g.fillRoundedRectangle (panel, 5.0f);
    g.setColour (ui::border.withAlpha (0.54f));
    g.drawRoundedRectangle (panel.reduced (0.5f), 5.0f, 0.8f);

    auto content = cameraTelemetryBounds.reduced (10, 5);
    auto title = content.removeFromTop (13);
    g.setColour (snapshot.right.present ? ui::textMuted : ui::textFaint);
    g.setFont (ui::metaFont());
    g.drawFittedText (snapshot.right.present ? "RIGHT HAND MOTION" : "RIGHT HAND  --",
                      title, juce::Justification::centredLeft, 1);

    const auto drawAxis = [&] (juce::String label, float value)
    {
        if (content.getHeight() < 18) return;
        auto row = content.removeFromTop (juce::jmin (22, content.getHeight()));
        auto labelArea = row.removeFromLeft (82);
        auto valueArea = row.removeFromRight (38);
        auto trackArea = row.reduced (5, juce::jmax (1, row.getHeight() / 2 - 2));

        g.setColour (ui::textMuted);
        g.setFont (ui::metaFont());
        g.drawFittedText (label, labelArea, juce::Justification::centredLeft, 1);
        g.drawFittedText (snapshot.right.present ? juce::String (juce::jlimit (0.0f, 1.0f, value), 2)
                                                 : juce::String ("--"),
                          valueArea, juce::Justification::centredRight, 1);

        if (trackArea.getWidth() <= 2 || trackArea.getHeight() <= 0) return;
        const auto y = static_cast<float> (trackArea.getCentreY());
        const auto left = static_cast<float> (trackArea.getX());
        const auto right = static_cast<float> (trackArea.getRight());
        const auto centre = (left + right) * 0.5f;
        g.setColour (ui::border.withAlpha (0.72f));
        g.drawLine (left, y, right, y, 1.0f);
        g.setColour (ui::border.withAlpha (0.48f));
        g.drawLine (centre, y - 3.0f, centre, y + 3.0f, 0.8f);

        if (snapshot.right.present)
        {
            const auto normalised = juce::jlimit (0.0f, 1.0f, value);
            const auto markerX = left + normalised * (right - left);
            g.setColour (ui::accent.withAlpha (0.78f));
            g.drawLine (left, y, markerX, y, 1.35f);
            g.setColour (ui::accent);
            g.fillEllipse (markerX - 2.7f, y - 2.7f, 5.4f, 5.4f);
        }
    };

    drawAxis ("X  HORIZONTAL", snapshot.right.palmX);
    drawAxis ("Y  VERTICAL", snapshot.right.height);
}

void GestureRackAudioProcessorEditor::resized()
{
    const auto outer = scaledMetric (uiScale, metrics::outerMargin);
    const auto gap = scaledMetric (uiScale, metrics::gutter);
    const auto topHeight = scaledMetric (uiScale, metrics::topBarHeight);
    auto root = getLocalBounds().reduced (outer);
    topBarBounds = root.removeFromTop (topHeight);
    root.removeFromTop (gap);
    auto workspace = root;

    const auto railMin = scaledMetric (uiScale, 200);
    const auto railMax = scaledMetric (uiScale, 260);
    const auto rightMin = scaledMetric (uiScale, 320);
    const auto rightMax = scaledMetric (uiScale, 380);
    const auto railWidth = juce::jlimit (railMin, railMax, juce::roundToInt (getWidth() * 0.138f));
    const auto rightWidth = juce::jlimit (rightMin, rightMax, juce::roundToInt (getWidth() * 0.203f));

    pluginRailBounds = workspace.removeFromLeft (railWidth);
    workspace.removeFromLeft (gap);
    auto rightColumn = workspace.removeFromRight (rightWidth);
    workspace.removeFromRight (gap);
    auto centerColumn = workspace;

    const auto native = embeddedCanvas != nullptr ? embeddedCanvas->getNativeEditorSize() : juce::Point<int>();
    auto upperHeight = metrics::upperRowHeightFor (centerColumn.getHeight(), native.y);
    upperHeight = juce::jlimit (scaledMetric (uiScale, 260),
                               juce::jmax (scaledMetric (uiScale, 260), centerColumn.getHeight() - gap - scaledMetric (uiScale, 270)),
                               upperHeight);

    pluginPanelBounds = centerColumn.removeFromTop (upperHeight);
    centerColumn.removeFromTop (gap);
    parameterInspector.setBounds (centerColumn);

    cameraPanelBounds = rightColumn.removeFromTop (upperHeight);
    rightColumn.removeFromTop (gap);
    if (gesturePanel != nullptr) gesturePanel->setBounds (rightColumn);

    auto railContent = pluginRailBounds.reduced (scaledMetric (uiScale, 14));
    auto railHeader = railContent.removeFromTop (scaledMetric (uiScale, 28));
    railSearchButton.setBounds (railHeader.removeFromRight (scaledMetric (uiScale, 30)));
    railContent.removeFromTop (scaledMetric (uiScale, 8));
    const auto slotGap = scaledMetric (uiScale, 7);
    const auto available = juce::jmax (1, railContent.getHeight() - slotGap * (GestureRackAudioProcessor::slotCount - 1));
    const auto slotHeight = juce::jmax (38, available / GestureRackAudioProcessor::slotCount);
    for (auto& button : slotButtons)
    {
        button.setBounds (railContent.removeFromTop (slotHeight));
        railContent.removeFromTop (slotGap);
    }

    auto pluginContent = pluginPanelBounds.reduced (scaledMetric (uiScale, 12));
    auto pluginChrome = pluginContent.removeFromTop (scaledMetric (uiScale, 36));
    pluginMoreButton.setBounds (pluginChrome.removeFromRight (scaledMetric (uiScale, 40)));
    pluginChrome.removeFromRight (scaledMetric (uiScale, 8));
    removeButton.setBounds (pluginChrome.removeFromRight (scaledMetric (uiScale, 40)));
    pluginChrome.removeFromRight (scaledMetric (uiScale, 8));
    bypassButton.setBounds (pluginChrome.removeFromRight (scaledMetric (uiScale, 102)));
    pluginContent.removeFromTop (scaledMetric (uiScale, 5));
    pluginViewportBounds = pluginContent;
    pluginViewport.setBounds (pluginViewportBounds);

    auto cameraContent = cameraPanelBounds.reduced (scaledMetric (uiScale, 14));
    cameraContent.removeFromTop (scaledMetric (uiScale, 32));
    cameraTelemetryBounds = cameraContent.removeFromBottom (scaledMetric (uiScale, 68));
    cameraContent.removeFromBottom (scaledMetric (uiScale, 8));
    cameraPreviewBounds = cameraContent;

    auto topRight = topBarBounds;
    menuButton.setBounds (topRight.removeFromRight (scaledMetric (uiScale, 34)));
    topRight.removeFromRight (scaledMetric (uiScale, 8));
    settingsButton.setBounds (topRight.removeFromRight (scaledMetric (uiScale, 34)));

    if (pluginBrowser != nullptr)
    {
        auto browserBounds = getLocalBounds().reduced (outer);
        browserBounds.removeFromTop (topHeight + gap);
        pluginBrowser->setBounds (browserBounds);
        if (pluginBrowser->isVisible()) pluginBrowser->toFront (false);
    }
    updateViewportScrollbars();
}

bool GestureRackAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    const auto code = juce::CharacterFunctions::toLowerCase (static_cast<juce_wchar> (key.getKeyCode()));
    if (modifiers.isCommandDown() && code == static_cast<juce_wchar> ('z'))
    {
        if (modifiers.isShiftDown()) return parameterInspector.redoLastMapping();
        return parameterInspector.undoLastMapping();
    }
    if (modifiers.isCommandDown() && code == static_cast<juce_wchar> ('y'))
        return parameterInspector.redoLastMapping();
    if (key == juce::KeyPress::escapeKey && processor.isParameterLearnArmed())
    {
        processor.cancelParameterLearn();
        return true;
    }
    return false;
}
