#pragma once

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
