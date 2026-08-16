#include "PluginProcessor.h"

void GestureRackAudioProcessor::loadPluginDescription (int slotIndex,
                                                        const juce::PluginDescription& description)
{
    if (! isValidSlotIndex (slotIndex))
        return;

    auto& slot = *slots[static_cast<size_t> (slotIndex)];
    slot.clearLastError();

    if (description.name.containsIgnoreCase ("Gesture Rack"))
    {
        slot.setLastError ("Gesture Rack cannot host itself.");
        return;
    }

    if (description.isInstrument || description.numInputChannels <= 0)
    {
        slot.setLastError ("This rack hosts audio effects, not instruments.");
        return;
    }

    parameterLearnManager.cancelIfSlot (slotIndex);
    if (slotIndex == getSelectedSlot())
        testSignalEnabled.store (false, std::memory_order_relaxed);

    loadDescriptionAsync (slotIndex, description, nullptr);
}

bool GestureRackAudioProcessor::moveSlot (int fromSlot, int toSlot)
{
    if (! isValidSlotIndex (fromSlot) || ! isValidSlotIndex (toSlot))
        return false;
    if (fromSlot == toSlot)
        return true;

    parameterLearnManager.cancel();
    testSignalEnabled.store (false, std::memory_order_relaxed);
    rightRuntime.disarmForSlotChange();

    // Async loaders target array indices. Invalidate unfinished work before the
    // slot objects move so a late callback can never install into the wrong place.
    for (auto& generation : slotLoadGenerations)
        generation.fetch_add (1, std::memory_order_relaxed);

    // Child editor windows are indexed by rack position, so close them before
    // moving the hosted processors.
    for (auto& window : childEditorWindows)
        window.reset();

    // Move the complete PluginSlot object, rather than swapping just its graph
    // node. GestureBypassWrapper holds a reference to PluginSlot::requestedBypass;
    // keeping those objects together preserves bypass control after reordering.
    auto moved = std::move (slots[static_cast<size_t> (fromSlot)]);
    if (fromSlot < toSlot)
    {
        for (int i = fromSlot; i < toSlot; ++i)
            slots[static_cast<size_t> (i)] = std::move (slots[static_cast<size_t> (i + 1)]);
    }
    else
    {
        for (int i = fromSlot; i > toSlot; --i)
            slots[static_cast<size_t> (i)] = std::move (slots[static_cast<size_t> (i - 1)]);
    }
    slots[static_cast<size_t> (toSlot)] = std::move (moved);

    for (int i = 0; i < slotCount; ++i)
        if (slots[static_cast<size_t> (i)] != nullptr)
            slots[static_cast<size_t> (i)]->setIndexForReorder (i);

    auto newSelected = getSelectedSlot();
    if (newSelected == fromSlot)
        newSelected = toSlot;
    else if (fromSlot < toSlot && newSelected > fromSlot && newSelected <= toSlot)
        --newSelected;
    else if (fromSlot > toSlot && newSelected >= toSlot && newSelected < fromSlot)
        ++newSelected;

    const auto channels = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels());
    graphManager.rebuildSerialConnections (channels);
    setSelectedSlot (newSelected);
    updateTotalLatency();
    updateMappingStatus ("RACK ORDER UPDATED");
    return true;
}
