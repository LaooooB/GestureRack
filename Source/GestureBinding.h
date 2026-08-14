#pragma once

#include <JuceHeader.h>
#include "ControlGesture.h"

namespace gr
{
enum class MappingTargetType : int
{
    slotAction = 0,
    childParameter
};

enum class MappingMode : int
{
    triggerSetActive = 0,
    triggerSetBypassed,
    absoluteHeight
};

struct ParameterDescriptor
{
    juce::String stableId;
    int index = -1;
    juce::String name;
    juce::String label;
    float normalizedValue = 0.0f;
    juce::String displayValue;
    bool automatable = false;
    bool discrete = false;
    int numSteps = 0;
};

struct GestureBinding
{
    juce::Uuid id;
    int slotIndex = 0;
    ControlGesture sourceGesture = ControlGesture::unknown;
    MappingTargetType targetType = MappingTargetType::childParameter;
    MappingMode mode = MappingMode::absoluteHeight;

    juce::String pluginIdentifier;
    juce::String parameterStableId;
    int parameterIndexFallback = -1;
    juce::String parameterName;

    float minValue = 0.0f;
    float maxValue = 1.0f;
    // smoothingMs is the one-EMA time constant tau, NOT a hard cap. tau=80ms
    // takes ~240ms to reach 95% of a step; tau=25ms reaches it in ~75ms. The
    // default is the snappy Live value; 80ms remains available as a Smooth
    // preset via the ParameterInspector smoothing control.
    float smoothingMs = 25.0f;
    float deadband = 0.008f;
    bool inverted = false;
    bool enabled = true;

    std::unique_ptr<juce::XmlElement> toXml() const;
    static std::optional<GestureBinding> fromXml (const juce::XmlElement& xml);
};

juce::String mappingTargetTypeToString (MappingTargetType type);
juce::String mappingModeToString (MappingMode mode);
}
