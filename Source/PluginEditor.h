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

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

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
