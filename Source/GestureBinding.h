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
    // Keep the first three numeric values stable for state compatibility.
    triggerSetActive = 0,
    triggerSetBypassed,
    absoluteHeight,
    toggleParameter,
    momentaryParameter,
    cycleParameter,
    stepUpParameter,
    stepDownParameter,
    triggerParameter
};

enum class ParameterKind : int
{
    continuous = 0,
    toggle,
    choice,
    stepped,
    readOnly
};

// Scope belongs to each individual binding, not to the gesture itself. This
// lets one gesture own GLOBAL targets and SELECTED-only targets at the same
// time, including multiple targets inside one hosted plug-in.
enum class BindingScope : int
{
    selected = 0,
    global
};

// Continuous mappings are no longer hard-wired to hand height. The enum is
// deliberately small today, but keeps the binding model ready for future Z,
// rotation, pinch, velocity, etc. without adding new MappingMode values.
enum class MappingAxis : int
{
    vertical = 0,
    horizontal
};

// Curve shape is stored independently from curveAmount so the UI can expose
// predictable named curves and still let the user continuously tune their
// intensity. Reverse direction remains the existing `inverted` flag.
enum class MappingCurveType : int
{
    linear = 0,
    easeIn,
    easeOut,
    sCurve,
    exponential,
    logarithmic
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
    bool boolean = false;
    bool orientationInverted = false;
    int numSteps = 0;
    ParameterKind kind = ParameterKind::continuous;
};

struct GestureBinding
{
    juce::Uuid id;
    int slotIndex = 0;
    ControlGesture sourceGesture = ControlGesture::unknown;
    MappingTargetType targetType = MappingTargetType::childParameter;
    MappingMode mode = MappingMode::absoluteHeight;
    BindingScope scope = BindingScope::selected;

    juce::String pluginIdentifier;
    juce::String parameterStableId;
    int parameterIndexFallback = -1;
    juce::String parameterName;

    float minValue = 0.0f;
    float maxValue = 1.0f;
    float smoothingMs = 25.0f;
    float deadband = 0.008f;

    MappingAxis sourceAxis = MappingAxis::vertical;
    MappingCurveType curveType = MappingCurveType::linear;

    // 0..1 curve intensity. Linear ignores this value. Old states used this
    // field as a signed -1..1 bend; fromXml migrates those states losslessly
    // into Ease In / Ease Out plus an unsigned amount.
    float curve = 1.0f;

    // 1x preserves the normal active camera range. >1x means less hand travel
    // produces more parameter travel; <1x deliberately makes the mapping calmer.
    float sensitivity = 1.0f;

    bool inverted = false;
    bool enabled = true;

    std::unique_ptr<juce::XmlElement> toXml() const;
    static std::optional<GestureBinding> fromXml (const juce::XmlElement& xml);
};

juce::String mappingTargetTypeToString (MappingTargetType type);
juce::String mappingModeToString (MappingMode mode);
juce::String parameterKindToString (ParameterKind kind);
juce::String bindingScopeToString (BindingScope scope);
juce::String mappingAxisToString (MappingAxis axis);
juce::String mappingCurveTypeToString (MappingCurveType type);

float applyMappingCurve (float source, MappingCurveType type, float amount) noexcept;
float applyMotionSensitivity (float source, float sensitivity) noexcept;
}
