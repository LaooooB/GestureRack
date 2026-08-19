#include "RackGraphManager.h"

namespace gr
{
RackGraphManager::RackGraphManager (juce::AudioProcessorGraph& graphToUse,
                                    SlotList& slotsToUse) noexcept
    : graph (graphToUse), slots (slotsToUse)
{
}

void RackGraphManager::initialise (const RackHostBusLayout& hostLayout)
{
    clear();

    inputNode = graph.addNode (
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    outputNode = graph.addNode (
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    midiInputNode = graph.addNode (
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
            juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));
    midiOutputNode = graph.addNode (
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor> (
            juce::AudioProcessorGraph::AudioGraphIOProcessor::midiOutputNode));

    rebuildRouting (hostLayout);
}

void RackGraphManager::clear()
{
    for (auto& slot : slots)
        if (slot != nullptr)
            slot->clearGraphNode();

    graph.clear();
    inputNode = nullptr;
    outputNode = nullptr;
    midiInputNode = nullptr;
    midiOutputNode = nullptr;
}

std::vector<int> RackGraphManager::getBusChannels (juce::AudioProcessor& processor,
                                                   bool isInput,
                                                   int busIndex)
{
    std::vector<int> result;
    if (busIndex < 0 || busIndex >= processor.getBusCount (isInput))
        return result;

    auto* bus = processor.getBus (isInput, busIndex);
    if (bus == nullptr || ! bus->isEnabled())
        return result;

    const auto count = processor.getChannelCountOfBus (isInput, busIndex);
    result.reserve (static_cast<size_t> (juce::jmax (0, count)));
    for (int channel = 0; channel < count; ++channel)
    {
        const auto flat = processor.getChannelIndexInProcessBlockBuffer (isInput, busIndex, channel);
        if (flat >= 0)
            result.push_back (flat);
    }
    return result;
}

void RackGraphManager::connectMapped (const std::vector<Endpoint>& sources,
                                      juce::AudioProcessorGraph::NodeID destinationNode,
                                      const std::vector<int>& destinationChannels)
{
    if (sources.empty() || destinationChannels.empty())
        return;

    for (size_t destination = 0; destination < destinationChannels.size(); ++destination)
    {
        const auto sourceIndex = juce::jmin (static_cast<int> (destination),
                                             static_cast<int> (sources.size()) - 1);
        graph.addConnection (
            { sources[static_cast<size_t> (sourceIndex)],
              { destinationNode, destinationChannels[destination] } },
            juce::AudioProcessorGraph::UpdateKind::none);
    }
}

void RackGraphManager::disconnectAllNodes()
{
    for (const auto& node : graph.getNodes())
        if (node != nullptr)
            graph.disconnectNode (node->nodeID, juce::AudioProcessorGraph::UpdateKind::none);
}

juce::AudioProcessorGraph::Node::Ptr RackGraphManager::installSlotProcessor (
    int slotIndex,
    std::unique_ptr<juce::AudioPluginInstance> processor,
    const RackHostBusLayout& hostLayout)
{
    if (! isValidSlotIndex (slotIndex) || processor == nullptr)
        return nullptr;

    auto& slot = *slots[static_cast<size_t> (slotIndex)];
    if (auto oldNode = slot.getGraphNode(); oldNode != nullptr)
    {
        const auto oldId = oldNode->nodeID;
        slot.clearGraphNode();
        graph.removeNode (oldId, juce::AudioProcessorGraph::UpdateKind::none);
    }

    auto newNode = graph.addNode (std::move (processor), std::nullopt,
                                  juce::AudioProcessorGraph::UpdateKind::none);
    if (newNode == nullptr)
        return nullptr;

    slot.setGraphNode (newNode);
    rebuildRouting (hostLayout);
    return newNode;
}

void RackGraphManager::removeSlotProcessor (int slotIndex,
                                            const RackHostBusLayout& hostLayout)
{
    if (! isValidSlotIndex (slotIndex))
        return;

    auto& slot = *slots[static_cast<size_t> (slotIndex)];
    if (auto node = slot.getGraphNode(); node != nullptr)
    {
        const auto nodeId = node->nodeID;
        slot.clearGraphNode();
        graph.removeNode (nodeId, juce::AudioProcessorGraph::UpdateKind::none);
    }

    rebuildRouting (hostLayout);
}

void RackGraphManager::removeAllSlotProcessors (const RackHostBusLayout& hostLayout)
{
    for (auto& slot : slots)
    {
        if (slot == nullptr)
            continue;

        if (auto node = slot->getGraphNode(); node != nullptr)
        {
            const auto nodeId = node->nodeID;
            slot->clearGraphNode();
            graph.removeNode (nodeId, juce::AudioProcessorGraph::UpdateKind::none);
        }
    }

    rebuildRouting (hostLayout);
}

void RackGraphManager::rebuildRouting (const RackHostBusLayout& hostLayout)
{
    if (inputNode == nullptr || outputNode == nullptr)
        return;

    disconnectAllNodes();

    std::vector<Endpoint> mainSources;
    for (int channel = 0; channel < hostLayout.mainInput.size(); ++channel)
        mainSources.push_back ({ inputNode->nodeID, channel });

    const auto sidechainOffset = hostLayout.inputOffsetForBus (1);
    std::vector<Endpoint> sidechainSources;
    for (int channel = 0; channel < hostLayout.sidechainInput.size(); ++channel)
        sidechainSources.push_back ({ inputNode->nodeID, sidechainOffset + channel });

    constexpr auto midiChannel = juce::AudioProcessorGraph::midiChannelIndex;
    Endpoint currentMidiSource {};
    bool hasMidiSource = midiInputNode != nullptr;
    if (hasMidiSource)
        currentMidiSource = { midiInputNode->nodeID, midiChannel };

    for (const auto& slot : slots)
    {
        if (slot == nullptr)
            continue;

        const auto node = slot->getGraphNode();
        auto* child = slot->getChild();
        if (node == nullptr || child == nullptr)
            continue;

        const auto mainInputs = getBusChannels (*child, true, 0);
        const auto mainOutputs = getBusChannels (*child, false, 0);
        if (! mainInputs.empty() && ! mainSources.empty())
            connectMapped (mainSources, node->nodeID, mainInputs);

        if (! mainOutputs.empty())
        {
            mainSources.clear();
            mainSources.reserve (mainOutputs.size());
            for (const auto channel : mainOutputs)
                mainSources.push_back ({ node->nodeID, channel });
        }

        if (! sidechainSources.empty() && child->getBusCount (true) > 1)
        {
            const auto sidechainInputs = getBusChannels (*child, true, 1);
            if (! sidechainInputs.empty())
                connectMapped (sidechainSources, node->nodeID, sidechainInputs);
        }

        const auto auxCount = juce::jmin (
            juce::jmax (0, child->getBusCount (false) - 1),
            static_cast<int> (hostLayout.auxOutputs.size()));
        for (int aux = 0; aux < auxCount; ++aux)
        {
            const auto& rackAuxLayout = hostLayout.auxOutputs[static_cast<size_t> (aux)];
            if (rackAuxLayout.isDisabled())
                continue;

            const auto childAux = getBusChannels (*child, false, aux + 1);
            if (childAux.empty())
                continue;

            std::vector<Endpoint> auxSources;
            auxSources.reserve (childAux.size());
            for (const auto channel : childAux)
                auxSources.push_back ({ node->nodeID, channel });

            std::vector<int> rackAuxDestinations;
            const auto rackOffset = hostLayout.outputOffsetForBus (aux + 1);
            for (int channel = 0; channel < rackAuxLayout.size(); ++channel)
                rackAuxDestinations.push_back (rackOffset + channel);
            connectMapped (auxSources, outputNode->nodeID, rackAuxDestinations);
        }

        if (hasMidiSource && child->acceptsMidi())
            graph.addConnection (
                { currentMidiSource, { node->nodeID, midiChannel } },
                juce::AudioProcessorGraph::UpdateKind::none);

        if (child->producesMidi())
        {
            currentMidiSource = { node->nodeID, midiChannel };
            hasMidiSource = true;
        }
    }

    if (midiOutputNode != nullptr && hasMidiSource)
        graph.addConnection (
            { currentMidiSource, { midiOutputNode->nodeID, midiChannel } },
            juce::AudioProcessorGraph::UpdateKind::none);

    std::vector<int> mainDestinations;
    for (int channel = 0; channel < hostLayout.mainOutput.size(); ++channel)
        mainDestinations.push_back (channel);
    connectMapped (mainSources, outputNode->nodeID, mainDestinations);

    graph.removeIllegalConnections (juce::AudioProcessorGraph::UpdateKind::none);
    graph.rebuild();
}

bool RackGraphManager::serviceDynamicChanges (const RackHostBusLayout& hostLayout)
{
    bool changed = false;
    for (auto& slot : slots)
        if (slot != nullptr && slot->hasPlugin())
            changed = slot->pollTopologyChanged() || changed;

    if (changed)
        rebuildRouting (hostLayout);

    return changed;
}

int RackGraphManager::getTotalLatencySamples() const noexcept
{
    return juce::jlimit (0, maxRackLatencySamples - 1,
                         juce::jmax (0, graph.getLatencySamples()));
}

double RackGraphManager::getTotalTailLengthSeconds() const noexcept
{
    double total = 0.0;
    for (const auto& slot : slots)
        if (slot != nullptr)
            if (auto* child = slot->getChild())
                total += juce::jmax (0.0, child->getTailLengthSeconds());
    return total;
}
}
