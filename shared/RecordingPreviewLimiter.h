#pragma once
#include <chrono>
#include <mutex>
#include <atomic>

namespace advancedfx {
// Wall-clock presentation cadence only. Never sleeps, changes host_framerate,
// or participates in capture/sample scheduling. Shared between engine and draw threads.
class RecordingPreviewLimiter {
public:
    using Clock = std::chrono::steady_clock;
    struct Stats { unsigned Fps=0; unsigned long long Presented=0, Skipped=0, Bypassed=0; };
    void Begin(unsigned fps) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Stats={fps,0,0,0}; m_HaveDeadline=false; m_Active.store(fps!=0);
    }
    Stats End() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto result=m_Stats; m_Stats={}; m_HaveDeadline=false; m_Active.store(false); return result;
    }
    bool Active() const { return m_Active.load(); }
    void Invalidate() {
        if(!Active()) return;
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_HaveDeadline=false;
    }
    bool ShouldSkip(bool eligible, Clock::time_point now=Clock::now()) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_Stats.Fps) return false;
        if (!eligible) { ++m_Stats.Bypassed; m_HaveDeadline=false; return false; }
        if (m_HaveDeadline && now<m_Deadline) { ++m_Stats.Skipped; return true; }
        ++m_Stats.Presented;
        m_Deadline=now+std::chrono::ceil<Clock::duration>(std::chrono::duration<double>(1.0/m_Stats.Fps));
        m_HaveDeadline=true; return false;
    }
private:
    std::mutex m_Mutex;
    std::atomic<bool> m_Active{false};
    Stats m_Stats{};
    Clock::time_point m_Deadline{};
    bool m_HaveDeadline=false;
};
inline RecordingPreviewLimiter &RecordingPreview() { static RecordingPreviewLimiter instance; return instance; }
}
