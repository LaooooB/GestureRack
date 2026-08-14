#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>
#include "ControlGesture.h"
#include "GestureMappingEngine.h"

namespace gr
{
class ParameterLearnManager final : private juce::AudioProcessorParameter::Listener
{
public:
    struct Capture
    {
        int slotIndex = -1;
        ControlGesture gesture = ControlGesture::unknown;
        int parameterIndex = -1;
    };

    explicit ParameterLearnManager (GestureMappingEngine& mappingEngineToUse) noexcept;
    ~ParameterLearnManager() override;

    bool arm (int slotIndex,
              ControlGesture gesture,
              juce::AudioPluginInstance* child,
              juce::String& error);
    void cancel();
    void cancelIfSlot (int slotIndex);

    bool isArmed() const noexcept;
    int getLearningSlot() const noexcept;
    ControlGesture getLearningGesture() const noexcept;
    juce::String getStatusText() const;

    std::optional<Capture> pollCapture();

private:
    struct Session
    {
        int slotIndex = -1;
        ControlGesture gesture = ControlGesture::unknown;
        std::vector<juce::AudioProcessorParameter*> parametersByIndex;
        std::vector<float> baselineByIndex;
        std::atomic<int> candidateIndex { -1 };
        std::atomic<float> candidateMagnitude { 0.0f };
        std::atomic<int64_t> lastCandidateMs { 0 };
        std::atomic<int64_t> armedAtMs { 0 };
    };

    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;
    void detachSession (const std::shared_ptr<Session>& session);

    GestureMappingEngine& mappingEngine;
    std::atomic<std::shared_ptr<Session>> activeSession;

    static constexpr float captureThreshold = 0.015f;
    static constexpr int settleMs = 120;
};
}
