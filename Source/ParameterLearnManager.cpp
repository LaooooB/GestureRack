#include "ParameterLearnManager.h"
#include <cmath>

namespace gr
{
ParameterLearnManager::ParameterLearnManager (GestureMappingEngine& mappingEngineToUse) noexcept
    : mappingEngine (mappingEngineToUse)
{
    activeSession.store (nullptr, std::memory_order_release);
}

ParameterLearnManager::~ParameterLearnManager()
{
    cancel();
}

bool ParameterLearnManager::arm (int slotIndex,
                                 ControlGesture gesture,
                                 juce::AudioPluginInstance* child,
                                 juce::String& error)
{
    error.clear();
    cancel();

    if (child == nullptr || gesture == ControlGesture::unknown)
    {
        error = "Load a plugin and choose a gesture before learning.";
        return false;
    }

    const auto& parameters = child->getParameters();
    if (parameters.isEmpty())
    {
        error = "This plugin exposes no host-visible parameters.";
        return false;
    }

    auto session = std::make_shared<Session>();
    session->slotIndex = slotIndex;
    session->gesture = gesture;

    auto maxIndex = -1;
    for (auto* parameter : parameters)
        if (parameter != nullptr)
            maxIndex = juce::jmax (maxIndex, parameter->getParameterIndex());

    session->parametersByIndex.resize (static_cast<size_t> (maxIndex + 1), nullptr);
    session->baselineByIndex.resize (static_cast<size_t> (maxIndex + 1), 0.0f);

    for (auto* parameter : parameters)
    {
        if (parameter == nullptr)
            continue;

        const auto index = parameter->getParameterIndex();
        if (! juce::isPositiveAndBelow (index, static_cast<int> (session->parametersByIndex.size())))
            continue;

        session->parametersByIndex[static_cast<size_t> (index)] = parameter;
        session->baselineByIndex[static_cast<size_t> (index)] = parameter->getValue();
        parameter->addListener (this);
    }

    session->armedAtMs.store (juce::Time::currentTimeMillis(), std::memory_order_relaxed);
    activeSession.store (session, std::memory_order_release);
    return true;
}

void ParameterLearnManager::detachSession (const std::shared_ptr<Session>& session)
{
    if (session == nullptr)
        return;

    for (auto* parameter : session->parametersByIndex)
        if (parameter != nullptr)
            parameter->removeListener (this);
}

void ParameterLearnManager::cancel()
{
    auto session = activeSession.exchange (nullptr, std::memory_order_acq_rel);
    detachSession (session);
}

void ParameterLearnManager::cancelIfSlot (int slotIndex)
{
    auto session = activeSession.load (std::memory_order_acquire);
    if (session != nullptr && session->slotIndex == slotIndex)
        cancel();
}

bool ParameterLearnManager::isArmed() const noexcept
{
    return activeSession.load (std::memory_order_acquire) != nullptr;
}

int ParameterLearnManager::getLearningSlot() const noexcept
{
    if (auto session = activeSession.load (std::memory_order_acquire))
        return session->slotIndex;
    return -1;
}

ControlGesture ParameterLearnManager::getLearningGesture() const noexcept
{
    if (auto session = activeSession.load (std::memory_order_acquire))
        return session->gesture;
    return ControlGesture::unknown;
}

juce::String ParameterLearnManager::getStatusText() const
{
    if (auto session = activeSession.load (std::memory_order_acquire))
        return "LEARNING " + controlGestureToString (session->gesture)
             + " - move one plugin parameter";
    return "LEARN IDLE";
}

void ParameterLearnManager::parameterValueChanged (int parameterIndex, float newValue)
{
    if (mappingEngine.isApplyingInternalWrite())
        return;

    auto session = activeSession.load (std::memory_order_acquire);
    if (session == nullptr
        || ! juce::isPositiveAndBelow (parameterIndex, static_cast<int> (session->baselineByIndex.size())))
        return;

    const auto baseline = session->baselineByIndex[static_cast<size_t> (parameterIndex)];
    const auto magnitude = std::abs (newValue - baseline);
    if (magnitude < captureThreshold)
        return;

    const auto now = juce::Time::currentTimeMillis();
    const auto currentMagnitude = session->candidateMagnitude.load (std::memory_order_relaxed);
    const auto currentTime = session->lastCandidateMs.load (std::memory_order_relaxed);

    if (magnitude >= currentMagnitude || now - currentTime > 80)
    {
        session->candidateIndex.store (parameterIndex, std::memory_order_relaxed);
        session->candidateMagnitude.store (magnitude, std::memory_order_relaxed);
    }

    session->lastCandidateMs.store (now, std::memory_order_release);
}

void ParameterLearnManager::parameterGestureChanged (int parameterIndex, bool gestureIsStarting)
{
    if (! gestureIsStarting || mappingEngine.isApplyingInternalWrite())
        return;

    auto session = activeSession.load (std::memory_order_acquire);
    if (session == nullptr
        || ! juce::isPositiveAndBelow (parameterIndex, static_cast<int> (session->parametersByIndex.size())))
        return;

    if (session->parametersByIndex[static_cast<size_t> (parameterIndex)] == nullptr)
        return;

    session->candidateIndex.store (parameterIndex, std::memory_order_relaxed);
    session->candidateMagnitude.store (1.0f, std::memory_order_relaxed);
    session->lastCandidateMs.store (juce::Time::currentTimeMillis(), std::memory_order_release);
}

std::optional<ParameterLearnManager::Capture> ParameterLearnManager::pollCapture()
{
    auto session = activeSession.load (std::memory_order_acquire);
    if (session == nullptr)
        return std::nullopt;

    const auto index = session->candidateIndex.load (std::memory_order_relaxed);
    if (index < 0)
        return std::nullopt;

    const auto now = juce::Time::currentTimeMillis();
    const auto lastChange = session->lastCandidateMs.load (std::memory_order_acquire);
    if (lastChange <= 0 || now - lastChange < settleMs)
        return std::nullopt;

    Capture capture { session->slotIndex, session->gesture, index };
    cancel();
    return capture;
}
}
