#include "RackGraphManager.h"

namespace gr
{
RackGraphManager::RackGraphManager (juce::AudioProcessorGraph& graphToUse,
                                    SlotList& slotsToUse) noexcept
    : graph (graphToUse), slots (slotsToUse)
{
}

void RackGraphManager::initialise (int numAudioChannels)
{
    graph.clear();

    for (auto& slot : slots)
        if (slot != nullptr)
            slot->clearGraphNode();

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

    rebuildSerialConnections (numAudioChannels);
}

void RackGraphManager::clear()
{
    graph.clear();
    inputNode = nullptr;
    outputNode = nullptr;
    midiInputNode = nullptr;
    midiOutputNode = nullptr;

    for (auto& slot : slots)
        if (slot != nullptr)
            slot->clearGraphNode();
}

juce::AudioProcessorGraph::Node::Ptr RackGraphManager::installSlotProcessor (
    int slotIndex,
    std::unique_ptr<juce::AudioProcessor> processor,
    int numAudioChannels)
{
    if (! isValidSlotIndex (slotIndex) || processor == nullptr)
        return nullptr;

    auto& slot = *slots[static_cast<size_t> (slotIndex)];

    if (auto oldNode = slot.getGraphNode(); oldNode != nullptr)
    {
        graph.removeNode (oldNode->nodeID, juce::AudioProcessorGraph::UpdateKind::none);
        slot.clearGraphNode();
    }

    auto newNode = graph.addNode (std::move (processor), std::nullopt,
                                  juce::AudioProcessorGraph::UpdateKind::none);
    slot.setGraphNode (newNode);
    rebuildSerialConnections (numAudioChannels);
    return newNode;
}

void RackGraphManager::removeSlotProcessor (int slotIndex, int numAudioChannels)
{
    if (! isValidSlotIndex (slotIndex))
        return;

    auto& slot = *slots[static_cast<size_t> (slotIndex)];
    if (auto node = slot.getGraphNode(); node != nullptr)
        graph.removeNode (node->nodeID, juce::AudioProcessorGraph::UpdateKind::none);

    slot.clearGraphNode();
    rebuildSerialConnections (numAudioChannels);
}

void RackGraphManager::removeAllSlotProcessors (int numAudioChannels)
{
    for (auto& slot : slots)
    {
        if (slot == nullptr)
            continue;

        if (auto node = slot->getGraphNode(); node != nullptr)
            graph.removeNode (node->nodeID, juce::AudioProcessorGraph::UpdateKind::none);

        slot->clearGraphNode();
    }

    rebuildSerialConnections (numAudioChannels);
}

void RackGraphManager::rebuildSerialConnections (int numAudioChannels)
{
    if (inputNode == nullptr || outputNode == nullptr)
        return;

    graph.disconnectNode (inputNode->nodeID, juce::AudioProcessorGraph::UpdateKind::none);
    graph.disconnectNode (outputNode->nodeID, juce::AudioProcessorGraph::UpdateKind::none);
    if (midiInputNode != nullptr)
        graph.disconnectNode (midiInputNode->nodeID, juce::AudioProcessorGraph::UpdateKind::none);
    if (midiOutputNode != nullptr)
        graph.disconnectNode (midiOutputNode->nodeID, juce::AudioProcessorGraph::UpdateKind::none);

    for (const auto& slot : slots)
        if (slot != nullptr)
            if (auto node = slot->getGraphNode(); node != nullptr)
                graph.disconnectNode (node->nodeID, juce::AudioProcessorGraph::UpdateKind::none);

    const auto channels = juce::jmax (0, numAudioChannels);

    for (int channel = 0; channel < channels; ++channel)
    {
        auto sourceNodeId = inputNode->nodeID;

        for (const auto& slot : slots)
        {
            if (slot == nullptr)
                continue;

            const auto node = slot->getGraphNode();
            if (node == nullptr)
                continue;

            graph.addConnection ({ { sourceNodeId, channel }, { node->nodeID, channel } },
                                 juce::AudioProcessorGraph::UpdateKind::none);
            sourceNodeId = node->nodeID;
        }

        graph.addConnection ({ { sourceNodeId, channel }, { outputNode->nodeID, channel } },
                             juce::AudioProcessorGraph::UpdateKind::none);
    }

    const auto totalGraphInputChannels =
        inputNode->getProcessor()->getTotalNumOutputChannels();
    const auto availableSidechainChannels =
        juce::jmax (0, totalGraphInputChannels - channels);

    if (availableSidechainChannels > 0)
    {
        for (const auto& slot : slots)
        {
            if (slot == nullptr)
                continue;

            const auto node = slot->getGraphNode();
            if (node == nullptr)
                continue;

            auto* processor = node->getProcessor();
            if (processor == nullptr || processor->getBusCount (true) < 2)
                continue;

            auto* sidechainBus = processor->getBus (true, 1);
            if (sidechainBus == nullptr || ! sidechainBus->isEnabled())
                continue;

            const auto sidechainChannels =
                juce::jmin (availableSidechainChannels,
                            processor->getChannelCountOfBus (true, 1));

            for (int channel = 0; channel < sidechainChannels; ++channel)
            {
                const auto destinationChannel =
                    processor->getChannelIndexInProcessBlockBuffer (true, 1, channel);
                if (destinationChannel < 0)
                    continue;

                graph.addConnection (
                    { { inputNode->nodeID, channels + channel },
                      { node->nodeID, destinationChannel } },
                    juce::AudioProcessorGraph::UpdateKind::none);
            }
        }
    }

    if (midiInputNode != nullptr && midiOutputNode != nullptr)
    {
        constexpr auto midiChannel = juce::AudioProcessorGraph::midiChannelIndex;

        graph.addConnection (
            { { midiInputNode->nodeID, midiChannel },
              { midiOutputNode->nodeID, midiChannel } },
            juce::AudioProcessorGraph::UpdateKind::none);

        for (const auto& slot : slots)
        {
            if (slot == nullptr)
                continue;

            const auto node = slot->getGraphNode();
            if (node == nullptr)
                continue;

            auto* processor = node->getProcessor();
            if (processor != nullptr && processor->acceptsMidi())
                graph.addConnection (
                    { { midiInputNode->nodeID, midiChannel },
                      { node->nodeID, midiChannel } },
                    juce::AudioProcessorGraph::UpdateKind::none);
        }
    }

    graph.rebuild();
}

int RackGraphManager::getTotalLatencySamples() const noexcept
{
    int64_t total = 0;

    for (const auto& slot : slots)
    {
        if (slot == nullptr)
            continue;

        if (auto* wrapper = slot->getWrapper())
            total += juce::jlimit (
                0,
                GestureBypassWrapper::maxCompensatedLatencySamples - 1,
                wrapper->getChildLatencySamples());
    }

    return static_cast<int> (
        juce::jlimit<int64_t> (0, maxRackLatencySamples - 1, total));
}

double RackGraphManager::getTotalTailLengthSeconds() const noexcept
{
    double total = 0.0;

    for (const auto& slot : slots)
    {
        if (slot == nullptr)
            continue;

        if (auto* child = slot->getChild())
            total += juce::jmax (0.0, child->getTailLengthSeconds());
    }

    return total;
}
}
