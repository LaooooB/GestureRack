#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>
#include <cstdint>
#include "PluginSlot.h"
#include "HostedPluginTopology.h"

namespace gr
{
class RackGraphManager final
{
public:
    static constexpr int maxRackLatencySamples = 5242880;
    using SlotList = std::vector<std::unique_ptr<PluginSlot>>;

    RackGraphManager (juce::AudioProcessorGraph& graphToUse, SlotList& slotsToUse) noexcept;

    void initialise (const RackHostBusLayout& hostLayout);
    void clear();

    juce::AudioProcessorGraph::Node::Ptr installSlotProcessor (
        int slotIndex,
        std::unique_ptr<juce::AudioPluginInstance> processor,
        const RackHostBusLayout& hostLayout);

    void removeSlotProcessor (int slotIndex, const RackHostBusLayout& hostLayout);
    void removeAllSlotProcessors (const RackHostBusLayout& hostLayout);
    void rebuildRouting (const RackHostBusLayout& hostLayout);
    bool serviceDynamicChanges (const RackHostBusLayout& hostLayout);

    int getTotalLatencySamples() const noexcept;
    double getTotalTailLengthSeconds() const noexcept;

private:
    using Endpoint = juce::AudioProcessorGraph::NodeAndChannel;

    bool isValidSlotIndex (int slotIndex) const noexcept
    {
        return slotIndex >= 0
            && slotIndex < static_cast<int> (slots.size())
            && slots[static_cast<size_t> (slotIndex)] != nullptr;
    }

    static std::vector<int> getBusChannels (juce::AudioProcessor& processor,
                                            bool isInput,
                                            int busIndex);
    void connectMapped (const std::vector<Endpoint>& sources,
                        juce::AudioProcessorGraph::NodeID destinationNode,
                        const std::vector<int>& destinationChannels);
    void disconnectAllNodes();

    juce::AudioProcessorGraph& graph;
    SlotList& slots;
    juce::AudioProcessorGraph::Node::Ptr inputNode;
    juce::AudioProcessorGraph::Node::Ptr outputNode;
    juce::AudioProcessorGraph::Node::Ptr midiInputNode;
    juce::AudioProcessorGraph::Node::Ptr midiOutputNode;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackGraphManager)
};
}
