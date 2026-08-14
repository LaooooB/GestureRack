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
const juce::Colour kSecondary { 130, 136, 148 };
const juce::Colour kAccent    { 245, 178, 60 };
const juce::Colour kBlue      { 80, 140, 220 };
const juce::Colour kGreen     { 92, 180, 120 };
const juce::Colour kRed       { 215, 80, 80 };
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
            on   = kRed;
        }
        else if (loaded)
        {
            base = isHover ? juce::Colour (237, 241, 247) : kCard;
            on   = kBlue;
        }
        else
        {
            base = isHover ? juce::Colour (238, 240, 244) : kRecessed;
            on   = kAccent;
        }

        button.setColour (juce::TextButton::buttonColourId, isSelected ? on : base);
        button.setColour (juce::TextButton::buttonOnColourId, on);
        button.setColour (juce::TextButton::textColourOffId,
                          isSelected ? juce::Colour (255, 255, 255) : kTitle);
        button.setColour (juce::TextButton::textColourOnId,
                          juce::Colour (255, 255, 255));
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
    else if (learnTargetRect.contains (p))
    {
        if (processor.isParameterLearnArmed())
            processor.cancelParameterLearn();
        processor.beginParameterLearn (draggedGesture, error);
    }
    else if (parameterInspector.getBounds().contains (p))
        parameterInspector.dropGestureAt (draggedGesture, parameterInspector.getLocalPoint (this, p));

    gestureDragging = false;
    draggedGesture = gr::ControlGesture::unknown;
    repaint();
}

void GestureRackAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    g.setColour (kTitle);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("GESTURE RACK", 24, 12, 260, 34, juce::Justification::centredLeft);

    const auto snap = processor.getDualHandVisionSnapshot();
    const auto connected = processor.isVisionConnected();
    const auto rightEmoji = snap.right.present
        ? gr::controlGestureToEmoji (snap.right.stableGesture)
        : juce::String ("--");
    const auto leftText = snap.left.present && snap.left.stableSlot > 0
        ? juce::String (snap.left.stableSlot) : juce::String ("--");

    juce::String health;
    if (connected)
        health = juce::String (snap.captureFps, 0) + "fps"
               + "  fa" + juce::String (snap.frameAgeAtSubmitMs, 0)
               + "/" + juce::String (snap.captureToResultMs, 0) + "ms"
               + "  |  " + (snap.swapHandedness ? juce::String ("L/R SWAP") : juce::String ("L/R OK"))
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
    content.removeFromLeft (12);

    g.setColour (kCard);
    g.fillRoundedRectangle (left.toFloat(), 14.0f);
    g.setColour (kCardBorder);
    g.drawRoundedRectangle (left.toFloat(), 14.0f, 1.0f);

    const auto snapshot = processor.getDualHandVisionSnapshot();
    auto handArea = left.reduced (16);
    auto infoArea = handArea.removeFromBottom (116);
    const auto handGap = 8;
    auto leftHandArea = handArea.removeFromTop ((handArea.getHeight() - handGap) / 2);
    handArea.removeFromTop (handGap);
    auto rightHandArea = handArea;

    g.setColour (kRecessed);
    g.fillRoundedRectangle (leftHandArea.toFloat(), 10.0f);
    g.fillRoundedRectangle (rightHandArea.toFloat(), 10.0f);

    drawHand (g, leftHandArea.toFloat(), snapshot.left, connected,
              kBlue, "\xE2\x9C\x8B SELECTOR");

    const auto rightArmed = processor.isRightControllerArmed();
    const auto rightColour = ! rightArmed
        ? kAccent
        : (processor.isGestureBypassed() ? kRed : kGreen);
    drawHand (g, rightHandArea.toFloat(), snapshot.right, connected,
              rightColour, rightArmed ? "\xE2\x9C\x8A CONTROL" : "\xE2\x9C\x8A ARM");

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
    g.drawText ("SLOT", infoArea.removeFromTop (16), juce::Justification::centredLeft);

    g.setColour (kTitle);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText (juce::String (selected + 1), infoArea.removeFromTop (28), juce::Justification::centredLeft);

    g.setColour (kSecondary);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (processor.getSlotPluginName (selected), infoArea.removeFromTop (22),
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

void GestureRackAudioProcessorEditor::drawHand (juce::Graphics& g,
                                                 juce::Rectangle<float> area,
                                                 const gr::HandSnapshot& hand,
                                                 bool connected,
                                                 juce::Colour colour,
                                                 const juce::String& label)
{
    auto inner = area.reduced (8.0f);
    auto labelArea = inner.removeFromTop (16.0f);

    g.setColour (kSecondary);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawFittedText (label, labelArea.toNearestInt(), juce::Justification::centredLeft, 1);

    if (! connected || ! hand.present)
    {
        g.setColour (kSecondary);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (connected ? "no hand" : "start vision",
                    inner.toNearestInt(), juce::Justification::centred);
        return;
    }

    std::array<juce::Point<float>, 21> points {};
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto x = juce::jlimit (0.0f, 1.0f, hand.landmarks[i].x);
        const auto y = juce::jlimit (0.0f, 1.0f, hand.landmarks[i].y);
        points[i] = { inner.getX() + x * inner.getWidth(),
                      inner.getY() + y * inner.getHeight() };
    }

    const auto time = juce::Time::getMillisecondCounterHiRes() * 0.004;
    const auto pulse = static_cast<float> (0.5 + 0.5 * std::sin (time));

    auto palmCentre = juce::Point<float>();
    for (const auto index : { 0, 5, 9, 13, 17 })
        palmCentre += points[static_cast<size_t> (index)];
    palmCentre /= 5.0f;

    g.setColour (colour.withAlpha (0.06f + 0.06f * pulse));
    g.fillEllipse (palmCentre.x - 24.0f - pulse * 4.0f,
                   palmCentre.y - 24.0f - pulse * 4.0f,
                   48.0f + pulse * 8.0f,
                   48.0f + pulse * 8.0f);

    g.setColour (colour.withAlpha (0.82f));
    for (const auto [a, b] : handConnections)
        g.drawLine ({ points[static_cast<size_t> (a)], points[static_cast<size_t> (b)] }, 1.8f);

    for (const auto& point : points)
    {
        g.setColour (colour.withAlpha (0.24f));
        g.fillEllipse (point.x - 3.5f, point.y - 3.5f, 7.0f, 7.0f);
        g.setColour (colour);
        g.fillEllipse (point.x - 1.8f, point.y - 1.8f, 3.6f, 3.6f);
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
