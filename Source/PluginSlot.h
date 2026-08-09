#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <optional>
#include "GestureBypassWrapper.h"

namespace gr
{
class PluginSlot final
{
public:
    explicit PluginSlot (int indexToUse) noexcept;

    int getIndex() const noexcept { return slotIndex; }

    bool hasPlugin() const noexcept { return graphNode != nullptr && description.has_value(); }
    bool isBypassed() const noexcept { return requestedBypass.load (std::memory_order_relaxed); }
    void setBypassed (bool shouldBypass) noexcept
    {
        requestedBypass.store (shouldBypass, std::memory_order_relaxed);
    }

    juce::String getPluginName() const;
    const std::optional<juce::PluginDescription>& getDescription() const noexcept { return description; }
    void setDescription (const juce::PluginDescription& newDescription) { description = newDescription; }
    void clearDescription() { description.reset(); }

    void setGraphNode (juce::AudioProcessorGraph::Node::Ptr newNode) { graphNode = std::move (newNode); }
    void clearGraphNode() { graphNode = nullptr; }
    juce::AudioProcessorGraph::Node::Ptr getGraphNode() const noexcept { return graphNode; }

    GestureBypassWrapper* getWrapper() const noexcept;
    juce::AudioPluginInstance* getChild() const noexcept;

    juce::String getLastError() const { return lastError; }
    void setLastError (juce::String error) { lastError = std::move (error); }
    void clearLastError() { lastError.clear(); }

    std::atomic<bool>& getBypassState() noexcept { return requestedBypass; }

private:
    int slotIndex = 0;
    std::optional<juce::PluginDescription> description;
    juce::AudioProcessorGraph::Node::Ptr graphNode;
    std::atomic<bool> requestedBypass { false };
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginSlot)
};
}
