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
