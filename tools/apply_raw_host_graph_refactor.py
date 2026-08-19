from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def write(path: str, text: str) -> None:
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding='utf-8', newline='\n')

def replace_once(path: str, old: str, new: str) -> None:
    p = ROOT / path
    text = p.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{path}: expected exactly one match, got {count}')
    p.write_text(text.replace(old, new, 1), encoding='utf-8', newline='\n')

def regex_once(path: str, pattern: str, replacement: str, flags=re.S) -> None:
    p = ROOT / path
    text = p.read_text(encoding='utf-8')
    next_text, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise RuntimeError(f'{path}: regex expected exactly one match, got {count}: {pattern[:80]}')
    p.write_text(next_text, encoding='utf-8', newline='\n')

HOSTED_TOPOLOGY_H = r'''#pragma once

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
'''

PLUGIN_SLOT_H = r'''#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <optional>
#include <vector>
#include "HostedPluginTopology.h"
#include "GestureBinding.h"

namespace gr
{
class PluginSlot final : private juce::AudioProcessorListener
{
public:
    explicit PluginSlot (int indexToUse) noexcept;
    ~PluginSlot() override;

    int getIndex() const noexcept { return slotIndex; }
    void setIndexForReorder (int newIndex);

    uint64_t getStableId() const noexcept { return stableId; }
    uint64_t beginLoad() noexcept
    {
        return loadGeneration.fetch_add (1, std::memory_order_relaxed) + 1;
    }
    void invalidatePendingLoads() noexcept
    {
        loadGeneration.fetch_add (1, std::memory_order_relaxed);
    }
    bool isLoadGenerationCurrent (uint64_t generation) const noexcept
    {
        return loadGeneration.load (std::memory_order_relaxed) == generation;
    }

    bool hasPlugin() const noexcept { return getChild() != nullptr && description.has_value(); }
    bool isBypassed() const noexcept { return requestedBypass.load (std::memory_order_relaxed); }
    void setBypassed (bool shouldBypass) noexcept;

    juce::String getPluginName() const;
    const std::optional<juce::PluginDescription>& getDescription() const noexcept { return description; }
    void setDescription (const juce::PluginDescription& newDescription) { description = newDescription; }
    void clearDescription() { description.reset(); }

    void setGraphNode (juce::AudioProcessorGraph::Node::Ptr newNode);
    void clearGraphNode();
    juce::AudioProcessorGraph::Node::Ptr getGraphNode() const noexcept { return graphNode; }

    juce::AudioPluginInstance* getChild() const noexcept;
    juce::AudioProcessorEditor* getOrCreateEmbeddedEditor();
    void releaseEmbeddedEditor();

    bool pollTopologyChanged();
    const HostedPluginTopology& getTopologySnapshot() const noexcept { return topologySnapshot; }

    juce::String getLastError() const { return lastError; }
    void setLastError (juce::String error) { lastError = std::move (error); }
    void clearLastError() { lastError.clear(); }

    std::vector<GestureBinding> getMappings() const;
    void addMapping (const GestureBinding& binding);
    bool updateMapping (const GestureBinding& binding);
    bool removeMapping (const juce::Uuid& id);
    void clearChildParameterMappings();
    void clearAllMappings();
    int getMappingCount (ControlGesture gesture) const;

private:
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override;
    void detachFromCurrentProcessor();

    static std::atomic<uint64_t> nextStableId;

    int slotIndex = 0;
    const uint64_t stableId;
    std::atomic<uint64_t> loadGeneration { 0 };
    std::optional<juce::PluginDescription> description;
    juce::AudioProcessorGraph::Node::Ptr graphNode;
    std::atomic<bool> requestedBypass { false };
    std::atomic<bool> topologyDirty { true };
    HostedPluginTopology topologySnapshot;
    std::unique_ptr<juce::AudioProcessorEditor> embeddedEditor;
    juce::String lastError;

    mutable juce::SpinLock mappingsLock;
    std::vector<GestureBinding> mappings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginSlot)
};
}
'''

PLUGIN_SLOT_CPP = r'''#include "PluginSlot.h"
#include <algorithm>

namespace gr
{
std::atomic<uint64_t> PluginSlot::nextStableId { 1 };

namespace
{
bool sameParameterTarget (const GestureBinding& a, const GestureBinding& b)
{
    if (a.targetType != MappingTargetType::childParameter
        || b.targetType != MappingTargetType::childParameter)
        return false;

    if (a.parameterStableId.isNotEmpty() && b.parameterStableId.isNotEmpty())
        return a.parameterStableId == b.parameterStableId;

    return a.parameterIndexFallback >= 0
        && a.parameterIndexFallback == b.parameterIndexFallback
        && a.parameterName == b.parameterName;
}
}

PluginSlot::PluginSlot (int indexToUse) noexcept
    : slotIndex (indexToUse),
      stableId (nextStableId.fetch_add (1, std::memory_order_relaxed))
{
}

PluginSlot::~PluginSlot()
{
    detachFromCurrentProcessor();
}

void PluginSlot::setIndexForReorder (int newIndex)
{
    const juce::SpinLock::ScopedLockType lock (mappingsLock);
    slotIndex = newIndex;
    for (auto& binding : mappings)
        binding.slotIndex = newIndex;
}

juce::String PluginSlot::getPluginName() const
{
    return description.has_value() ? description->name : "EMPTY";
}

juce::AudioPluginInstance* PluginSlot::getChild() const noexcept
{
    if (graphNode == nullptr)
        return nullptr;

    return dynamic_cast<juce::AudioPluginInstance*> (graphNode->getProcessor());
}

void PluginSlot::detachFromCurrentProcessor()
{
    releaseEmbeddedEditor();
    if (auto* child = getChild())
        child->removeListener (this);
}

void PluginSlot::setGraphNode (juce::AudioProcessorGraph::Node::Ptr newNode)
{
    detachFromCurrentProcessor();
    graphNode = std::move (newNode);
    topologySnapshot = {};
    topologyDirty.store (true, std::memory_order_release);

    if (auto* child = getChild())
    {
        child->addListener (this);
        topologySnapshot = captureHostedPluginTopology (*child);
        topologyDirty.store (false, std::memory_order_release);
        graphNode->setBypassed (requestedBypass.load (std::memory_order_relaxed));
    }
}

void PluginSlot::clearGraphNode()
{
    detachFromCurrentProcessor();
    graphNode = nullptr;
    topologySnapshot = {};
    topologyDirty.store (true, std::memory_order_release);
}

void PluginSlot::setBypassed (bool shouldBypass) noexcept
{
    requestedBypass.store (shouldBypass, std::memory_order_relaxed);
    if (graphNode != nullptr)
        graphNode->setBypassed (shouldBypass);
}

juce::AudioProcessorEditor* PluginSlot::getOrCreateEmbeddedEditor()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    auto* child = getChild();
    if (child == nullptr || ! child->hasEditor())
        return nullptr;

    if (embeddedEditor == nullptr)
        embeddedEditor.reset (child->createEditorIfNeeded());

    return embeddedEditor.get();
}

void PluginSlot::releaseEmbeddedEditor()
{
    if (embeddedEditor != nullptr)
    {
        jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
        embeddedEditor.reset();
    }
}

void PluginSlot::audioProcessorChanged (
    juce::AudioProcessor*,
    const juce::AudioProcessorListener::ChangeDetails&)
{
    // VST3 restartComponent notifications are surfaced through AudioProcessor
    // change notifications, but not every plug-in reports every I/O mutation.
    // The message-thread poll below is therefore authoritative; this flag only
    // makes notified changes cheap to detect.
    topologyDirty.store (true, std::memory_order_release);
}

bool PluginSlot::pollTopologyChanged()
{
    auto* child = getChild();
    if (child == nullptr)
        return false;

    const auto current = captureHostedPluginTopology (*child);
    const auto dirty = topologyDirty.exchange (false, std::memory_order_acq_rel);
    if (dirty || current != topologySnapshot)
    {
        const auto changed = current != topologySnapshot;
        topologySnapshot = current;
        return changed;
    }

    return false;
}

std::vector<GestureBinding> PluginSlot::getMappings() const
{
    const juce::SpinLock::ScopedLockType lock (mappingsLock);
    return mappings;
}

void PluginSlot::addMapping (const GestureBinding& binding)
{
    const juce::SpinLock::ScopedLockType lock (mappingsLock);
    std::erase_if (mappings, [&binding] (const GestureBinding& existing)
    {
        return existing.id == binding.id || sameParameterTarget (existing, binding);
    });
    mappings.push_back (binding);
}

bool PluginSlot::updateMapping (const GestureBinding& binding)
{
    const juce::SpinLock::ScopedLockType lock (mappingsLock);
    const auto original = std::find_if (mappings.begin(), mappings.end(), [&binding] (const GestureBinding& existing)
    {
        return existing.id == binding.id;
    });
    if (original == mappings.end())
        return false;

    std::erase_if (mappings, [&binding] (const GestureBinding& existing)
    {
        return existing.id != binding.id && sameParameterTarget (existing, binding);
    });

    const auto target = std::find_if (mappings.begin(), mappings.end(), [&binding] (const GestureBinding& existing)
    {
        return existing.id == binding.id;
    });
    if (target == mappings.end())
        return false;

    *target = binding;
    return true;
}

bool PluginSlot::removeMapping (const juce::Uuid& id)
{
    const juce::SpinLock::ScopedLockType lock (mappingsLock);
    const auto before = mappings.size();
    std::erase_if (mappings, [&id] (const GestureBinding& binding) { return binding.id == id; });
    return mappings.size() != before;
}

void PluginSlot::clearChildParameterMappings()
{
    const juce::SpinLock::ScopedLockType lock (mappingsLock);
    std::erase_if (mappings, [] (const GestureBinding& binding)
    {
        return binding.targetType == MappingTargetType::childParameter;
    });
}

void PluginSlot::clearAllMappings()
{
    const juce::SpinLock::ScopedLockType lock (mappingsLock);
    mappings.clear();
}

int PluginSlot::getMappingCount (ControlGesture gesture) const
{
    const juce::SpinLock::ScopedLockType lock (mappingsLock);
    return static_cast<int> (std::count_if (mappings.begin(), mappings.end(), [gesture] (const GestureBinding& binding)
    {
        return binding.sourceGesture == gesture;
    }));
}
}
'''

RACK_GRAPH_H = r'''#pragma once

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
'''

RACK_GRAPH_CPP = r'''#include "RackGraphManager.h"

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
    if (midiInputNode != nullptr && midiOutputNode != nullptr)
        graph.addConnection (
            { { midiInputNode->nodeID, midiChannel },
              { midiOutputNode->nodeID, midiChannel } },
            juce::AudioProcessorGraph::UpdateKind::none);

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

        if (midiInputNode != nullptr && child->acceptsMidi())
            graph.addConnection (
                { { midiInputNode->nodeID, midiChannel },
                  { node->nodeID, midiChannel } },
                juce::AudioProcessorGraph::UpdateKind::none);

        if (midiOutputNode != nullptr && child->producesMidi())
            graph.addConnection (
                { { node->nodeID, midiChannel },
                  { midiOutputNode->nodeID, midiChannel } },
                juce::AudioProcessorGraph::UpdateKind::none);
    }

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
'''

write('Source/HostedPluginTopology.h', HOSTED_TOPOLOGY_H)
write('Source/PluginSlot.h', PLUGIN_SLOT_H)
write('Source/PluginSlot.cpp', PLUGIN_SLOT_CPP)
write('Source/RackGraphManager.h', RACK_GRAPH_H)
write('Source/RackGraphManager.cpp', RACK_GRAPH_CPP)

# GestureRack now exposes a stable rack-facing bus bank. Main and sidechain keep
# their existing identities; eight optional stereo aux outputs create real host
# endpoints for nested racks without baking any child plug-in name into routing.
replace_once('Source/PluginProcessor.cpp',
'''constexpr int currentStateVersion = 7;''',
'''constexpr int currentStateVersion = 8;''')

regex_once('Source/PluginProcessor.cpp',
           r'GestureRackAudioProcessor::GestureRackAudioProcessor\(\)\n    : juce::AudioProcessor \(BusesProperties\(\).*?\),\n      graphManager',
'''GestureRackAudioProcessor::GestureRackAudioProcessor()\n    : juce::AudioProcessor (BusesProperties()\n                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)\n                                .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)\n                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)\n                                .withOutput ("Aux 1", juce::AudioChannelSet::stereo(), false)\n                                .withOutput ("Aux 2", juce::AudioChannelSet::stereo(), false)\n                                .withOutput ("Aux 3", juce::AudioChannelSet::stereo(), false)\n                                .withOutput ("Aux 4", juce::AudioChannelSet::stereo(), false)\n                                .withOutput ("Aux 5", juce::AudioChannelSet::stereo(), false)\n                                .withOutput ("Aux 6", juce::AudioChannelSet::stereo(), false)\n                                .withOutput ("Aux 7", juce::AudioChannelSet::stereo(), false)\n                                .withOutput ("Aux 8", juce::AudioChannelSet::stereo(), false)),\n      graphManager''')

regex_once('Source/PluginProcessor.cpp',
           r'    graphManager\.initialise \(\n        juce::jmin \(\n            getTotalNumInputChannels\(\),\n            getTotalNumOutputChannels\(\)\)\);',
           '    graphManager.initialise (buildHostBusLayout());')

regex_once('Source/PluginProcessor.cpp',
           r'void GestureRackAudioProcessor::prepareToPlay \(double sampleRate, int samplesPerBlock\)\n\{.*?\n\}',
'''void GestureRackAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)\n{\n    prepared = true;\n    hostSampleRate.store (sampleRate, std::memory_order_relaxed);\n    hostBlockSize.store (samplesPerBlock, std::memory_order_relaxed);\n\n    graph.setPlayConfigDetails (getTotalNumInputChannels(), getTotalNumOutputChannels(),\n                                sampleRate, samplesPerBlock);\n    graph.setPlayHead (getPlayHead());\n    graph.setNonRealtime (isNonRealtime());\n    graphManager.rebuildRouting (buildHostBusLayout());\n    graph.prepareToPlay (sampleRate, samplesPerBlock);\n\n    juce::dsp::ProcessSpec spec { sampleRate,\n                                 static_cast<juce::uint32> (juce::jmax (1, samplesPerBlock)),\n                                 static_cast<juce::uint32> (juce::jmax (1, getTotalNumOutputChannels())) };\n    hostBypassDelay.prepare (spec);\n    hostBypassDelay.reset();\n    updateTotalLatency();\n}''')

regex_once('Source/PluginProcessor.cpp',
           r'bool GestureRackAudioProcessor::isBusesLayoutSupported \(const BusesLayout& layouts\) const\n\{.*?\n\}',
'''bool GestureRackAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const\n{\n    const auto in = layouts.getMainInputChannelSet();\n    const auto out = layouts.getMainOutputChannelSet();\n    if (in != out || (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()))\n        return false;\n\n    if (layouts.inputBuses.size() > 1)\n    {\n        const auto sidechain = layouts.getChannelSet (true, 1);\n        if (! sidechain.isDisabled()\n            && sidechain != juce::AudioChannelSet::mono()\n            && sidechain != juce::AudioChannelSet::stereo())\n            return false;\n    }\n\n    for (int busIndex = 1; busIndex < layouts.outputBuses.size(); ++busIndex)\n    {\n        const auto aux = layouts.getChannelSet (false, busIndex);\n        if (! aux.isDisabled()\n            && aux != juce::AudioChannelSet::mono()\n            && aux != juce::AudioChannelSet::stereo())\n            return false;\n    }\n\n    return true;\n}''')

replace_once('Source/PluginProcessor.cpp',
'''void GestureRackAudioProcessor::timerCallback()\n{\n    constexpr auto controlDeltaSeconds = 1.0f / 100.0f;''',
'''void GestureRackAudioProcessor::timerCallback()\n{\n    constexpr auto controlDeltaSeconds = 1.0f / 100.0f;\n\n    // I/O and latency mutations are legal host events, not safety failures.\n    // Service them on the message thread and atomically hand JUCE a rebuilt\n    // render sequence instead of touching plug-in lifecycle from audio.\n    if (graphManager.serviceDynamicChanges (buildHostBusLayout()))\n        updateTotalLatency();''')

regex_once('Source/PluginProcessor.cpp',
           r'juce::AudioProcessorEditor\* GestureRackAudioProcessor::getOrCreateSlotEditor \(int slotIndex\)\n\{.*?\n\}\n\nbool GestureRackAudioProcessor::slotHasNativeEditor \(int slotIndex\) const noexcept\n\{.*?\n\}',
'''juce::AudioProcessorEditor* GestureRackAudioProcessor::getOrCreateSlotEditor (int slotIndex)\n{\n    if (! isValidSlotIndex (slotIndex))\n        return nullptr;\n    return slots[static_cast<size_t> (slotIndex)]->getOrCreateEmbeddedEditor();\n}\n\nbool GestureRackAudioProcessor::slotHasNativeEditor (int slotIndex) const noexcept\n{\n    if (! isValidSlotIndex (slotIndex))\n        return false;\n    if (auto* child = slots[static_cast<size_t> (slotIndex)]->getChild())\n        return child->hasEditor();\n    return false;\n}''')

regex_once('Source/PluginProcessor.cpp',
           r'void GestureRackAudioProcessor::installChild \(\n    int slotIndex,\n    uint64_t loadGeneration,\n    std::unique_ptr<juce::AudioPluginInstance> instance,\n    const juce::PluginDescription& description,\n    const juce::MemoryBlock\* restoredState\)\n\{.*?\n\}\n\nvoid GestureRackAudioProcessor::removeSlotPlugin',
'''void GestureRackAudioProcessor::installChild (\n    int slotIndex,\n    uint64_t loadGeneration,\n    std::unique_ptr<juce::AudioPluginInstance> instance,\n    const juce::PluginDescription& description,\n    const juce::MemoryBlock* restoredState)\n{\n    if (! isValidSlotIndex (slotIndex) || instance == nullptr)\n        return;\n\n    const auto stableId = slots[static_cast<size_t> (slotIndex)]->getStableId();\n    auto& slot = *slots[static_cast<size_t> (slotIndex)];\n    if (! slot.isLoadGenerationCurrent (loadGeneration))\n        return;\n\n    const auto hostLayout = buildHostBusLayout();\n    juce::String compatibilityError;\n    if (! gr::configureHostedPluginForRack (*instance, hostLayout,\n                                            description.isInstrument, compatibilityError))\n    {\n        slot.setLastError ("HOST COMPATIBILITY: " + compatibilityError);\n        return;\n    }\n\n    instance->setPlayHead (getPlayHead());\n\n    if (restoredState != nullptr && restoredState->getSize() > 0)\n        instance->setStateInformation (restoredState->getData(),\n                                       static_cast<int> (restoredState->getSize()));\n\n    // State restore may legitimately change active buses. Re-negotiate only\n    // rack endpoints; never disable child AUX/sidechain buses globally.\n    if (! gr::configureHostedPluginForRack (*instance, hostLayout,\n                                            description.isInstrument, compatibilityError))\n    {\n        slot.setLastError ("HOST STATE COMPATIBILITY: " + compatibilityError);\n        return;\n    }\n\n    instance->setPlayHead (getPlayHead());\n\n    const auto currentIndex = findSlotIndexByStableId (stableId);\n    if (! isValidSlotIndex (currentIndex))\n        return;\n\n    auto& currentSlot = *slots[static_cast<size_t> (currentIndex)];\n    if (! currentSlot.isLoadGenerationCurrent (loadGeneration))\n        return;\n\n    parameterLearnManager.cancelIfSlot (currentIndex);\n    if (const auto& oldDescription = currentSlot.getDescription(); oldDescription.has_value())\n        if (! oldDescription->matchesIdentifierString (description.createIdentifierString()))\n            mappingEngine.clearChildParameterMappings (currentIndex);\n\n    if (graphManager.installSlotProcessor (currentIndex, std::move (instance), hostLayout) == nullptr)\n    {\n        currentSlot.setLastError ("GRAPH INSERT: Could not insert the hosted plug-in into the rack.");\n        return;\n    }\n\n    currentSlot.setDescription (description);\n    currentSlot.clearLastError();\n    ensureTrailingEmptySlot();\n    updateTotalLatency();\n}\n\nvoid GestureRackAudioProcessor::removeSlotPlugin''')

# Graph-manager APIs now take a bus-aware host layout. Replace the old channel-count calls.
regex_once('Source/PluginProcessor.cpp',
           r'    graphManager\.removeSlotProcessor \(\n        slotIndex,\n        juce::jmin \(\n            getMainBusNumInputChannels\(\),\n            getMainBusNumOutputChannels\(\)\)\);',
           '    graphManager.removeSlotProcessor (slotIndex, buildHostBusLayout());')

# There may be multiple reorder/state-restore rebuild sites. They are intentionally
# all converted to the same routing-plan rebuild.
p = ROOT / 'Source/PluginProcessor.cpp'
text = p.read_text(encoding='utf-8')
text = re.sub(r'graphManager\.rebuildSerialConnections \(\s*juce::jmin \(\s*getMainBusNumInputChannels\(\),\s*getMainBusNumOutputChannels\(\)\)\s*\);',
              'graphManager.rebuildRouting (buildHostBusLayout());', text)
text = re.sub(r'graphManager\.removeAllSlotProcessors \(\s*juce::jmin \(\s*getMainBusNumInputChannels\(\),\s*getMainBusNumOutputChannels\(\)\)\s*\);',
              'graphManager.removeAllSlotProcessors (buildHostBusLayout());', text)
p.write_text(text, encoding='utf-8', newline='\n')

# Insert host bus snapshot builder immediately before latency update.
replace_once('Source/PluginProcessor.cpp',
'''void GestureRackAudioProcessor::updateTotalLatency()\n{''',
'''gr::RackHostBusLayout GestureRackAudioProcessor::buildHostBusLayout() const\n{\n    gr::RackHostBusLayout layout;\n    layout.mainInput = getChannelLayoutOfBus (true, 0);\n    layout.mainOutput = getChannelLayoutOfBus (false, 0);\n\n    if (getBusCount (true) > 1)\n        if (auto* sidechain = getBus (true, 1); sidechain != nullptr && sidechain->isEnabled())\n            layout.sidechainInput = getChannelLayoutOfBus (true, 1);\n\n    for (int busIndex = 1; busIndex < getBusCount (false); ++busIndex)\n    {\n        auto set = juce::AudioChannelSet::disabled();\n        if (auto* bus = getBus (false, busIndex); bus != nullptr && bus->isEnabled())\n            set = getChannelLayoutOfBus (false, busIndex);\n        layout.auxOutputs.push_back (set);\n    }\n\n    return layout;\n}\n\nvoid GestureRackAudioProcessor::updateTotalLatency()\n{''')

replace_once('Source/PluginProcessor.h',
'''    void updateTotalLatency();\n    void updateMappingStatus (const juce::String& text);''',
'''    gr::RackHostBusLayout buildHostBusLayout() const;\n    void updateTotalLatency();\n    void updateMappingStatus (const juce::String& text);''')

# Source registration: raw child instances are graph nodes; the old hosting wrapper
# is deliberately removed from the target so it cannot silently re-enter the audio path.
p = ROOT / 'CMakeLists.txt'
cmake = p.read_text(encoding='utf-8')
cmake = cmake.replace('    Source/GestureBypassWrapper.h\n    Source/GestureBypassWrapper.cpp\n', '')
cmake = cmake.replace('    Source/PluginSlot.h\n', '    Source/HostedPluginTopology.h\n    Source/PluginSlot.h\n')
p.write_text(cmake, encoding='utf-8', newline='\n')

# Keep the repository honest: if anything still depends on the wrapper the build must
# fail rather than leaving a dormant second host implementation around.
for dead in ['Source/GestureBypassWrapper.h', 'Source/GestureBypassWrapper.cpp']:
    path = ROOT / dead
    if path.exists():
        path.unlink()

# Structural assertions before compilation.
processor = (ROOT / 'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
slot = (ROOT / 'Source/PluginSlot.cpp').read_text(encoding='utf-8')
graph = (ROOT / 'Source/RackGraphManager.cpp').read_text(encoding='utf-8')
assert 'GestureBypassWrapper' not in processor
assert 'GestureBypassWrapper' not in slot
assert 'disableNonMainBuses' not in ''.join(p.read_text(encoding='utf-8') for p in (ROOT / 'Source').glob('*.cpp'))
assert 'std::unique_ptr<juce::AudioPluginInstance>' in graph
assert 'serviceDynamicChanges' in processor
assert '.withOutput ("Aux 8"' in processor
print('Raw hosted graph refactor staged successfully.')
