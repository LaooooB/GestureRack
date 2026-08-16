#include "PluginEditor.h"
#include "PluginBrowser.h"
#include <cmath>
#include <limits>

namespace
{
constexpr std::array<std::pair<int, int>, 21> handConnections {{
    {0,1}, {1,2}, {2,3}, {3,4},
    {0,5}, {5,6}, {6,7}, {7,8},
    {5,9}, {9,10}, {10,11}, {11,12},
    {9,13}, {13,14}, {14,15}, {15,16},
    {13,17}, {17,18}, {18,19}, {19,20},
    {17,0}
}};

const juce::Colour kBg        { 18, 20, 24 };
const juce::Colour kCard      { 27, 30, 36 };
const juce::Colour kRaised    { 35, 39, 46 };
const juce::Colour kBorder    { 55, 61, 71 };
const juce::Colour kTitle     { 232, 235, 240 };
const juce::Colour kSecondary { 139, 148, 162 };
const juce::Colour kAccent    { 245, 178, 60 };
const juce::Colour kBlue      { 86, 156, 235 };
const juce::Colour kGreen     { 93, 190, 126 };
const juce::Colour kRed       { 225, 94, 94 };
const juce::Colour kCameraBg  { 10, 12, 15 };

bool handRolesTrusted (const gr::DualHandVisionSnapshot& snapshot)
{
    if (snapshot.protocol < 2)
        return true;
    if (snapshot.handCalibrationActive)
        return false;

    const auto source = snapshot.handRoleSource.trim().toUpperCase();
    return source.isNotEmpty()
        && source != "UNCALIBRATED"
        && source != "CALIBRATING"
        && source != "DEFAULT";
}

juce::Colour darkButtonColour (bool enabled)
{
    return enabled ? kRaised : kCard;
}
}

GestureRackAudioProcessorEditor::GestureRackAudioProcessorEditor (GestureRackAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p),
      parameterInspector (p)
{
    setSize (1280, 760);
    setResizable (true, true);
    setResizeLimits (1000, 640, 1800, 1100);

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
            repaint();
        };
        button.dragDown = [this, i] (const juce::MouseEvent& e)
        {
            handleSlotMouseDown (i, e);
        };
        button.dragMove = [this] (const juce::MouseEvent& e)
        {
            handleSlotMouseDrag (e);
        };
        button.dragUp = [this] (const juce::MouseEvent& e)
        {
            handleSlotMouseUp (e);
        };
        button.hoverMove = [this] (const juce::MouseEvent& e)
        {
            lastMousePos = e.getEventRelativeTo (this).getPosition();
            repaint();
        };
        button.hoverExit = [this]
        {
            lastMousePos = { -9999, -9999 };
            repaint();
        };
    }

    addAndMakeVisible (loadButton);
    addAndMakeVisible (openButton);
    addAndMakeVisible (removeButton);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (enableButton);
    addAndMakeVisible (calibrateHandsButton);
    addAndMakeVisible (swapHandsButton);
    addAndMakeVisible (parameterInspector);

    for (auto* button : { &loadButton, &openButton, &removeButton, &bypassButton,
                          &enableButton, &calibrateHandsButton, &swapHandsButton })
    {
        button->setColour (juce::TextButton::buttonColourId, kRaised);
        button->setColour (juce::TextButton::buttonOnColourId, kBlue);
        button->setColour (juce::TextButton::textColourOffId, kTitle);
        button->setColour (juce::TextButton::textColourOnId, kTitle);
    }

    loadButton.onClick = [this] { showPluginBrowser(); };
    openButton.onClick = [this] { processor.openChildEditor(); };
    removeButton.onClick = [this] { processor.removeSlotPlugin (processor.getSelectedSlot()); };
    bypassButton.onClick = [this]
    {
        const auto slot = processor.getSelectedSlot();
        processor.setSlotBypassed (slot, ! processor.isSlotBypassed (slot));
    };
    enableButton.onClick = [this]
    {
        processor.setGestureEnabled (! processor.isGestureEnabled());
        repaint();
    };
    calibrateHandsButton.onClick = [this]
    {
        processor.beginHandCalibration();
    };
    swapHandsButton.onClick = [this]
    {
        processor.toggleSwapHandedness();
    };

    pluginBrowser = std::make_unique<PluginBrowserComponent> (
        [this] (int slotIndex, const juce::PluginDescription& description)
        {
            processor.setSelectedSlot (slotIndex);
            processor.loadPluginDescription (slotIndex, description);
            updateSlotButtons();
            repaint();
        },
        [this] { hidePluginBrowser(); });
    addAndMakeVisible (*pluginBrowser);
    pluginBrowser->setVisible (false);

    updateSlotButtons();
    startTimerHz (30);
}

GestureRackAudioProcessorEditor::~GestureRackAudioProcessorEditor()
{
    stopTimer();
    pluginBrowser.reset();
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

    enableButton.setButtonText (processor.isGestureEnabled() ? "GESTURE ON" : "GESTURE OFF");
    bypassButton.setButtonText (bypassed ? "BYPASSED" : "ACTIVE");
    calibrateHandsButton.setButtonText (vision.handCalibrationActive ? "SHOW RIGHT"
                                        : (rolesTrusted ? "RECAL RIGHT" : "CAL RIGHT"));
    swapHandsButton.setButtonText (vision.swapHandedness ? "L/R SWAPPED" : "SWAP L/R");

    openButton.setEnabled (loaded);
    removeButton.setEnabled (loaded);
    bypassButton.setEnabled (loaded);
    calibrateHandsButton.setEnabled (connected && ! vision.handCalibrationActive);
    swapHandsButton.setEnabled (connected && ! vision.handCalibrationActive);

    calibrateHandsButton.setColour (juce::TextButton::buttonColourId,
                                    connected && ! rolesTrusted ? kAccent.withAlpha (0.28f)
                                                                : darkButtonColour (true));
    calibrateHandsButton.setColour (juce::TextButton::textColourOffId, kTitle);

    updateSlotButtons();
    repaint();
}

void GestureRackAudioProcessorEditor::showPluginBrowser()
{
    if (pluginBrowser == nullptr)
        return;

    parameterInspector.clearGestureDragPreview();
    gestureDragging = false;
    slotDragging = false;
    slotDragSource = -1;
    slotDragTarget = -1;
    pluginBrowser->showForSlot (processor.getSelectedSlot());
}

void GestureRackAudioProcessorEditor::hidePluginBrowser()
{
    if (pluginBrowser != nullptr)
        pluginBrowser->setVisible (false);
    repaint();
}

void GestureRackAudioProcessorEditor::updateSlotButtons()
{
    const auto selected = processor.getSelectedSlot();
    hoveredSlot = -1;

    for (int i = 0; i < GestureRackAudioProcessor::slotCount; ++i)
    {
        auto& button = slotButtons[static_cast<size_t> (i)];
        const auto loaded = processor.isSlotLoaded (i);
        const auto bypassed = processor.isSlotBypassed (i);
        const auto mappingCount = processor.getSlotMappingCount (i);
        auto name = processor.getSlotPluginName (i);

        if (name.length() > 12)
            name = name.substring (0, 10) + "..";

        const auto text = juce::String (i + 1) + "  " + (loaded ? name : "+");
        button.setButtonText (text);
        button.setTooltip ("Slot " + juce::String (i + 1) + " / "
                           + processor.getSlotPluginName (i)
                           + " / " + juce::String (mappingCount) + " mappings");
        button.setToggleState (i == selected, juce::dontSendNotification);

        const auto isSelected = i == selected;
        const auto isHover = button.isMouseOver() && ! isSelected;
        if (button.isMouseOver())
            hoveredSlot = i;

        juce::Colour base = isHover ? kRaised.brighter (0.08f) : kCard;
        juce::Colour on = loaded ? kBlue : kAccent;
        if (bypassed && loaded)
        {
            base = isHover ? kRed.withAlpha (0.18f) : kRed.withAlpha (0.10f);
            on = kRed;
        }

        button.setColour (juce::TextButton::buttonColourId, isSelected ? on.withAlpha (0.30f) : base);
        button.setColour (juce::TextButton::buttonOnColourId, on.withAlpha (0.30f));
        button.setColour (juce::TextButton::textColourOffId, isSelected ? on.brighter (0.25f) : kTitle);
        button.setColour (juce::TextButton::textColourOnId, on.brighter (0.25f));
    }
}

void GestureRackAudioProcessorEditor::handleSlotMouseDown (int slotIndex, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (slotIndex, GestureRackAudioProcessor::slotCount))
        return;

    slotDragSource = slotIndex;
    slotDragTarget = slotIndex;
    slotDragging = false;
    slotDragOrigin = e.getEventRelativeTo (this).getPosition();
}

void GestureRackAudioProcessorEditor::handleSlotMouseDrag (const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (slotDragSource, GestureRackAudioProcessor::slotCount))
        return;

    const auto point = e.getEventRelativeTo (this).getPosition();
    const auto dx = point.x - slotDragOrigin.x;
    const auto dy = point.y - slotDragOrigin.y;
    if (! slotDragging && dx * dx + dy * dy < 36)
        return;

    slotDragging = true;
    slotDragTarget = findNearestSlot (point);
    repaint();
}

void GestureRackAudioProcessorEditor::handleSlotMouseUp (const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (slotDragSource, GestureRackAudioProcessor::slotCount))
        return;

    if (slotDragging)
    {
        const auto point = e.getEventRelativeTo (this).getPosition();
        slotDragTarget = findNearestSlot (point);
        if (slotDragTarget >= 0 && slotDragTarget != slotDragSource)
            processor.moveSlot (slotDragSource, slotDragTarget);
    }

    slotDragging = false;
    slotDragSource = -1;
    slotDragTarget = -1;
    updateSlotButtons();
    repaint();
}

int GestureRackAudioProcessorEditor::findNearestSlot (juce::Point<int> editorPoint) const
{
    if (slotButtons.empty())
        return -1;

    auto rowBounds = slotButtons.front().getBounds();
    for (size_t i = 1; i < slotButtons.size(); ++i)
        rowBounds = rowBounds.getUnion (slotButtons[i].getBounds());

    if (! rowBounds.expanded (20, 30).contains (editorPoint))
        return slotDragSource;

    auto best = 0;
    auto bestDistance = std::numeric_limits<int>::max();
    for (int i = 0; i < GestureRackAudioProcessor::slotCount; ++i)
    {
        const auto distance = std::abs (editorPoint.x
                                       - slotButtons[static_cast<size_t> (i)].getBounds().getCentreX());
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

void GestureRackAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (pluginBrowser != nullptr && pluginBrowser->isVisible())
        return;

    parameterInspector.clearGestureDragPreview();
    const auto p = e.getEventRelativeTo (this).getPosition();
    for (int i = 0; i < static_cast<int> (gestureRects.size()); ++i)
        if (gestureRects[static_cast<size_t> (i)].contains (p))
        {
            const std::array<gr::ControlGesture, 7> gestures {
                gr::ControlGesture::openPalm, gr::ControlGesture::closedFist,
                gr::ControlGesture::victory, gr::ControlGesture::thumbUp,
                gr::ControlGesture::thumbDown, gr::ControlGesture::pointRight,
                gr::ControlGesture::pointLeft
            };
            draggedGesture = gestures[static_cast<size_t> (i)];
            gestureDragging = draggedGesture != gr::ControlGesture::unknown;
            gestureDragPoint = p;
            repaint();
            return;
        }
}

void GestureRackAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! gestureDragging)
        return;

    gestureDragPoint = e.getEventRelativeTo (this).getPosition();
    if (parameterInspector.getBounds().contains (gestureDragPoint))
        parameterInspector.setGestureDragPreview (
            draggedGesture, parameterInspector.getLocalPoint (this, gestureDragPoint));
    else
        parameterInspector.clearGestureDragPreview();
    repaint();
}

void GestureRackAudioProcessorEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! gestureDragging)
        return;

    const auto p = e.getEventRelativeTo (this).getPosition();
    juce::String error;

    if (activeTargetRect.contains (p))
        processor.addSlotActionGestureMapping (draggedGesture, gr::MappingMode::triggerSetActive, error);
    else if (bypassTargetRect.contains (p))
        processor.addSlotActionGestureMapping (draggedGesture, gr::MappingMode::triggerSetBypassed, error);
    else if (parameterInspector.getBounds().contains (p))
        parameterInspector.dropGestureAt (draggedGesture, parameterInspector.getLocalPoint (this, p));

    parameterInspector.clearGestureDragPreview();
    gestureDragging = false;
    draggedGesture = gr::ControlGesture::unknown;
    repaint();
}

juce::Rectangle<int> GestureRackAudioProcessorEditor::getGesturePaletteBounds() const
{
    auto content = getLocalBounds().reduced (24);
    content.removeFromTop (112);
    content.removeFromBottom (58);
    const auto leftWidth = juce::jlimit (300, 420, content.getWidth() * 32 / 100);
    content.removeFromLeft (leftWidth);
    content.removeFromLeft (12);
    return content.removeFromTop (104);
}

void GestureRackAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    const auto panel = getGesturePaletteBounds();
    g.setColour (kCard);
    g.fillRoundedRectangle (panel.toFloat(), 12.0f);
    g.setColour (kBorder);
    g.drawRoundedRectangle (panel.toFloat(), 12.0f, 1.0f);

    g.setColour (kTitle);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("GESTURES", panel.getX() + 12, panel.getY() + 8,
                100, 18, juce::Justification::centredLeft);

    const std::array<gr::ControlGesture, 7> gestures {
        gr::ControlGesture::openPalm, gr::ControlGesture::closedFist,
        gr::ControlGesture::victory, gr::ControlGesture::thumbUp,
        gr::ControlGesture::thumbDown, gr::ControlGesture::pointRight,
        gr::ControlGesture::pointLeft
    };

    auto chipRow = panel.reduced (12).withTrimmedTop (28).removeFromTop (36);
    const auto gap = 5;
    const auto chipWidth = juce::jmax (1, (chipRow.getWidth() - gap * 6) / 7);
    const auto live = processor.getLiveRightGesture();

    for (int i = 0; i < 7; ++i)
    {
        const auto gesture = gestures[static_cast<size_t> (i)];
        auto chip = chipRow.removeFromLeft (chipWidth);
        gestureRects[static_cast<size_t> (i)] = chip;
        const auto liveGesture = gesture == live;
        const auto hot = gestureDragging ? gesture == draggedGesture : chip.contains (lastMousePos);

        g.setColour (liveGesture ? kAccent.withAlpha (0.20f)
                                 : (hot ? kBlue.withAlpha (0.15f) : kRaised));
        g.fillRoundedRectangle (chip.toFloat(), 6.0f);
        g.setColour (liveGesture ? kAccent : (hot ? kBlue : kBorder));
        g.drawRoundedRectangle (chip.toFloat(), 6.0f, hot ? 1.5f : 1.0f);
        g.setColour (liveGesture ? kAccent.brighter (0.18f) : kTitle);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawFittedText (gr::controlGestureToShortLabel (gesture), chip.reduced (3, 0),
                          juce::Justification::centred, 1);
        chipRow.removeFromLeft (gap);
    }

    auto actions = panel.reduced (12).removeFromBottom (26);
    activeTargetRect = actions.removeFromLeft (juce::jmin (112, actions.getWidth() / 2));
    actions.removeFromLeft (6);
    bypassTargetRect = actions.removeFromLeft (juce::jmin (112, actions.getWidth()));

    const auto drawTarget = [&] (juce::Rectangle<int> rect, const juce::String& text, juce::Colour accent)
    {
        const auto hot = gestureDragging && rect.contains (gestureDragPoint);
        g.setColour (hot ? accent.withAlpha (0.18f) : kRaised);
        g.fillRoundedRectangle (rect.toFloat(), 6.0f);
        g.setColour (hot ? accent : kBorder);
        g.drawRoundedRectangle (rect.toFloat(), 6.0f, 1.0f);
        g.setColour (hot ? accent.brighter (0.20f) : kSecondary);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (text, rect, juce::Justification::centred);
    };

    drawTarget (activeTargetRect, "ACTIVE", kGreen);
    drawTarget (bypassTargetRect, "BYPASS", kRed);

    if (gestureDragging)
    {
        g.setColour (kAccent.withAlpha (0.80f));
        g.fillEllipse (static_cast<float> (gestureDragPoint.x - 5),
                       static_cast<float> (gestureDragPoint.y - 5), 10.0f, 10.0f);
    }

    if (slotDragging && juce::isPositiveAndBelow (slotDragTarget, GestureRackAudioProcessor::slotCount))
    {
        const auto target = slotButtons[static_cast<size_t> (slotDragTarget)].getBounds().expanded (2);
        g.setColour (kAccent.withAlpha (0.12f));
        g.fillRoundedRectangle (target.toFloat(), 7.0f);
        g.setColour (kAccent);
        g.drawRoundedRectangle (target.toFloat(), 7.0f, 2.0f);
    }
}

void GestureRackAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    g.setColour (kTitle);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("GESTURE RACK", 24, 12, 260, 34, juce::Justification::centredLeft);

    const auto snapshot = processor.getDualHandVisionSnapshot();
    const auto connected = processor.isVisionConnected();
    const auto rolesTrusted = handRolesTrusted (snapshot);
    const auto rightGesture = rolesTrusted && snapshot.right.present
        ? gr::controlGestureToShortLabel (snapshot.right.stableGesture)
        : juce::String ("--");
    const auto leftText = rolesTrusted && snapshot.left.present && snapshot.left.stableSlot > 0
        ? juce::String (snapshot.left.stableSlot) : juce::String ("--");

    juce::String health;
    if (connected)
    {
        health = juce::String (snapshot.captureFps, 0) + " FPS"
               + "  /  " + juce::String (snapshot.captureToResultMs, 0) + " MS"
               + "  /  L" + leftText
               + "  /  " + rightGesture;
        if (! rolesTrusted)
            health += "  /  CALIBRATE";
        else if (snapshot.swapHandedness)
            health += "  /  SWAPPED";
    }
    else
    {
        health = "VISION OFFLINE";
    }

    g.setColour (! connected ? kRed : (rolesTrusted ? kGreen : kAccent));
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawFittedText (health, 300, 14, getWidth() - 324, 32,
                      juce::Justification::centredRight, 1);

    auto content = getLocalBounds().reduced (24);
    content.removeFromTop (112);
    content.removeFromBottom (58);

    auto left = content.removeFromLeft (juce::jlimit (300, 420, content.getWidth() * 32 / 100));

    g.setColour (kCard);
    g.fillRoundedRectangle (left.toFloat(), 14.0f);
    g.setColour (kBorder);
    g.drawRoundedRectangle (left.toFloat(), 14.0f, 1.0f);

    auto cameraCard = left.reduced (12);
    auto header = cameraCard.removeFromTop (30);
    g.setColour (kTitle);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("CAMERA", header, juce::Justification::centredLeft);

    auto infoArea = cameraCard.removeFromBottom (106);
    cameraCard.removeFromBottom (8);
    const auto previewArea = cameraCard;

    g.setColour (kCameraBg);
    g.fillRoundedRectangle (previewArea.toFloat(), 9.0f);

    juce::Rectangle<int> imageRect;
    if (connected && cameraFrame.isValid())
    {
        const auto imageWidth = cameraFrame.image.getWidth();
        const auto imageHeight = cameraFrame.image.getHeight();
        const auto scale = juce::jmin (static_cast<float> (previewArea.getWidth()) / static_cast<float> (imageWidth),
                                      static_cast<float> (previewArea.getHeight()) / static_cast<float> (imageHeight));
        const auto drawWidth = juce::jmax (1, juce::roundToInt (imageWidth * scale));
        const auto drawHeight = juce::jmax (1, juce::roundToInt (imageHeight * scale));
        imageRect = juce::Rectangle<int> (drawWidth, drawHeight).withCentre (previewArea.getCentre());

        g.drawImage (cameraFrame.image,
                     imageRect.getX(), imageRect.getY(), imageRect.getWidth(), imageRect.getHeight(),
                     0, 0, imageWidth, imageHeight, false);

        if (cameraFrame.timestampMs == snapshot.timestampMs)
        {
            drawHandOverlay (g, imageRect.toFloat(), snapshot.left, kBlue);
            drawHandOverlay (g, imageRect.toFloat(), snapshot.right, kGreen);
        }

        auto badge = imageRect.reduced (8).removeFromTop (22);
        auto leftBadge = badge.removeFromLeft (juce::jmin (94, badge.getWidth() / 2));
        g.setColour (juce::Colours::black.withAlpha (0.68f));
        g.fillRoundedRectangle (leftBadge.toFloat(), 5.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        const auto leftLabel = ! rolesTrusted
            ? juce::String ("L ?")
            : (snapshot.left.present
                ? "L " + juce::String (snapshot.left.stableSlot)
                : juce::String ("L --"));
        g.drawFittedText (leftLabel, leftBadge, juce::Justification::centred, 1);

        const auto rightWidth = juce::jmin (112, badge.getWidth());
        auto rightBadge = badge.removeFromRight (rightWidth);
        g.setColour (juce::Colours::black.withAlpha (0.68f));
        g.fillRoundedRectangle (rightBadge.toFloat(), 5.0f);
        g.setColour (juce::Colours::white);
        const auto rightLabel = ! rolesTrusted
            ? juce::String ("R CAL")
            : (snapshot.right.present
                ? "R " + gr::controlGestureToShortLabel (snapshot.right.stableGesture)
                : juce::String ("R --"));
        g.drawFittedText (rightLabel, rightBadge, juce::Justification::centred, 1);
    }
    else
    {
        g.setColour (kSecondary);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (connected ? "WAITING FOR CAMERA" : "VISION OFFLINE",
                    previewArea, juce::Justification::centred);
    }

    g.setColour (snapshot.handCalibrationActive || ! rolesTrusted ? kAccent : kSecondary);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    juce::String roleText;
    if (snapshot.handCalibrationActive)
        roleText = snapshot.handCalibrationStatus + "  " + juce::String (snapshot.handCalibrationSamples);
    else if (! rolesTrusted)
        roleText = "RIGHT HAND NOT CALIBRATED";
    else
        roleText = snapshot.swapHandedness ? "HANDS SWAPPED" : "HANDS READY";
    g.drawFittedText (roleText, infoArea.removeFromTop (18), juce::Justification::centredLeft, 1);

    if (snapshot.shadowModelLoaded)
    {
        juce::String shadowText = "SHADOW ";
        if (snapshot.right.shadowAvailable)
            shadowText += juce::String (snapshot.right.shadowConfidence * 100.0f, 0) + "%  "
                       + juce::String (snapshot.right.shadowInferenceMs, 2) + " MS";
        else
            shadowText += "WAIT";
        g.setColour (snapshot.right.shadowAvailable && ! snapshot.right.shadowAgrees ? kAccent : kSecondary);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawFittedText (shadowText, infoArea.removeFromTop (16), juce::Justification::centredLeft, 1);
    }

    const auto selected = processor.getSelectedSlot();
    const auto loaded = processor.isSlotLoaded (selected);
    const auto bypassed = processor.isSlotBypassed (selected);
    const auto stateColour = bypassed ? kRed : kGreen;

    g.setColour (kSecondary);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText ("SLOT " + juce::String (selected + 1), infoArea.removeFromTop (16),
                juce::Justification::centredLeft);

    g.setColour (kTitle);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawFittedText (processor.getSlotPluginName (selected), infoArea.removeFromTop (24),
                      juce::Justification::centredLeft, 1);

    auto stateRect = infoArea.removeFromTop (24).reduced (0, 1);
    g.setColour (loaded ? stateColour.withAlpha (0.12f) : kRaised);
    g.fillRoundedRectangle (stateRect.toFloat(), 6.0f);
    g.setColour (loaded ? stateColour : kSecondary);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (loaded ? (bypassed ? "BYPASSED" : "ACTIVE") : "EMPTY",
                stateRect, juce::Justification::centred);

    if (const auto error = processor.getSlotLastError (selected); error.isNotEmpty())
    {
        auto errorRect = getLocalBounds().reduced (24).removeFromBottom (22);
        g.setColour (kRed);
        g.setFont (juce::FontOptions (10.5f));
        g.drawFittedText ("S" + juce::String (selected + 1) + ": " + error,
                          errorRect, juce::Justification::centredLeft, 1);
    }
}

void GestureRackAudioProcessorEditor::drawHandOverlay (juce::Graphics& g,
                                                        juce::Rectangle<float> imageArea,
                                                        const gr::HandSnapshot& hand,
                                                        juce::Colour colour)
{
    if (! hand.present || imageArea.isEmpty())
        return;

    std::array<juce::Point<float>, 21> points {};
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto x = juce::jlimit (0.0f, 1.0f, hand.landmarks[i].x);
        const auto y = juce::jlimit (0.0f, 1.0f, hand.landmarks[i].y);
        points[i] = { imageArea.getX() + x * imageArea.getWidth(),
                      imageArea.getY() + y * imageArea.getHeight() };
    }

    g.setColour (colour.withAlpha (0.92f));
    for (const auto [a, b] : handConnections)
        g.drawLine ({ points[static_cast<size_t> (a)], points[static_cast<size_t> (b)] }, 2.0f);

    for (const auto& point : points)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (point.x - 4.0f, point.y - 4.0f, 8.0f, 8.0f);
        g.setColour (colour);
        g.fillEllipse (point.x - 2.4f, point.y - 2.4f, 4.8f, 4.8f);
    }
}

void GestureRackAudioProcessorEditor::resized()
{
    auto slotRow = getLocalBounds().reduced (24).withTrimmedTop (66).removeFromTop (42);
    constexpr auto gap = 5;
    const auto totalGap = gap * (GestureRackAudioProcessor::slotCount - 1);
    const auto slotWidth = juce::jmax (1, (slotRow.getWidth() - totalGap) / GestureRackAudioProcessor::slotCount);

    for (auto& button : slotButtons)
    {
        button.setBounds (slotRow.removeFromLeft (slotWidth));
        slotRow.removeFromLeft (gap);
    }

    auto content = getLocalBounds().reduced (24);
    content.removeFromTop (112);
    content.removeFromBottom (58);
    content.removeFromLeft (juce::jlimit (300, 420, content.getWidth() * 32 / 100));
    content.removeFromLeft (12);
    content.removeFromTop (112);
    parameterInspector.setBounds (content);

    auto bottom = getLocalBounds().reduced (24).removeFromBottom (42);
    loadButton.setBounds (bottom.removeFromLeft (94));
    bottom.removeFromLeft (8);
    openButton.setBounds (bottom.removeFromLeft (82));
    bottom.removeFromLeft (8);
    removeButton.setBounds (bottom.removeFromLeft (82));
    bottom.removeFromLeft (8);
    bypassButton.setBounds (bottom.removeFromLeft (96));
    bottom.removeFromLeft (8);
    enableButton.setBounds (bottom.removeFromLeft (112));
    bottom.removeFromLeft (12);
    calibrateHandsButton.setBounds (bottom.removeFromLeft (116));
    bottom.removeFromLeft (8);
    swapHandsButton.setBounds (bottom.removeFromLeft (108));

    if (pluginBrowser != nullptr)
    {
        auto overlay = getLocalBounds().reduced (64, 52);
        overlay.setY (juce::jmax (58, overlay.getY()));
        pluginBrowser->setBounds (overlay);
        pluginBrowser->toFront (false);
    }
}
