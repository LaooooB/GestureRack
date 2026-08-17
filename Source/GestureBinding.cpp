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

float applyMappingCurve (float source, float curve) noexcept
{
    const auto x = juce::jlimit (0.0f, 1.0f, source);
    const auto c = juce::jlimit (-1.0f, 1.0f, curve);
    if (std::abs (c) < 0.0001f)
        return x;

    // Exponent spans 1..6. The shape is continuous at c=0 and symmetrical in
    // perceptual intent: positive gives finer low-range control, negative gives
    // faster low-range movement while preserving exact 0/1 endpoints.
    const auto exponent = 1.0f + 5.0f * std::abs (c);
    return c > 0.0f ? std::pow (x, exponent)
                    : 1.0f - std::pow (1.0f - x, exponent);
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
    xml->setAttribute ("curve", static_cast<double> (curve));
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
    binding.curve = static_cast<float> (xml.getDoubleAttribute ("curve", 0.0));
    binding.inverted = xml.getBoolAttribute ("inverted", false);
    binding.enabled = xml.getBoolAttribute ("enabled", true);

    binding.minValue = juce::jlimit (0.0f, 1.0f, binding.minValue);
    binding.maxValue = juce::jlimit (0.0f, 1.0f, binding.maxValue);
    binding.smoothingMs = juce::jlimit (0.0f, 5000.0f, binding.smoothingMs);
    binding.deadband = juce::jlimit (0.0f, 0.25f, binding.deadband);
    binding.curve = juce::jlimit (-1.0f, 1.0f, binding.curve);
    return binding;
}
}
