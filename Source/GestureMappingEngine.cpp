#include "GestureMappingEngine.h"
#include <cmath>

namespace gr
{
namespace
{
class InternalWriteGuard final
{
public:
    explicit InternalWriteGuard (std::atomic<int>& depthToUse) : depth (depthToUse)
    {
        depth.fetch_add (1, std::memory_order_relaxed);
    }

    ~InternalWriteGuard()
    {
        depth.fetch_sub (1, std::memory_order_relaxed);
    }

private:
    std::atomic<int>& depth;
};
}

GestureMappingEngine::GestureMappingEngine (RackGraphManager::SlotArray& slotsToUse) noexcept
    : slots (slotsToUse)
{
}

bool GestureMappingEngine::isValidSlotIndex (int slotIndex) const noexcept
{
    return slotIndex >= 0 && slotIndex < RackGraphManager::slotCount
        && slots[static_cast<size_t> (slotIndex)] != nullptr;
}

juce::String GestureMappingEngine::getPluginIdentifier (int slotIndex) const
{
    if (! isValidSlotIndex (slotIndex))
        return {};

    const auto& description = slots[static_cast<size_t> (slotIndex)]->getDescription();
    return description.has_value() ? description->createIdentifierString() : juce::String();
}

juce::String GestureMappingEngine::getParameterStableId (const juce::AudioProcessorParameter& parameter)
{
    if (const auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*> (&parameter))
        return hosted->getParameterID();

    return juce::LegacyAudioParameter::getParamID (&parameter, false);
}

ParameterDescriptor GestureMappingEngine::describeParameter (const juce::AudioProcessorParameter& parameter) const
{
    ParameterDescriptor descriptor;
    descriptor.stableId = getParameterStableId (parameter);
    descriptor.index = parameter.getParameterIndex();
    descriptor.name = parameter.getName (256);
    descriptor.label = parameter.getLabel();
    descriptor.normalizedValue = juce::jlimit (0.0f, 1.0f, parameter.getValue());
    descriptor.displayValue = parameter.getCurrentValueAsText();
    if (descriptor.displayValue.isEmpty())
        descriptor.displayValue = parameter.getText (descriptor.normalizedValue, 96);
    descriptor.automatable = parameter.isAutomatable();
    descriptor.discrete = parameter.isDiscrete();
    descriptor.numSteps = parameter.getNumSteps();
    return descriptor;
}

std::vector<ParameterDescriptor> GestureMappingEngine::enumerateParameters (int slotIndex) const
{
    std::vector<ParameterDescriptor> result;
    if (! isValidSlotIndex (slotIndex))
        return result;

    auto* child = slots[static_cast<size_t> (slotIndex)]->getChild();
    if (child == nullptr)
        return result;

    const auto& parameters = child->getParameters();
    result.reserve (static_cast<size_t> (parameters.size()));

    for (auto* parameter : parameters)
        if (parameter != nullptr)
            result.push_back (describeParameter (*parameter));

    return result;
}

std::vector<GestureBinding> GestureMappingEngine::getMappings (int slotIndex) const
{
    if (! isValidSlotIndex (slotIndex))
        return {};

    return slots[static_cast<size_t> (slotIndex)]->getMappings();
}

bool GestureMappingEngine::sameParameterTarget (const GestureBinding& a, const GestureBinding& b)
{
    if (a.targetType != MappingTargetType::childParameter
        || b.targetType != MappingTargetType::childParameter)
        return false;

    if (a.parameterStableId.isNotEmpty() && b.parameterStableId.isNotEmpty())
        return a.parameterStableId == b.parameterStableId;

    return a.parameterIndexFallback >= 0
        && a.parameterIndexFallback == b.parameterIndexFallback
        && a.parameterName == b.parameterName;
}

bool GestureMappingEngine::addParameterBinding (int slotIndex,
                                                ControlGesture gesture,
                                                int parameterIndex,
                                                juce::String& error)
{
    error.clear();
    if (! isValidSlotIndex (slotIndex) || gesture == ControlGesture::unknown)
    {
        error = "Invalid slot or gesture.";
        return false;
    }

    auto* child = slots[static_cast<size_t> (slotIndex)]->getChild();
    if (child == nullptr)
    {
        error = "Load a plugin in this slot first.";
        return false;
    }

    const auto& parameters = child->getParameters();
    if (! juce::isPositiveAndBelow (parameterIndex, parameters.size())
        || parameters[parameterIndex] == nullptr)
    {
        error = "The selected parameter is no longer available.";
        return false;
    }

    const auto descriptor = describeParameter (*parameters[parameterIndex]);

    GestureBinding binding;
    binding.slotIndex = slotIndex;
    binding.sourceGesture = gesture;
    binding.targetType = MappingTargetType::childParameter;
    binding.mode = MappingMode::absoluteHeight;
    binding.pluginIdentifier = getPluginIdentifier (slotIndex);
    binding.parameterStableId = descriptor.stableId;
    binding.parameterIndexFallback = descriptor.index;
    binding.parameterName = descriptor.name;

    for (const auto& existing : getMappings (slotIndex))
    {
        if (existing.sourceGesture == gesture && sameParameterTarget (existing, binding))
        {
            error = "This gesture is already mapped to that parameter.";
            return false;
        }
    }

    slots[static_cast<size_t> (slotIndex)]->addMapping (binding);
    return true;
}

bool GestureMappingEngine::addSlotActionBinding (int slotIndex,
                                                 ControlGesture gesture,
                                                 MappingMode mode,
                                                 juce::String& error)
{
    error.clear();
    if (! isValidSlotIndex (slotIndex) || gesture == ControlGesture::unknown)
    {
        error = "Invalid slot or gesture.";
        return false;
    }

    if (mode != MappingMode::triggerSetActive && mode != MappingMode::triggerSetBypassed)
    {
        error = "Invalid slot action mapping mode.";
        return false;
    }

    for (const auto& existing : getMappings (slotIndex))
    {
        if (existing.sourceGesture != gesture || existing.targetType != MappingTargetType::slotAction)
            continue;

        if (existing.mode == mode)
        {
            error = "This gesture already owns that slot-state action.";
            return false;
        }

        if ((existing.mode == MappingMode::triggerSetActive && mode == MappingMode::triggerSetBypassed)
            || (existing.mode == MappingMode::triggerSetBypassed && mode == MappingMode::triggerSetActive))
        {
            error = "This gesture already owns the opposite slot-state action.";
            return false;
        }
    }

    GestureBinding binding;
    binding.slotIndex = slotIndex;
    binding.sourceGesture = gesture;
    binding.targetType = MappingTargetType::slotAction;
    binding.mode = mode;
    slots[static_cast<size_t> (slotIndex)]->addMapping (binding);
    return true;
}

bool GestureMappingEngine::updateBinding (const GestureBinding& binding, juce::String& error)
{
    error.clear();
    if (! isValidSlotIndex (binding.slotIndex))
    {
        error = "Invalid slot.";
        return false;
    }

    auto updated = binding;
    updated.minValue = juce::jlimit (0.0f, 1.0f, updated.minValue);
    updated.maxValue = juce::jlimit (0.0f, 1.0f, updated.maxValue);
    updated.smoothingMs = juce::jlimit (0.0f, 5000.0f, updated.smoothingMs);
    updated.deadband = juce::jlimit (0.0f, 0.25f, updated.deadband);

    if (! slots[static_cast<size_t> (updated.slotIndex)]->updateMapping (updated))
    {
        error = "Mapping no longer exists.";
        return false;
    }

    runtimeStates.erase (updated.id.toString().toStdString());
    return true;
}

bool GestureMappingEngine::removeBinding (int slotIndex, const juce::Uuid& id)
{
    if (! isValidSlotIndex (slotIndex))
        return false;

    runtimeStates.erase (id.toString().toStdString());
    return slots[static_cast<size_t> (slotIndex)]->removeMapping (id);
}

void GestureMappingEngine::clearChildParameterMappings (int slotIndex)
{
    if (! isValidSlotIndex (slotIndex))
        return;

    slots[static_cast<size_t> (slotIndex)]->clearChildParameterMappings();
    pruneRuntimeStatesForSlot (slotIndex);
}

void GestureMappingEngine::clearAllMappings (int slotIndex)
{
    if (! isValidSlotIndex (slotIndex))
        return;

    slots[static_cast<size_t> (slotIndex)]->clearAllMappings();
    pruneRuntimeStatesForSlot (slotIndex);
}

void GestureMappingEngine::pruneRuntimeStatesForSlot (int)
{
    runtimeStates.clear();
}

juce::AudioProcessorParameter* GestureMappingEngine::resolveParameter (int slotIndex,
                                                                       const GestureBinding& binding) const
{
    if (! isValidSlotIndex (slotIndex)
        || binding.targetType != MappingTargetType::childParameter)
        return nullptr;

    auto& slot = *slots[static_cast<size_t> (slotIndex)];
    auto* child = slot.getChild();
    if (child == nullptr)
        return nullptr;

    if (binding.pluginIdentifier.isNotEmpty())
    {
        const auto& description = slot.getDescription();
        if (! description.has_value() || ! description->matchesIdentifierString (binding.pluginIdentifier))
            return nullptr;
    }

    const auto& parameters = child->getParameters();
    if (binding.parameterStableId.isNotEmpty())
    {
        for (auto* parameter : parameters)
        {
            if (parameter != nullptr && getParameterStableId (*parameter) == binding.parameterStableId)
                return parameter;
        }
    }

    const auto fallback = binding.parameterIndexFallback;
    if (juce::isPositiveAndBelow (fallback, parameters.size()))
    {
        auto* parameter = parameters[fallback];
        if (parameter != nullptr)
        {
            const auto name = parameter->getName (256);
            if (binding.parameterName.isEmpty() || name == binding.parameterName)
                return parameter;
        }
    }

    return nullptr;
}

void GestureMappingEngine::triggerGestureEntered (int slotIndex, ControlGesture gesture)
{
    if (! isValidSlotIndex (slotIndex) || gesture == ControlGesture::unknown)
        return;

    auto& slot = *slots[static_cast<size_t> (slotIndex)];
    for (const auto& binding : slot.getMappings())
    {
        if (! binding.enabled || binding.sourceGesture != gesture
            || binding.targetType != MappingTargetType::slotAction)
            continue;

        if (binding.mode == MappingMode::triggerSetActive)
            slot.setBypassed (false);
        else if (binding.mode == MappingMode::triggerSetBypassed)
            slot.setBypassed (true);
    }
}

void GestureMappingEngine::processContinuous (int slotIndex,
                                              ControlGesture gesture,
                                              float normalizedSource,
                                              float deltaSeconds)
{
    if (! isValidSlotIndex (slotIndex) || gesture == ControlGesture::unknown)
        return;

    const auto sourceInput = juce::jlimit (0.0f, 1.0f, normalizedSource);
    const auto dt = juce::jlimit (0.0001f, 1.0f, deltaSeconds);

    for (const auto& binding : slots[static_cast<size_t> (slotIndex)]->getMappings())
    {
        if (! binding.enabled || binding.sourceGesture != gesture
            || binding.targetType != MappingTargetType::childParameter
            || binding.mode != MappingMode::absoluteHeight)
            continue;

        auto* parameter = resolveParameter (slotIndex, binding);
        if (parameter == nullptr)
            continue;

        auto& state = runtimeStates[binding.id.toString().toStdString()];
        auto source = binding.inverted ? (1.0f - sourceInput) : sourceInput;

        if (state.initialised && std::abs (source - state.lastSource) < binding.deadband)
            source = state.lastSource;
        else
            state.lastSource = source;

        const auto target = juce::jlimit (0.0f, 1.0f,
                                          binding.minValue + source * (binding.maxValue - binding.minValue));

        if (! state.initialised)
        {
            state.initialised = true;
            state.smoothedOutput = target;
        }
        else if (binding.smoothingMs <= 0.0f)
        {
            state.smoothedOutput = target;
        }
        else
        {
            const auto tau = juce::jmax (0.001f, binding.smoothingMs * 0.001f);
            const auto alpha = 1.0f - std::exp (-dt / tau);
            state.smoothedOutput += alpha * (target - state.smoothedOutput);
        }

        const auto output = juce::jlimit (0.0f, 1.0f, state.smoothedOutput);
        if (std::abs (parameter->getValue() - output) < 0.00001f)
            continue;

        InternalWriteGuard guard (internalWriteDepth);
        parameter->setValueNotifyingHost (output);
    }
}
}
