#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class GestureRackAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit GestureRackAudioProcessorEditor (GestureRackAudioProcessor&);
    ~GestureRackAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void choosePlugin();
    void drawHand (juce::Graphics&, juce::Rectangle<float>, const gr::VisionSnapshot&, bool connected);

    GestureRackAudioProcessor& processor;
    juce::TextButton loadButton { "LOAD VST3" };
    juce::TextButton openButton { "OPEN PLUGIN" };
    juce::TextButton enableButton { "GESTURE ON" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GestureRackAudioProcessorEditor)
};
