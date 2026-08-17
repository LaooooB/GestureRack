#include "RightGestureRuntime.h"

namespace gr
{
void RightGestureRuntime::clearCandidate() noexcept
{
    candidateGesture = ControlGesture::unknown;
    candidateFrames = 0;
}

void RightGestureRuntime::reset() noexcept
{
    armed = true;
    acceptedGesture = ControlGesture::unknown;
    blockedGesture = ControlGesture::unknown;
    missingFrames = 0;
    clearCandidate();
}

void RightGestureRuntime::disarmForSlotChange() noexcept
{
    if (acceptedGesture == ControlGesture::unknown)
    {
        armed = true;
        blockedGesture = ControlGesture::unknown;
    }
    else
    {
        armed = false;
        blockedGesture = acceptedGesture;
        // The old slot has already released its active mappings. Keep only the
        // blocked gesture token so the new slot cannot inherit a held gesture.
        acceptedGesture = ControlGesture::unknown;
    }
    clearCandidate();
    missingFrames = 0;
}

RightGestureRuntimeFrame RightGestureRuntime::update (bool handPresent,
                                                      ControlGesture stableGesture) noexcept
{
    RightGestureRuntimeFrame frame;
    frame.armed = armed;

    if (! handPresent || stableGesture == ControlGesture::unknown)
    {
        ++missingFrames;
        clearCandidate();
        if (missingFrames >= releaseFrames && acceptedGesture != ControlGesture::unknown)
        {
            frame.exited = true;
            frame.exitedGesture = acceptedGesture;
            acceptedGesture = ControlGesture::unknown;
        }
        if (missingFrames >= releaseFrames)
        {
            armed = true;
            blockedGesture = ControlGesture::unknown;
        }
        frame.armed = armed;
        return frame;
    }

    missingFrames = 0;

    if (! armed)
    {
        if (stableGesture == blockedGesture)
        {
            frame.gesture = acceptedGesture;
            frame.armed = false;
            return frame;
        }
        // Changing to a different gesture is allowed, but it still has to pass
        // the same dwell filter before it can trigger.
        armed = true;
        blockedGesture = ControlGesture::unknown;
        clearCandidate();
    }

    if (stableGesture == acceptedGesture && acceptedGesture != ControlGesture::unknown)
    {
        clearCandidate();
        frame.gesture = acceptedGesture;
        frame.continuousActive = true;
        frame.armed = true;
        return frame;
    }

    if (candidateGesture != stableGesture)
    {
        candidateGesture = stableGesture;
        candidateFrames = 1;
    }
    else
        ++candidateFrames;

    const auto requiredFrames = acceptedGesture == ControlGesture::unknown ? enterFrames : switchFrames;
    if (candidateFrames < requiredFrames)
    {
        // Hold the last accepted gesture during short classifier jitter instead
        // of sending rapid enter/exit events to plugin parameters.
        frame.gesture = acceptedGesture;
        frame.continuousActive = acceptedGesture != ControlGesture::unknown;
        frame.armed = true;
        return frame;
    }

    if (acceptedGesture != ControlGesture::unknown)
    {
        frame.exited = true;
        frame.exitedGesture = acceptedGesture;
    }
    acceptedGesture = stableGesture;
    clearCandidate();
    frame.gesture = acceptedGesture;
    frame.entered = true;
    frame.continuousActive = true;
    frame.armed = true;
    return frame;
}

void RightGestureRuntime::restoreArmingState (bool shouldBeArmed, ControlGesture blocked) noexcept
{
    armed = shouldBeArmed;
    acceptedGesture = ControlGesture::unknown;
    blockedGesture = shouldBeArmed ? ControlGesture::unknown : blocked;
    missingFrames = 0;
    clearCandidate();
}
}
