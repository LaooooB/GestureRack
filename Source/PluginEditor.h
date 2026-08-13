#pragma once

#include <JuceHeader.h>
#include <array>
#include "PluginProcessor.h"
#include "ParameterInspector.h"

class GestureRackAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer,
                                              public juce::FileDragAndDropTarget
{
public:
    explicit GestureRackAudioProcessorEditor (GestureRackAudioProcessor&);
    ~GestureRackAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void paintOverChildren (juce::Graphics& g) override
    {
        auto content = getLocalBounds().reduced (24);
        content.removeFromTop (112);
        content.removeFromBottom (58);
        auto panel = content.removeFromLeft (juce::jlimit (300, 420, content.getWidth() * 32 / 100));

        g.setColour (juce::Colour::fromRGB (18, 21, 27));
        g.fillRoundedRectangle (panel.toFloat(), 14.0f);
        g.setColour (juce::Colour::fromRGB (226, 231, 238));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("GESTURE MODULATORS", panel.getX() + 16, panel.getY() + 14,
                    panel.getWidth() - 32, 20, juce::Justification::centredLeft);

        const auto live = processor.getLiveRightGesture();
        const std::array<gr::ControlGesture, 7> gestures {
            gr::ControlGesture::openPalm, gr::ControlGesture::closedFist,
            gr::ControlGesture::victory, gr::ControlGesture::thumbUp,
            gr::ControlGesture::thumbDown, gr::ControlGesture::pointRight,
            gr::ControlGesture::pointLeft
        };
        auto row = panel.reduced (16).withTrimmedTop (48).removeFromTop (36);
        const auto gap = 5;
        const auto width = juce::jmax (1, (row.getWidth() - gap * 6) / 7);
        for (const auto gesture : gestures)
        {
            auto chip = row.removeFromLeft (width);
            g.setColour (gesture == live ? juce::Colour::fromRGB (69, 109, 190)
                                         : juce::Colour::fromRGB (29, 34, 43));
            g.fillRoundedRectangle (chip.toFloat(), 7.0f);
            g.setColour (juce::Colour::fromRGB (190, 199, 212));
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            g.drawFittedText (gr::controlGestureToString (gesture).toUpperCase(), chip.reduced (2),
                              juce::Justification::centred, 1);
            row.removeFromLeft (gap);
        }
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& path : files)
            if (path.endsWithIgnoreCase (".vst3"))
                return true;
        return false;
    }

    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
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
                processor.setSelectedSlot (targetSlot);
                processor.loadVst3FromFile (targetSlot, juce::File (path));
                updateSlotButtons();
                repaint();
                break;
            }
    }

private:
    void timerCallback() override;
    void choosePluginForSelectedSlot();
    void updateSlotButtons();
    void drawHand (juce::Graphics&,
                   juce::Rectangle<float>,
                   const gr::HandSnapshot&,
                   bool connected,
                   juce::Colour colour,
                   const juce::String& label);

    GestureRackAudioProcessor& processor;

    std::array<juce::TextButton, GestureRackAudioProcessor::slotCount> slotButtons;
    std::array<juce::Rectangle<int>, GestureRackAudioProcessor::slotCount> slotDropRects {};
    juce::TextButton loadButton { "LOAD / REPLACE" };
    juce::TextButton openButton { "OPEN PLUGIN" };
    juce::TextButton removeButton { "REMOVE" };
    juce::TextButton bypassButton { "ACTIVE" };
    juce::TextButton enableButton { "GESTURE ON" };

    gr::ParameterInspector parameterInspector;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GestureRackAudioProcessorEditor)
};
