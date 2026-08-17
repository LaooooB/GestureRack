#pragma once
#include <JuceHeader.h>
#include <atomic>

namespace gr
{
class GestureBypassWrapper final : public juce::AudioProcessor
{
public:
    static constexpr int maxCompensatedLatencySamples = 524288;
    static constexpr int minimumRealtimeScratchSamples = 8192;

    GestureBypassWrapper (std::unique_ptr<juce::AudioPluginInstance> childToOwn,
                          const juce::AudioChannelSet& inputSet,
                          const juce::AudioChannelSet& outputSet,
                          std::atomic<bool>& requestedBypassState);
    ~GestureBypassWrapper() override;

    juce::AudioPluginInstance* getChild() const noexcept { return child.get(); }
    int getChildLatencySamples() const noexcept;
    bool hasChildEditor() const noexcept { return child != nullptr && child->hasEditor(); }

    juce::AudioProcessorEditor* getOrCreateEmbeddedEditor();
    void releaseEmbeddedEditor();

    const juce::String getName() const override { return "Gesture Bypass Wrapper"; }
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    double getTailLengthSeconds() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override { return false; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    std::unique_ptr<juce::AudioPluginInstance> child;
    std::atomic<bool>& requestedBypass;

    juce::AudioBuffer<float> dryBuffer;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> dryDelay
        { maxCompensatedLatencySamples };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix { 1.0f };
    bool lastRequestedBypass = false;
    bool childPrepared = false;
    int scratchCapacitySamples = 0;

    std::unique_ptr<juce::AudioProcessorEditor> embeddedEditor;
};
}
