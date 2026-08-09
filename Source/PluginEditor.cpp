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
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setSize (920, 620);
    setResizable (true, true);
    setResizeLimits (760, 520, 1400, 980);

    for (int i = 0; i < GestureRackAudioProcessor::slotCount; ++i)
    {
        auto& button = slotButtons[static_cast<size_t> (i)];
        addAndMakeVisible (button);
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
    startTimerHz (60);
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
        auto name = processor.getSlotPluginName (i);

        if (name.length() > 12)
            name = name.substring (0, 9) + "...";

        button.setButtonText (juce::String (i + 1) + "  " + (loaded ? name : "EMPTY"));
        button.setTooltip (juce::String ("Slot ") + juce::String (i + 1) + ": "
                           + processor.getSlotPluginName (i));
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
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (juce::Colour::fromRGB (12, 14, 18));

    g.setColour (juce::Colour::fromRGB (235, 238, 242));
    g.setFont (juce::FontOptions (25.0f, juce::Font::bold));
    g.drawText ("GESTURE RACK", 24, 16, getWidth() - 48, 32, juce::Justification::centredLeft);

    g.setColour (juce::Colour::fromRGB (104, 111, 126));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("SERIAL SIGNAL CHAIN  1  >  2  >  3  >  4  >  5  >  6  >  7  >  8  >  9",
                24, 47, getWidth() - 48, 18, juce::Justification::centredLeft);

    auto visualArea = bounds.reduced (24.0f)
                            .withTrimmedTop (112.0f)
                            .withTrimmedBottom (76.0f);
    auto left = visualArea.removeFromLeft (visualArea.getWidth() * 0.58f);
    auto right = visualArea.reduced (12.0f, 0.0f);

    g.setColour (juce::Colour::fromRGB (22, 25, 31));
    g.fillRoundedRectangle (left, 18.0f);
    g.setColour (juce::Colour::fromRGB (45, 50, 61));
    g.drawRoundedRectangle (left, 18.0f, 1.0f);

    const auto snapshot = processor.getVisionSnapshot();
    const auto connected = processor.isVisionConnected();
    drawHand (g, left.reduced (24.0f), snapshot, connected);

    g.setColour (juce::Colour::fromRGB (22, 25, 31));
    g.fillRoundedRectangle (right, 18.0f);
    g.setColour (juce::Colour::fromRGB (45, 50, 61));
    g.drawRoundedRectangle (right, 18.0f, 1.0f);

    const auto selected = processor.getSelectedSlot();
    const auto loaded = processor.isSlotLoaded (selected);
    const auto bypassed = processor.isSlotBypassed (selected);
    const auto stateColour = bypassed ? juce::Colour::fromRGB (235, 92, 92)
                                      : juce::Colour::fromRGB (92, 220, 155);

    auto panel = right.toNearestInt().reduced (20);

    g.setColour (juce::Colour::fromRGB (130, 137, 151));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("SELECTED SLOT", panel.removeFromTop (22), juce::Justification::centredLeft);

    g.setColour (juce::Colour::fromRGB (238, 241, 245));
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText ("SLOT " + juce::String (selected + 1), panel.removeFromTop (42), juce::Justification::centredLeft);

    panel.removeFromTop (8);
    g.setColour (juce::Colour::fromRGB (130, 137, 151));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("PLUGIN", panel.removeFromTop (22), juce::Justification::centredLeft);

    g.setColour (juce::Colour::fromRGB (230, 233, 238));
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawFittedText (processor.getSlotPluginName (selected), panel.removeFromTop (54),
                      juce::Justification::centredLeft, 2);

    panel.removeFromTop (8);
    auto stateRect = panel.removeFromTop (64);
    g.setColour ((loaded ? stateColour : juce::Colour::fromRGB (100, 106, 118)).withAlpha (0.13f));
    g.fillRoundedRectangle (stateRect.toFloat(), 12.0f);
    g.setColour (loaded ? stateColour : juce::Colour::fromRGB (120, 126, 138));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (loaded ? (bypassed ? "BYPASSED" : "ACTIVE") : "EMPTY SLOT",
                stateRect, juce::Justification::centred);

    panel.removeFromTop (16);
    g.setColour (juce::Colour::fromRGB (130, 137, 151));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("CURRENT GESTURE", panel.removeFromTop (22), juce::Justification::centredLeft);

    g.setColour (connected ? juce::Colour::fromRGB (230, 233, 238)
                           : juce::Colour::fromRGB (120, 126, 138));
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    const auto gestureText = connected ? gr::gestureToString (snapshot.stableGesture) : "VISION OFFLINE";
    g.drawText (gestureText, panel.removeFromTop (34), juce::Justification::centredLeft);

    if (const auto error = processor.getSlotLastError (selected); error.isNotEmpty())
    {
        auto errorRect = getLocalBounds().reduced (24).removeFromBottom (62);
        g.setColour (juce::Colour::fromRGB (235, 92, 92));
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText ("Slot " + juce::String (selected + 1) + ": " + error,
                          errorRect, juce::Justification::centredLeft, 2);
    }
}

void GestureRackAudioProcessorEditor::drawHand (juce::Graphics& g,
                                                 juce::Rectangle<float> area,
                                                 const gr::VisionSnapshot& snapshot,
                                                 bool connected)
{
    const auto bypassed = processor.isGestureBypassed();
    const auto activeColour = bypassed ? juce::Colour::fromRGB (235, 92, 92)
                                       : juce::Colour::fromRGB (92, 220, 155);

    if (! connected || ! snapshot.handPresent)
    {
        g.setColour (juce::Colour::fromRGB (88, 94, 106));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (connected ? "SHOW YOUR HAND" : "START VISION ENGINE",
                    area.toNearestInt(), juce::Justification::centred);
        return;
    }

    std::array<juce::Point<float>, 21> points {};
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto x = juce::jlimit (0.0f, 1.0f, snapshot.landmarks[i].x);
        const auto y = juce::jlimit (0.0f, 1.0f, snapshot.landmarks[i].y);
        points[i] = { area.getX() + x * area.getWidth(),
                      area.getY() + y * area.getHeight() };
    }

    const auto time = juce::Time::getMillisecondCounterHiRes() * 0.004;
    const auto pulse = static_cast<float> (0.5 + 0.5 * std::sin (time));

    auto palmCentre = juce::Point<float>();
    for (const auto index : { 0, 5, 9, 13, 17 })
        palmCentre += points[static_cast<size_t> (index)];
    palmCentre /= 5.0f;

    g.setColour (activeColour.withAlpha (0.08f + 0.08f * pulse));
    g.fillEllipse (palmCentre.x - 48.0f - pulse * 8.0f,
                   palmCentre.y - 48.0f - pulse * 8.0f,
                   96.0f + pulse * 16.0f,
                   96.0f + pulse * 16.0f);

    g.setColour (activeColour.withAlpha (0.85f));
    for (const auto [a, b] : handConnections)
        g.drawLine ({ points[static_cast<size_t> (a)], points[static_cast<size_t> (b)] }, 3.0f);

    for (const auto& point : points)
    {
        g.setColour (activeColour.withAlpha (0.25f));
        g.fillEllipse (point.x - 6.0f, point.y - 6.0f, 12.0f, 12.0f);
        g.setColour (activeColour);
        g.fillEllipse (point.x - 3.0f, point.y - 3.0f, 6.0f, 6.0f);
    }
}

void GestureRackAudioProcessorEditor::resized()
{
    auto slotRow = getLocalBounds().reduced (24).withTrimmedTop (68).removeFromTop (42);
    const auto gap = 5;
    const auto totalGap = gap * (GestureRackAudioProcessor::slotCount - 1);
    const auto slotWidth = juce::jmax (1, (slotRow.getWidth() - totalGap) / GestureRackAudioProcessor::slotCount);

    for (auto& button : slotButtons)
    {
        button.setBounds (slotRow.removeFromLeft (slotWidth));
        slotRow.removeFromLeft (gap);
    }

    auto bottom = getLocalBounds().reduced (24).removeFromBottom (44);
    const auto buttonWidth = 145;

    loadButton.setBounds (bottom.removeFromLeft (buttonWidth));
    bottom.removeFromLeft (8);
    openButton.setBounds (bottom.removeFromLeft (buttonWidth));
    bottom.removeFromLeft (8);
    removeButton.setBounds (bottom.removeFromLeft (110));
    bottom.removeFromLeft (8);
    bypassButton.setBounds (bottom.removeFromLeft (120));
    bottom.removeFromLeft (8);
    enableButton.setBounds (bottom.removeFromLeft (130));
}
