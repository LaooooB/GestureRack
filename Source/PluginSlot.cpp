#include "PluginSlot.h"
#include <algorithm>

namespace gr
{
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
    : slotIndex (indexToUse)
{
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

    // A parameter has exactly one gesture owner. A gesture may still fan out to
    // any number of different parameters. Replacing the gesture on an already
    // mapped parameter therefore removes only that parameter's previous binding;
    // sibling parameters driven by the same gesture are untouched.
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

    // Keep the one-parameter/one-gesture invariant even when mappings are
    // restored through undo/state migration or edited by future UI code.
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
