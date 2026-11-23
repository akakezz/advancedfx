from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = r'''
#include "shared/DemoRenderClockStabilizer.h"
#include <cassert>
#include <cmath>
#include <limits>
int main()
{
    advancedfx::DemoRenderClockStabilizer clock;
    constexpr double tick = 1.0 / 66.0;
    const double start = 100.0, offset = 580.0;
    double corrected = 0;
    assert(!clock.Evaluate(start, start+offset, tick, corrected));
    assert(clock.HasAnchor());
    for (int i=1; i<=100; ++i)
    {
        const double demo = start + tick*i;
        const double raw = demo + offset - (i%3==0 ? 2*tick : tick);
        assert(clock.Evaluate(demo, raw, tick, corrected));
        assert(std::abs(corrected - (demo+offset)) < 1e-8);
    }
    assert(!clock.Evaluate(start-20, start-20+offset, tick, corrected));
    assert(clock.HasAnchor());
    assert(!clock.Evaluate(start+3, start+3+offset, tick, corrected));
    assert(!clock.Evaluate(start+3+tick, start+3+offset-9*tick, tick, corrected));
    assert(!clock.Evaluate(start+3+2*tick, start+3+offset, 0, corrected));
    assert(!clock.HasAnchor());
    assert(!clock.Evaluate(std::numeric_limits<double>::quiet_NaN(),0,tick,corrected));
}
'''
with tempfile.TemporaryDirectory(prefix="hlae-demo-render-clock-") as d:
    cpp=Path(d)/"clock.cpp"
    binary=Path(d)/"clock_test"
    cpp.write_text(source, encoding="utf8")
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-I",str(root),str(cpp),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("Demo render clock reset, offset mapping and safety bounds passed.")
