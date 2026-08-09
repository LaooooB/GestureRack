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
    if (child != nullptr)
        child->releaseResources();
}

int GestureBypassWrapper::getChildLatencySamples() const noexcept
{
    return child != nullptr ? child->getLatencySamples() : 0;
}

void GestureBypassWrapper::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto channels = static_cast<juce::uint32> (juce::jmax (1, getTotalNumOutputChannels()));
    juce::dsp::ProcessSpec spec { sampleRate,
                                 static_cast<juce::uint32> (samplesPerBlock),
                                 channels };

    dryDelay.prepare (spec);
    dryDelay.reset();
    dryBuffer.setSize (getTotalNumOutputChannels(), samplesPerBlock, false, false, true);

    wetMix.reset (sampleRate, 0.015);
    wetMix.setCurrentAndTargetValue (requestedBypass.load() ? 0.0f : 1.0f);
    lastRequestedBypass = requestedBypass.load();

    if (child != nullptr)
    {
        child->setPlayHead (getPlayHead());
        child->setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);
        child->prepareToPlay (sampleRate, samplesPerBlock);
    }
}

void GestureBypassWrapper::releaseResources()
{
    if (child != nullptr)
        child->releaseResources();

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

    dryBuffer.makeCopyOf (buffer, true);

    const auto latency = juce::jlimit (0, maxCompensatedLatencySamples - 1, child->getLatencySamples());
    dryDelay.setDelay (static_cast<float> (latency));

    const auto channels = juce::jmin (buffer.getNumChannels(), dryBuffer.getNumChannels());
    const auto samples = buffer.getNumSamples();

    for (int sample = 0; sample < samples; ++sample)
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            dryDelay.pushSample (channel, dryBuffer.getSample (channel, sample));
            dryBuffer.setSample (channel, sample, dryDelay.popSample (channel));
        }
    }

    child->processBlock (buffer, midi);

    for (int sample = 0; sample < samples; ++sample)
    {
        const auto wet = wetMix.getNextValue();
        const auto dry = 1.0f - wet;

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto wetSample = buffer.getSample (channel, sample);
            const auto drySample = dryBuffer.getSample (channel, sample);
            buffer.setSample (channel, sample, wetSample * wet + drySample * dry);
        }
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
