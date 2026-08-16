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

    // Cancel any async load that still targets a physical rack index. Existing
    // loaded processors remain alive; only unfinished load callbacks are invalidated.
    for (auto& generation : slotLoadGenerations)
        generation.fetch_add (1, std::memory_order_relaxed);

    // Child editor windows are tied to physical slot indices, so close them before
    // the hosted processors move to new positions.
    for (auto& window : childEditorWindows)
        window.reset();

    if (fromSlot < toSlot)
    {
        for (int i = fromSlot; i < toSlot; ++i)
            slots[static_cast<size_t> (i)]->swapContentsWith (*slots[static_cast<size_t> (i + 1)]);
    }
    else
    {
        for (int i = fromSlot; i > toSlot; --i)
            slots[static_cast<size_t> (i)]->swapContentsWith (*slots[static_cast<size_t> (i - 1)]);
    }

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
