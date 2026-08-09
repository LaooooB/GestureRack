#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <map>
#include <string>
#include <vector>
#include "GestureBinding.h"
#include "RackGraphManager.h"

namespace gr
{
class GestureMappingEngine final
{
public:
    explicit GestureMappingEngine (RackGraphManager::SlotArray& slotsToUse) noexcept;

    std::vector<ParameterDescriptor> enumerateParameters (int slotIndex) const;
    std::vector<GestureBinding> getMappings (int slotIndex) const;

    bool addParameterBinding (int slotIndex,
                              ControlGesture gesture,
                              int parameterIndex,
                              juce::String& error);
    bool addSlotActionBinding (int slotIndex,
                               ControlGesture gesture,
                               MappingMode mode,
                               juce::String& error);
    bool updateBinding (const GestureBinding& binding, juce::String& error);
    bool removeBinding (int slotIndex, const juce::Uuid& id);

    void clearChildParameterMappings (int slotIndex);
    void clearAllMappings (int slotIndex);

    void triggerGestureEntered (int slotIndex, ControlGesture gesture);
    void processContinuous (int slotIndex,
                            ControlGesture gesture,
                            float normalizedSource,
                            float deltaSeconds);

    juce::AudioProcessorParameter* resolveParameter (int slotIndex, const GestureBinding& binding) const;
    ParameterDescriptor describeParameter (const juce::AudioProcessorParameter& parameter) const;

    bool isApplyingInternalWrite() const noexcept
    {
        return internalWriteDepth.load (std::memory_order_relaxed) > 0;
    }

private:
    struct RuntimeState
    {
        bool initialised = false;
        float lastSource = 0.0f;
        float smoothedOutput = 0.0f;
    };

    bool isValidSlotIndex (int slotIndex) const noexcept;
    juce::String getPluginIdentifier (int slotIndex) const;
    static juce::String getParameterStableId (const juce::AudioProcessorParameter& parameter);
    static bool sameParameterTarget (const GestureBinding& a, const GestureBinding& b);
    void pruneRuntimeStatesForSlot (int slotIndex);

    RackGraphManager::SlotArray& slots;
    std::map<std::string, RuntimeState> runtimeStates;
    std::atomic<int> internalWriteDepth { 0 };
};
}
