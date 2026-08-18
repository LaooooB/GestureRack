#include "GestureBinding.h"
#include <cmath>

namespace gr
{
juce::String mappingTargetTypeToString (MappingTargetType type)
{
    switch (type)
    {
        case MappingTargetType::slotAction:      return "Slot Action";
        case MappingTargetType::childParameter: return "Child Parameter";
        default:                                 return "Unknown";
    }
}

juce::String mappingModeToString (MappingMode mode)
{
    switch (mode)
    {
        case MappingMode::triggerSetActive:     return "Set Active";
        case MappingMode::triggerSetBypassed:   return "Set Bypassed";
        case MappingMode::absoluteHeight:       return "Continuous";
        case MappingMode::toggleParameter:      return "Toggle";
        case MappingMode::momentaryParameter:   return "Momentary";
        case MappingMode::cycleParameter:       return "Cycle";
        case MappingMode::stepUpParameter:      return "Step +";
        case MappingMode::stepDownParameter:    return "Step -";
        case MappingMode::triggerParameter:     return "Trigger";
        default:                                return "Unknown";
    }
}

juce::String parameterKindToString (ParameterKind kind)
{
    switch (kind)
    {
        case ParameterKind::continuous: return "CONT";
        case ParameterKind::toggle:     return "TOGGLE";
        case ParameterKind::choice:     return "CHOICE";
        case ParameterKind::stepped:    return "STEPS";
        case ParameterKind::readOnly:   return "READ ONLY";
        default:                        return "PARAM";
    }
}

juce::String mappingAxisToString (MappingAxis axis)
{
    switch (axis)
    {
        case MappingAxis::horizontal: return "Horizontal X";
        case MappingAxis::vertical:   return "Vertical Y";
        default:                      return "Vertical Y";
    }
}

juce::String mappingCurveTypeToString (MappingCurveType type)
{
    switch (type)
    {
        case MappingCurveType::linear:      return "Linear";
        case MappingCurveType::easeIn:      return "Ease In";
        case MappingCurveType::easeOut:     return "Ease Out";
        case MappingCurveType::sCurve:      return "S Curve";
        case MappingCurveType::exponential: return "Exponential";
        case MappingCurveType::logarithmic: return "Logarithmic";
        default:                            return "Linear";
    }
}

float applyMappingCurve (float source, MappingCurveType type, float amount) noexcept
{
    const auto x = juce::jlimit (0.0f, 1.0f, source);
    const auto a = juce::jlimit (0.0f, 1.0f, amount);
    if (type == MappingCurveType::linear || a <= 0.0001f)
        return x;

    switch (type)
    {
        case MappingCurveType::easeIn:
        {
            const auto exponent = 1.0f + 5.0f * a;
            return std::pow (x, exponent);
        }
        case MappingCurveType::easeOut:
        {
            const auto exponent = 1.0f + 5.0f * a;
            return 1.0f - std::pow (1.0f - x, exponent);
        }
        case MappingCurveType::sCurve:
        {
            const auto exponent = 1.0f + 4.0f * a;
            const auto left = std::pow (x, exponent);
            const auto right = std::pow (1.0f - x, exponent);
            const auto denominator = left + right;
            return denominator > 0.000001f ? left / denominator : x;
        }
        case MappingCurveType::exponential:
        {
            const auto k = 5.0f * a;
            const auto denominator = std::expm1 (k);
            return std::abs (denominator) > 0.000001f
                ? std::expm1 (k * x) / denominator : x;
        }
        case MappingCurveType::logarithmic:
        {
            const auto k = 5.0f * a;
            const auto gain = std::expm1 (k);
            return k > 0.000001f
                ? std::log1p (gain * x) / k : x;
        }
        case MappingCurveType::linear:
        default:
            return x;
    }
}

float applyMotionSensitivity (float source, float sensitivity) noexcept
{
    const auto x = juce::jlimit (0.0f, 1.0f, source);
    const auto gain = juce::jlimit (0.25f, 8.0f, sensitivity);
    return juce::jlimit (0.0f, 1.0f, 0.5f + (x - 0.5f) * gain);
}

std::unique_ptr<juce::XmlElement> GestureBinding::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("BINDING");
    xml->setAttribute ("id", id.toString());
    xml->setAttribute ("slot", slotIndex);
    xml->setAttribute ("gesture", controlGestureToString (sourceGesture));
    xml->setAttribute ("targetType", static_cast<int> (targetType));
    xml->setAttribute ("mode", static_cast<int> (mode));
    xml->setAttribute ("pluginIdentifier", pluginIdentifier);
    xml->setAttribute ("parameterStableId", parameterStableId);
    xml->setAttribute ("parameterIndexFallback", parameterIndexFallback);
    xml->setAttribute ("parameterName", parameterName);
    xml->setAttribute ("minValue", static_cast<double> (minValue));
    xml->setAttribute ("maxValue", static_cast<double> (maxValue));
    xml->setAttribute ("smoothingMs", static_cast<double> (smoothingMs));
    xml->setAttribute ("deadband", static_cast<double> (deadband));
    xml->setAttribute ("sourceAxis", static_cast<int> (sourceAxis));
    xml->setAttribute ("curveType", static_cast<int> (curveType));
    xml->setAttribute ("curve", static_cast<double> (curve));
    xml->setAttribute ("sensitivity", static_cast<double> (sensitivity));
    xml->setAttribute ("inverted", inverted);
    xml->setAttribute ("enabled", enabled);
    return xml;
}

std::optional<GestureBinding> GestureBinding::fromXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName ("BINDING"))
        return std::nullopt;

    GestureBinding binding;
    if (const auto idText = xml.getStringAttribute ("id"); idText.isNotEmpty())
        binding.id = juce::Uuid (idText);

    binding.slotIndex = xml.getIntAttribute ("slot", 0);
    binding.sourceGesture = controlGestureFromString (xml.getStringAttribute ("gesture"));
    binding.targetType = static_cast<MappingTargetType> (xml.getIntAttribute ("targetType", 1));
    binding.mode = static_cast<MappingMode> (xml.getIntAttribute ("mode", 2));
    binding.pluginIdentifier = xml.getStringAttribute ("pluginIdentifier");
    binding.parameterStableId = xml.getStringAttribute ("parameterStableId");
    binding.parameterIndexFallback = xml.getIntAttribute ("parameterIndexFallback", -1);
    binding.parameterName = xml.getStringAttribute ("parameterName");
    binding.minValue = static_cast<float> (xml.getDoubleAttribute ("minValue", 0.0));
    binding.maxValue = static_cast<float> (xml.getDoubleAttribute ("maxValue", 1.0));
    binding.smoothingMs = static_cast<float> (xml.getDoubleAttribute ("smoothingMs", 25.0));
    binding.deadband = static_cast<float> (xml.getDoubleAttribute ("deadband", 0.008));
    binding.sourceAxis = static_cast<MappingAxis> (xml.getIntAttribute ("sourceAxis", 0));
    binding.sensitivity = static_cast<float> (xml.getDoubleAttribute ("sensitivity", 1.0));

    if (xml.hasAttribute ("curveType"))
    {
        binding.curveType = static_cast<MappingCurveType> (xml.getIntAttribute ("curveType", 0));
        binding.curve = static_cast<float> (xml.getDoubleAttribute ("curve", 1.0));
    }
    else
    {
        // State v5 and earlier stored a signed bend in `curve`. Preserve its
        // audible result by migrating positive values to Ease In and negative
        // values to Ease Out. Zero remains Linear.
        const auto legacyCurve = static_cast<float> (xml.getDoubleAttribute ("curve", 0.0));
        if (legacyCurve > 0.0001f)
        {
            binding.curveType = MappingCurveType::easeIn;
            binding.curve = std::abs (legacyCurve);
        }
        else if (legacyCurve < -0.0001f)
        {
            binding.curveType = MappingCurveType::easeOut;
            binding.curve = std::abs (legacyCurve);
        }
        else
        {
            binding.curveType = MappingCurveType::linear;
            binding.curve = 1.0f;
        }
    }

    binding.inverted = xml.getBoolAttribute ("inverted", false);
    binding.enabled = xml.getBoolAttribute ("enabled", true);

    binding.minValue = juce::jlimit (0.0f, 1.0f, binding.minValue);
    binding.maxValue = juce::jlimit (0.0f, 1.0f, binding.maxValue);
    binding.smoothingMs = juce::jlimit (0.0f, 5000.0f, binding.smoothingMs);
    binding.deadband = juce::jlimit (0.0f, 0.25f, binding.deadband);
    binding.curve = juce::jlimit (0.0f, 1.0f, binding.curve);
    binding.sensitivity = juce::jlimit (0.25f, 8.0f, binding.sensitivity);

    const auto axisValue = static_cast<int> (binding.sourceAxis);
    if (axisValue < static_cast<int> (MappingAxis::vertical)
        || axisValue > static_cast<int> (MappingAxis::horizontal))
        binding.sourceAxis = MappingAxis::vertical;

    const auto curveValue = static_cast<int> (binding.curveType);
    if (curveValue < static_cast<int> (MappingCurveType::linear)
        || curveValue > static_cast<int> (MappingCurveType::logarithmic))
        binding.curveType = MappingCurveType::linear;

    return binding;
}
}
