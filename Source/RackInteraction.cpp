#include "PluginProcessor.h"

void GestureRackAudioProcessor::loadPluginDescription (
    int slotIndex,
    const juce::PluginDescription& description)
{
    if (! isValidSlotIndex (slotIndex))
        return;

    auto& slot =
        *slots[static_cast<size_t> (slotIndex)];

    slot.clearLastError();

    if (description.name.containsIgnoreCase (
            "Gesture Rack"))
    {
        slot.setLastError (
            "Gesture Rack cannot host itself.");
        return;
    }

    slot.setLastError (
        "LOADING "
        + description.name
        + "...");

    parameterLearnManager.cancelIfSlot (
        slotIndex);

    mappingEngine.releaseAllActiveGestures();

    if (slotIndex == getSelectedSlot())
        testSignalEnabled.store (
            false,
            std::memory_order_relaxed);

    loadDescriptionAsync (
        slotIndex,
        description,
        nullptr);
}

bool GestureRackAudioProcessor::moveSlot (
    int fromSlot,
    int toSlot)
{
    const auto chainCount =
        getChainSlotCount();

    if (fromSlot < 0
        || toSlot < 0
        || fromSlot >= chainCount
        || toSlot >= chainCount)
        return false;

    if (fromSlot == toSlot)
        return true;

    parameterLearnManager.cancel();
    mappingEngine.releaseAllActiveGestures();

    testSignalEnabled.store (
        false,
        std::memory_order_relaxed);

    rightRuntime.disarmForSlotChange();

    auto moved =
        std::move (
            slots[
                static_cast<size_t> (
                    fromSlot)]);

    slots.erase (
        slots.begin()
        + fromSlot);

    slots.insert (
        slots.begin()
        + toSlot,
        std::move (moved));

    reindexSlots();

    auto newSelected =
        getSelectedSlot();

    if (newSelected == fromSlot)
        newSelected = toSlot;
    else if (fromSlot < toSlot
             && newSelected > fromSlot
             && newSelected <= toSlot)
        --newSelected;
    else if (fromSlot > toSlot
             && newSelected >= toSlot
             && newSelected < fromSlot)
        ++newSelected;

    selectedSlot.store (
        juce::jlimit (
            0,
            getAddSlotIndex(),
            newSelected),
        std::memory_order_relaxed);

    graphManager.rebuildSerialConnections (
        juce::jmin (
            getMainBusNumInputChannels(),
            getMainBusNumOutputChannels()));

    updateTotalLatency();
    updateMappingStatus (
        "RACK ORDER UPDATED");

    return true;
}
