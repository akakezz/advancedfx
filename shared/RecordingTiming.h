#pragma once
#include "RecordingLog.h"
#include <chrono>
#include <cstdio>

namespace advancedfx { namespace RecordingTiming {
using Clock = std::chrono::steady_clock;
struct Counter {
    unsigned long long Calls=0;
    Clock::duration Total{};
    void Add(Clock::time_point start) noexcept { ++Calls; Total += Clock::now()-start; }
    double Milliseconds() const noexcept { return std::chrono::duration<double,std::milli>(Total).count(); }
};
// Each counter is owned by one sequential capture/stream worker. Log only
// once at teardown; no per-sample log allocation or console call from workers.
struct Scope {
    Counter &Value;
    Clock::time_point Start=Clock::now();
    ~Scope() { Value.Add(Start); }
};
inline void Report(const char *source, const char *message) noexcept {
    try { RecordingLog::Get().Append(source,message,std::char_traits<char>::length(message)); }
    catch (...) { /* Diagnostics must not change teardown behavior. */ }
}
} }
