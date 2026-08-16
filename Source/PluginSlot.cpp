#include "PluginSlot.h"
#include <algorithm>

namespace gr
{
PluginSlot::PluginSlot (int indexToUse) noexcept
    : slotIndex (indexToUse)
{
}

juce::String PluginSlot::getPluginName() const
{
    return description.has_value() ? description->name : "EMPTY";
}

GestureBypassWrapper* PluginSlot::getWrapper() const noexcept
{
    if (graphNode == nullptr)
        return nullptr;

    return dynamic_cast<GestureBypassWrapper*> (graphNode->getProcessor());
}

juce::AudioPluginInstance* PluginSlot::getChild() const noexcept
{
    if (auto* wrapper = getWrapper())
        return wrapper->getChild();

    return nullptr;
}

void PluginSlot::swapContentsWith (PluginSlot& other)
{
    if (&other == this)
        return;

    // Rack reordering is initiated on the message thread. No other code takes
    // two slot locks simultaneously, so keeping a stable left-to-right lock
    // order avoids any possibility of an opposite-order deadlock later.
    auto* first = slotIndex < other.slotIndex ? this : &other;
    auto* second = first == this ? &other : this;
    const juce::SpinLock::ScopedLockType firstLock (first->mappingsLock);
    const juce::SpinLock::ScopedLockType secondLock (second->mappingsLock);

    std::swap (description, other.description);
    std::swap (graphNode, other.graphNode);
    std::swap (lastError, other.lastError);
    mappings.swap (other.mappings);

    const auto thisBypass = requestedBypass.load (std::memory_order_relaxed);
    const auto otherBypass = other.requestedBypass.load (std::memory_order_relaxed);
    requestedBypass.store (otherBypass, std::memory_order_relaxed);
    other.requestedBypass.store (thisBypass, std::memory_order_relaxed);

    for (auto& binding : mappings)
        binding.slotIndex = slotIndex;
    for (auto& binding : other.mappings)
        binding.slotIndex = other.slotIndex;
}

std::vector<GestureBinding> PluginSlot::getMappings() const
{
    const juce::SpinLock::ScopedLockType lock (mappingsLock);
    return mappings;
}

void PluginSlot::addMapping (const GestureBinding& binding)
{
    const juce::SpinLock::ScopedLockType lock (mappingsLock);
    mappings.push_back (binding);
}

bool PluginSlot::updateMapping (const GestureBinding& binding)
{
    const juce::SpinLock::ScopedLockType lock (mappingsLock);
    for (auto& existing : mappings)
    {
        if (existing.id == binding.id)
        {
            existing = binding;
            return true;
        }
    }
    return false;
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
