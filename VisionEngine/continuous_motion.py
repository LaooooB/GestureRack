from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Optional


def _alpha(cutoff_hz: float, dt_seconds: float) -> float:
    cutoff = max(1.0e-4, float(cutoff_hz))
    dt = max(1.0e-4, float(dt_seconds))
    tau = 1.0 / (2.0 * math.pi * cutoff)
    return 1.0 / (1.0 + tau / dt)


@dataclass
class OneEuroFilter:
    """Adaptive low-pass filter for realtime hand motion.

    Slow/noisy movement uses min_cutoff for stability. Fast movement raises the
    cutoff according to beta * filtered velocity, reducing lag automatically.
    The filter consumes actual callback timestamps rather than assuming 30 Hz.
    """

    min_cutoff_hz: float = 2.5
    beta: float = 1.2
    derivative_cutoff_hz: float = 1.0
    value: Optional[float] = None
    derivative: float = 0.0
    timestamp_ms: Optional[int] = None

    def reset(self) -> None:
        self.value = None
        self.derivative = 0.0
        self.timestamp_ms = None

    def update(self, value: float, timestamp_ms: int) -> float:
        x = float(value)
        now = int(timestamp_ms)
        if self.value is None or self.timestamp_ms is None:
            self.value = x
            self.derivative = 0.0
            self.timestamp_ms = now
            return x

        dt = max(0.001, min(0.25, (now - self.timestamp_ms) * 0.001))
        raw_derivative = (x - self.value) / dt
        derivative_alpha = _alpha(self.derivative_cutoff_hz, dt)
        self.derivative += derivative_alpha * (raw_derivative - self.derivative)

        dynamic_cutoff = self.min_cutoff_hz + self.beta * abs(self.derivative)
        value_alpha = _alpha(dynamic_cutoff, dt)
        self.value += value_alpha * (x - self.value)
        self.timestamp_ms = now
        return self.value


class HeightMotionFilter:
    """Dedicated right-hand continuous-control pipeline.

    Discrete gesture hysteresis and continuous position filtering intentionally
    remain separate. This prevents recognition hold/release timing from adding
    latency to parameter automation while still giving the parameter source a
    stable, musically usable signal.
    """

    def __init__(self, *, min_cutoff_hz: float = 2.5, beta: float = 1.2,
                 derivative_cutoff_hz: float = 1.0):
        self.filter = OneEuroFilter(
            min_cutoff_hz=min_cutoff_hz,
            beta=beta,
            derivative_cutoff_hz=derivative_cutoff_hz,
        )

    def reset(self) -> None:
        self.filter.reset()

    def update(self, raw_height: float, timestamp_ms: int) -> float:
        raw = max(0.0, min(1.0, float(raw_height)))
        return max(0.0, min(1.0, self.filter.update(raw, timestamp_ms)))
