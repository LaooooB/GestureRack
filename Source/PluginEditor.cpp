#include "PluginEditor.h"
#include "PluginBrowser.h"
#include <cmath>
#include <limits>

namespace
{
using namespace gr;
namespace ui = gr::ui;

constexpr std::array<std::pair<int, int>, 21> handConnections {{
    {0,1}, {1,2}, {2,3}, {3,4}, {0,5}, {5,6}, {6,7}, {7,8}, {5,9}, {9,10}, {10,11}, {11,12},
    {9,13}, {13,14}, {14,15}, {15,16}, {13,17}, {17,18}, {18,19}, {19,20}, {17,0}
}};

constexpr int outerMargin = 18;
constexpr int headerHeight = 52;
constexpr int footerHeight = 48;
constexpr int layoutGap = 12;
constexpr int railWidth = 168;
constexpr int cameraColumnWidth = 326;
constexpr int gesturePanelHeight = 178;
constexpr int pluginHeaderHeight = 42;
constexpr int minimumParameterHeight = 228;
constexpr int minimumEditorWidth = 1120;
constexpr int minimumEditorHeight = 720;
constexpr int defaultEditorWidth = 1440;
constexpr int defaultEditorHeight = 900;

bool handRolesTrusted (const gr::DualHandVisionSnapshot& snapshot)
{
    if (snapshot.protocol < 2) return true;
    if (snapshot.handCalibrationActive) return false;
    const auto source = snapshot.handRoleSource.trim().toUpperCase();
    return source.isNotEmpty() && source != "UNCALIBRATED" && source != "CALIBRATING" && source != "DEFAULT";
}

void drawDashedPath (juce::Graphics& g, const juce::Path& source, juce::Colour colour,
                     float thickness = 1.0f, float dash = 4.0f, float gap = 4.0f)
{
    const float pattern[] { dash, gap };
    juce::Path dashed;
    juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
        .createDashedStroke (dashed, source, pattern, 2);
    g.setColour (colour);
    g.fillPath (dashed);
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
        if (editor != nullptr)
            addAndMakeVisible (*editor);
        syncNativeSize();
        repaint();
    }

    void detach()
    {
        if (auto* editor = nativeEditor.getComponent())
            removeChildComponent (editor);
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
            auto area = getLocalBounds().reduced (24);
            g.setColour (ui::textMuted);
            g.setFont (ui::controlFont());
            g.drawText (name.isNotEmpty() ? "NO NATIVE EDITOR" : "NO DEVICE",
                        area, juce::Justification::centred);
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
        if (getWidth() != desired.x || getHeight() != desired.y)
            setSize (desired.x, desired.y);
        else
            resized();
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
        startTimerHz (60);
    }

    ~GesturePanel() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        ui::drawPanel (g, getLocalBounds().toFloat(), true);
        ui::drawSectionTitle (g, "GESTURES", { 12, 8, 130, 20 });

        const auto enabled = owner.processor.isGestureEnabled();
        const auto enableFocus = juce::jmax (enableHover, enabled ? 1.0f : 0.0f);
        g.setColour (ui::blend (ui::control, ui::canvas, enableHover * 0.32f));
        g.fillRoundedRectangle (enableRect.toFloat(), ui::controlRadius);
        g.setColour (ui::blend (ui::border, ui::accent, enableFocus));
        g.drawRoundedRectangle (enableRect.toFloat().reduced (0.5f), ui::controlRadius, 1.0f);
        g.setColour (enabled ? ui::accent : ui::textMuted);
        g.setFont (ui::controlFont());
        g.drawText (enabled ? "ON" : "OFF", enableRect, juce::Justification::centred);

        const std::array<gr::ControlGesture, 7> gestures {
            gr::ControlGesture::openPalm, gr::ControlGesture::closedFist, gr::ControlGesture::victory,
            gr::ControlGesture::thumbUp, gr::ControlGesture::thumbDown,
            gr::ControlGesture::pointRight, gr::ControlGesture::pointLeft };
        const auto live = owner.processor.getLiveRightGesture();

        for (int i = 0; i < 7; ++i)
        {
            const auto gesture = gestures[static_cast<size_t> (i)];
            const auto rect = chipRects[static_cast<size_t> (i)];
            if (rect.isEmpty()) continue;
            const auto liveAmount = gesture == live ? 1.0f : 0.0f;
            const auto focus = juce::jmax (hover[static_cast<size_t> (i)], liveAmount);
            auto fill = ui::blend (ui::control, ui::canvas, hover[static_cast<size_t> (i)] * 0.36f);
            if (liveAmount > 0.0f) fill = ui::blend (fill, ui::accent, 0.08f);
            g.setColour (fill);
            g.fillRoundedRectangle (rect.toFloat(), ui::controlRadius);
            g.setColour (ui::blend (ui::border, ui::accent, focus));
            g.drawRoundedRectangle (rect.toFloat().reduced (0.5f), ui::controlRadius, 1.0f);
            g.setColour (liveAmount > 0.0f ? ui::accent : ui::text);
            g.setFont (ui::font (9.6f, juce::Font::bold));
            g.drawFittedText (gr::controlGestureToShortLabel (gesture), rect.reduced (4, 0),
                              juce::Justification::centred, 1);
        }

        const auto drawTarget = [&] (juce::Rectangle<int> rect, const juce::String& label)
        {
            const auto hot = dragging && rect.contains (dragPoint);
            g.setColour (ui::blend (ui::surfaceHigh, ui::canvas, hot ? 0.25f : 0.0f));
            g.fillRoundedRectangle (rect.toFloat(), ui::controlRadius);
            g.setColour (hot ? ui::accent : ui::border);
            g.drawRoundedRectangle (rect.toFloat().reduced (0.5f), ui::controlRadius, 1.0f);
            g.setColour (hot ? ui::accent : ui::textMuted);
            g.setFont (ui::font (9.0f, juce::Font::bold));
            g.drawText (label, rect, juce::Justification::centred);
        };
        drawTarget (activeRect, "ACTIVE");
        drawTarget (bypassRect, "BYPASS");

        if (dragging)
        {
            auto bubble = juce::Rectangle<float> (74.0f, 22.0f).withCentre (dragPoint.toFloat());
            bubble = bubble.constrainedWithin (getLocalBounds().toFloat().reduced (2.0f));
            g.setColour (ui::canvas.withAlpha (0.92f));
            g.fillRoundedRectangle (bubble, 6.0f);
            g.setColour (ui::accent);
            g.drawRoundedRectangle (bubble, 6.0f, 1.0f);
            g.setFont (ui::font (9.0f, juce::Font::bold));
            g.drawText (gr::controlGestureToShortLabel (draggedGesture), bubble.toNearestInt(),
                        juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto content = getLocalBounds().reduced (12);
        auto header = content.removeFromTop (22);
        enableRect = header.removeFromRight (48);
        content.removeFromTop (8);

        const auto setRow = [this] (juce::Rectangle<int> row, int start, int count)
        {
            constexpr int gap = 5;
            const auto width = juce::jmax (1, (row.getWidth() - gap * (count - 1)) / count);
            for (int i = 0; i < count; ++i)
            {
                chipRects[static_cast<size_t> (start + i)] = row.removeFromLeft (width);
                if (i + 1 < count) row.removeFromLeft (gap);
            }
        };

        setRow (content.removeFromTop (29), 0, 4);
        content.removeFromTop (5);
        setRow (content.removeFromTop (29), 4, 3);
        content.removeFromTop (9);

        auto targets = content.removeFromTop (27);
        activeRect = targets.removeFromLeft ((targets.getWidth() - 6) / 2);
        targets.removeFromLeft (6);
        bypassRect = targets;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        pointer = e.getPosition();
        updateHoverTargets();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        pointer = { -9999, -9999 };
        updateHoverTargets();
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

        const std::array<gr::ControlGesture, 7> gestures {
            gr::ControlGesture::openPalm, gr::ControlGesture::closedFist, gr::ControlGesture::victory,
            gr::ControlGesture::thumbUp, gr::ControlGesture::thumbDown,
            gr::ControlGesture::pointRight, gr::ControlGesture::pointLeft };
        for (int i = 0; i < 7; ++i)
            if (chipRects[static_cast<size_t> (i)].contains (pointer))
            {
                draggedGesture = gestures[static_cast<size_t> (i)];
                dragging = true;
                dragPoint = pointer;
                repaint();
                break;
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
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        dragPoint = e.getPosition();
        juce::String error;
        if (activeRect.contains (dragPoint))
            owner.processor.addSlotActionGestureMapping (draggedGesture, gr::MappingMode::triggerSetActive, error);
        else if (bypassRect.contains (dragPoint))
            owner.processor.addSlotActionGestureMapping (draggedGesture, gr::MappingMode::triggerSetBypassed, error);
        else
        {
            const auto ownerPoint = owner.getLocalPoint (this, dragPoint);
            if (owner.parameterInspector.getBounds().contains (ownerPoint))
                owner.parameterInspector.dropGestureAt (
                    draggedGesture, owner.parameterInspector.getLocalPoint (&owner, ownerPoint));
        }

        owner.parameterInspector.clearGestureDragPreview();
        dragging = false;
        draggedGesture = gr::ControlGesture::unknown;
        updateHoverTargets();
        repaint();
    }

private:
    void updateHoverTargets()
    {
        enableHoverTarget = enableRect.contains (pointer) ? 1.0f : 0.0f;
        for (int i = 0; i < 7; ++i)
            hoverTarget[static_cast<size_t> (i)] = chipRects[static_cast<size_t> (i)].contains (pointer) ? 1.0f : 0.0f;
    }

    void timerCallback() override
    {
        bool needsRepaint = false;
        const auto move = [&needsRepaint] (float& value, float target)
        {
            const auto speed = target > value ? 0.22f : 0.15f;
            const auto next = value + (target - value) * speed;
            if (std::abs (next - value) > 0.001f) needsRepaint = true;
            value = std::abs (target - next) < 0.008f ? target : next;
        };
        move (enableHover, enableHoverTarget);
        for (int i = 0; i < 7; ++i)
            move (hover[static_cast<size_t> (i)], hoverTarget[static_cast<size_t> (i)]);
        if (needsRepaint || dragging) repaint();
    }

    GestureRackAudioProcessorEditor& owner;
    std::array<juce::Rectangle<int>, 7> chipRects {};
    std::array<float, 7> hover {};
    std::array<float, 7> hoverTarget {};
    juce::Rectangle<int> enableRect;
    juce::Rectangle<int> activeRect;
    juce::Rectangle<int> bypassRect;
    juce::Point<int> pointer { -9999, -9999 };
    juce::Point<int> dragPoint;
    float enableHover = 0.0f;
    float enableHoverTarget = 0.0f;
    bool dragging = false;
    gr::ControlGesture draggedGesture = gr::ControlGesture::unknown;
};

void GestureRackAudioProcessorEditor::RackSlotButton::setSlotVisualState (
    int slotNumberToUse, bool loadedToUse, bool bypassedToUse, int mappingCountToUse, juce::String pluginNameToUse)
{
    slotNumber = slotNumberToUse;
    loaded = loadedToUse;
    bypassed = bypassedToUse;
    mappingCount = mappingCountToUse;
    pluginName = std::move (pluginNameToUse);
    repaint();
}

void GestureRackAudioProcessorEditor::RackSlotButton::paintButton (juce::Graphics& g, bool, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto selected = getToggleState();
    const auto focus = juce::jmax (getHoverAmount(), selected ? 1.0f : 0.0f);
    auto fill = ui::blend (ui::surfaceHigh, ui::canvas, getHoverAmount() * 0.36f);
    if (selected) fill = ui::blend (fill, ui::accent, 0.075f);
    if (down) fill = ui::blend (fill, ui::canvas, 0.28f);
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 8.0f);

    const auto line = ui::blend (ui::border, ui::accent, focus);
    if (! loaded)
    {
        juce::Path outline;
        outline.addRoundedRectangle (bounds.reduced (1.0f), 8.0f);
        drawDashedPath (g, outline, line.withAlpha (0.88f), 1.0f, 5.0f, 4.0f);

        auto plusBounds = bounds.withSizeKeepingCentre (30.0f, 30.0f);
        juce::Path plus;
        plus.startNewSubPath (plusBounds.getCentreX(), plusBounds.getY() + 3.0f);
        plus.lineTo (plusBounds.getCentreX(), plusBounds.getBottom() - 3.0f);
        plus.startNewSubPath (plusBounds.getX() + 3.0f, plusBounds.getCentreY());
        plus.lineTo (plusBounds.getRight() - 3.0f, plusBounds.getCentreY());
        drawDashedPath (g, plus, selected ? ui::accent : ui::gray.withAlpha (0.82f), 1.5f, 3.0f, 2.5f);
    }
    else
    {
        g.setColour (line);
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
    }

    auto inner = getLocalBounds().reduced (10, 7);
    auto numberArea = inner.removeFromTop (15);
    g.setFont (ui::font (9.0f, juce::Font::bold));
    g.setColour (selected ? ui::accent : ui::textMuted);
    g.drawText ("0" + juce::String (slotNumber), numberArea, juce::Justification::centredLeft);

    if (loaded)
    {
        auto footer = inner.removeFromBottom (14);
        g.setFont (ui::font (11.0f, juce::Font::bold));
        g.setColour (bypassed ? ui::textMuted : ui::text);
        g.drawFittedText (pluginName, inner, juce::Justification::centredLeft, 2);

        g.setFont (ui::metaFont());
        g.setColour (ui::textMuted);
        if (bypassed)
            g.drawText ("BYP", footer.removeFromLeft (34), juce::Justification::centredLeft);
        if (mappingCount > 0)
            g.drawText (juce::String (mappingCount) + " MAP", footer, juce::Justification::centredRight);
    }
}

GestureRackAudioProcessorEditor::GestureRackAudioProcessorEditor (GestureRackAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p), parameterInspector (p)
{
    setSize (defaultEditorWidth, defaultEditorHeight);
    setResizable (true, true);
    setResizeLimits (minimumEditorWidth, minimumEditorHeight, 2600, 1800);

    for (int i = 0; i < GestureRackAudioProcessor::slotCount; ++i)
    {
        auto& button = slotButtons[static_cast<size_t> (i)];
        addAndMakeVisible (button);
        button.setClickingTogglesState (false);
        button.setTriggeredOnMouseDown (true);
        button.onClick = [this, i]
        {
            processor.setSelectedSlot (i);
            updateSlotButtons();
            updateEmbeddedEditor();
            repaint();
        };
        button.dragDown = [this, i] (const juce::MouseEvent& e) { handleSlotMouseDown (i, e); };
        button.dragMove = [this] (const juce::MouseEvent& e) { handleSlotMouseDrag (e); };
        button.dragUp = [this] (const juce::MouseEvent& e) { handleSlotMouseUp (e); };
    }

    for (auto* button : { &loadButton, &removeButton, &bypassButton, &calibrateHandsButton, &swapHandsButton })
        addAndMakeVisible (*button);

    loadButton.onClick = [this] { showPluginBrowser(); };
    removeButton.onClick = [this] { removeSelectedPlugin(); };
    bypassButton.onClick = [this]
    {
        const auto slot = processor.getSelectedSlot();
        processor.setSlotBypassed (slot, ! processor.isSlotBypassed (slot));
        updateSlotButtons();
        repaint();
    };
    calibrateHandsButton.onClick = [this] { processor.beginHandCalibration(); };
    swapHandsButton.onClick = [this] { processor.toggleSwapHandedness(); };

    addAndMakeVisible (parameterInspector);

    embeddedCanvas = std::make_unique<EmbeddedEditorCanvas>();
    addAndMakeVisible (pluginViewport);
    pluginViewport.setViewedComponent (embeddedCanvas.get(), false);
    pluginViewport.setScrollBarsShown (false, false);
    pluginViewport.setScrollBarThickness (8);
    pluginViewport.setColour (juce::ScrollBar::thumbColourId, ui::gray.withAlpha (0.62f));
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
    adaptEditorToNativeSize (true);
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
    const auto rolesTrusted = handRolesTrusted (vision);
    frameReader.readLatest (cameraFrame);

    bypassButton.setButtonText (bypassed ? "BYPASS" : "ACTIVE");
    bypassButton.setToggleState (loaded && ! bypassed, juce::dontSendNotification);
    removeButton.setEnabled (loaded);
    bypassButton.setEnabled (loaded);
    calibrateHandsButton.setButtonText (vision.handCalibrationActive ? "SHOW RIGHT"
                                        : (rolesTrusted ? "RECAL RIGHT" : "CAL RIGHT"));
    swapHandsButton.setButtonText (vision.swapHandedness ? "L/R SWAPPED" : "SWAP L/R");
    calibrateHandsButton.setEnabled (connected && ! vision.handCalibrationActive);
    swapHandsButton.setEnabled (connected && ! vision.handCalibrationActive);

    updateSlotButtons();
    updateEmbeddedEditor();
    if (gesturePanel != nullptr) gesturePanel->repaint();
    repaint();
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
        {
            auto* native = processor.getOrCreateSlotEditor (slot);
            embeddedCanvas->attach (native, processor.getSlotPluginName (slot));
        }
        else
            embeddedCanvas->attach (nullptr, {});
    }

    const auto nativeChanged = embeddedCanvas->syncNativeSize();
    const auto nativeSize = embeddedCanvas->getNativeEditorSize();
    if (identityChanged || nativeChanged || nativeSize != lastNativeEditorSize)
    {
        lastNativeEditorSize = nativeSize;
        adaptEditorToNativeSize (true);
    }

    updateViewportScrollbars();
}

void GestureRackAudioProcessorEditor::adaptEditorToNativeSize (bool force)
{
    if (adaptiveResizeInProgress || embeddedCanvas == nullptr) return;
    const auto native = embeddedCanvas->getNativeEditorSize();
    if (! force && native.x <= 0 && native.y <= 0) return;

    const auto centerWidth = native.x > 0 ? juce::jmax (660, native.x + 28) : 760;
    const auto pluginHeight = native.y > 0 ? juce::jmax (260, native.y + pluginHeaderHeight + 24) : 390;

    constexpr int railMinimumWorkspaceHeight = 610;
    constexpr int rightMinimumWorkspaceHeight = 610;
    const auto centerWorkspaceHeight = pluginHeight + layoutGap + minimumParameterHeight;
    const auto workspaceHeight = juce::jmax (railMinimumWorkspaceHeight,
                                              juce::jmax (rightMinimumWorkspaceHeight, centerWorkspaceHeight));

    auto desiredWidth = outerMargin * 2 + railWidth + layoutGap + centerWidth + layoutGap + cameraColumnWidth;
    auto desiredHeight = outerMargin * 2 + headerHeight + layoutGap + workspaceHeight + layoutGap + footerHeight;

    auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect (getScreenBounds());
    if (display == nullptr) display = displays.getPrimaryDisplay();

    int maxWidth = 2600;
    int maxHeight = 1800;
    if (display != nullptr)
    {
        const auto user = display->userBounds.toNearestInt();
        maxWidth = juce::jmax (minimumEditorWidth, juce::roundToInt (user.getWidth() * 0.96f));
        maxHeight = juce::jmax (minimumEditorHeight, juce::roundToInt (user.getHeight() * 0.94f));
    }

    desiredWidth = juce::jlimit (minimumEditorWidth, maxWidth, desiredWidth);
    desiredHeight = juce::jlimit (minimumEditorHeight, maxHeight, desiredHeight);
    setResizeLimits (minimumEditorWidth, minimumEditorHeight, maxWidth, maxHeight);

    if (getWidth() == desiredWidth && getHeight() == desiredHeight) return;
    adaptiveResizeInProgress = true;
    setSize (desiredWidth, desiredHeight);
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

void GestureRackAudioProcessorEditor::showPluginBrowser()
{
    if (pluginBrowser == nullptr) return;
    parameterInspector.clearGestureDragPreview();
    slotDragging = false;
    slotDragSource = slotDragTarget = -1;
    pluginBrowser->showForSlot (processor.getSelectedSlot());
    repaint();
}

void GestureRackAudioProcessorEditor::hidePluginBrowser()
{
    if (pluginBrowser != nullptr) pluginBrowser->setVisible (false);
    repaint();
}

void GestureRackAudioProcessorEditor::updateSlotButtons()
{
    const auto selected = processor.getSelectedSlot();
    for (int i = 0; i < GestureRackAudioProcessor::slotCount; ++i)
    {
        auto& button = slotButtons[static_cast<size_t> (i)];
        const auto loaded = processor.isSlotLoaded (i);
        const auto bypassed = processor.isSlotBypassed (i);
        const auto mappingCount = processor.getSlotMappingCount (i);
        auto name = processor.getSlotPluginName (i);
        if (name.length() > 18) name = name.substring (0, 16) + "..";
        button.setSlotVisualState (i + 1, loaded, bypassed, mappingCount, name);
        button.setTooltip ("Slot " + juce::String (i + 1) + " / " + processor.getSlotPluginName (i)
                           + " / " + juce::String (mappingCount) + " mappings");
        button.setToggleState (i == selected, juce::dontSendNotification);
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
    if (slotButtons.empty()) return slotDragSource;
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
    if (slotDragTarget > slotDragSource)
        line.setY (static_cast<float> (target.getBottom() + 1));
    g.setColour (ui::accent);
    g.fillRoundedRectangle (line, 1.0f);
}

void GestureRackAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::canvas);

    g.setColour (ui::text);
    g.setFont (ui::titleFont());
    g.drawText ("GESTURE RACK", outerMargin, 10, 270, 30, juce::Justification::centredLeft);

    const auto snapshot = processor.getDualHandVisionSnapshot();
    const auto connected = processor.isVisionConnected();
    const auto rolesTrusted = handRolesTrusted (snapshot);
    const auto sr = processor.getHostSampleRateForUi();
    const auto bs = processor.getHostBlockSizeForUi();

    juce::String health = connected
        ? juce::String (snapshot.captureFps, 0) + " FPS  ·  " + juce::String (snapshot.captureToResultMs, 0) + " MS"
        : juce::String ("VISION OFFLINE");
    if (sr > 0.0 && bs > 0)
        health += "  ·  " + juce::String (sr / 1000.0, 1) + "K / " + juce::String (bs);
    g.setColour (connected && rolesTrusted ? ui::textMuted : ui::accent);
    g.setFont (ui::metaFont());
    g.drawFittedText (health, 300, 12, getWidth() - 318, 26, juce::Justification::centredRight, 1);

    ui::drawPanel (g, pluginRailBounds.toFloat(), true);
    ui::drawPanel (g, pluginPanelBounds.toFloat(), true);
    ui::drawPanel (g, cameraPanelBounds.toFloat(), true);
    ui::drawPanel (g, footerPanelBounds.toFloat(), false);

    ui::drawSectionTitle (g, "PLUGINS", pluginRailBounds.reduced (12).removeFromTop (24));

    auto pluginHeader = pluginPanelBounds.reduced (12).removeFromTop (pluginHeaderHeight - 8);
    auto pluginTitle = pluginHeader;
    pluginTitle.removeFromRight (170);
    ui::drawSectionTitle (g, "PLUGIN", pluginTitle.removeFromLeft (76));
    pluginTitle.removeFromLeft (8);
    const auto selected = processor.getSelectedSlot();
    const auto loaded = processor.isSlotLoaded (selected);
    auto pluginName = processor.getSlotPluginName (selected);
    if (! loaded) pluginName = "NO DEVICE";
    g.setColour (loaded ? ui::text : ui::textMuted);
    g.setFont (ui::font (11.0f, juce::Font::bold));
    g.drawFittedText ("S0" + juce::String (selected + 1) + "  ·  " + pluginName,
                      pluginTitle, juce::Justification::centredLeft, 1);

    if (const auto error = processor.getSlotLastError (selected); error.isNotEmpty())
    {
        auto errorArea = pluginPanelBounds.reduced (12).removeFromBottom (20);
        g.setColour (ui::accent);
        g.setFont (ui::metaFont());
        g.drawFittedText (error, errorArea, juce::Justification::centredLeft, 1);
    }

    auto cameraHeader = cameraPanelBounds.reduced (12).removeFromTop (24);
    ui::drawSectionTitle (g, "CAMERA", cameraHeader);

    g.setColour (ui::viewport);
    g.fillRoundedRectangle (cameraPreviewBounds.toFloat(), 8.0f);
    g.setColour (ui::border.withAlpha (0.55f));
    g.drawRoundedRectangle (cameraPreviewBounds.toFloat().reduced (0.5f), 8.0f, 1.0f);

    juce::Rectangle<int> imageRect;
    if (connected && cameraFrame.isValid())
    {
        const auto iw = cameraFrame.image.getWidth();
        const auto ih = cameraFrame.image.getHeight();
        const auto scale = juce::jmin (static_cast<float> (cameraPreviewBounds.getWidth()) / static_cast<float> (iw),
                                      static_cast<float> (cameraPreviewBounds.getHeight()) / static_cast<float> (ih));
        const auto dw = juce::jmax (1, juce::roundToInt (iw * scale));
        const auto dh = juce::jmax (1, juce::roundToInt (ih * scale));
        imageRect = juce::Rectangle<int> (dw, dh).withCentre (cameraPreviewBounds.getCentre());
        g.drawImage (cameraFrame.image, imageRect.getX(), imageRect.getY(), imageRect.getWidth(), imageRect.getHeight(),
                     0, 0, iw, ih, false);
        if (cameraFrame.timestampMs == snapshot.timestampMs)
        {
            drawHandOverlay (g, imageRect.toFloat(), snapshot.left, ui::gray);
            drawHandOverlay (g, imageRect.toFloat(), snapshot.right, ui::accent);
        }
    }
    else
    {
        g.setColour (ui::textMuted);
        g.setFont (ui::controlFont());
        g.drawText (connected ? "WAITING FOR CAMERA" : "VISION OFFLINE",
                    cameraPreviewBounds, juce::Justification::centred);
    }

    auto info = cameraInfoBounds;
    const auto drawInfo = [&] (juce::String key, juce::String value, bool accentValue = false)
    {
        auto row = info.removeFromTop (19);
        g.setColour (ui::textMuted);
        g.setFont (ui::metaFont());
        g.drawText (key, row.removeFromLeft (72), juce::Justification::centredLeft);
        g.setColour (accentValue ? ui::accent : ui::text);
        g.setFont (ui::font (9.4f, juce::Font::bold));
        g.drawFittedText (value, row, juce::Justification::centredRight, 1);
    };

    juce::String handValue;
    if (! connected) handValue = "OFFLINE";
    else if (snapshot.handCalibrationActive) handValue = "CALIBRATING";
    else if (! rolesTrusted) handValue = "CALIBRATE";
    else handValue = snapshot.swapHandedness ? "SWAPPED" : "READY";
    drawInfo ("HAND", handValue, ! connected || ! rolesTrusted);

    juce::String liveValue = "--";
    if (rolesTrusted && snapshot.right.present)
        liveValue = gr::controlGestureToShortLabel (snapshot.right.stableGesture);
    drawInfo ("GESTURE", liveValue, false);

    const auto leftSlot = rolesTrusted && snapshot.left.present && snapshot.left.stableSlot > 0
        ? juce::String (snapshot.left.stableSlot) : juce::String ("--");
    drawInfo ("SELECT", "SLOT " + leftSlot, false);
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
        g.drawLine ({ points[static_cast<size_t> (a)], points[static_cast<size_t> (b)] }, 1.7f);
    for (const auto& point : points)
    {
        g.setColour (ui::canvas.withAlpha (0.60f));
        g.fillEllipse (point.x - 3.3f, point.y - 3.3f, 6.6f, 6.6f);
        g.setColour (colour);
        g.fillEllipse (point.x - 1.9f, point.y - 1.9f, 3.8f, 3.8f);
    }
}

void GestureRackAudioProcessorEditor::resized()
{
    auto root = getLocalBounds().reduced (outerMargin);
    root.removeFromTop (headerHeight);
    root.removeFromTop (layoutGap);
    footerPanelBounds = root.removeFromBottom (footerHeight);
    root.removeFromBottom (layoutGap);
    auto workspace = root;

    pluginRailBounds = workspace.removeFromLeft (railWidth);
    workspace.removeFromLeft (layoutGap);
    auto rightColumn = workspace.removeFromRight (cameraColumnWidth);
    workspace.removeFromRight (layoutGap);
    auto center = workspace;

    auto railContent = pluginRailBounds.reduced (12);
    railContent.removeFromTop (30);
    constexpr int slotGap = 6;
    const auto availableForSlots = juce::jmax (1, railContent.getHeight() - slotGap * (GestureRackAudioProcessor::slotCount - 1));
    const auto slotHeight = juce::jmax (38, availableForSlots / GestureRackAudioProcessor::slotCount);
    for (auto& button : slotButtons)
    {
        button.setBounds (railContent.removeFromTop (slotHeight));
        railContent.removeFromTop (slotGap);
    }

    auto gestureBounds = rightColumn.removeFromBottom (juce::jmin (gesturePanelHeight, rightColumn.getHeight() / 3));
    rightColumn.removeFromBottom (layoutGap);
    cameraPanelBounds = rightColumn;
    if (gesturePanel != nullptr) gesturePanel->setBounds (gestureBounds);

    auto cameraContent = cameraPanelBounds.reduced (12);
    cameraContent.removeFromTop (30);
    cameraInfoBounds = cameraContent.removeFromBottom (66);
    cameraContent.removeFromBottom (8);
    cameraPreviewBounds = cameraContent;

    const auto native = embeddedCanvas != nullptr ? embeddedCanvas->getNativeEditorSize() : juce::Point<int>();
    const auto desiredPluginHeight = native.y > 0 ? native.y + pluginHeaderHeight + 24 : 360;
    const auto maxPluginHeight = juce::jmax (180, center.getHeight() - layoutGap - minimumParameterHeight);
    const auto pluginHeight = juce::jlimit (180, maxPluginHeight, desiredPluginHeight);
    pluginPanelBounds = center.removeFromTop (pluginHeight);
    center.removeFromTop (layoutGap);
    parameterInspector.setBounds (center);

    auto pluginContent = pluginPanelBounds.reduced (12);
    auto pluginHeader = pluginContent.removeFromTop (pluginHeaderHeight - 8);
    removeButton.setBounds (pluginHeader.removeFromRight (72));
    pluginHeader.removeFromRight (6);
    bypassButton.setBounds (pluginHeader.removeFromRight (78));
    pluginContent.removeFromTop (4);
    pluginViewportBounds = pluginContent;
    pluginViewport.setBounds (pluginViewportBounds);

    auto footer = footerPanelBounds.reduced (10, 7);
    loadButton.setBounds (footer.removeFromLeft (100));
    footer.removeFromLeft (12);
    calibrateHandsButton.setBounds (footer.removeFromLeft (108));
    footer.removeFromLeft (8);
    swapHandsButton.setBounds (footer.removeFromLeft (104));

    if (pluginBrowser != nullptr)
    {
        auto browserBounds = getLocalBounds().reduced (outerMargin);
        browserBounds.removeFromTop (headerHeight);
        pluginBrowser->setBounds (browserBounds);
        pluginBrowser->toFront (false);
    }

    updateViewportScrollbars();
}
