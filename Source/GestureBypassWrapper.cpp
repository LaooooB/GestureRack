#include "GestureBypassWrapper.h"

namespace
{
juce::AudioProcessor::BusesProperties makeBridgeBuses (const juce::AudioChannelSet& inputSet,
                                                       const juce::AudioChannelSet& outputSet,
                                                       const juce::AudioChannelSet& sidechainSet,
                                                       const gr::HostedPluginCapabilities& capabilities)
{
    if (capabilities.sidechainInputBus >= 1 && ! sidechainSet.isDisabled())
        return juce::AudioProcessor::BusesProperties()
            .withInput ("Input", inputSet, true)
            .withInput ("Sidechain", sidechainSet, true)
            .withOutput ("Output", outputSet, true);

    return juce::AudioProcessor::BusesProperties()
        .withInput ("Input", inputSet, true)
        .withOutput ("Output", outputSet, true);
}

bool isRackMainLayout (const juce::AudioChannelSet& set)
{
    return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
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

    if (! requested.isDisabled() && child.setChannelLayoutOfBus (isInput, 0, requested))
        return true;

    current = child.getChannelLayoutOfBus (isInput, 0);
    if (isRackMainLayout (current))
        return true;

    for (const auto fallback : { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono() })
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
}

namespace gr
{
bool GestureBypassWrapper::configureChildForHosting (juce::AudioPluginInstance& child,
                                                       const juce::AudioChannelSet& requestedMainInput,
                                                       const juce::AudioChannelSet& requestedMainOutput,
                                                       const juce::AudioChannelSet& hostSidechainLayout,
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

    if (capabilities.inputBusCount <= 0 || capabilities.outputBusCount <= 0)
    {
        error = "The hosted plug-in has no routable main audio input/output bus.";
        return false;
    }

    // A meta/rack effect may expose sidechains and many auxiliary outputs. Those buses
    // must not make the main stereo path fail. Disable them first, then selectively
    // re-enable one sidechain input when the outer Gesture Rack sidechain is available.
    capabilities.nonMainBusesDisabled = child.disableNonMainBuses();

    if (! configureMainBus (child, true, requestedMainInput)
        || ! configureMainBus (child, false, requestedMainOutput))
    {
        error = "The hosted plug-in cannot provide a mono/stereo main audio path.";
        return false;
    }

    capabilities.mainInputChannels = child.getMainBusNumInputChannels();
    capabilities.mainOutputChannels = child.getMainBusNumOutputChannels();
    if (capabilities.mainInputChannels <= 0 || capabilities.mainOutputChannels <= 0
        || capabilities.mainInputChannels > 2 || capabilities.mainOutputChannels > 2)
    {
        error = "The hosted plug-in main bus is not compatible with the rack mono/stereo bridge.";
        return false;
    }

    if (! hostSidechainLayout.isDisabled())
    {
        for (int busIndex = 1; busIndex < child.getBusCount (true); ++busIndex)
        {
            auto* bus = child.getBus (true, busIndex);
            if (bus == nullptr || ! bus->enable (true))
                continue;

            auto configured = child.setChannelLayoutOfBus (true, busIndex, hostSidechainLayout);
            if (! configured)
            {
                auto current = child.getChannelLayoutOfBus (true, busIndex);
                configured = isRackMainLayout (current);
                if (! configured)
                    for (const auto fallback : { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono() })
                        if (child.setChannelLayoutOfBus (true, busIndex, fallback))
                        {
                            configured = true;
                            break;
                        }
            }

            if (configured)
            {
                capabilities.sidechainInputBus = busIndex;
                capabilities.sidechainInputChannels = child.getChannelCountOfBus (true, busIndex);
                break;
            }

            bus->enable (false);
        }
    }

    capabilities.auxiliaryOutputBusCount = juce::jmax (0, child.getBusCount (false) - 1);
    for (int busIndex = 1; busIndex < child.getBusCount (false); ++busIndex)
        if (auto* bus = child.getBus (false, busIndex); bus != nullptr && bus->isEnabled())
            ++capabilities.activeAuxiliaryOutputBusCount;

    return true;
}

GestureBypassWrapper::GestureBypassWrapper (std::unique_ptr<juce::AudioPluginInstance> childToOwn,
                                            const juce::AudioChannelSet& inputSet,
                                            const juce::AudioChannelSet& outputSet,
                                            const juce::AudioChannelSet& hostSidechainSet,
                                            HostedPluginCapabilities capabilitiesToUse,
                                            std::atomic<bool>& requestedBypassState)
    : juce::AudioProcessor (makeBridgeBuses (inputSet, outputSet, hostSidechainSet, capabilitiesToUse)),
      child (std::move (childToOwn)),
      capabilities (std::move (capabilitiesToUse)),
      requestedBypass (requestedBypassState)
{
}

GestureBypassWrapper::~GestureBypassWrapper()
{
    embeddedEditor.reset();
    if (childPrepared && child != nullptr)
        child->releaseResources();
}

int GestureBypassWrapper::getChildLatencySamples() const noexcept
{
    return child != nullptr ? child->getLatencySamples() : 0;
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
    if (childPrepared && child != nullptr)
    {
        child->releaseResources();
        childPrepared = false;
    }

    const auto mainOutputChannels = juce::jmax (1, getMainBusNumOutputChannels());
    juce::dsp::ProcessSpec spec { sampleRate,
                                 static_cast<juce::uint32> (juce::jmax (1, samplesPerBlock)),
                                 static_cast<juce::uint32> (mainOutputChannels) };
    dryDelay.prepare (spec);
    dryDelay.reset();

    scratchCapacitySamples = juce::jmax (minimumRealtimeScratchSamples, samplesPerBlock);
    dryBuffer.setSize (mainOutputChannels, scratchCapacitySamples, false, false, true);

    childBufferChannels = child != nullptr
        ? juce::jmax (1, juce::jmax (child->getTotalNumInputChannels(), child->getTotalNumOutputChannels()))
        : 1;
    childBuffer.setSize (childBufferChannels, scratchCapacitySamples, false, false, true);

    wetMix.reset (sampleRate, 0.015);
    wetMix.setCurrentAndTargetValue (requestedBypass.load (std::memory_order_relaxed) ? 0.0f : 1.0f);
    lastRequestedBypass = requestedBypass.load (std::memory_order_relaxed);

    if (child != nullptr)
    {
        child->setPlayHead (getPlayHead());
        child->setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);
        child->prepareToPlay (sampleRate, samplesPerBlock);
        childPrepared = true;
    }
}

void GestureBypassWrapper::releaseResources()
{
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
    if (in != out || ! isRackMainLayout (in))
        return false;

    if (getBusCount (true) > 1)
    {
        const auto sidechain = layouts.getChannelSet (true, 1);
        if (! sidechain.isDisabled() && ! isRackMainLayout (sidechain))
            return false;
    }

    return true;
}

void GestureBypassWrapper::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    if (child == nullptr)
        return;

    child->setPlayHead (getPlayHead());
    const auto shouldBypass = requestedBypass.load (std::memory_order_relaxed);
    if (shouldBypass != lastRequestedBypass)
    {
        lastRequestedBypass = shouldBypass;
        wetMix.setTargetValue (shouldBypass ? 0.0f : 1.0f);
    }

    const auto samples = buffer.getNumSamples();
    auto hostMainInput = getBusBuffer (buffer, true, 0);
    auto hostMainOutput = getBusBuffer (buffer, false, 0);

    // Never allocate or touch a too-small scratch buffer on the audio thread. An
    // unexpectedly large host block is passed through dry; the normal preallocated
    // bridge resumes on the next block.
    if (samples > scratchCapacitySamples)
        return;

    copyAdapted (hostMainInput, dryBuffer, samples);

    const auto dryChannels = dryBuffer.getNumChannels();
    const auto latency = juce::jlimit (0, maxCompensatedLatencySamples - 1, child->getLatencySamples());
    dryDelay.setDelay (static_cast<float> (latency));
    for (int sample = 0; sample < samples; ++sample)
        for (int channel = 0; channel < dryChannels; ++channel)
        {
            dryDelay.pushSample (channel, dryBuffer.getSample (channel, sample));
            dryBuffer.setSample (channel, sample, dryDelay.popSample (channel));
        }

    const auto requiredChildChannels = juce::jmax (1, juce::jmax (child->getTotalNumInputChannels(),
                                                                   child->getTotalNumOutputChannels()));
    if (requiredChildChannels != childBufferChannels)
    {
        copyAdapted (dryBuffer, hostMainOutput, samples);
        return;
    }

    childBuffer.clear (0, samples);
    auto childMainInput = child->getBusBuffer (childBuffer, true, 0);
    copyAdapted (hostMainInput, childMainInput, samples);

    if (capabilities.sidechainInputBus >= 1 && getBusCount (true) > 1)
    {
        auto hostSidechain = getBusBuffer (buffer, true, 1);
        auto childSidechain = child->getBusBuffer (childBuffer, true, capabilities.sidechainInputBus);
        copyAdapted (hostSidechain, childSidechain, samples);
    }

    child->processBlock (childBuffer, midi);

    auto childMainOutput = child->getBusBuffer (childBuffer, false, 0);
    copyAdapted (childMainOutput, hostMainOutput, samples);

    for (int sample = 0; sample < samples; ++sample)
    {
        const auto wet = wetMix.getNextValue();
        const auto dry = 1.0f - wet;
        for (int channel = 0; channel < hostMainOutput.getNumChannels(); ++channel)
            hostMainOutput.setSample (channel, sample,
                                      hostMainOutput.getSample (channel, sample) * wet
                                      + dryBuffer.getSample (juce::jmin (channel, dryChannels - 1), sample) * dry);
    }
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
