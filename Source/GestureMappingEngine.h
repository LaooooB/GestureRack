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
    explicit GestureMappingEngine (RackGraphManager::SlotList& slotsToUse) noexcept;

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
    void triggerGestureExited (int slotIndex, ControlGesture gesture);
    void releaseAllActiveGestures();

    // normalizedX is the right-hand palm X from the mirrored camera frame.
    // normalizedY is the existing filtered 0..1 hand-height signal. Each
    // binding selects which axis it consumes, so one gesture can fan out to
    // horizontal and vertical targets at the same time.
    void processContinuous (int slotIndex,
                            ControlGesture gesture,
                            float normalizedX,
                            float normalizedY,
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
        bool hostGestureOpen = false;
        float lastSource = 0.0f;
        float smoothedOutput = 0.0f;
    };

    bool isValidSlotIndex (int slotIndex) const noexcept;
    juce::String getPluginIdentifier (int slotIndex) const;
    static juce::String getParameterStableId (const juce::AudioProcessorParameter& parameter);
    static bool sameParameterTarget (const GestureBinding& a, const GestureBinding& b);
    static MappingMode defaultModeForParameter (const ParameterDescriptor& descriptor) noexcept;
    static float normaliseHorizontalPalmX (float palmX) noexcept;
    void removeMappingsOwnedByGesture (int slotIndex, ControlGesture gesture, const juce::Uuid* exceptId = nullptr);
    void pruneRuntimeStatesForSlot (int slotIndex);
    void endHostGesture (int slotIndex, const GestureBinding& binding);
    void writeParameter (juce::AudioProcessorParameter& parameter, float normalizedValue);
    float discreteStepDelta (const juce::AudioProcessorParameter& parameter) const noexcept;

    RackGraphManager::SlotList& slots;
    std::map<std::string, RuntimeState> runtimeStates;
    std::atomic<int> internalWriteDepth { 0 };
};
}
