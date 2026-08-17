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
    ControlGesture getCurrentGesture() const noexcept { return acceptedGesture; }
    ControlGesture getBlockedGesture() const noexcept { return blockedGesture; }
    void restoreArmingState (bool shouldBeArmed, ControlGesture blocked) noexcept;

private:
    static constexpr int enterFrames = 3;      // ~30 ms at 100 Hz
    static constexpr int switchFrames = 4;     // resist one-frame class swaps
    static constexpr int releaseFrames = 6;    // ~60 ms dropout hysteresis

    void clearCandidate() noexcept;

    bool armed = true;
    ControlGesture acceptedGesture = ControlGesture::unknown;
    ControlGesture blockedGesture = ControlGesture::unknown;
    ControlGesture candidateGesture = ControlGesture::unknown;
    int candidateFrames = 0;
    int missingFrames = 0;
};
}
