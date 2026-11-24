#pragma once
#include "DemoRenderClockStabilizer.h"

namespace advancedfx
{
// Match the native SSE sequence: (predicted - tick) + fraction * tick.
// Correct only the return value for the verified interpolation call site.
struct Tf2EffectiveInterpSample
{
    float RawEffective = 0, TargetEffective = 0, ReturnedPredicted = 0;
    bool Applied = false;
};
inline float Tf2EffectiveInterpTime(float predicted, float tick, float fraction)
{
    const float base = predicted - tick;
    const float part = fraction * tick;
    return base + part;
}
class Tf2EffectiveInterpolationClock
{
public:
    void Reset() { m_Clock.Reset(); }
    Tf2EffectiveInterpSample Evaluate(double demoTime, float predicted,
                                     float tick, float fraction, bool enabled)
    {
        Tf2EffectiveInterpSample r;
        r.ReturnedPredicted = predicted;
        r.RawEffective = r.TargetEffective = Tf2EffectiveInterpTime(predicted, tick, fraction);
        if (!std::isfinite(demoTime) || !std::isfinite(predicted) ||
            !std::isfinite(tick) || tick <= 0 || tick > 1 ||
            !std::isfinite(fraction) || fraction < 0 || fraction > 1)
        {
            Reset();
            return r;
        }
        if (!enabled) { Reset(); return r; }
        double target = 0;
        if (!m_Clock.Evaluate(demoTime, r.RawEffective, tick, target)) return r;
        const double shift = target - r.RawEffective;
        const float adjusted = static_cast<float>(double(predicted) + shift);
        const float effective = Tf2EffectiveInterpTime(adjusted, tick, fraction);
        if (!std::isfinite(adjusted) || !std::isfinite(effective) ||
            std::abs(double(effective) - r.RawEffective) > 2.5 * tick + 0.0001 ||
            std::abs(double(effective) - target) > 0.0002)
        {
            Reset();
            return r;
        }
        r.ReturnedPredicted = adjusted;
        r.TargetEffective = effective;
        r.Applied = adjusted != predicted;
        return r;
    }
private:
    DemoRenderClockStabilizer m_Clock;
};
}
