#include "shared/RecordingPreviewLimiter.h"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace std::chrono;
int main() {
    advancedfx::RecordingPreviewLimiter limiter;
    using Clock=advancedfx::RecordingPreviewLimiter::Clock;
    const auto zero=Clock::time_point{};
    assert(!limiter.Active() && !limiter.ShouldSkip(true,zero));
    limiter.Begin(15);
    assert(limiter.Active());
    assert(!limiter.ShouldSkip(true,zero));
    assert(limiter.ShouldSkip(true,zero+milliseconds(1)));
    assert(limiter.ShouldSkip(true,zero+milliseconds(66)));
    assert(!limiter.ShouldSkip(true,zero+milliseconds(67)));
    // Ineligible calls present normally, reset cadence and never starve recovery.
    assert(!limiter.ShouldSkip(false,zero+milliseconds(68)));
    assert(!limiter.ShouldSkip(true,zero+milliseconds(69)));
    limiter.Invalidate();
    assert(!limiter.ShouldSkip(true,zero+milliseconds(70)));
    auto stats=limiter.End();
    assert(stats.Fps==15 && stats.Presented==4 && stats.Skipped==2 && stats.Bypassed==1);
    assert(!limiter.Active() && !limiter.ShouldSkip(true,zero+milliseconds(71)));
    limiter.Begin(30);
    assert(!limiter.ShouldSkip(true,zero));
    assert(limiter.ShouldSkip(true,zero+milliseconds(33)));
    assert(!limiter.ShouldSkip(true,zero+milliseconds(34)));
    stats=limiter.End(); assert(stats.Fps==30 && stats.Presented==2 && stats.Skipped==1);
    limiter.Begin(0); assert(!limiter.Active()&&!limiter.ShouldSkip(true,zero));
    assert(limiter.End().Fps==0);
    // Sparse rendering should not be slowed by the cap, even with a long gap.
    limiter.Begin(15);
    for(int i=0;i<100;++i) assert(!limiter.ShouldSkip(true,zero+milliseconds(i*100)));
    assert(limiter.End().Skipped==0);
    std::cout<<"Preview cadence, bypass, reset, stop and sparse frames passed\n";
}
