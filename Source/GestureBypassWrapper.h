#pragma once
#include <JuceHeader.h>
#include <atomic>

namespace gr
{
struct HostedPluginCapabilities
{
    int inputBusCount = 0;
    int outputBusCount = 0;
    int mainInputChannels = 0;
    int mainOutputChannels = 0;
    int sidechainInputBus = -1;
    int sidechainInputChannels = 0;
    int auxiliaryOutputBusCount = 0;
    int activeAuxiliaryOutputBusCount = 0;
    bool acceptsMidi = false;
    bool producesMidi = false;
    bool hasNativeEditor = false;
    bool nonMainBusesDisabled = false;
};

class GestureBypassWrapper final : public juce::AudioProcessor
{
public:
    static constexpr int maxCompensatedLatencySamples = 524288;
    static constexpr int minimumRealtimeScratchSamples = 8192;

    static bool configureChildForHosting (juce::AudioPluginInstance& child,
                                          const juce::AudioChannelSet& requestedMainInput,
                                          const juce::AudioChannelSet& requestedMainOutput,
                                          const juce::AudioChannelSet& hostSidechainLayout,
                                          HostedPluginCapabilities& capabilities,
                                          juce::String& error);

    GestureBypassWrapper (std::unique_ptr<juce::AudioPluginInstance> childToOwn,
                          const juce::AudioChannelSet& inputSet,
                          const juce::AudioChannelSet& outputSet,
                          const juce::AudioChannelSet& hostSidechainSet,
                          HostedPluginCapabilities capabilitiesToUse,
                          std::atomic<bool>& requestedBypassState);
    ~GestureBypassWrapper() override;

    juce::AudioPluginInstance* getChild() const noexcept { return child.get(); }
    const HostedPluginCapabilities& getCapabilities() const noexcept { return capabilities; }
    int getChildLatencySamples() const noexcept;
    bool hasChildEditor() const noexcept { return child != nullptr && child->hasEditor(); }

    juce::AudioProcessorEditor* getOrCreateEmbeddedEditor();
    void releaseEmbeddedEditor();

    const juce::String getName() const override { return "Hosted Plugin Bridge"; }
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
    HostedPluginCapabilities capabilities;
    std::atomic<bool>& requestedBypass;

    juce::AudioBuffer<float> childBuffer;
    juce::AudioBuffer<float> dryBuffer;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> dryDelay
        { maxCompensatedLatencySamples };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix { 1.0f };
    bool lastRequestedBypass = false;
    bool childPrepared = false;
    int scratchCapacitySamples = 0;
    int childBufferChannels = 0;

    std::unique_ptr<juce::AudioProcessorEditor> embeddedEditor;
};
}
