#include "PluginEditor.h"
#include <cmath>

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

const juce::Colour kBg        { 224, 228, 234 };
const juce::Colour kCard      { 247, 248, 250 };
const juce::Colour kCardBorder{ 205, 210, 218 };
const juce::Colour kRecessed  { 235, 238, 242 };
const juce::Colour kTitle     { 45, 48, 56 };
const juce::Colour kSecondary { 112, 120, 134 };
const juce::Colour kAccent    { 245, 178, 60 };
const juce::Colour kBlue      { 80, 140, 220 };
const juce::Colour kGreen     { 92, 180, 120 };
const juce::Colour kRed       { 215, 80, 80 };
const juce::Colour kCameraBg  { 30, 33, 39 };
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
        button.onClick = [this, i]
        {
            processor.setSelectedSlot (i);
            updateSlotButtons();
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
        button->setColour (juce::TextButton::buttonColourId, juce::Colour (255, 255, 255));
        button->setColour (juce::TextButton::textColourOffId, kTitle);
        button->setColour (juce::TextButton::textColourOnId, kTitle);
    }

    loadButton.onClick = [this] { choosePluginForSelectedSlot(); };
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

    updateSlotButtons();
    startTimerHz (30);
}

GestureRackAudioProcessorEditor::~GestureRackAudioProcessorEditor()
{
    stopTimer();
}

void GestureRackAudioProcessorEditor::timerCallback()
{
    const auto selected = processor.getSelectedSlot();
    const auto loaded = processor.isSlotLoaded (selected);
    const auto bypassed = processor.isSlotBypassed (selected);
    const auto vision = processor.getDualHandVisionSnapshot();
    const auto connected = processor.isVisionConnected();

    frameReader.readLatest (cameraFrame);

    enableButton.setButtonText (processor.isGestureEnabled() ? "GESTURE ON" : "GESTURE OFF");
    bypassButton.setButtonText (bypassed ? "BYPASSED" : "ACTIVE");
    calibrateHandsButton.setButtonText (vision.handCalibrationActive ? "SHOW RIGHT HAND..."
                                                                      : "CALIBRATE RIGHT");
    swapHandsButton.setButtonText (vision.swapHandedness ? "L/R SWAPPED" : "SWAP L/R");

    openButton.setEnabled (loaded);
    removeButton.setEnabled (loaded);
    bypassButton.setEnabled (loaded);
    calibrateHandsButton.setEnabled (connected && ! vision.handCalibrationActive);
    swapHandsButton.setEnabled (connected && ! vision.handCalibrationActive);

    updateSlotButtons();
    repaint();
}

void GestureRackAudioProcessorEditor::updateSlotButtons()
{
    const auto selected = processor.getSelectedSlot();

    hoveredSlot = -1;
    for (int i = 0; i < GestureRackAudioProcessor::slotCount; ++i)
        if (slotButtons[static_cast<size_t> (i)].getBounds().contains (lastMousePos))
        {
            hoveredSlot = i;
            break;
        }

    for (int i = 0; i < GestureRackAudioProcessor::slotCount; ++i)
    {
        auto& button = slotButtons[static_cast<size_t> (i)];
        const auto loaded = processor.isSlotLoaded (i);
        const auto bypassed = processor.isSlotBypassed (i);
        const auto mappingCount = processor.getSlotMappingCount (i);
        auto name = processor.getSlotPluginName (i);

        if (name.length() > 9)
            name = name.substring (0, 7) + "..";

        const auto text = juce::String (i + 1) + "  "
                        + (loaded ? name : "+")
                        + (mappingCount > 0 ? "  " + juce::String (mappingCount) : juce::String());

        button.setButtonText (text);
        button.setTooltip ("Slot " + juce::String (i + 1) + ": "
                           + processor.getSlotPluginName (i)
                           + " | mappings: " + juce::String (mappingCount));
        button.setToggleState (i == selected, juce::dontSendNotification);

        const auto isSelected = i == selected;
        const auto isHover = i == hoveredSlot && ! isSelected;

        juce::Colour base, on;
        if (bypassed && loaded)
        {
            base = isHover ? juce::Colour (232, 200, 200) : juce::Colour (245, 224, 224);
            on = kRed;
        }
        else if (loaded)
        {
            base = isHover ? juce::Colour (237, 241, 247) : kCard;
            on = kBlue;
        }
        else
        {
            base = isHover ? juce::Colour (238, 240, 244) : kRecessed;
            on = kAccent;
        }

        button.setColour (juce::TextButton::buttonColourId, isSelected ? on : base);
        button.setColour (juce::TextButton::buttonOnColourId, on);
        button.setColour (juce::TextButton::textColourOffId,
                          isSelected ? juce::Colour (255, 255, 255) : kTitle);
        button.setColour (juce::TextButton::textColourOnId, juce::Colour (255, 255, 255));
    }
}

void GestureRackAudioProcessorEditor::choosePluginForSelectedSlot()
{
    const auto targetSlot = processor.getSelectedSlot();

    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose a VST3 effect for Slot " + juce::String (targetSlot + 1),
        juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory),
        "*.vst3");

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::canSelectDirectories;

    fileChooser->launchAsync (flags, [this, targetSlot] (const juce::FileChooser& chooser)
    {
        const auto chosen = chooser.getResult();
        if (chosen.exists())
            processor.loadVst3FromFile (targetSlot, chosen);
    });
}

void GestureRackAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
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
    gestureDragPoint = e.getPosition();
    repaint();
}

void GestureRackAudioProcessorEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! gestureDragging)
        return;

    const auto p = e.getPosition();
    juce::String error;

    if (activeTargetRect.contains (p))
        processor.addSlotActionGestureMapping (draggedGesture, gr::MappingMode::triggerSetActive, error);
    else if (bypassTargetRect.contains (p))
        processor.addSlotActionGestureMapping (draggedGesture, gr::MappingMode::triggerSetBypassed, error);
    else if (parameterInspector.getBounds().contains (p))
        parameterInspector.dropGestureAt (draggedGesture, parameterInspector.getLocalPoint (this, p));

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
    return content.removeFromTop (116);
}

void GestureRackAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    const auto panel = getGesturePaletteBounds();
    g.setColour (juce::Colour (255, 255, 255));
    g.fillRoundedRectangle (panel.toFloat(), 12.0f);
    g.setColour (kCardBorder);
    g.drawRoundedRectangle (panel.toFloat(), 12.0f, 1.0f);

    g.setColour (kTitle);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("GESTURE SOURCES", panel.getX() + 12, panel.getY() + 8,
                panel.getWidth() - 24, 18, juce::Justification::centredLeft);

    g.setColour (kSecondary);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("DRAG A GESTURE ONTO A PARAMETER ROW",
                panel.getX() + 12, panel.getY() + 26,
                panel.getWidth() - 24, 16, juce::Justification::centredLeft);

    const std::array<gr::ControlGesture, 7> gestures {
        gr::ControlGesture::openPalm, gr::ControlGesture::closedFist,
        gr::ControlGesture::victory, gr::ControlGesture::thumbUp,
        gr::ControlGesture::thumbDown, gr::ControlGesture::pointRight,
        gr::ControlGesture::pointLeft
    };
    const std::array<juce::String, 7> labels {
        "PALM", "FIST", "VICTORY", "UP", "DOWN", "RIGHT", "LEFT"
    };

    auto chipRow = panel.reduced (12).withTrimmedTop (38).removeFromTop (38);
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

        g.setColour (liveGesture ? kAccent.withAlpha (0.92f)
                                 : (hot ? kBlue.withAlpha (0.12f) : juce::Colour (255, 255, 255)));
        g.fillRoundedRectangle (chip.toFloat(), 7.0f);
        g.setColour (liveGesture ? kAccent : (hot ? kBlue : kCardBorder));
        g.drawRoundedRectangle (chip.toFloat(), 7.0f, hot ? 1.5f : 1.0f);

        auto emojiArea = chip.removeFromLeft (juce::jmin (27, chip.getWidth() / 3));
        g.setColour (kTitle);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (gr::controlGestureToEmoji (gesture), emojiArea, juce::Justification::centred);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawFittedText (labels[static_cast<size_t> (i)], chip.reduced (2, 0),
                          juce::Justification::centredLeft, 1);
        chipRow.removeFromLeft (gap);
    }

    auto actions = panel.reduced (12).removeFromBottom (26);
    g.setColour (kSecondary);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("OPTIONAL SLOT ACTIONS", actions.removeFromLeft (150),
                juce::Justification::centredLeft);

    actions.removeFromLeft (8);
    activeTargetRect = actions.removeFromLeft (juce::jmin (118, actions.getWidth() / 2));
    actions.removeFromLeft (6);
    bypassTargetRect = actions.removeFromLeft (juce::jmin (118, actions.getWidth()));

    const auto drawTarget = [&] (juce::Rectangle<int> rect, const juce::String& text, juce::Colour accent)
    {
        const auto hot = gestureDragging && rect.contains (gestureDragPoint);
        g.setColour (hot ? accent.withAlpha (0.16f) : juce::Colour (255, 255, 255));
        g.fillRoundedRectangle (rect.toFloat(), 6.0f);
        g.setColour (hot ? accent : kCardBorder);
        g.drawRoundedRectangle (rect.toFloat(), 6.0f, 1.0f);
        g.setColour (hot ? accent : kTitle);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (text, rect, juce::Justification::centred);
    };

    drawTarget (activeTargetRect, "\xE2\x96\xB6 ENABLE", kGreen);
    drawTarget (bypassTargetRect, "\xE2\x8F\xB8 BYPASS", kRed);

    if (gestureDragging)
    {
        g.setColour (kAccent.withAlpha (0.75f));
        g.fillEllipse (static_cast<float> (gestureDragPoint.x - 6),
                       static_cast<float> (gestureDragPoint.y - 6), 12.0f, 12.0f);
    }
}

void GestureRackAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    g.setColour (kTitle);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("GESTURE RACK", 24, 12, 260, 34, juce::Justification::centredLeft);

    const auto snapshot = processor.getDualHandVisionSnapshot();
    const auto connected = processor.isVisionConnected();
    const auto rightEmoji = snapshot.right.present
        ? gr::controlGestureToEmoji (snapshot.right.stableGesture)
        : juce::String ("--");
    const auto leftText = snapshot.left.present && snapshot.left.stableSlot > 0
        ? juce::String (snapshot.left.stableSlot) : juce::String ("--");

    juce::String health;
    if (connected)
        health = juce::String (snapshot.captureFps, 0) + "fps"
               + "  fa" + juce::String (snapshot.frameAgeAtSubmitMs, 0)
               + "/" + juce::String (snapshot.captureToResultMs, 0) + "ms"
               + "  |  " + (snapshot.swapHandedness ? juce::String ("L/R SWAP") : juce::String ("L/R OK"))
               + "  |  L" + leftText + "  " + rightEmoji;
    else
        health = "VISION OFFLINE";

    g.setColour (connected ? kGreen : kRed);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawFittedText (health, 300, 14, getWidth() - 324, 32,
                      juce::Justification::centredRight, 1);

    auto content = getLocalBounds().reduced (24);
    content.removeFromTop (112);
    content.removeFromBottom (58);

    auto left = content.removeFromLeft (juce::jlimit (300, 420, content.getWidth() * 32 / 100));

    g.setColour (kCard);
    g.fillRoundedRectangle (left.toFloat(), 14.0f);
    g.setColour (kCardBorder);
    g.drawRoundedRectangle (left.toFloat(), 14.0f, 1.0f);

    auto cameraCard = left.reduced (12);
    auto header = cameraCard.removeFromTop (34);
    g.setColour (kTitle);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("CAMERA + HAND TRACKING", header, juce::Justification::centredLeft);

    auto infoArea = cameraCard.removeFromBottom (122);
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

        const auto aligned = cameraFrame.timestampMs == snapshot.timestampMs;
        if (aligned)
        {
            drawHandOverlay (g, imageRect.toFloat(), snapshot.left, kBlue);
            drawHandOverlay (g, imageRect.toFloat(), snapshot.right, kGreen);
        }

        auto badge = imageRect.reduced (8).removeFromTop (24);
        auto leftBadge = badge.removeFromLeft (juce::jmin (150, badge.getWidth() / 2));
        g.setColour (juce::Colour (0, 0, 0).withAlpha (0.58f));
        g.fillRoundedRectangle (leftBadge.toFloat(), 5.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        const auto leftLabel = snapshot.left.present
            ? "LEFT  SLOT " + juce::String (snapshot.left.stableSlot)
            : juce::String ("LEFT  --");
        g.drawText (leftLabel, leftBadge, juce::Justification::centred);

        const auto rightWidth = juce::jmin (176, badge.getWidth());
        auto rightBadge = badge.removeFromRight (rightWidth);
        g.setColour (juce::Colour (0, 0, 0).withAlpha (0.58f));
        g.fillRoundedRectangle (rightBadge.toFloat(), 5.0f);
        g.setColour (juce::Colours::white);
        const auto rightLabel = snapshot.right.present
            ? "RIGHT  " + gr::controlGestureToString (snapshot.right.stableGesture).toUpperCase()
            : juce::String ("RIGHT  --");
        g.drawFittedText (rightLabel, rightBadge, juce::Justification::centred, 1);
    }
    else
    {
        g.setColour (juce::Colour (205, 210, 218));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (connected ? "WAITING FOR CAMERA FRAME" : "START VISION ENGINE",
                    previewArea, juce::Justification::centred);
    }

    g.setColour (snapshot.handCalibrationActive ? kAccent : kSecondary);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    auto roleText = snapshot.handCalibrationActive
        ? snapshot.handCalibrationStatus + "  " + juce::String (snapshot.handCalibrationSamples)
        : (snapshot.swapHandedness ? juce::String ("HANDS: SWAPPED") : juce::String ("HANDS: NORMAL"));
    if (! snapshot.handRoleSource.isEmpty() && ! snapshot.handCalibrationActive)
        roleText += "  /  " + snapshot.handRoleSource;
    g.drawFittedText (roleText, infoArea.removeFromTop (20), juce::Justification::centredLeft, 1);

    const auto selected = processor.getSelectedSlot();
    const auto loaded = processor.isSlotLoaded (selected);
    const auto bypassed = processor.isSlotBypassed (selected);
    const auto stateColour = bypassed ? kRed : kGreen;

    g.setColour (kSecondary);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("SELECTED SLOT", infoArea.removeFromTop (16), juce::Justification::centredLeft);

    g.setColour (kTitle);
    g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    auto slotLine = infoArea.removeFromTop (30);
    g.drawText (juce::String (selected + 1), slotLine.removeFromLeft (34), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawFittedText (processor.getSlotPluginName (selected), slotLine,
                      juce::Justification::centredLeft, 1);

    auto stateRect = infoArea.removeFromTop (26).reduced (0, 1);
    g.setColour (loaded ? stateColour.withAlpha (0.15f) : kRecessed);
    g.fillRoundedRectangle (stateRect.toFloat(), 6.0f);
    g.setColour (loaded ? stateColour : kSecondary);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (loaded ? (bypassed ? "\xE2\x8F\xB8 BYPASSED" : "\xE2\x96\xB6 ACTIVE") : "EMPTY",
                stateRect, juce::Justification::centred);

    if (const auto error = processor.getSlotLastError (selected); error.isNotEmpty())
    {
        auto errorRect = getLocalBounds().reduced (24).removeFromBottom (22);
        g.setColour (kRed);
        g.setFont (juce::FontOptions (11.0f));
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
    content.removeFromTop (124);
    parameterInspector.setBounds (content);

    auto bottom = getLocalBounds().reduced (24).removeFromBottom (42);
    loadButton.setBounds (bottom.removeFromLeft (90));
    bottom.removeFromLeft (8);
    openButton.setBounds (bottom.removeFromLeft (90));
    bottom.removeFromLeft (8);
    removeButton.setBounds (bottom.removeFromLeft (80));
    bottom.removeFromLeft (8);
    bypassButton.setBounds (bottom.removeFromLeft (100));
    bottom.removeFromLeft (8);
    enableButton.setBounds (bottom.removeFromLeft (120));
    bottom.removeFromLeft (12);
    calibrateHandsButton.setBounds (bottom.removeFromLeft (150));
    bottom.removeFromLeft (8);
    swapHandsButton.setBounds (bottom.removeFromLeft (110));
}
