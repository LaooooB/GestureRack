#include "GestureBypassWrapper.h"
#include <cmath>

namespace
{
struct AudioProcessorBusAccess : juce::AudioProcessor
{
    using BusesProperties = juce::AudioProcessor::BusesProperties;
};

AudioProcessorBusAccess::BusesProperties makeBridgeBuses (
    const juce::AudioChannelSet& inputSet,
    const juce::AudioChannelSet& outputSet,
    const juce::AudioChannelSet&,
    const gr::HostedPluginCapabilities&)
{
    // v0.5 stability policy: the graph-facing bridge is intentionally main-I/O only.
    // Child sidechains/AUX buses are contained inside the hosted plug-in and disabled.
    return AudioProcessorBusAccess::BusesProperties()
        .withInput ("Input", inputSet, true)
        .withOutput ("Output", outputSet, true);
}

bool isRackMainLayout (const juce::AudioChannelSet& set)
{
    return set == juce::AudioChannelSet::mono()
        || set == juce::AudioChannelSet::stereo();
}

bool configureMainBus (juce::AudioPluginInstance& child,
                       bool isInput,
                       const juce::AudioChannelSet& requested)
{
    if (child.getBusCount (isInput) <= 0)
        return false;

    auto current = child.getChannelLayoutOfBus (isInput, 0);
    if (! requested.isDisabled() && current == requested)
        return true;

    if (! requested.isDisabled()
        && child.setChannelLayoutOfBus (isInput, 0, requested))
        return true;

    current = child.getChannelLayoutOfBus (isInput, 0);
    if (isRackMainLayout (current))
        return true;

    for (const auto fallback : { juce::AudioChannelSet::stereo(),
                                 juce::AudioChannelSet::mono() })
        if (child.setChannelLayoutOfBus (isInput, 0, fallback))
            return true;

    return false;
}

void copyAdapted (const juce::AudioBuffer<float>& source,
                  juce::AudioBuffer<float>& destination,
                  int samples)
{
    const auto sourceChannels = source.getNumChannels();
    const auto destinationChannels = destination.getNumChannels();

    if (destinationChannels <= 0 || samples <= 0)
        return;

    destination.clear (0, samples);
    if (sourceChannels <= 0)
        return;

    if (sourceChannels == destinationChannels)
    {
        for (int channel = 0; channel < destinationChannels; ++channel)
            destination.copyFrom (channel, 0, source, channel, 0, samples);
        return;
    }

    if (destinationChannels == 1)
    {
        const auto gain = 1.0f / static_cast<float> (sourceChannels);
        for (int channel = 0; channel < sourceChannels; ++channel)
            destination.addFrom (0, 0, source, channel, 0, samples, gain);
        return;
    }

    if (sourceChannels == 1)
    {
        for (int channel = 0; channel < destinationChannels; ++channel)
            destination.copyFrom (channel, 0, source, 0, 0, samples);
        return;
    }

    const auto commonChannels = juce::jmin (sourceChannels, destinationChannels);
    for (int channel = 0; channel < commonChannels; ++channel)
        destination.copyFrom (channel, 0, source, channel, 0, samples);

    for (int channel = commonChannels; channel < destinationChannels; ++channel)
        destination.copyFrom (channel, 0, source, commonChannels - 1, 0, samples);
}

void copySameLayoutWithoutDestroyingAliasedInput (
    const juce::AudioBuffer<float>& source,
    juce::AudioBuffer<float>& destination,
    int samples)
{
    const auto common = juce::jmin (source.getNumChannels(), destination.getNumChannels());
    for (int channel = 0; channel < common; ++channel)
    {
        if (source.getReadPointer (channel) != destination.getWritePointer (channel))
            destination.copyFrom (channel, 0, source, channel, 0, samples);
    }

    for (int channel = common; channel < destination.getNumChannels(); ++channel)
        destination.clear (channel, 0, samples);
}

float finitePeak (const juce::AudioBuffer<float>& buffer,
                  int samples,
                  bool& allFinite) noexcept
{
    float peak = 0.0f;
    allFinite = true;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto value = data[sample];
            if (! std::isfinite (value))
            {
                allFinite = false;
                continue;
            }

            peak = juce::jmax (peak, std::abs (value));
        }
    }

    return peak;
}
}

namespace gr
{
bool GestureBypassWrapper::configureChildForHosting (
    juce::AudioPluginInstance& child,
    const juce::AudioChannelSet& requestedMainInput,
    const juce::AudioChannelSet& requestedMainOutput,
    const juce::AudioChannelSet&,
    bool allowZeroMainInput,
    HostedPluginCapabilities& capabilities,
    juce::String& error)
{
    capabilities = {};
    error.clear();

    capabilities.inputBusCount = child.getBusCount (true);
    capabilities.outputBusCount = child.getBusCount (false);
    capabilities.acceptsMidi = child.acceptsMidi();
    capabilities.producesMidi = child.producesMidi();
    capabilities.hasNativeEditor = child.hasEditor();

    if (capabilities.outputBusCount <= 0)
    {
        error = "The hosted plug-in has no routable main audio output bus.";
        return false;
    }

    // Main-only policy. Do not infer that a secondary bus should be active simply
    // because a plug-in exposes one. This is the key containment rule for meta racks.
    child.disableNonMainBuses();
    bool allNonMainDisabled = true;

    for (const auto isInput : { true, false })
    {
        for (int busIndex = 1; busIndex < child.getBusCount (isInput); ++busIndex)
        {
            auto* bus = child.getBus (isInput, busIndex);
            if (bus == nullptr)
                continue;

            if (isInput && capabilities.availableSidechainInputBus < 0)
                capabilities.availableSidechainInputBus = busIndex;

            if (bus->isEnabled() && ! bus->enable (false))
                allNonMainDisabled = false;
        }
    }

    capabilities.nonMainBusesDisabled = allNonMainDisabled;

    bool mainInputConfigured = false;
    if (capabilities.inputBusCount > 0)
        mainInputConfigured = configureMainBus (child, true, requestedMainInput);

    if (! mainInputConfigured && ! allowZeroMainInput)
    {
        error = "The hosted effect cannot provide a mono/stereo main audio input.";
        return false;
    }

    if (! configureMainBus (child, false, requestedMainOutput))
    {
        error = "The hosted plug-in cannot provide a mono/stereo main audio output.";
        return false;
    }

    capabilities.mainInputChannels =
        mainInputConfigured ? child.getMainBusNumInputChannels() : 0;
    capabilities.mainOutputChannels = child.getMainBusNumOutputChannels();
    capabilities.zeroInputInstrument =
        allowZeroMainInput && capabilities.mainInputChannels <= 0;

    if (capabilities.mainInputChannels < 0
        || capabilities.mainInputChannels > 2
        || capabilities.mainOutputChannels <= 0
        || capabilities.mainOutputChannels > 2)
    {
        error = "The hosted plug-in main bus is not compatible with the rack mono/stereo bridge.";
        return false;
    }

    capabilities.sidechainInputBus = -1;
    capabilities.sidechainInputChannels = 0;
    capabilities.auxiliaryOutputBusCount = juce::jmax (0, child.getBusCount (false) - 1);

    for (int busIndex = 1; busIndex < child.getBusCount (false); ++busIndex)
    {
        auto* bus = child.getBus (false, busIndex);
        if (bus != nullptr && bus->isEnabled())
            ++capabilities.activeAuxiliaryOutputBusCount;
    }

    return true;
}

GestureBypassWrapper::GestureBypassWrapper (
    std::unique_ptr<juce::AudioPluginInstance> childToOwn,
    const juce::AudioChannelSet& inputSet,
    const juce::AudioChannelSet& outputSet,
    const juce::AudioChannelSet& hostSidechainSet,
    HostedPluginCapabilities capabilitiesToUse,
    std::atomic<bool>& requestedBypassState)
    : juce::AudioProcessor (makeBridgeBuses (inputSet,
                                             outputSet,
                                             hostSidechainSet,
                                             capabilitiesToUse)),
      child (std::move (childToOwn)),
      capabilities (std::move (capabilitiesToUse)),
      requestedBypass (requestedBypassState),
      requestedMainInput (inputSet),
      requestedMainOutput (outputSet),
      allowZeroMainInput (capabilities.zeroInputInstrument)
{
    configuredTopology = captureChildTopology();
    updateTopologyTelemetry (configuredTopology);
}

GestureBypassWrapper::~GestureBypassWrapper()
{
    cancelPendingUpdate();
    embeddedEditor.reset();

    const juce::SpinLock::ScopedLockType lock (topologyLock);
    if (childPrepared && child != nullptr)
        child->releaseResources();
}

HostedPluginTelemetry GestureBypassWrapper::getTelemetry() const noexcept
{
    HostedPluginTelemetry result;
    result.inputPeak = telemetryInputPeak.load (std::memory_order_relaxed);
    result.outputPeak = telemetryOutputPeak.load (std::memory_order_relaxed);
    result.processMicros = telemetryProcessMicros.load (std::memory_order_relaxed);
    result.safetyTripCount = telemetrySafetyTripCount.load (std::memory_order_relaxed);
    result.topologyMismatchCount = telemetryTopologyMismatchCount.load (std::memory_order_relaxed);
    result.inputBusCount = telemetryInputBusCount.load (std::memory_order_relaxed);
    result.outputBusCount = telemetryOutputBusCount.load (std::memory_order_relaxed);
    result.totalInputChannels = telemetryTotalInputChannels.load (std::memory_order_relaxed);
    result.totalOutputChannels = telemetryTotalOutputChannels.load (std::memory_order_relaxed);
    result.activeAuxiliaryOutputBusCount = telemetryActiveAuxiliaryOutputs.load (std::memory_order_relaxed);
    result.sidechainActive = false;
    result.topologyPending = telemetryTopologyPending.load (std::memory_order_relaxed);
    result.safetyActive = telemetrySafetyActive.load (std::memory_order_relaxed);
    result.lastSafetyReason = static_cast<HostedSafetyReason> (
        telemetryLastSafetyReason.load (std::memory_order_relaxed));
    return result;
}

int GestureBypassWrapper::getChildLatencySamples() const noexcept
{
    return child != nullptr ? child->getLatencySamples() : 0;
}

GestureBypassWrapper::BusTopologySnapshot
GestureBypassWrapper::captureChildTopology() const noexcept
{
    BusTopologySnapshot topology;
    if (child == nullptr)
        return topology;

    topology.inputBusCount = child->getBusCount (true);
    topology.outputBusCount = child->getBusCount (false);
    topology.totalInputChannels = child->getTotalNumInputChannels();
    topology.totalOutputChannels = child->getTotalNumOutputChannels();
    topology.mainInputChannels = child->getMainBusNumInputChannels();
    topology.mainOutputChannels = child->getMainBusNumOutputChannels();

    for (int busIndex = 1; busIndex < child->getBusCount (true); ++busIndex)
    {
        auto* bus = child->getBus (true, busIndex);
        if (bus != nullptr && bus->isEnabled())
            topology.activeNonMainInputChannels += child->getChannelCountOfBus (true, busIndex);
    }

    for (int busIndex = 1; busIndex < child->getBusCount (false); ++busIndex)
    {
        auto* bus = child->getBus (false, busIndex);
        if (bus != nullptr && bus->isEnabled())
            topology.activeNonMainOutputChannels += child->getChannelCountOfBus (false, busIndex);
    }

    return topology;
}

void GestureBypassWrapper::updateTopologyTelemetry (
    const BusTopologySnapshot& topology) noexcept
{
    telemetryInputBusCount.store (topology.inputBusCount, std::memory_order_relaxed);
    telemetryOutputBusCount.store (topology.outputBusCount, std::memory_order_relaxed);
    telemetryTotalInputChannels.store (topology.totalInputChannels, std::memory_order_relaxed);
    telemetryTotalOutputChannels.store (topology.totalOutputChannels, std::memory_order_relaxed);
    telemetryActiveAuxiliaryOutputs.store (capabilities.activeAuxiliaryOutputBusCount,
                                           std::memory_order_relaxed);
    telemetrySidechainActive.store (false, std::memory_order_relaxed);
}

void GestureBypassWrapper::allocateProcessingBuffers (double sampleRate,
                                                       int samplesPerBlock)
{
    const auto capacity = juce::jmax (
        minimumRealtimeScratchSamples,
        juce::jmax (samplesPerBlock,
                    requestedScratchSamples.load (std::memory_order_relaxed)));

    const auto mainOutputChannels = juce::jmax (1, getMainBusNumOutputChannels());
    juce::dsp::ProcessSpec spec {
        sampleRate,
        static_cast<juce::uint32> (juce::jmax (1, capacity)),
        static_cast<juce::uint32> (mainOutputChannels)
    };

    dryDelay.prepare (spec);
    dryDelay.reset();
    dryBuffer.setSize (mainOutputChannels, capacity, false, false, true);

    childBufferChannels = child != nullptr
        ? juce::jmax (1,
                      juce::jmax (child->getTotalNumInputChannels(),
                                  child->getTotalNumOutputChannels()))
        : 1;

    childBuffer.setSize (childBufferChannels, capacity, false, false, true);
    scratchCapacitySamples.store (capacity, std::memory_order_release);
    requestedScratchSamples.store (capacity, std::memory_order_relaxed);
}

void GestureBypassWrapper::requestReconfiguration (int minimumSamples,
                                                   HostedSafetyReason reason) noexcept
{
    auto requested = requestedScratchSamples.load (std::memory_order_relaxed);
    while (requested < minimumSamples
           && ! requestedScratchSamples.compare_exchange_weak (
               requested, minimumSamples, std::memory_order_relaxed))
    {
    }

    reconfigurationRequested.store (true, std::memory_order_release);
    telemetryTopologyPending.store (true, std::memory_order_relaxed);
    telemetryLastSafetyReason.store (static_cast<int> (reason), std::memory_order_relaxed);
    triggerAsyncUpdate();
}

void GestureBypassWrapper::handleAsyncUpdate()
{
    if (! reconfigurationRequested.load (std::memory_order_acquire)
        || child == nullptr)
        return;

    const juce::SpinLock::ScopedLockType lock (topologyLock);

    if (childPrepared)
    {
        child->releaseResources();
        childPrepared = false;
    }

    HostedPluginCapabilities nextCapabilities;
    juce::String error;
    if (! configureChildForHosting (*child,
                                    requestedMainInput,
                                    requestedMainOutput,
                                    juce::AudioChannelSet::disabled(),
                                    allowZeroMainInput,
                                    nextCapabilities,
                                    error))
    {
        telemetryTopologyPending.store (true, std::memory_order_relaxed);
        telemetrySafetyActive.store (true, std::memory_order_relaxed);
        return;
    }

    capabilities = nextCapabilities;
    const auto sampleRate = currentSampleRate.load (std::memory_order_relaxed);
    const auto blockSize = juce::jmax (
        currentBlockSize.load (std::memory_order_relaxed),
        requestedScratchSamples.load (std::memory_order_relaxed));

    allocateProcessingBuffers (sampleRate, blockSize);
    child->setPlayHead (getPlayHead());
    child->setRateAndBufferSizeDetails (sampleRate, blockSize);
    child->prepareToPlay (sampleRate, blockSize);
    childPrepared = true;

    configuredTopology = captureChildTopology();
    updateTopologyTelemetry (configuredTopology);
    reconfigurationRequested.store (false, std::memory_order_release);
    telemetryTopologyPending.store (false, std::memory_order_relaxed);
    telemetrySafetyActive.store (false, std::memory_order_relaxed);
}

void GestureBypassWrapper::renderImmediateFallback (
    const juce::AudioBuffer<float>& input,
    juce::AudioBuffer<float>& output,
    int samples) noexcept
{
    const auto bypassed = requestedBypass.load (std::memory_order_relaxed);
    if (allowZeroMainInput && ! bypassed)
        output.clear (0, samples);
    else
        copySameLayoutWithoutDestroyingAliasedInput (input, output, samples);
}

void GestureBypassWrapper::renderPreparedFallback (
    juce::AudioBuffer<float>& output,
    int samples) noexcept
{
    const auto bypassed = requestedBypass.load (std::memory_order_relaxed);
    if (allowZeroMainInput && ! bypassed)
        output.clear (0, samples);
    else
        copyAdapted (dryBuffer, output, samples);
}

juce::AudioProcessorEditor* GestureBypassWrapper::getOrCreateEmbeddedEditor()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (child == nullptr || ! child->hasEditor())
        return nullptr;

    if (embeddedEditor == nullptr)
        embeddedEditor.reset (child->createEditorIfNeeded());

    return embeddedEditor.get();
}

void GestureBypassWrapper::releaseEmbeddedEditor()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    embeddedEditor.reset();
}

void GestureBypassWrapper::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    cancelPendingUpdate();
    const juce::SpinLock::ScopedLockType lock (topologyLock);

    currentSampleRate.store (sampleRate, std::memory_order_relaxed);
    currentBlockSize.store (samplesPerBlock, std::memory_order_relaxed);

    if (childPrepared && child != nullptr)
    {
        child->releaseResources();
        childPrepared = false;
    }

    allocateProcessingBuffers (sampleRate, samplesPerBlock);
    wetMix.reset (sampleRate, 0.015);
    wetMix.setCurrentAndTargetValue (
        requestedBypass.load (std::memory_order_relaxed) ? 0.0f : 1.0f);
    safetyWet.reset (sampleRate, 0.010);
    safetyWet.setCurrentAndTargetValue (1.0f);
    safetyHoldRemaining.store (0, std::memory_order_relaxed);
    telemetrySafetyActive.store (false, std::memory_order_relaxed);
    lastRequestedBypass = requestedBypass.load (std::memory_order_relaxed);

    if (child != nullptr)
    {
        child->setPlayHead (getPlayHead());
        child->setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);
        child->prepareToPlay (sampleRate, samplesPerBlock);
        childPrepared = true;
        configuredTopology = captureChildTopology();
        updateTopologyTelemetry (configuredTopology);
        telemetryTopologyPending.store (false, std::memory_order_relaxed);
    }
}

void GestureBypassWrapper::releaseResources()
{
    cancelPendingUpdate();
    const juce::SpinLock::ScopedLockType lock (topologyLock);

    if (childPrepared && child != nullptr)
    {
        child->releaseResources();
        childPrepared = false;
    }

    dryDelay.reset();
}

bool GestureBypassWrapper::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return in == out && isRackMainLayout (in);
}

void GestureBypassWrapper::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    if (child == nullptr)
        return;

    const auto samples = buffer.getNumSamples();
    auto hostMainInput = getBusBuffer (buffer, true, 0);
    auto hostMainOutput = getBusBuffer (buffer, false, 0);

    const juce::SpinLock::ScopedTryLockType lock (topologyLock);
    if (! lock.isLocked())
    {
        telemetrySafetyActive.store (true, std::memory_order_relaxed);
        telemetryLastSafetyReason.store (
            static_cast<int> (HostedSafetyReason::bridgeBusy),
            std::memory_order_relaxed);
        renderImmediateFallback (hostMainInput, hostMainOutput, samples);
        return;
    }

    child->setPlayHead (getPlayHead());

    const auto shouldBypass = requestedBypass.load (std::memory_order_relaxed);
    if (shouldBypass != lastRequestedBypass)
    {
        lastRequestedBypass = shouldBypass;
        wetMix.setTargetValue (shouldBypass ? 0.0f : 1.0f);
    }

    if (samples > scratchCapacitySamples.load (std::memory_order_acquire))
    {
        requestReconfiguration (samples, HostedSafetyReason::blockTooLarge);
        telemetrySafetyActive.store (true, std::memory_order_relaxed);
        renderImmediateFallback (hostMainInput, hostMainOutput, samples);
        return;
    }

    const auto liveTopology = captureChildTopology();
    if (liveTopology != configuredTopology)
    {
        telemetryTopologyMismatchCount.fetch_add (1, std::memory_order_relaxed);
        telemetrySafetyActive.store (true, std::memory_order_relaxed);
        requestReconfiguration (samples, HostedSafetyReason::topologyChanged);
        renderImmediateFallback (hostMainInput, hostMainOutput, samples);
        return;
    }

    copyAdapted (hostMainInput, dryBuffer, samples);
    bool inputFinite = true;
    const auto inputPeak = finitePeak (dryBuffer, samples, inputFinite);
    telemetryInputPeak.store (inputPeak, std::memory_order_relaxed);

    if (! inputFinite)
    {
        telemetrySafetyTripCount.fetch_add (1, std::memory_order_relaxed);
        telemetrySafetyActive.store (true, std::memory_order_relaxed);
        telemetryLastSafetyReason.store (
            static_cast<int> (HostedSafetyReason::nonFiniteOutput),
            std::memory_order_relaxed);
        hostMainOutput.clear (0, samples);
        return;
    }

    const auto dryChannels = dryBuffer.getNumChannels();
    const auto latency = juce::jlimit (0,
                                       maxCompensatedLatencySamples - 1,
                                       child->getLatencySamples());
    dryDelay.setDelay (static_cast<float> (latency));

    for (int sample = 0; sample < samples; ++sample)
        for (int channel = 0; channel < dryChannels; ++channel)
        {
            dryDelay.pushSample (channel, dryBuffer.getSample (channel, sample));
            dryBuffer.setSample (channel, sample, dryDelay.popSample (channel));
        }

    auto hold = safetyHoldRemaining.load (std::memory_order_relaxed);
    if (hold > 0)
    {
        safetyHoldRemaining.store (hold - 1, std::memory_order_relaxed);
        telemetrySafetyActive.store (true, std::memory_order_relaxed);
        renderPreparedFallback (hostMainOutput, samples);
        return;
    }

    const auto requiredChildChannels = juce::jmax (
        1,
        juce::jmax (child->getTotalNumInputChannels(),
                    child->getTotalNumOutputChannels()));

    if (requiredChildChannels != childBufferChannels)
    {
        telemetryTopologyMismatchCount.fetch_add (1, std::memory_order_relaxed);
        telemetrySafetyActive.store (true, std::memory_order_relaxed);
        requestReconfiguration (samples, HostedSafetyReason::topologyChanged);
        renderPreparedFallback (hostMainOutput, samples);
        return;
    }

    childBuffer.clear (0, samples);
    if (capabilities.mainInputChannels > 0 && child->getBusCount (true) > 0)
    {
        auto childMainInput = child->getBusBuffer (childBuffer, true, 0);
        copyAdapted (hostMainInput, childMainInput, samples);
    }

    const auto startMs = juce::Time::getMillisecondCounterHiRes();
    child->processBlock (childBuffer, midi);
    const auto elapsedMicros = static_cast<float> (
        (juce::Time::getMillisecondCounterHiRes() - startMs) * 1000.0);
    telemetryProcessMicros.store (elapsedMicros, std::memory_order_relaxed);

    auto childMainOutput = child->getBusBuffer (childBuffer, false, 0);
    bool outputFinite = true;
    const auto childPeak = finitePeak (childMainOutput, samples, outputFinite);
    telemetryOutputPeak.store (childPeak, std::memory_order_relaxed);

    HostedSafetyReason safetyReason = HostedSafetyReason::none;
    if (! outputFinite)
        safetyReason = HostedSafetyReason::nonFiniteOutput;
    else if (childPeak > runawayPeakLinear)
        safetyReason = HostedSafetyReason::runawayPeak;

    if (safetyReason != HostedSafetyReason::none)
    {
        telemetrySafetyTripCount.fetch_add (1, std::memory_order_relaxed);
        telemetryLastSafetyReason.store (static_cast<int> (safetyReason),
                                         std::memory_order_relaxed);
        telemetrySafetyActive.store (true, std::memory_order_relaxed);
        safetyHoldRemaining.store (safetyHoldBlocks, std::memory_order_relaxed);
        safetyWet.setCurrentAndTargetValue (0.0f);
        renderPreparedFallback (hostMainOutput, samples);
        return;
    }

    copyAdapted (childMainOutput, hostMainOutput, samples);
    safetyWet.setTargetValue (1.0f);

    for (int sample = 0; sample < samples; ++sample)
    {
        const auto userWet = wetMix.getNextValue();
        const auto safetyGain = safetyWet.getNextValue();
        const auto wet = userWet * safetyGain;
        const auto dry = allowZeroMainInput
            ? (1.0f - userWet)
            : (1.0f - wet);

        for (int channel = 0; channel < hostMainOutput.getNumChannels(); ++channel)
        {
            const auto dryChannel = juce::jmin (channel, dryChannels - 1);
            hostMainOutput.setSample (
                channel,
                sample,
                hostMainOutput.getSample (channel, sample) * wet
                    + dryBuffer.getSample (dryChannel, sample) * dry);
        }
    }

    const auto safetyStillSmoothing = safetyWet.isSmoothing();
    telemetrySafetyActive.store (safetyStillSmoothing, std::memory_order_relaxed);
    if (! safetyStillSmoothing)
        telemetryLastSafetyReason.store (static_cast<int> (HostedSafetyReason::none),
                                         std::memory_order_relaxed);
}

double GestureBypassWrapper::getTailLengthSeconds() const
{
    return child != nullptr ? child->getTailLengthSeconds() : 0.0;
}

bool GestureBypassWrapper::acceptsMidi() const
{
    return child != nullptr && child->acceptsMidi();
}

bool GestureBypassWrapper::producesMidi() const
{
    return child != nullptr && child->producesMidi();
}
}
