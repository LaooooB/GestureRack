#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

namespace gr
{
struct HostedPluginCapabilities
{
    int inputBusCount = 0;
    int outputBusCount = 0;
    int mainInputChannels = 0;
    int mainOutputChannels = 0;
    int availableSidechainInputBus = -1;
    int sidechainInputBus = -1;
    int sidechainInputChannels = 0;
    int auxiliaryOutputBusCount = 0;
    int activeAuxiliaryOutputBusCount = 0;
    bool acceptsMidi = false;
    bool producesMidi = false;
    bool hasNativeEditor = false;
    bool nonMainBusesDisabled = false;
    bool zeroInputInstrument = false;
};

enum class HostedSafetyReason : int
{
    none = 0,
    nonFiniteOutput,
    runawayPeak,
    topologyChanged,
    bridgeBusy,
    blockTooLarge
};

struct HostedPluginTelemetry
{
    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    float processMicros = 0.0f;
    uint64_t safetyTripCount = 0;
    uint64_t topologyMismatchCount = 0;
    int inputBusCount = 0;
    int outputBusCount = 0;
    int totalInputChannels = 0;
    int totalOutputChannels = 0;
    int activeAuxiliaryOutputBusCount = 0;
    bool sidechainActive = false;
    bool topologyPending = false;
    bool safetyActive = false;
    HostedSafetyReason lastSafetyReason = HostedSafetyReason::none;
};

class GestureBypassWrapper final : public juce::AudioProcessor
{
public:
    static constexpr int maxCompensatedLatencySamples = 524288;
    static constexpr int minimumRealtimeScratchSamples = 8192;
    static constexpr float runawayPeakLinear = 6.0f;
    static constexpr int safetyHoldBlocks = 24;

    static bool configureChildForHosting (juce::AudioPluginInstance& child,
                                          const juce::AudioChannelSet& requestedMainInput,
                                          const juce::AudioChannelSet& requestedMainOutput,
                                          const juce::AudioChannelSet& hostSidechainLayout,
                                          bool allowZeroMainInput,
                                          HostedPluginCapabilities& capabilities,
                                          juce::String& error);

    GestureBypassWrapper (std::unique_ptr<juce::AudioPluginInstance> childToOwn,
                          const juce::AudioChannelSet& inputSet,
                          const juce::AudioChannelSet& outputSet,
                          const juce::AudioChannelSet& hostSidechainSet,
                          bool allowZeroMainInputToUse,
                          HostedPluginCapabilities capabilitiesToUse,
                          std::atomic<bool>& requestedBypassState);
    ~GestureBypassWrapper() override;

    juce::AudioPluginInstance* getChild() const noexcept { return child.get(); }
    const HostedPluginCapabilities& getCapabilities() const noexcept { return capabilities; }
    HostedPluginTelemetry getTelemetry() const noexcept;
    int getChildLatencySamples() const noexcept;
    bool hasChildEditor() const noexcept { return child != nullptr && child->hasEditor(); }

    bool servicePendingReconfiguration (const juce::AudioChannelSet& hostSidechainLayout,
                                        double sampleRate,
                                        int samplesPerBlock);

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
    struct BusTopologySnapshot
    {
        int inputBusCount = 0;
        int outputBusCount = 0;
        int totalInputChannels = 0;
        int totalOutputChannels = 0;
        int mainInputChannels = 0;
        int mainOutputChannels = 0;
        int activeNonMainInputChannels = 0;
        int activeNonMainOutputChannels = 0;

        bool operator== (const BusTopologySnapshot& other) const noexcept
        {
            return inputBusCount == other.inputBusCount
                && outputBusCount == other.outputBusCount
                && totalInputChannels == other.totalInputChannels
                && totalOutputChannels == other.totalOutputChannels
                && mainInputChannels == other.mainInputChannels
                && mainOutputChannels == other.mainOutputChannels
                && activeNonMainInputChannels == other.activeNonMainInputChannels
                && activeNonMainOutputChannels == other.activeNonMainOutputChannels;
        }

        bool operator!= (const BusTopologySnapshot& other) const noexcept
        {
            return ! (*this == other);
        }
    };

    BusTopologySnapshot captureChildTopology() const noexcept;
    void allocateProcessingBuffers (double sampleRate, int samplesPerBlock);
    void requestReconfigurationForBlockSize (int samples) noexcept;
    void renderImmediateFallback (const juce::AudioBuffer<float>& input,
                                  juce::AudioBuffer<float>& output,
                                  int samples) noexcept;
    void renderPreparedFallback (juce::AudioBuffer<float>& output,
                                 int samples) noexcept;
    void updateTopologyTelemetry (const BusTopologySnapshot& topology) noexcept;

    std::unique_ptr<juce::AudioPluginInstance> child;
    HostedPluginCapabilities capabilities;
    std::atomic<bool>& requestedBypass;

    const juce::AudioChannelSet requestedMainInput;
    const juce::AudioChannelSet requestedMainOutput;
    juce::AudioChannelSet configuredHostSidechain;
    const bool allowZeroMainInput;

    juce::AudioBuffer<float> childBuffer;
    juce::AudioBuffer<float> dryBuffer;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> dryDelay
        { maxCompensatedLatencySamples };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> safetyWet { 1.0f };
    bool lastRequestedBypass = false;
    bool childPrepared = false;
    std::atomic<int> scratchCapacitySamples { 0 };
    int childBufferChannels = 0;
    BusTopologySnapshot configuredTopology;
    juce::SpinLock topologyLock;
    std::atomic<bool> reconfigurationRequested { false };
    std::atomic<int> requestedScratchSamples { 0 };
    std::atomic<int> safetyHoldRemaining { 0 };

    std::atomic<float> telemetryInputPeak { 0.0f };
    std::atomic<float> telemetryOutputPeak { 0.0f };
    std::atomic<float> telemetryProcessMicros { 0.0f };
    std::atomic<uint64_t> telemetrySafetyTripCount { 0 };
    std::atomic<uint64_t> telemetryTopologyMismatchCount { 0 };
    std::atomic<int> telemetryInputBusCount { 0 };
    std::atomic<int> telemetryOutputBusCount { 0 };
    std::atomic<int> telemetryTotalInputChannels { 0 };
    std::atomic<int> telemetryTotalOutputChannels { 0 };
    std::atomic<int> telemetryActiveAuxiliaryOutputs { 0 };
    std::atomic<bool> telemetrySidechainActive { false };
    std::atomic<bool> telemetryTopologyPending { false };
    std::atomic<bool> telemetrySafetyActive { false };
    std::atomic<int> telemetryLastSafetyReason { static_cast<int> (HostedSafetyReason::none) };

    std::unique_ptr<juce::AudioProcessorEditor> embeddedEditor;
};
}
