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
    setSize (700, 480);
    setResizable (true, true);
    setResizeLimits (560, 400, 1100, 800);

    addAndMakeVisible (loadButton);
    addAndMakeVisible (openButton);
    addAndMakeVisible (enableButton);

    loadButton.onClick = [this] { choosePlugin(); };
    openButton.onClick = [this] { processor.openChildEditor(); };
    enableButton.onClick = [this]
    {
        processor.setGestureEnabled (! processor.isGestureEnabled());
        repaint();
    };

    startTimerHz (60);
}

GestureRackAudioProcessorEditor::~GestureRackAudioProcessorEditor()
{
    stopTimer();
}

void GestureRackAudioProcessorEditor::timerCallback()
{
    enableButton.setButtonText (processor.isGestureEnabled() ? "GESTURE ON" : "GESTURE OFF");
    repaint();
}

void GestureRackAudioProcessorEditor::choosePlugin()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose a VST3 effect",
        juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory),
        "*.vst3");

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::canSelectDirectories;

    fileChooser->launchAsync (flags, [this] (const juce::FileChooser& chooser)
    {
        const auto chosen = chooser.getResult();
        if (chosen.exists())
            processor.loadVst3FromFile (chosen);
    });
}

void GestureRackAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (juce::Colour::fromRGB (12, 14, 18));

    g.setColour (juce::Colour::fromRGB (235, 238, 242));
    g.setFont (juce::FontOptions (25.0f, juce::Font::bold));
    g.drawText ("GESTURE RACK", 24, 18, getWidth() - 48, 32, juce::Justification::centredLeft);

    auto visualArea = bounds.reduced (24.0f).withTrimmedTop (66.0f).withTrimmedBottom (76.0f);
    auto left = visualArea.removeFromLeft (visualArea.getWidth() * 0.62f);
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

    const auto bypassed = processor.isGestureBypassed();
    const auto gestureText = connected ? gr::gestureToString (snapshot.stableGesture) : "VISION OFFLINE";

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (juce::Colour::fromRGB (130, 137, 151));
    g.drawText ("GESTURE", right.toNearestInt().withTrimmedLeft (20).withHeight (28), juce::Justification::centredLeft);

    auto statusLine = right.toNearestInt().withTrimmedLeft (20).withTrimmedRight (20).withTrimmedTop (34).withHeight (42);
    g.setFont (juce::FontOptions (23.0f, juce::Font::bold));
    g.setColour (connected ? juce::Colour::fromRGB (238, 241, 245) : juce::Colour::fromRGB (120, 126, 138));
    g.drawText (gestureText, statusLine, juce::Justification::centredLeft);

    auto stateRect = right.toNearestInt().withTrimmedLeft (20).withTrimmedRight (20).withTrimmedTop (105).withHeight (72);
    const auto stateColour = bypassed ? juce::Colour::fromRGB (235, 92, 92)
                                      : juce::Colour::fromRGB (92, 220, 155);
    g.setColour (stateColour.withAlpha (0.13f));
    g.fillRoundedRectangle (stateRect.toFloat(), 12.0f);
    g.setColour (stateColour);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (bypassed ? "BYPASSED" : "ACTIVE", stateRect, juce::Justification::centred);

    auto pluginLabel = right.toNearestInt().withTrimmedLeft (20).withTrimmedRight (20).withTrimmedTop (205).withHeight (24);
    g.setColour (juce::Colour::fromRGB (130, 137, 151));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("TARGET", pluginLabel, juce::Justification::centredLeft);

    auto pluginName = pluginLabel.translated (0, 28).withHeight (52);
    g.setColour (juce::Colour::fromRGB (230, 233, 238));
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawFittedText (processor.getLoadedPluginName(), pluginName, juce::Justification::centredLeft, 2);

    if (const auto error = processor.getLastError(); error.isNotEmpty())
    {
        auto errorRect = getLocalBounds().reduced (24).removeFromBottom (60);
        g.setColour (juce::Colour::fromRGB (235, 92, 92));
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (error, errorRect, juce::Justification::centredLeft, 2);
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

    for (const auto& p : points)
    {
        g.setColour (activeColour.withAlpha (0.25f));
        g.fillEllipse (p.x - 6.0f, p.y - 6.0f, 12.0f, 12.0f);
        g.setColour (activeColour);
        g.fillEllipse (p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
    }
}

void GestureRackAudioProcessorEditor::resized()
{
    auto bottom = getLocalBounds().reduced (24).removeFromBottom (44);
    loadButton.setBounds (bottom.removeFromLeft (150));
    bottom.removeFromLeft (10);
    openButton.setBounds (bottom.removeFromLeft (150));
    bottom.removeFromLeft (10);
    enableButton.setBounds (bottom.removeFromLeft (150));
}
