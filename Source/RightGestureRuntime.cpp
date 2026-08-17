#include "RightGestureRuntime.h"

namespace gr
{
void RightGestureRuntime::reset() noexcept
{
    armed = true;
    missingFrameCount = 0;
    currentGesture = ControlGesture::unknown;
    previousGesture = ControlGesture::unknown;
    blockedGesture = ControlGesture::unknown;
}

void RightGestureRuntime::disarmForSlotChange() noexcept
{
    if (currentGesture == ControlGesture::unknown)
    {
        armed = true;
        blockedGesture = ControlGesture::unknown;
        previousGesture = ControlGesture::unknown;
        return;
    }

    armed = false;
    blockedGesture = currentGesture;
    previousGesture = currentGesture;
}

RightGestureRuntimeFrame RightGestureRuntime::update (bool handPresent,
                                                      ControlGesture stableGesture) noexcept
{
    RightGestureRuntimeFrame frame;

    if (! handPresent)
    {
        currentGesture = ControlGesture::unknown;
        ++missingFrameCount;
        if (missingFrameCount >= missingFramesBeforeReset)
        {
            if (previousGesture != ControlGesture::unknown)
            {
                frame.exited = true;
                frame.exitedGesture = previousGesture;
            }
            reset();
        }
        frame.armed = armed;
        return frame;
    }

    missingFrameCount = 0;
    currentGesture = stableGesture;

    if (! armed)
    {
        if (stableGesture == ControlGesture::unknown)
        {
            armed = true;
            blockedGesture = ControlGesture::unknown;
            previousGesture = ControlGesture::unknown;
            frame.armed = true;
            return frame;
        }

        if (stableGesture == blockedGesture)
        {
            frame.gesture = stableGesture;
            frame.armed = false;
            return frame;
        }

        armed = true;
        blockedGesture = ControlGesture::unknown;
        previousGesture = stableGesture;
        frame.gesture = stableGesture;
        frame.entered = true;
        frame.continuousActive = true;
        frame.armed = true;
        return frame;
    }

    frame.armed = true;
    frame.gesture = stableGesture;

    if (stableGesture == ControlGesture::unknown)
    {
        if (previousGesture != ControlGesture::unknown)
        {
            frame.exited = true;
            frame.exitedGesture = previousGesture;
        }
        previousGesture = ControlGesture::unknown;
        return frame;
    }

    frame.continuousActive = true;
    if (stableGesture != previousGesture)
    {
        if (previousGesture != ControlGesture::unknown)
        {
            frame.exited = true;
            frame.exitedGesture = previousGesture;
        }
        frame.entered = true;
        previousGesture = stableGesture;
    }

    return frame;
}

void RightGestureRuntime::restoreArmingState (bool shouldBeArmed, ControlGesture blocked) noexcept
{
    armed = shouldBeArmed;
    missingFrameCount = 0;
    blockedGesture = shouldBeArmed ? ControlGesture::unknown : blocked;
    currentGesture = ControlGesture::unknown;
    previousGesture = shouldBeArmed ? ControlGesture::unknown : blocked;
}
}
