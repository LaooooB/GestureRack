#pragma once

#include "ControlGesture.h"

namespace gr
{
struct RightGestureRuntimeFrame
{
    ControlGesture gesture = ControlGesture::unknown;
    bool entered = false;
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
    bool armed = true;
    ControlGesture currentGesture = ControlGesture::unknown;
    ControlGesture previousGesture = ControlGesture::unknown;
    ControlGesture blockedGesture = ControlGesture::unknown;
};
}
