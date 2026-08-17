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
    ~InternalWriteGuard() { depth.fetch_sub (1, std::memory_order_relaxed); }
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
    return {};
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
    descriptor.boolean = parameter.isBoolean();
    descriptor.orientationInverted = parameter.isOrientationInverted();
    descriptor.numSteps = parameter.getNumSteps();

    if (! descriptor.automatable)
        descriptor.kind = ParameterKind::readOnly;
    else if (descriptor.boolean || descriptor.numSteps == 2)
        descriptor.kind = ParameterKind::toggle;
    else if (descriptor.discrete && descriptor.numSteps > 2 && descriptor.numSteps <= 64)
        descriptor.kind = ParameterKind::choice;
    else if (descriptor.discrete)
        descriptor.kind = ParameterKind::stepped;
    else
        descriptor.kind = ParameterKind::continuous;
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
    if (a.targetType != MappingTargetType::childParameter || b.targetType != MappingTargetType::childParameter)
        return false;
    if (a.parameterStableId.isNotEmpty() && b.parameterStableId.isNotEmpty())
        return a.parameterStableId == b.parameterStableId;
    return a.parameterIndexFallback >= 0
        && a.parameterIndexFallback == b.parameterIndexFallback
        && a.parameterName == b.parameterName;
}

MappingMode GestureMappingEngine::defaultModeForParameter (const ParameterDescriptor& descriptor) noexcept
{
    switch (descriptor.kind)
    {
        case ParameterKind::toggle:  return MappingMode::toggleParameter;
        case ParameterKind::choice:
        case ParameterKind::stepped: return MappingMode::cycleParameter;
        case ParameterKind::continuous:
        default:                     return MappingMode::absoluteHeight;
    }
}

void GestureMappingEngine::removeMappingsOwnedByGesture (int slotIndex,
                                                          ControlGesture gesture,
                                                          const juce::Uuid* exceptId)
{
    if (! isValidSlotIndex (slotIndex) || gesture == ControlGesture::unknown)
        return;
    auto mappings = getMappings (slotIndex);
    for (const auto& existing : mappings)
    {
        if (existing.sourceGesture != gesture)
            continue;
        if (exceptId != nullptr && existing.id == *exceptId)
            continue;
        endHostGesture (slotIndex, existing);
        runtimeStates.erase (existing.id.toString().toStdString());
        slots[static_cast<size_t> (slotIndex)]->removeMapping (existing.id);
    }
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
    if (! juce::isPositiveAndBelow (parameterIndex, parameters.size()) || parameters[parameterIndex] == nullptr)
    {
        error = "The selected parameter is no longer available.";
        return false;
    }
    const auto descriptor = describeParameter (*parameters[parameterIndex]);
    if (! descriptor.automatable)
    {
        error = "This parameter is read-only.";
        return false;
    }

    GestureBinding binding;
    binding.slotIndex = slotIndex;
    binding.sourceGesture = gesture;
    binding.targetType = MappingTargetType::childParameter;
    binding.mode = defaultModeForParameter (descriptor);
    binding.pluginIdentifier = getPluginIdentifier (slotIndex);
    binding.parameterStableId = descriptor.stableId;
    binding.parameterIndexFallback = descriptor.index;
    binding.parameterName = descriptor.name;

    // A gesture is a control source, not an exclusive owner. It may fan out to
    // any number of parameters in the selected slot. Only reject the exact same
    // gesture -> parameter pair so accidental duplicate bindings do not stack.
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

    // Parameter fan-out is allowed, but two slot-state actions for the same
    // gesture would fight each other on the same enter event.
    for (const auto& existing : getMappings (slotIndex))
    {
        if (existing.sourceGesture != gesture || existing.targetType != MappingTargetType::slotAction)
            continue;

        if (existing.mode == mode)
        {
            error = "This gesture is already mapped to that slot action.";
            return false;
        }

        error = "This gesture already owns the opposite slot-state action.";
        return false;
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
    updated.curve = juce::jlimit (-1.0f, 1.0f, updated.curve);

    endHostGesture (updated.slotIndex, updated);
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
    for (const auto& binding : getMappings (slotIndex))
        if (binding.id == id)
            endHostGesture (slotIndex, binding);
    runtimeStates.erase (id.toString().toStdString());
    return slots[static_cast<size_t> (slotIndex)]->removeMapping (id);
}

void GestureMappingEngine::clearChildParameterMappings (int slotIndex)
{
    if (! isValidSlotIndex (slotIndex))
        return;
    for (const auto& binding : getMappings (slotIndex))
        if (binding.targetType == MappingTargetType::childParameter)
            endHostGesture (slotIndex, binding);
    slots[static_cast<size_t> (slotIndex)]->clearChildParameterMappings();
    pruneRuntimeStatesForSlot (slotIndex);
}

void GestureMappingEngine::clearAllMappings (int slotIndex)
{
    if (! isValidSlotIndex (slotIndex))
        return;
    for (const auto& binding : getMappings (slotIndex))
        endHostGesture (slotIndex, binding);
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
    if (! isValidSlotIndex (slotIndex) || binding.targetType != MappingTargetType::childParameter)
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
        for (auto* parameter : parameters)
            if (parameter != nullptr && getParameterStableId (*parameter) == binding.parameterStableId)
                return parameter;

    const auto fallback = binding.parameterIndexFallback;
    if (juce::isPositiveAndBelow (fallback, parameters.size()))
    {
        auto* parameter = parameters[fallback];
        if (parameter != nullptr && (binding.parameterName.isEmpty() || parameter->getName (256) == binding.parameterName))
            return parameter;
    }
    return nullptr;
}

void GestureMappingEngine::writeParameter (juce::AudioProcessorParameter& parameter, float normalizedValue)
{
    const auto value = juce::jlimit (0.0f, 1.0f, normalizedValue);
    if (std::abs (parameter.getValue() - value) < 0.00001f)
        return;
    InternalWriteGuard guard (internalWriteDepth);
    parameter.setValueNotifyingHost (value);
}

float GestureMappingEngine::discreteStepDelta (const juce::AudioProcessorParameter& parameter) const noexcept
{
    const auto steps = parameter.getNumSteps();
    return steps > 1 && steps < 1000000
        ? 1.0f / static_cast<float> (steps - 1)
        : 0.05f;
}

void GestureMappingEngine::triggerGestureEntered (int slotIndex, ControlGesture gesture)
{
    if (! isValidSlotIndex (slotIndex) || gesture == ControlGesture::unknown)
        return;

    auto& slot = *slots[static_cast<size_t> (slotIndex)];
    for (const auto& binding : slot.getMappings())
    {
        if (! binding.enabled || binding.sourceGesture != gesture)
            continue;
        if (binding.targetType == MappingTargetType::slotAction)
        {
            if (binding.mode == MappingMode::triggerSetActive)
                slot.setBypassed (false);
            else if (binding.mode == MappingMode::triggerSetBypassed)
                slot.setBypassed (true);
            continue;
        }

        auto* parameter = resolveParameter (slotIndex, binding);
        if (parameter == nullptr)
            continue;
        auto& state = runtimeStates[binding.id.toString().toStdString()];
        if (! state.hostGestureOpen)
        {
            parameter->beginChangeGesture();
            state.hostGestureOpen = true;
        }

        switch (binding.mode)
        {
            case MappingMode::toggleParameter:
                writeParameter (*parameter, parameter->getValue() >= 0.5f ? binding.minValue : binding.maxValue);
                endHostGesture (slotIndex, binding);
                break;
            case MappingMode::cycleParameter:
            {
                const auto step = discreteStepDelta (*parameter);
                auto next = parameter->getValue() + step;
                if (next > binding.maxValue + 0.0001f)
                    next = binding.minValue;
                writeParameter (*parameter, next);
                endHostGesture (slotIndex, binding);
                break;
            }
            case MappingMode::stepUpParameter:
                writeParameter (*parameter, juce::jmin (binding.maxValue, parameter->getValue() + discreteStepDelta (*parameter)));
                endHostGesture (slotIndex, binding);
                break;
            case MappingMode::stepDownParameter:
                writeParameter (*parameter, juce::jmax (binding.minValue, parameter->getValue() - discreteStepDelta (*parameter)));
                endHostGesture (slotIndex, binding);
                break;
            case MappingMode::momentaryParameter:
                writeParameter (*parameter, binding.maxValue);
                break;
            case MappingMode::triggerParameter:
                writeParameter (*parameter, binding.maxValue);
                endHostGesture (slotIndex, binding);
                break;
            case MappingMode::absoluteHeight:
                break;
            default:
                endHostGesture (slotIndex, binding);
                break;
        }
    }
}

void GestureMappingEngine::endHostGesture (int slotIndex, const GestureBinding& binding)
{
    const auto key = binding.id.toString().toStdString();
    auto it = runtimeStates.find (key);
    if (it == runtimeStates.end() || ! it->second.hostGestureOpen)
        return;
    if (auto* parameter = resolveParameter (slotIndex, binding))
        parameter->endChangeGesture();
    it->second.hostGestureOpen = false;
}

void GestureMappingEngine::triggerGestureExited (int slotIndex, ControlGesture gesture)
{
    if (! isValidSlotIndex (slotIndex) || gesture == ControlGesture::unknown)
        return;
    for (const auto& binding : getMappings (slotIndex))
    {
        if (! binding.enabled || binding.sourceGesture != gesture || binding.targetType != MappingTargetType::childParameter)
            continue;
        if (binding.mode == MappingMode::momentaryParameter)
            if (auto* parameter = resolveParameter (slotIndex, binding))
                writeParameter (*parameter, binding.minValue);
        endHostGesture (slotIndex, binding);
    }
}

void GestureMappingEngine::releaseAllActiveGestures()
{
    for (int slotIndex = 0; slotIndex < RackGraphManager::slotCount; ++slotIndex)
        if (isValidSlotIndex (slotIndex))
            for (const auto& binding : getMappings (slotIndex))
                endHostGesture (slotIndex, binding);
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
        if (! state.hostGestureOpen)
        {
            parameter->beginChangeGesture();
            state.hostGestureOpen = true;
        }

        auto source = binding.inverted ? 1.0f - sourceInput : sourceInput;
        if (state.initialised && std::abs (source - state.lastSource) < binding.deadband)
            source = state.lastSource;
        else
            state.lastSource = source;
        source = applyMappingCurve (source, binding.curve);

        const auto target = juce::jlimit (0.0f, 1.0f,
            binding.minValue + source * (binding.maxValue - binding.minValue));
        if (! state.initialised)
        {
            state.initialised = true;
            state.smoothedOutput = target;
        }
        else if (binding.smoothingMs <= 0.0f)
            state.smoothedOutput = target;
        else
        {
            const auto tau = juce::jmax (0.001f, binding.smoothingMs * 0.001f);
            const auto alpha = 1.0f - std::exp (-dt / tau);
            state.smoothedOutput += alpha * (target - state.smoothedOutput);
        }
        writeParameter (*parameter, state.smoothedOutput);
    }
}
}
