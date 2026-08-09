#include "PluginSlot.h"

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
}
