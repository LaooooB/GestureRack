#include "GestureBypassWrapper.h"

namespace gr
{
GestureBypassWrapper::GestureBypassWrapper (std::unique_ptr<juce::AudioPluginInstance> childToOwn,
                                            const juce::AudioChannelSet& inputSet,
                                            const juce::AudioChannelSet& outputSet,
                                            std::atomic<bool>& requestedBypassState)
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", inputSet, true)
                                .withOutput ("Output", outputSet, true)),
      child (std::move (childToOwn)),
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

    const auto channels = static_cast<juce::uint32> (juce::jmax (1, getTotalNumOutputChannels()));
    juce::dsp::ProcessSpec spec { sampleRate,
                                 static_cast<juce::uint32> (juce::jmax (1, samplesPerBlock)),
                                 channels };
    dryDelay.prepare (spec);
    dryDelay.reset();

    scratchCapacitySamples = juce::jmax (minimumRealtimeScratchSamples, samplesPerBlock);
    dryBuffer.setSize (getTotalNumOutputChannels(), scratchCapacitySamples, false, false, true);

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
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
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
    const auto channels = juce::jmin (buffer.getNumChannels(), dryBuffer.getNumChannels());

    if (samples > scratchCapacitySamples || channels <= 0)
    {
        child->processBlock (buffer, midi);
        return;
    }

    for (int channel = 0; channel < channels; ++channel)
        dryBuffer.copyFrom (channel, 0, buffer, channel, 0, samples);

    const auto latency = juce::jlimit (0, maxCompensatedLatencySamples - 1, child->getLatencySamples());
    dryDelay.setDelay (static_cast<float> (latency));
    for (int sample = 0; sample < samples; ++sample)
        for (int channel = 0; channel < channels; ++channel)
        {
            dryDelay.pushSample (channel, dryBuffer.getSample (channel, sample));
            dryBuffer.setSample (channel, sample, dryDelay.popSample (channel));
        }

    child->processBlock (buffer, midi);

    for (int sample = 0; sample < samples; ++sample)
    {
        const auto wet = wetMix.getNextValue();
        const auto dry = 1.0f - wet;
        for (int channel = 0; channel < channels; ++channel)
            buffer.setSample (channel, sample,
                              buffer.getSample (channel, sample) * wet
                              + dryBuffer.getSample (channel, sample) * dry);
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
