#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>
#include <cstdint>
#include "PluginSlot.h"

namespace gr
{
class RackGraphManager final
{
public:
    static constexpr int maxRackLatencySamples =
        GestureBypassWrapper::maxCompensatedLatencySamples * 10;

    using SlotList = std::vector<std::unique_ptr<PluginSlot>>;

    RackGraphManager (juce::AudioProcessorGraph& graphToUse, SlotList& slotsToUse) noexcept;

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
        return slotIndex >= 0
            && slotIndex < static_cast<int> (slots.size())
            && slots[static_cast<size_t> (slotIndex)] != nullptr;
    }

    juce::AudioProcessorGraph& graph;
    SlotList& slots;
    juce::AudioProcessorGraph::Node::Ptr inputNode;
    juce::AudioProcessorGraph::Node::Ptr outputNode;
    juce::AudioProcessorGraph::Node::Ptr midiInputNode;
    juce::AudioProcessorGraph::Node::Ptr midiOutputNode;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackGraphManager)
};
}
