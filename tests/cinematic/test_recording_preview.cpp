#include "shared/RecordingPreviewLimiter.h"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace std::chrono;
int main() {
    using Preview=advancedfx::RecordingPreviewLimiter;
    Preview limiter;
    using Clock=Preview::Clock;
    const auto zero=Clock::time_point{};
    unsigned fps=99;
    for(const char *valid:{"0","1","7","15","30","60","123","2147483647"}) {
        assert(advancedfx::ParseRecordingPreviewFps(valid,fps));
    }
    assert(fps==2147483647u);
    for(const char *invalid:{"","-1","+1","1.5","15fps","2147483648","999999999999999999999"}) {
        assert(!advancedfx::ParseRecordingPreviewFps(invalid,fps));
        assert(fps==2147483647u);
    }
    assert(!limiter.Active() && !limiter.ShouldSkip(0,zero));
    for(unsigned limit:{1u,7u,15u,30u,60u,123u,2147483647u}) {
        limiter.Begin(limit);
        assert(limiter.Active());
        const auto interval=ceil<Clock::duration>(duration<double>(1.0/limit));
        assert(!limiter.ShouldSkip(0,zero));
        assert(limiter.ShouldSkip(0,zero+interval-Clock::duration(1)));
        assert(!limiter.ShouldSkip(0,zero+interval));
        assert(!limiter.ShouldSkip(Preview::AfterSwap|Preview::ReShade,zero+interval));
        assert(!limiter.ShouldSkip(0,zero+interval));
        limiter.Invalidate();
        assert(!limiter.ShouldSkip(0,zero+interval));
        limiter.NoteBlocked();
        auto stats=limiter.End();
        assert(stats.Fps==limit && stats.Presented==4 && stats.Skipped==1 && stats.Bypassed==1 && stats.Blocked==1);
        assert(stats.Reasons[3]==1 && stats.Reasons[4]==1);
        assert(!limiter.Active() && !limiter.ShouldSkip(0,zero));
    }
    limiter.Begin(7,false);
    assert(!limiter.ShouldSkip(0,zero));
    assert(!limiter.ShouldSkip(0,zero));
    auto stats=limiter.End();
    assert(stats.Fps==7 && stats.Bypassed==2 && stats.Reasons[0]==2);
    limiter.Begin(0);
    assert(limiter.Active() && !limiter.ShouldSkip(Preview::SpecialPresent,zero));
    auto measure=limiter.StartPresent();
    limiter.FinishPresent(measure,true,false,measure.Start+milliseconds(3));
    auto stale=limiter.StartPresent();
    stats=limiter.End();
    assert(stats.Fps==0 && stats.Presented==1 && stats.Bypassed==0 && stats.Completed==1 && stats.Fitted==1 && stats.PresentMs==3);
    limiter.Begin(5);
    limiter.FinishPresent(stale,false,true,stale.Start+milliseconds(50));
    measure=limiter.StartPresent();
    limiter.FinishPresent(measure,false,true,measure.Start+milliseconds(2));
    stats=limiter.End();
    assert(stats.Completed==1 && stats.Failed==1 && stats.Fitted==0 && stats.PresentMs==2);
    limiter.Begin(15);
    for(int i=0;i<100;++i) assert(!limiter.ShouldSkip(0,zero+milliseconds(i*100)));
    assert(limiter.End().Skipped==0);
    std::cout<<"Arbitrary FPS, parsing, cadence, bypass reasons, timings, reset and stop passed\n";
}
