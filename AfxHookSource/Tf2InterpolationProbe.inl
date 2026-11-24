// TF2 native interpolation A/B probe for two verified client builds.
// Interp_Interpolate is inlined into BaseInterpolatePart1 in both binaries.
// Limit the return-value experiment to its one verified predicted-time call site.
#include <intrin.h>
#include <shared/Tf2EffectiveInterpolationClock.h>
namespace
{
#ifdef _WIN64
using Tf2BaseInterp = int (*)(void *, float *, void *, void *, void *, int *);
constexpr size_t Tf2InterpRva = 0x1de9c0, Tf2MapTimeOffset = 0x4c, Tf2OriginOffset = 0x368;
using Tf2PredictedTime = float (*)(void *);
constexpr size_t Tf2PredictedRva = 0x1f4120, Tf2PredictedReturnRva = 0x1dea98;
#else
using Tf2BaseInterp = int (__fastcall *)(void *, void *, float *, void *, void *, void *, int *);
constexpr size_t Tf2InterpRva = 0x1c79b0, Tf2MapTimeOffset = 0x2c, Tf2OriginOffset = 0x2bc;
using Tf2PredictedTime = float (__fastcall *)(void *, void *);
constexpr size_t Tf2PredictedRva = 0x1d9990, Tf2PredictedReturnRva = 0x1c7a55;
#endif
Tf2BaseInterp g_Tf2BaseInterp = nullptr;
Tf2PredictedTime g_Tf2PredictedTime = nullptr;
HMODULE g_Tf2InterpModule = nullptr;
DWORD g_Tf2InterpThread = 0;
struct Tf2InterpRow
{
    int Tick, Frame, Result, NoMoreChanges;
    double DemoTime, GlobalTime, Fraction;
    float Input, Effective, PreviousMapTime, MapTime;
    float Before[3], After[3];
    float TickInterval = 0;
    bool StableMode = false, PredictedCall = false, CorrectionApplied = false;
    float RawPredicted = std::numeric_limits<float>::quiet_NaN();
    float ReturnedPredicted = std::numeric_limits<float>::quiet_NaN();
    float RawEffective = std::numeric_limits<float>::quiet_NaN();
    float TargetEffective = std::numeric_limits<float>::quiet_NaN();
};
struct Tf2InterpCapture
{
    std::atomic<bool> Active{false};
    void *Target = nullptr;
    int Entity = 0, User = 0, LastTick = -1;
    bool Stabilize = false;
    unsigned PredictedCalls = 0, Corrections = 0, Verified = 0, Mismatches = 0;
    advancedfx::Tf2EffectiveInterpolationClock Clock;
    std::vector<Tf2InterpRow> Rows;
} g_Tf2InterpCapture;
constexpr size_t Tf2InterpLimit = 65536;

// The pointer is valid only during the original BaseInterpolatePart1 call.
// Nested calls (including unselected entities) mask and restore this context.
thread_local Tf2InterpRow *g_Tf2InterpContext = nullptr;
struct Tf2InterpScope
{
    Tf2InterpRow *Previous;
    explicit Tf2InterpScope(Tf2InterpRow *row) : Previous(g_Tf2InterpContext)
    { g_Tf2InterpContext = row; }
    ~Tf2InterpScope() { g_Tf2InterpContext = Previous; }
};

__declspec(noinline) float __fastcall Tf2ObservePredictedTime(void *self
#ifndef _WIN64
    , void *edx
#endif
)
{
    const void *caller = _ReturnAddress();
    const float raw = g_Tf2PredictedTime(self
#ifndef _WIN64
        , edx
#endif
    );
    auto *row = g_Tf2InterpContext;
    auto &p = g_Tf2InterpCapture;
    if (!row || row->PredictedCall || !p.Active.load() || !g_Tf2InterpModule ||
        GetCurrentThreadId() != g_Tf2InterpThread ||
        caller != reinterpret_cast<const unsigned char *>(g_Tf2InterpModule) + Tf2PredictedReturnRva)
        return raw;
    row->PredictedCall = true;
    row->RawPredicted = raw;
    const auto decision = p.Clock.Evaluate(row->DemoTime, raw, row->TickInterval,
                                            static_cast<float>(row->Fraction), row->StableMode && !MirvFix_GetDemoRenderClock());
    row->RawEffective = decision.RawEffective;
    row->TargetEffective = decision.TargetEffective;
    row->ReturnedPredicted = decision.ReturnedPredicted;
    row->CorrectionApplied = decision.Applied;
    return decision.ReturnedPredicted;
}

int __fastcall Tf2ObserveBaseInterp(void *self,
#ifndef _WIN64
    void *edx,
#endif
    float *time, void *origin, void *angles, void *velocity, int *noMore)
{
    auto &p = g_Tf2InterpCapture;
    bool record = false;
    Tf2InterpRow row{};
    if (!g_Tf2InterpContext && p.Active.load() &&
        GetCurrentThreadId() == g_Tf2InterpThread && self == p.Target)
    {
        if (!g_VEngineClient || !g_VEngineClient->IsPlayingDemo() ||
            g_VEngineClient->IsSkippingDemo() || g_VEngineClient->IsDrawingLoadingImage())
            p.Active.store(false);
        else if (!g_VEngineClient->IsPaused() && time && noMore)
        {
            int tick = -1;
            if (!GetCurrentDemoTick(tick) || tick < p.LastTick)
                p.Active.store(false);
            else
            {
                p.LastTick = tick;
                auto *gl = g_Hook_VClient_RenderView.GetGlobals();
                double demoTime = 0;
                if (gl && GetCurrentDemoTime(demoTime) && p.Rows.size() < Tf2InterpLimit)
                {
                    row.Tick = tick;
                    row.Frame = gl->framecount_get();
                    row.DemoTime = demoTime;
                    row.GlobalTime = gl->curtime_get();
                    row.Fraction = gl->interpolation_amount_get();
                    row.Input = *time;
                    row.TickInterval = gl->interval_per_tick_get();
                    row.StableMode = p.Stabilize;
                    // Both offsets are verified against this binary's native loop:
                    // last interpolation time and local origin.
                    const auto *bytes = static_cast<const unsigned char *>(self);
                    std::memcpy(&row.PreviousMapTime, bytes + Tf2MapTimeOffset, sizeof(float));
                    std::memcpy(row.Before, bytes + Tf2OriginOffset, sizeof(row.Before));
                    record = true;
                }
            }
        }
    }
    int result;
    {
        Tf2InterpScope scope(record ? &row : nullptr);
        result = g_Tf2BaseInterp(self,
#ifndef _WIN64
            edx,
#endif
            time, origin, angles, velocity, noMore);
    }
    if (record && p.Active.load() && p.Rows.size() < Tf2InterpLimit)
    {
        row.Result = result;
        row.NoMoreChanges = *noMore;
        row.Effective = *time;
        const auto *bytes = static_cast<const unsigned char *>(self);
        std::memcpy(&row.MapTime, bytes + Tf2MapTimeOffset, sizeof(float));
        std::memcpy(row.After, bytes + Tf2OriginOffset, sizeof(row.After));
        if (row.PredictedCall)
        {
            ++p.PredictedCalls;
            if (row.CorrectionApplied) ++p.Corrections;
            // A successful native interpolation evaluation must agree with
            // both the caller's time reference and the map timestamp.
            if (result && std::isfinite(row.Effective) && std::isfinite(row.MapTime) &&
                std::abs(double(row.Effective) - row.TargetEffective) <= 0.0002 &&
                std::abs(double(row.MapTime) - row.TargetEffective) <= 0.0002)
                ++p.Verified;
            else if (row.CorrectionApplied)
            {
                ++p.Mismatches;
                // Stop altering motion if the native path does not behave
                // as expected. Keep the trace and original detours usable.
                p.Stabilize = false;
                p.Clock.Reset();
            }
        }
        p.Rows.push_back(row);
        if (p.Rows.size() == Tf2InterpLimit) p.Active.store(false);
    }
    return result;
}

bool Tf2VerifiedInterpImage(HMODULE &module)
{
    module = GetModuleHandleW(L"client.dll");
    if (!module) return false;
    auto *base = reinterpret_cast<const unsigned char *>(module);
    IMAGE_DOS_HEADER dos{};
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), base, &dos, sizeof(dos), &read) ||
        read != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew > 65536) return false;
    IMAGE_NT_HEADERS nt{};
    if (!ReadProcessMemory(GetCurrentProcess(), base + dos.e_lfanew, &nt, sizeof(nt), &read) ||
        read != sizeof(nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
         
#ifdef _WIN64
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.FileHeader.TimeDateStamp != 0x6ad26f17 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.SizeOfImage != 0x133a000
#else
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt.FileHeader.TimeDateStamp != 0x6ad26f10 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt.OptionalHeader.SizeOfImage != 0xea6000
#endif
        ) return false;
    const auto matches = [&](size_t rva, const unsigned char *expected, size_t size)
    {
        unsigned char actual[32]{};
        if (size > sizeof(actual)) return false;
        SIZE_T got = 0;
        return ReadProcessMemory(GetCurrentProcess(), base + rva, actual, size, &got) &&
               got == size && !std::memcmp(actual, expected, size);
    };
 #ifdef _WIN64
    const unsigned char entry[] = {0x48,0x89,0x5c,0x24,0x18,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x30,0x4c,0x8b,0xbc,0x24,0x88,0x00,0x00};
    const unsigned char predicted[] = {0x48,0x8b,0xc8,0xe8,0x88,0x56,0x01,0x00,0xf3,0x0f,0x11,0x07,0x48,0x8b,0x05,0xad,0xa0,0xdf,0x00,0xf3,0x0f,0x5c,0x40,0x1c};
    const unsigned char loop[] = {0xf3,0x0f,0x10,0x37,0x0f,0x2f,0x73,0x4c,0x73,0x29,0x41,0x8b,0xd4,0x39,0x53,0x48,0x7e,0x21,0x66,0x66,0x0f,0x1f,0x84,0x00};
    const unsigned char watcher[] = {0x48,0x8b,0x4c,0xf5,0x10,0x0f,0x28,0xce,0x48,0x8b,0x01,0xff,0x50,0x30,0x85,0xc0};
    const unsigned char predictedFunction[] = {0x66,0x0f,0x6e,0x81,0x24,0x17,0x00,0x00,
        0x48,0x8b,0x05,0x21,0x4a,0xde,0x00,0x0f,0x5b,0xc0,0xf3,0x0f,0x59,0x40,0x1c,0xc3};
    return matches(Tf2PredictedRva, predictedFunction, sizeof(predictedFunction)) &&
           matches(0x1de9c0, entry, sizeof(entry)) &&
           matches(0x1dea90, predicted, sizeof(predicted)) &&
           matches(0x1deb24, loop, sizeof(loop)) &&
           matches(0x1deb82, watcher, sizeof(watcher));
#else
    // All ranges exclude absolute addresses subject to PE relocation.
    const unsigned char entry[] = {0x55,0x8b,0xec,0x8b,0x45,0x18,0x56,0x8b,0xf1,0xc7,0x00,0x01,0x00,0x00,0x00,0xf6,0x46,0x7c,0x01,0x74,0x40,0x80,0xbe,0xa4};
    const unsigned char predicted[] = {0x8b,0xca,0xe8,0x3b,0x1f,0x01,0x00,0xd9,0x55,0x08,0xf3,0x0f,0x10,0x4d,0x08,0xd9,0x1f};
    const unsigned char loop[] = {0xf3,0x0f,0x10,0x0f,0x8b,0xf9,0xf3,0x0f,0x10,0x46,0x2c,0x0f,0x2f,0xc1,0xf3,0x0f,0x11,0x4d,0x08,0x76,0x1c,0x33,0xd2,0x39};
    const unsigned char watcher[] = {0x8b,0x4f,0x08,0x51,0xd9,0x1c,0x24,0x8b,0x01,0xff,0x50,0x18,0xd9,0x45,0x08,0x85};
    const unsigned char predictedFunctionStart[] = {0xdb,0x81,0x58,0x12,0x00,0x00,0xa1};
    const unsigned char predictedFunctionEnd[] = {0xd8,0x48,0x1c,0xc3};
    return matches(Tf2PredictedRva, predictedFunctionStart, sizeof(predictedFunctionStart)) &&
           matches(Tf2PredictedRva + 11, predictedFunctionEnd, sizeof(predictedFunctionEnd)) &&
           matches(0x1c79b0, entry, sizeof(entry)) &&
           matches(0x1c7a4e, predicted, sizeof(predicted)) &&
           matches(0x1c7ae3, loop, sizeof(loop)) &&
           matches(0x1c7b35, watcher, sizeof(watcher));
#endif
}

bool Tf2InstallInterpProbe()
{
    if (g_Tf2BaseInterp)
        return g_Tf2PredictedTime && GetCurrentThreadId() == g_Tf2InterpThread &&
               GetModuleHandleW(L"client.dll") == g_Tf2InterpModule;
    HMODULE module = nullptr;
    if (!Tf2VerifiedInterpImage(module)) return false;
    auto *address = reinterpret_cast<unsigned char *>(module) + Tf2InterpRva;
    g_Tf2BaseInterp = reinterpret_cast<Tf2BaseInterp>(address);
    g_Tf2PredictedTime = reinterpret_cast<Tf2PredictedTime>(
        reinterpret_cast<unsigned char *>(module) + Tf2PredictedRva);
    LONG error = DetourTransactionBegin();
    if (error == NO_ERROR)
    {
        error = DetourUpdateThread(GetCurrentThread());
        if (error == NO_ERROR)
            error = DetourAttach(reinterpret_cast<PVOID *>(&g_Tf2BaseInterp),
                                 reinterpret_cast<PVOID>(Tf2ObserveBaseInterp));
        if (error == NO_ERROR)
            error = DetourAttach(reinterpret_cast<PVOID *>(&g_Tf2PredictedTime),
                                 reinterpret_cast<PVOID>(Tf2ObservePredictedTime));
        if (error == NO_ERROR) error = DetourTransactionCommit();
        else DetourTransactionAbort();
    }
    if (error != NO_ERROR)
    {
        g_Tf2BaseInterp = nullptr;
        g_Tf2PredictedTime = nullptr;
        return false;
    }
    g_Tf2InterpModule = module;
    g_Tf2InterpThread = GetCurrentThreadId();
    return true;
}
}
 
// Called at TF2 frame/network boundaries even if the selected entity receives
// no native interpolation calls. Disarm on a demo change or entity replacement.
void MirvInterpProbeFrameGuard()
{
    auto &p = g_Tf2InterpCapture;
    if (!p.Active.load() || GetCurrentThreadId() != g_Tf2InterpThread) return;
    int tick = -1, user = 0;
    char name[64]{};
    if (!g_VEngineClient || !g_VEngineClient->IsPlayingDemo() ||
        g_VEngineClient->IsSkippingDemo() || g_VEngineClient->IsDrawingLoadingImage() ||
        !GetCurrentDemoTick(tick) || (p.LastTick >= 0 &&
            (tick < p.LastTick || tick - p.LastTick > 32)) ||
        Mirv_Tf2_DiagnosticEntity(p.Entity) != p.Target ||
        !Mirv_Tf2_GetPlayerInfo(p.Entity, name, sizeof(name), &user) || user != p.User)
    {
        p.Active.store(false);
        p.Stabilize = false;
        p.Clock.Reset();
        return;
    }
    if (g_VEngineClient->IsPaused()) p.Clock.Reset();
    if (p.Stabilize && MirvFix_GetDemoRenderClock())
    {
        p.Stabilize = false;
        p.Clock.Reset();
    }
}

CON_COMMAND(mirv_interp_probe, "Native interpolation timing A/B experiment, verified TF2 builds")
{
    auto &p = g_Tf2InterpCapture;
    const int argc = args->ArgC();
    const char *cmd = argc > 1 ? args->ArgV(1) : "status";
    if (!_stricmp(cmd, "start") && (argc == 3 || argc == 4))
    {
        const bool stable = argc == 4 && !_stricmp(args->ArgV(3), "stable");
        if (argc == 4 && !stable && _stricmp(args->ArgV(3), "native"))
        {
            Tier0_Warning("Choose native or stable. Native is the read-only control.\n");
            return;
        }
        if (stable && MirvFix_GetDemoRenderClock())
        {
            Tier0_Warning("Disable mirv_fix demoRenderClock before this targeted experiment.\n");
            return;
        }
        char *end = nullptr;
        const long entity = std::strtol(args->ArgV(2), &end, 10);
        if (g_SourceSdkVer != SourceSdkVer_TF2 || !g_VEngineClient ||
            !g_VEngineClient->IsPlayingDemo() || !g_VEngineClient->IsPaused() ||
            g_VEngineClient->IsSkippingDemo() || g_VEngineClient->IsDrawingLoadingImage() ||
            p.Active.load() || end == args->ArgV(2) || *end || entity < 1 || entity > 128)
        {
            Tier0_Warning("Pause a loaded TF2 demo and select a player entity index. Stop any previous capture first.\n");
            return;
        }
        void *target = Mirv_Tf2_DiagnosticEntity(int(entity));
        int user = 0;
        char name[64]{};
        if (!target || !Mirv_Tf2_GetPlayerInfo(int(entity), name, sizeof(name), &user) || user <= 0 ||
            !Tf2InstallInterpProbe())
        {
            Tier0_Warning("Player unavailable or client.dll does not match the verified build; no capture started.\n");
            return;
        }
        p.Rows.clear();
        p.Rows.reserve(Tf2InterpLimit);
        p.Target = target;
        p.Entity = int(entity);
        p.User = user;
        p.LastTick = -1;
        p.Stabilize = stable;
        p.PredictedCalls = p.Corrections = p.Verified = p.Mismatches = 0;
        p.Clock.Reset();
        p.Active.store(true);
        Tier0_Msg("Native interpolation %s capture armed for player %d. Resume; stop before seeking/reloading.\n",
                  stable ? "STABLE (experimental)" : "NATIVE (control)", p.Entity);
        return;
    }
    if (!_stricmp(cmd, "stop") && argc == 2)
    {
        p.Active.store(false);
        p.Stabilize = false;
        p.Clock.Reset();
        Tier0_Msg("Capture stopped; %u matched calls retained.\n", unsigned(p.Rows.size()));
        return;
    }
    if (!_stricmp(cmd, "save") && argc == 3)
    {
        if (p.Active.load() || p.Rows.empty())
        {
            Tier0_Warning("Stop capture first; at least one matched call is required.\n");
            return;
        }
        try
        {
            auto path = std::filesystem::u8path(args->ArgV(2));
            if (std::filesystem::exists(path))
            {
                Tier0_Warning("Output already exists; choose a new filename.\n");
                return;
            }
            std::ofstream out(path, std::ios::binary);
            out.imbue(std::locale::classic());
            out.precision(17);
            out << "tick,frame,entity,demo_time,global_curtime,interpolation_fraction,input_time,effective_time,"
                   "previous_map_time,map_time,result,no_more_changes,before_x,before_y,before_z,after_x,after_y,after_z,"
                   "stable_mode,predicted_call,correction_applied,tick_interval,raw_predicted_time,"
                   "returned_predicted_time,raw_effective_time,target_effective_time\n";
            for (const auto &r : p.Rows)
            {
                out << r.Tick << ',' << r.Frame << ',' << p.Entity << ',' << r.DemoTime << ','
                    << r.GlobalTime << ',' << r.Fraction << ',' << r.Input << ',' << r.Effective << ','
                    << r.PreviousMapTime << ',' << r.MapTime << ',' << r.Result << ',' << r.NoMoreChanges;
                for (float v : r.Before) out << ',' << v;
                for (float v : r.After) out << ',' << v;
                out << ',' << int(r.StableMode) << ',' << int(r.PredictedCall) << ','
                    << int(r.CorrectionApplied) << ',' << r.TickInterval << ','
                    << r.RawPredicted << ',' << r.ReturnedPredicted << ','
                    << r.RawEffective << ',' << r.TargetEffective << '\n';
            }
            out.close();
            if (!out) Tier0_Warning("Failed writing native interpolation CSV.\n");
            else Tier0_Msg("Saved %u native interpolation calls.\n", unsigned(p.Rows.size()));
        }
        catch (...) { Tier0_Warning("Unable to save native interpolation CSV.\n"); }
        return;
    }
    HMODULE module = nullptr;
    Tier0_Msg("Native interpolation probe: installed %d, capturing %d, selected entity %d, matched calls %u.\n",
              int(g_Tf2BaseInterp != nullptr), int(p.Active.load()), p.Entity, unsigned(p.Rows.size()));
    if (!g_Tf2BaseInterp)
        Tier0_Msg("Matching client image: %d.\n", int(Tf2VerifiedInterpImage(module)));
    Tier0_Msg("Mode: %s; predicted calls %u, corrections %u, verified map evaluations %u, mismatches %u.\n",
              p.Stabilize ? "STABLE" : "NATIVE/OFF", p.PredictedCalls, p.Corrections,
              p.Verified, p.Mismatches);
    Tier0_Msg("mirv_interp_probe start <entity> [native|stable] | stop | save <new.csv> | status\n"
              "Stable is experimental and affects only the selected entity's verified interpolation call. "
              "Stop before seeking/reloading; restart TF2 to remove installed detours.\n");
}
