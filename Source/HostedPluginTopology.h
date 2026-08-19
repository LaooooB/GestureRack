#pragma once

#include <JuceHeader.h>
#include <vector>

namespace gr
{
struct HostedBusDescriptor
{
    bool isInput = false;
    int busIndex = 0;
    bool enabled = false;
    int channels = 0;
    int flatChannelStart = -1;
    juce::String name;
    juce::AudioChannelSet layout;

    bool operator== (const HostedBusDescriptor& other) const noexcept
    {
        return isInput == other.isInput
            && busIndex == other.busIndex
            && enabled == other.enabled
            && channels == other.channels
            && flatChannelStart == other.flatChannelStart
            && name == other.name
            && layout == other.layout;
    }

    bool operator!= (const HostedBusDescriptor& other) const noexcept
    {
        return ! (*this == other);
    }
};

struct HostedPluginTopology
{
    std::vector<HostedBusDescriptor> inputs;
    std::vector<HostedBusDescriptor> outputs;
    int latencySamples = 0;
    bool acceptsMidi = false;
    bool producesMidi = false;
    bool hasEditor = false;

    bool operator== (const HostedPluginTopology& other) const noexcept
    {
        return inputs == other.inputs
            && outputs == other.outputs
            && latencySamples == other.latencySamples
            && acceptsMidi == other.acceptsMidi
            && producesMidi == other.producesMidi
            && hasEditor == other.hasEditor;
    }

    bool operator!= (const HostedPluginTopology& other) const noexcept
    {
        return ! (*this == other);
    }
};

struct RackHostBusLayout
{
    juce::AudioChannelSet mainInput = juce::AudioChannelSet::stereo();
    juce::AudioChannelSet sidechainInput = juce::AudioChannelSet::disabled();
    juce::AudioChannelSet mainOutput = juce::AudioChannelSet::stereo();
    std::vector<juce::AudioChannelSet> auxOutputs;

    int inputOffsetForBus (int busIndex) const noexcept
    {
        if (busIndex <= 0)
            return 0;
        return mainInput.size();
    }

    int outputOffsetForBus (int busIndex) const noexcept
    {
        if (busIndex <= 0)
            return 0;

        int offset = mainOutput.size();
        for (int index = 1; index < busIndex; ++index)
        {
            const auto auxIndex = static_cast<size_t> (index - 1);
            if (auxIndex < auxOutputs.size())
                offset += auxOutputs[auxIndex].size();
        }
        return offset;
    }
};

inline HostedPluginTopology captureHostedPluginTopology (const juce::AudioProcessor& processor)
{
    HostedPluginTopology result;
    result.latencySamples = juce::jmax (0, processor.getLatencySamples());
    result.acceptsMidi = processor.acceptsMidi();
    result.producesMidi = processor.producesMidi();
    result.hasEditor = processor.hasEditor();

    const auto captureDirection = [&processor] (bool isInput,
                                                std::vector<HostedBusDescriptor>& destination)
    {
        const auto busCount = processor.getBusCount (isInput);
        destination.reserve (static_cast<size_t> (juce::jmax (0, busCount)));

        for (int busIndex = 0; busIndex < busCount; ++busIndex)
        {
            HostedBusDescriptor bus;
            bus.isInput = isInput;
            bus.busIndex = busIndex;

            if (auto* processorBus = processor.getBus (isInput, busIndex))
            {
                bus.enabled = processorBus->isEnabled();
                bus.name = processorBus->getName();
                bus.layout = processor.getChannelLayoutOfBus (isInput, busIndex);
                bus.channels = bus.enabled ? processor.getChannelCountOfBus (isInput, busIndex) : 0;
                if (bus.channels > 0)
                    bus.flatChannelStart = processor.getChannelIndexInProcessBlockBuffer (isInput, busIndex, 0);
            }

            destination.push_back (std::move (bus));
        }
    };

    captureDirection (true, result.inputs);
    captureDirection (false, result.outputs);
    return result;
}

inline bool configureRequiredMainBus (juce::AudioPluginInstance& child,
                                      bool isInput,
                                      const juce::AudioChannelSet& requested,
                                      bool allowDisabled,
                                      juce::String& error)
{
    if (child.getBusCount (isInput) <= 0)
    {
        if (allowDisabled)
            return true;
        error = isInput ? "Hosted plug-in has no main input bus."
                        : "Hosted plug-in has no main output bus.";
        return false;
    }

    auto* bus = child.getBus (isInput, 0);
    if (bus == nullptr)
    {
        error = "Hosted plug-in main bus is unavailable.";
        return false;
    }

    if (allowDisabled && requested.isDisabled())
        return true;

    if (! bus->isEnabled() && ! bus->enable (true))
    {
        if (allowDisabled)
            return true;
        error = "Hosted plug-in refused main bus activation.";
        return false;
    }

    if (child.getChannelLayoutOfBus (isInput, 0) == requested)
        return true;

    if (child.setChannelLayoutOfBus (isInput, 0, requested))
        return true;

    if (allowDisabled && child.getChannelLayoutOfBus (isInput, 0).isDisabled())
        return true;

    error = (isInput ? "Hosted plug-in cannot match rack main input layout: "
                     : "Hosted plug-in cannot match rack main output layout: ")
          + requested.getDescription();
    return false;
}

inline void configureOptionalBus (juce::AudioPluginInstance& child,
                                  bool isInput,
                                  int busIndex,
                                  const juce::AudioChannelSet& requested)
{
    if (requested.isDisabled() || busIndex < 0 || busIndex >= child.getBusCount (isInput))
        return;

    auto* bus = child.getBus (isInput, busIndex);
    if (bus == nullptr)
        return;

    if (! bus->isEnabled() && ! bus->enable (true))
        return;

    if (child.getChannelLayoutOfBus (isInput, busIndex) != requested)
        child.setChannelLayoutOfBus (isInput, busIndex, requested);
}

inline bool configureHostedPluginForRack (juce::AudioPluginInstance& child,
                                          const RackHostBusLayout& hostLayout,
                                          bool allowZeroMainInput,
                                          juce::String& error)
{
    error.clear();

    if (! configureRequiredMainBus (child, true, hostLayout.mainInput,
                                    allowZeroMainInput, error))
        return false;

    if (! configureRequiredMainBus (child, false, hostLayout.mainOutput,
                                    false, error))
        return false;

    // A host must not globally disable non-main buses. Activate only buses that
    // have a real rack endpoint, and otherwise preserve the plug-in's own state.
    if (! hostLayout.sidechainInput.isDisabled() && child.getBusCount (true) > 1)
        configureOptionalBus (child, true, 1, hostLayout.sidechainInput);

    const auto childAuxCount = juce::jmax (0, child.getBusCount (false) - 1);
    const auto routedAuxCount = juce::jmin (childAuxCount,
                                           static_cast<int> (hostLayout.auxOutputs.size()));
    for (int aux = 0; aux < routedAuxCount; ++aux)
        if (! hostLayout.auxOutputs[static_cast<size_t> (aux)].isDisabled())
            configureOptionalBus (child, false, aux + 1,
                                  hostLayout.auxOutputs[static_cast<size_t> (aux)]);

    return true;
}
}
