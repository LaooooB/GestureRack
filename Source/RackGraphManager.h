#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include <cstdint>
#include "PluginSlot.h"

namespace gr
{
class RackGraphManager final
{
public:
    static constexpr int slotCount = 9;
    static constexpr int maxRackLatencySamples =
        GestureBypassWrapper::maxCompensatedLatencySamples * slotCount;

    using SlotArray = std::array<std::unique_ptr<PluginSlot>, slotCount>;

    RackGraphManager (juce::AudioProcessorGraph& graphToUse, SlotArray& slotsToUse) noexcept;

    void initialise (int numAudioChannels);
    void clear();

    juce::AudioProcessorGraph::Node::Ptr installSlotProcessor (
        int slotIndex,
        std::unique_ptr<juce::AudioProcessor> processor,
        int numAudioChannels);

    void removeSlotProcessor (int slotIndex, int numAudioChannels);
    void removeAllSlotProcessors (int numAudioChannels);
    void rebuildSerialConnections (int numAudioChannels);

    int getTotalLatencySamples() const noexcept;
    double getTotalTailLengthSeconds() const noexcept;

private:
    bool isValidSlotIndex (int slotIndex) const noexcept
    {
        return slotIndex >= 0 && slotIndex < slotCount;
    }

    juce::AudioProcessorGraph& graph;
    SlotArray& slots;
    juce::AudioProcessorGraph::Node::Ptr inputNode;
    juce::AudioProcessorGraph::Node::Ptr outputNode;
    juce::AudioProcessorGraph::Node::Ptr midiInputNode;
    juce::AudioProcessorGraph::Node::Ptr midiOutputNode;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackGraphManager)
};
}
