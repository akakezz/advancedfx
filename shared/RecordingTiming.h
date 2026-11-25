#pragma once
#include "RecordingLog.h"
#include <chrono>
#include <atomic>
#include <cstdio>

namespace advancedfx { namespace RecordingTiming {
using Clock = std::chrono::steady_clock;
struct Counter {
    unsigned long long Calls=0;
    Clock::duration Total{};
    void Add(Clock::time_point start) noexcept { ++Calls; Total += Clock::now()-start; }
    double Milliseconds() const noexcept { return std::chrono::duration<double,std::milli>(Total).count(); }
};
// CPU locks can be requested by different capture consumers. Aggregate them
// separately with atomic counters; read only after all capture references drain.
struct AtomicCounter {
    std::atomic<unsigned long long> Calls{0};
    std::atomic<long long> Nanoseconds{0};
    void Add(Clock::time_point start) noexcept {
        Nanoseconds.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count(),std::memory_order_relaxed);
        Calls.fetch_add(1,std::memory_order_relaxed);
    }
    double Milliseconds() const noexcept { return Nanoseconds.load(std::memory_order_relaxed)/1e6; }
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
