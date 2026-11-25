#pragma once
#include <chrono>
#include <mutex>
#include <atomic>
#include <climits>

namespace advancedfx {
// Shared console/UI integer setting: 0 is unlimited, every positive int is valid.
inline bool ParseRecordingPreviewFps(const char *text, unsigned &fps) {
    if (!text || !*text) return false;
    unsigned result=0;
    for (; *text; ++text) {
        if (*text<'0' || *text>'9') return false;
        const unsigned digit=static_cast<unsigned>(*text-'0');
        if (result>(static_cast<unsigned>(INT_MAX)-digit)/10) return false;
        result=result*10+digit;
    }
    fps=result; return true;
}
// Wall-clock presentation cadence only. Never sleeps or changes capture scheduling.
class RecordingPreviewLimiter {
public:
    using Clock = std::chrono::steady_clock;
    enum Bypass : unsigned { UnsupportedRecording=1, WindowUnconfirmed=2, SpecialPresent=4,
        AfterSwap=8, ReShade=16, ExternalInterop=32 };
    struct Stats {
        unsigned Fps=0;
        unsigned long long Presented=0, Skipped=0, Bypassed=0, Blocked=0;
        unsigned long long Reasons[6]={};
        unsigned long long Completed=0, Fitted=0, Failed=0;
        double PresentMs=0;
    };
    struct Measurement { unsigned long long Generation=0; Clock::time_point Start{}; };
    void Begin(unsigned fps, bool eligible=true) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        ++m_Generation; m_Stats={}; m_Stats.Fps=fps; m_Eligible=eligible;
        m_HaveDeadline=false; m_Active.store(true);
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
    void NoteBlocked() {
        if(!Active()) return;
        std::lock_guard<std::mutex> lock(m_Mutex);
        if(Active()) ++m_Stats.Blocked;
    }
    bool ShouldSkip(unsigned reasons, Clock::time_point now=Clock::now()) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!Active()) return false;
        if (!m_Stats.Fps) { ++m_Stats.Presented; return false; }
        if (!m_Eligible) reasons|=UnsupportedRecording;
        if (reasons) {
            ++m_Stats.Bypassed;
            for(unsigned i=0;i<6;++i) if(reasons&(1u<<i)) ++m_Stats.Reasons[i];
            m_HaveDeadline=false; return false;
        }
        if (m_HaveDeadline && now<m_Deadline) { ++m_Stats.Skipped; return true; }
        ++m_Stats.Presented;
        m_Deadline=now+std::chrono::ceil<Clock::duration>(std::chrono::duration<double>(1.0/m_Stats.Fps));
        m_HaveDeadline=true; return false;
    }
    Measurement StartPresent() {
        if(!Active()) return {};
        std::lock_guard<std::mutex> lock(m_Mutex);
        return Active() ? Measurement{m_Generation,Clock::now()} : Measurement{};
    }
    void FinishPresent(Measurement measurement, bool fitted, bool failed, Clock::time_point now=Clock::now()) {
        if(!measurement.Generation) return;
        std::lock_guard<std::mutex> lock(m_Mutex);
        if(!Active() || measurement.Generation!=m_Generation) return;
        ++m_Stats.Completed;
        if(fitted) ++m_Stats.Fitted;
        if(failed) ++m_Stats.Failed;
        m_Stats.PresentMs+=std::chrono::duration<double,std::milli>(now-measurement.Start).count();
    }
private:
    std::mutex m_Mutex;
    std::atomic<bool> m_Active{false};
    Stats m_Stats{};
    unsigned long long m_Generation=0;
    Clock::time_point m_Deadline{};
    bool m_HaveDeadline=false, m_Eligible=false;
};
inline RecordingPreviewLimiter &RecordingPreview() { static RecordingPreviewLimiter instance; return instance; }
}
