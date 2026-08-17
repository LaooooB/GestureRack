#pragma once

#include "ControlGesture.h"

namespace gr
{
struct RightGestureRuntimeFrame
{
    ControlGesture gesture = ControlGesture::unknown;
    ControlGesture exitedGesture = ControlGesture::unknown;
    bool entered = false;
    bool exited = false;
    bool continuousActive = false;
    bool armed = true;
};

class RightGestureRuntime final
{
public:
    void reset() noexcept;
    void disarmForSlotChange() noexcept;
    RightGestureRuntimeFrame update (bool handPresent, ControlGesture stableGesture) noexcept;

    bool isArmed() const noexcept { return armed; }
    ControlGesture getCurrentGesture() const noexcept { return currentGesture; }
    ControlGesture getBlockedGesture() const noexcept { return blockedGesture; }

    void restoreArmingState (bool shouldBeArmed, ControlGesture blocked) noexcept;

private:
    static constexpr int missingFramesBeforeReset = 5; // ~100 ms at the 50 Hz control rate.

    bool armed = true;
    int missingFrameCount = 0;
    ControlGesture currentGesture = ControlGesture::unknown;
    ControlGesture previousGesture = ControlGesture::unknown;
    ControlGesture blockedGesture = ControlGesture::unknown;
};
}
