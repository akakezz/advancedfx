from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = r'''
#include "shared/Tf2EffectiveInterpolationClock.h"
#include <cassert>
#include <cmath>
#include <limits>
using namespace advancedfx;
int main()
{
    constexpr float tick = .015f;
    Tf2EffectiveInterpolationClock clock;
    // A real-valued demo clock; predicted ticks can vary independently by
    // one simulation tick, as in the original native interpolation trace.
    float previous = 0;
    unsigned corrections = 0, backwards = 0;
    for (int i = 0; i < 180; ++i)
    {
        const double demo = (354 + i * .8) * double(tick);
        const int demoTick = int(std::floor(demo / tick));
        const float fraction = float(demo / tick - demoTick);
        const float predicted = (demoTick + 37853 + (i % 3 == 0 ? 0 : -1)) * tick;
        const auto sample = clock.Evaluate(demo, predicted, tick, fraction, true);
        assert(sample.RawEffective == Tf2EffectiveInterpTime(predicted, tick, fraction));
        assert(sample.TargetEffective == Tf2EffectiveInterpTime(sample.ReturnedPredicted, tick, fraction));
        assert(std::abs(double(sample.TargetEffective) - sample.RawEffective) <= 2.5 * tick + .0001);
        if (sample.Applied) ++corrections;
        if (i && sample.TargetEffective < previous) ++backwards;
        previous = sample.TargetEffective;
    }
    assert(corrections > 100);
    assert(backwards == 0);
    // Native mode must never alter the original return value.
    const auto native = clock.Evaluate(100, 700, tick, .5f, false);
    assert(!native.Applied && native.ReturnedPredicted == 700);
    assert(native.TargetEffective == native.RawEffective);
    // Entering a mode, a seek or a large offset switch anchors but never jumps.
    assert(!clock.Evaluate(101, 701, tick, .5f, true).Applied);
    assert(!clock.Evaluate(80, 680, tick, .5f, true).Applied);
    assert(!clock.Evaluate(80.015, 681, tick, .5f, true).Applied);
    assert(!clock.Evaluate(90, 691, tick, .5f, true).Applied);
    assert(!clock.Evaluate(90.015, 691, tick, 2.0f, true).Applied);
    assert(!clock.Evaluate(90.03, 691, tick, .5f, true).Applied);
    assert(!clock.Evaluate(90.045, 691, 0, .5f, true).Applied);
    assert(!clock.Evaluate(90.06, std::numeric_limits<float>::quiet_NaN(), tick, .5f, true).Applied);
}
'''
with tempfile.TemporaryDirectory(prefix="hlae-tf2-effective-clock-") as d:
    cpp = Path(d) / "test.cpp"
    binary = Path(d) / "test"
    cpp.write_text(source, encoding="utf8")
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-ffp-contract=off", "-I", str(root), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("TF2 native effective interpolation clock mapping, passthrough and discontinuity tests passed.")
