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
    // Scanner channel counts are discovery metadata only. Complex VST3 rack/meta
    // effects can report zero or incomplete I/O until a real instance is created and
    // its buses are negotiated. Only the explicit instrument policy is enforced here;
    // configureChildForHosting() is the runtime compatibility authority.
    if (description.isInstrument)
    {
        slot.setLastError ("This rack hosts audio effects, not instruments.");
        return;
    }

    slot.setLastError ("LOADING " + description.name + "...");
    parameterLearnManager.cancelIfSlot (slotIndex);
    mappingEngine.releaseAllActiveGestures();
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
    mappingEngine.releaseAllActiveGestures();
    testSignalEnabled.store (false, std::memory_order_relaxed);
    rightRuntime.disarmForSlotChange();

    for (auto& generation : slotLoadGenerations)
        generation.fetch_add (1, std::memory_order_relaxed);

    // The complete slot object moves, including bypass state, mappings, graph
    // node, hosted processor and its embedded editor. No UI state is left behind.
    auto moved = std::move (slots[static_cast<size_t> (fromSlot)]);
    if (fromSlot < toSlot)
        for (int i = fromSlot; i < toSlot; ++i)
            slots[static_cast<size_t> (i)] = std::move (slots[static_cast<size_t> (i + 1)]);
    else
        for (int i = fromSlot; i > toSlot; --i)
            slots[static_cast<size_t> (i)] = std::move (slots[static_cast<size_t> (i - 1)]);
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
