#pragma once
#include <JuceHeader.h>
#include <atomic>
#include "VisionReceiver.h"
#include "GestureBypassWrapper.h"

class GestureRackAudioProcessor final : public juce::AudioProcessor,
                                        private juce::Timer
{
public:
    GestureRackAudioProcessor();
    ~GestureRackAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void loadVst3FromFile (const juce::File& file);
    void openChildEditor();
    juce::String getLoadedPluginName() const;
    juce::String getLastError() const;

    gr::VisionSnapshot getVisionSnapshot() const { return vision.getSnapshot(); }
    bool isVisionConnected() const { return vision.isConnected(); }
    bool isGestureEnabled() const noexcept { return gestureEnabled.load(); }
    void setGestureEnabled (bool enabled) noexcept { gestureEnabled.store (enabled); }
    bool isGestureBypassed() const noexcept { return requestedBypass.load(); }

private:
    class ChildEditorWindow;

    void timerCallback() override;
    void initialiseGraph();
    void connectDirect();
    void connectThroughChild();
    void installChild (std::unique_ptr<juce::AudioPluginInstance> instance,
                       const juce::PluginDescription& description,
                       const juce::MemoryBlock* restoredState);
    void loadDescriptionAsync (juce::PluginDescription description,
                               std::shared_ptr<juce::MemoryBlock> restoredState);
    gr::GestureBypassWrapper* getWrapper() const noexcept;
    juce::AudioPluginInstance* getChild() const noexcept;
    void updateLatencyFromChild();

    gr::VisionReceiver vision;
    std::atomic<bool> gestureEnabled { true };
    std::atomic<bool> requestedBypass { false };
    gr::Gesture lastAppliedGesture = gr::Gesture::unknown;

    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::Node::Ptr inputNode;
    juce::AudioProcessorGraph::Node::Ptr outputNode;
    juce::AudioProcessorGraph::Node::Ptr childNode;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    std::optional<juce::PluginDescription> loadedDescription;

    std::unique_ptr<ChildEditorWindow> childEditorWindow;
    juce::String lastError;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> hostBypassDelay
        { gr::GestureBypassWrapper::maxCompensatedLatencySamples };
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GestureRackAudioProcessor)
};
