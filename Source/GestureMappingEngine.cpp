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

GestureMappingEngine::GestureMappingEngine (RackGraphManager::SlotList& slotsToUse) noexcept
    : slots (slotsToUse)
{
}

bool GestureMappingEngine::isValidSlotIndex (int slotIndex) const noexcept
{
    return slotIndex >= 0
        && slotIndex < static_cast<int> (slots.size())
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

float GestureMappingEngine::normaliseHorizontalPalmX (float palmX) noexcept
{
    // Match the vertical control window already used by VisionEngine: users do
    // not need to touch the camera edges to reach 0/1. The preview is mirrored,
    // so moving the physical right hand toward the visible right side increases X.
    constexpr auto left = 0.15f;
    constexpr auto right = 0.85f;
    return juce::jlimit (0.0f, 1.0f, (palmX - left) / (right - left));
}

bool GestureMappingEngine::bindingRespondsInContext (const GestureBinding& binding,
                                                     int bindingSlotIndex,
                                                     int selectedSlotIndex) const noexcept
{
    // Slot actions intentionally remain local to the selected plug-in. Scope is
    // a parameter-routing concept; a GLOBAL Palm must not power/bypass every FX.
    if (binding.targetType == MappingTargetType::slotAction)
        return bindingSlotIndex == selectedSlotIndex;

    return binding.scope == BindingScope::global
        || bindingSlotIndex == selectedSlotIndex;
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
    binding.scope = nextBindingScope;
    binding.pluginIdentifier = getPluginIdentifier (slotIndex);
    binding.parameterStableId = descriptor.stableId;
    binding.parameterIndexFallback = descriptor.index;
    binding.parameterName = descriptor.name;

    // A parameter still has one gesture owner, while one gesture may fan out to
    // any number of different parameters. Scope belongs to the binding. Rebinding
    // the exact same Gesture + Plugin + Parameter in the other scope MOVES that
    // binding instead of creating a second writer, enforcing the single-scope
    // invariant requested by the UI model.
    for (const auto& existing : getMappings (slotIndex))
    {
        if (! sameParameterTarget (existing, binding))
            continue;

        if (existing.sourceGesture == gesture)
        {
            if (existing.scope == binding.scope)
            {
                error = "This gesture is already mapped to that parameter in "
                      + bindingScopeToString (binding.scope) + ".";
                return false;
            }

            auto replacement = existing;
            replacement.scope = binding.scope;
            replacement.slotIndex = slotIndex;
            replacement.pluginIdentifier = binding.pluginIdentifier;
            replacement.parameterStableId = binding.parameterStableId;
            replacement.parameterIndexFallback = binding.parameterIndexFallback;
            replacement.parameterName = binding.parameterName;

            endHostGesture (slotIndex, existing);
            runtimeStates.erase (existing.id.toString().toStdString());
            if (! slots[static_cast<size_t> (slotIndex)]->updateMapping (replacement))
            {
                error = "The parameter mapping scope could not be changed.";
                return false;
            }
            return true;
        }

        auto replacement = existing;
        replacement.sourceGesture = gesture;
        replacement.scope = binding.scope;
        replacement.slotIndex = slotIndex;
        replacement.pluginIdentifier = binding.pluginIdentifier;
        replacement.parameterStableId = binding.parameterStableId;
        replacement.parameterIndexFallback = binding.parameterIndexFallback;
        replacement.parameterName = binding.parameterName;

        // Close any active host automation gesture before changing ownership so
        // the old source cannot leave an unmatched beginChangeGesture behind.
        endHostGesture (slotIndex, existing);
        runtimeStates.erase (existing.id.toString().toStdString());
        if (! slots[static_cast<size_t> (slotIndex)]->updateMapping (replacement))
        {
            error = "The previous parameter mapping could not be replaced.";
            return false;
        }
        return true;
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
    binding.scope = BindingScope::selected;
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
    updated.curve = juce::jlimit (0.0f, 1.0f, updated.curve);
    updated.sensitivity = juce::jlimit (0.25f, 8.0f, updated.sensitivity);

    const auto scope = static_cast<int> (updated.scope);
    if (scope < static_cast<int> (BindingScope::selected)
        || scope > static_cast<int> (BindingScope::global))
        updated.scope = BindingScope::selected;
    if (updated.targetType == MappingTargetType::slotAction)
        updated.scope = BindingScope::selected;

    const auto axis = static_cast<int> (updated.sourceAxis);
    if (axis < static_cast<int> (MappingAxis::vertical)
        || axis > static_cast<int> (MappingAxis::horizontal))
        updated.sourceAxis = MappingAxis::vertical;
    const auto curveType = static_cast<int> (updated.curveType);
    if (curveType < static_cast<int> (MappingCurveType::linear)
        || curveType > static_cast<int> (MappingCurveType::logarithmic))
        updated.curveType = MappingCurveType::linear;

    if (updated.targetType == MappingTargetType::childParameter)
    {
        for (const auto& existing : getMappings (updated.slotIndex))
        {
            if (existing.id == updated.id)
                continue;
            if (existing.sourceGesture == updated.sourceGesture
                && sameParameterTarget (existing, updated))
            {
                error = "The same gesture and parameter can only exist in one scope.";
                return false;
            }
        }
    }

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

    for (int bindingSlotIndex = 0; bindingSlotIndex < static_cast<int> (slots.size()); ++bindingSlotIndex)
        if (isValidSlotIndex (bindingSlotIndex))
            triggerGestureEnteredForSlot (bindingSlotIndex, slotIndex, gesture);
}

void GestureMappingEngine::triggerGestureEnteredForSlot (int bindingSlotIndex,
                                                          int selectedSlotIndex,
                                                          ControlGesture gesture)
{
    auto& slot = *slots[static_cast<size_t> (bindingSlotIndex)];
    for (const auto& binding : slot.getMappings())
    {
        if (! binding.enabled || binding.sourceGesture != gesture
            || ! bindingRespondsInContext (binding, bindingSlotIndex, selectedSlotIndex))
            continue;
        if (binding.targetType == MappingTargetType::slotAction)
        {
            if (binding.mode == MappingMode::triggerSetActive)
                slot.setBypassed (false);
            else if (binding.mode == MappingMode::triggerSetBypassed)
                slot.setBypassed (true);
            continue;
        }

        auto* parameter = resolveParameter (bindingSlotIndex, binding);
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
                endHostGesture (bindingSlotIndex, binding);
                break;
            case MappingMode::cycleParameter:
            {
                const auto step = discreteStepDelta (*parameter);
                auto next = parameter->getValue() + step;
                if (next > binding.maxValue + 0.0001f)
                    next = binding.minValue;
                writeParameter (*parameter, next);
                endHostGesture (bindingSlotIndex, binding);
                break;
            }
            case MappingMode::stepUpParameter:
                writeParameter (*parameter, juce::jmin (binding.maxValue, parameter->getValue() + discreteStepDelta (*parameter)));
                endHostGesture (bindingSlotIndex, binding);
                break;
            case MappingMode::stepDownParameter:
                writeParameter (*parameter, juce::jmax (binding.minValue, parameter->getValue() - discreteStepDelta (*parameter)));
                endHostGesture (bindingSlotIndex, binding);
                break;
            case MappingMode::momentaryParameter:
                writeParameter (*parameter, binding.maxValue);
                break;
            case MappingMode::triggerParameter:
                writeParameter (*parameter, binding.maxValue);
                endHostGesture (bindingSlotIndex, binding);
                break;
            case MappingMode::absoluteHeight:
                break;
            default:
                endHostGesture (bindingSlotIndex, binding);
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

    for (int bindingSlotIndex = 0; bindingSlotIndex < static_cast<int> (slots.size()); ++bindingSlotIndex)
        if (isValidSlotIndex (bindingSlotIndex))
            triggerGestureExitedForSlot (bindingSlotIndex, slotIndex, gesture);
}

void GestureMappingEngine::triggerGestureExitedForSlot (int bindingSlotIndex,
                                                         int selectedSlotIndex,
                                                         ControlGesture gesture)
{
    for (const auto& binding : getMappings (bindingSlotIndex))
    {
        if (! binding.enabled || binding.sourceGesture != gesture
            || binding.targetType != MappingTargetType::childParameter
            || ! bindingRespondsInContext (binding, bindingSlotIndex, selectedSlotIndex))
            continue;
        if (binding.mode == MappingMode::momentaryParameter)
            if (auto* parameter = resolveParameter (bindingSlotIndex, binding))
                writeParameter (*parameter, binding.minValue);
        endHostGesture (bindingSlotIndex, binding);
    }
}

void GestureMappingEngine::releaseAllActiveGestures()
{
    for (int slotIndex = 0; slotIndex < static_cast<int> (slots.size()); ++slotIndex)
        if (isValidSlotIndex (slotIndex))
            for (const auto& binding : getMappings (slotIndex))
                endHostGesture (slotIndex, binding);
}

void GestureMappingEngine::processContinuous (int slotIndex,
                                              ControlGesture gesture,
                                              float normalizedX,
                                              float normalizedY,
                                              float deltaSeconds)
{
    if (! isValidSlotIndex (slotIndex) || gesture == ControlGesture::unknown)
        return;

    const auto sourceX = normaliseHorizontalPalmX (normalizedX);
    const auto sourceY = juce::jlimit (0.0f, 1.0f, normalizedY);
    const auto dt = juce::jlimit (0.0001f, 1.0f, deltaSeconds);

    for (int bindingSlotIndex = 0; bindingSlotIndex < static_cast<int> (slots.size()); ++bindingSlotIndex)
        if (isValidSlotIndex (bindingSlotIndex))
            processContinuousForSlot (bindingSlotIndex, slotIndex, gesture,
                                      sourceX, sourceY, dt);
}

void GestureMappingEngine::processContinuousForSlot (int bindingSlotIndex,
                                                      int selectedSlotIndex,
                                                      ControlGesture gesture,
                                                      float sourceX,
                                                      float sourceY,
                                                      float deltaSeconds)
{
    for (const auto& binding : slots[static_cast<size_t> (bindingSlotIndex)]->getMappings())
    {
        if (! binding.enabled || binding.sourceGesture != gesture
            || binding.targetType != MappingTargetType::childParameter
            || binding.mode != MappingMode::absoluteHeight
            || ! bindingRespondsInContext (binding, bindingSlotIndex, selectedSlotIndex))
            continue;
        auto* parameter = resolveParameter (bindingSlotIndex, binding);
        if (parameter == nullptr)
            continue;

        auto& state = runtimeStates[binding.id.toString().toStdString()];
        if (! state.hostGestureOpen)
        {
            parameter->beginChangeGesture();
            state.hostGestureOpen = true;
        }

        const auto selectedInput = binding.sourceAxis == MappingAxis::horizontal ? sourceX : sourceY;
        auto source = binding.inverted ? 1.0f - selectedInput : selectedInput;

        // Deadband is evaluated before sensitivity so increasing sensitivity
        // does not also amplify camera landmark noise. Sensitivity then narrows
        // or widens the useful physical travel around the centre of the frame.
        if (state.initialised && std::abs (source - state.lastSource) < binding.deadband)
            source = state.lastSource;
        else
            state.lastSource = source;

        source = applyMotionSensitivity (source, binding.sensitivity);
        source = applyMappingCurve (source, binding.curveType, binding.curve);

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
            const auto alpha = 1.0f - std::exp (-deltaSeconds / tau);
            state.smoothedOutput += alpha * (target - state.smoothedOutput);
        }
        writeParameter (*parameter, state.smoothedOutput);
    }
}
}
