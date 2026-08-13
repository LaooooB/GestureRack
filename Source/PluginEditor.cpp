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
    addAndMakeVisible (parameterInspector);

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

    enableButton.setButtonText (processor.isGestureEnabled() ? "GESTURE ON" : "GESTURE OFF");
    bypassButton.setButtonText (bypassed ? "BYPASSED" : "ACTIVE");

    openButton.setEnabled (loaded);
    removeButton.setEnabled (loaded);
    bypassButton.setEnabled (loaded);

    updateSlotButtons();
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

        if (name.length() > 11)
            name = name.substring (0, 8) + "...";

        auto text = juce::String (i + 1) + "  "
                  + (loaded ? name : "+ DROP VST3")
                  + "  |  " + juce::String (mappingCount) + " MOD";

        button.setButtonText (text);
        button.setTooltip ("Slot " + juce::String (i + 1) + ": "
                           + processor.getSlotPluginName (i)
                           + " | mappings: " + juce::String (mappingCount));
        button.setToggleState (i == selected, juce::dontSendNotification);

        const auto base = loaded ? juce::Colour::fromRGB (36, 42, 52)
                                 : juce::Colour::fromRGB (25, 29, 36);
        const auto selectedColour = juce::Colour::fromRGB (69, 109, 190);
        const auto bypassColour = juce::Colour::fromRGB (104, 58, 62);

        button.setColour (juce::TextButton::buttonColourId,
                          i == selected ? selectedColour : (bypassed && loaded ? bypassColour : base));
        button.setColour (juce::TextButton::buttonOnColourId, selectedColour);
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

void GestureRackAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (12, 14, 18));

    g.setColour (juce::Colour::fromRGB (235, 238, 242));
    g.setFont (juce::FontOptions (25.0f, juce::Font::bold));
    g.drawText ("GESTURE RACK", 24, 14, 250, 32, juce::Justification::centredLeft);

    const auto topSnapshot = processor.getDualHandVisionSnapshot();
    const auto topConnected = processor.isVisionConnected();
    const auto packetAge = topSnapshot.receivedAtMs > 0
        ? juce::jmax<int64_t> (0, juce::Time::currentTimeMillis() - topSnapshot.receivedAtMs)
        : static_cast<int64_t> (0);
    const auto leftText = topSnapshot.left.present && topSnapshot.left.stableSlot > 0
        ? juce::String (topSnapshot.left.stableSlot)
        : juce::String ("--");
    const auto rightText = topSnapshot.right.present
        ? gr::controlGestureToString (topSnapshot.right.stableGesture).toUpperCase()
        : juce::String ("--");
    const auto health = topConnected
        ? "CAM " + juce::String (topSnapshot.captureFps, 1)
          + "  VISION " + juce::String (topSnapshot.visionFps, 1)
          + "  LAT " + juce::String (topSnapshot.captureToResultMs, 0) + " ms"
          + "  AGE " + juce::String (packetAge) + " ms"
          + "  |  LEFT " + leftText
          + "  RIGHT " + rightText
        : juce::String ("VISION OFFLINE  |  START GESTURE VISION ENGINE");
    g.setColour (topConnected ? juce::Colour::fromRGB (150, 210, 175)
                              : juce::Colour::fromRGB (235, 120, 104));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawFittedText (health, 290, 14, getWidth() - 314, 32,
                      juce::Justification::centredRight, 1);

    g.setColour (juce::Colour::fromRGB (104, 111, 126));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("SERIAL SIGNAL CHAIN  1  >  2  >  3  >  4  >  5  >  6  >  7  >  8  >  9",
                24, 45, getWidth() - 48, 18, juce::Justification::centredLeft);

    auto content = getLocalBounds().reduced (24);
    content.removeFromTop (112);
    content.removeFromBottom (58);

    auto left = content.removeFromLeft (juce::jlimit (300, 420, content.getWidth() * 32 / 100));
    content.removeFromLeft (12);

    g.setColour (juce::Colour::fromRGB (22, 25, 31));
    g.fillRoundedRectangle (left.toFloat(), 14.0f);
    g.setColour (juce::Colour::fromRGB (45, 50, 61));
    g.drawRoundedRectangle (left.toFloat(), 14.0f, 1.0f);

    const auto snapshot = processor.getDualHandVisionSnapshot();
    const auto connected = processor.isVisionConnected();
    auto handArea = left.reduced (16);
    auto infoArea = handArea.removeFromBottom (142);
    const auto handGap = 8;
    auto leftHandArea = handArea.removeFromTop ((handArea.getHeight() - handGap) / 2);
    handArea.removeFromTop (handGap);
    auto rightHandArea = handArea;

    g.setColour (juce::Colour::fromRGB (30, 34, 42));
    g.fillRoundedRectangle (leftHandArea.toFloat(), 10.0f);
    g.fillRoundedRectangle (rightHandArea.toFloat(), 10.0f);

    drawHand (g, leftHandArea.toFloat(), snapshot.left, connected,
              juce::Colour::fromRGB (96, 158, 235), "PHYSICAL LEFT / SELECTOR");

    const auto rightArmed = processor.isRightControllerArmed();
    const auto rightColour = ! rightArmed
        ? juce::Colour::fromRGB (238, 174, 76)
        : (processor.isGestureBypassed()
            ? juce::Colour::fromRGB (235, 92, 92)
            : juce::Colour::fromRGB (92, 220, 155));
    drawHand (g, rightHandArea.toFloat(), snapshot.right, connected,
              rightColour, rightArmed ? "PHYSICAL RIGHT / CONTROL" : "PHYSICAL RIGHT / RE-ARM");

    const auto selected = processor.getSelectedSlot();
    const auto loaded = processor.isSlotLoaded (selected);
    const auto bypassed = processor.isSlotBypassed (selected);
    const auto stateColour = bypassed ? juce::Colour::fromRGB (235, 92, 92)
                                      : juce::Colour::fromRGB (92, 220, 155);

    g.setColour (juce::Colour::fromRGB (128, 136, 151));
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("SELECTED SLOT", infoArea.removeFromTop (18), juce::Justification::centredLeft);

    g.setColour (juce::Colour::fromRGB (238, 241, 245));
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("SLOT " + juce::String (selected + 1), infoArea.removeFromTop (30), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawFittedText (processor.getSlotPluginName (selected), infoArea.removeFromTop (30),
                      juce::Justification::centredLeft, 1);

    auto stateRect = infoArea.removeFromTop (32).reduced (0, 2);
    g.setColour ((loaded ? stateColour : juce::Colour::fromRGB (100, 106, 118)).withAlpha (0.13f));
    g.fillRoundedRectangle (stateRect.toFloat(), 8.0f);
    g.setColour (loaded ? stateColour : juce::Colour::fromRGB (120, 126, 138));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (loaded ? (bypassed ? "BYPASSED" : "ACTIVE") : "EMPTY SLOT",
                stateRect, juce::Justification::centred);

    const auto runtimeLine = infoArea.removeFromTop (16);
    g.setColour (rightArmed ? juce::Colour::fromRGB (116, 205, 161)
                            : juce::Colour::fromRGB (238, 174, 76));
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawFittedText (processor.getGestureRuntimeStatus(), runtimeLine,
                      juce::Justification::centredLeft, 1);

    g.setColour (juce::Colour::fromRGB (118, 125, 139));
    g.setFont (juce::FontOptions (9.0f));
    const auto leftStatus = connected
        ? (snapshot.left.present
            ? "L:" + juce::String (snapshot.left.stableSlot > 0 ? snapshot.left.stableSlot : 0)
            : juce::String ("L:--"))
        : juce::String ("VISION OFFLINE");
    const auto rightHeight = snapshot.right.present
        ? juce::String (snapshot.right.height, 2)
        : juce::String ("--");

    g.drawFittedText (leftStatus
                      + "  |  H:" + rightHeight
                      + "  |  LIVE:" + juce::String (processor.getLiveMappingCount())
                      + "  |  TOTAL:" + juce::String (processor.getSlotMappingCount (selected)),
                      infoArea, juce::Justification::centredLeft, 1);

    if (const auto error = processor.getSlotLastError (selected); error.isNotEmpty())
    {
        auto errorRect = getLocalBounds().reduced (24).removeFromBottom (22);
        g.setColour (juce::Colour::fromRGB (235, 92, 92));
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText ("Slot " + juce::String (selected + 1) + ": " + error,
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
    auto labelArea = inner.removeFromTop (18.0f);

    g.setColour (juce::Colour::fromRGB (130, 137, 151));
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawFittedText (label, labelArea.toNearestInt(), juce::Justification::centredLeft, 1);

    if (! connected || ! hand.present)
    {
        g.setColour (juce::Colour::fromRGB (88, 94, 106));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (connected ? "NO HAND" : "START VISION ENGINE",
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

    g.setColour (colour.withAlpha (0.85f));
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("ROLE " + juce::String (hand.handednessConfidence, 2),
                labelArea.toNearestInt(), juce::Justification::centredRight);
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
    loadButton.setBounds (bottom.removeFromLeft (145));
    bottom.removeFromLeft (8);
    openButton.setBounds (bottom.removeFromLeft (135));
    bottom.removeFromLeft (8);
    removeButton.setBounds (bottom.removeFromLeft (96));
    bottom.removeFromLeft (8);
    bypassButton.setBounds (bottom.removeFromLeft (110));
    bottom.removeFromLeft (8);
    enableButton.setBounds (bottom.removeFromLeft (120));
}