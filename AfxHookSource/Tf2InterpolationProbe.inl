// Read-only TF2 interpolation probe for the two verified client builds.
// Interp_Interpolate is inlined into BaseInterpolatePart1 in this binary.
namespace
{
#ifdef _WIN64
using Tf2BaseInterp = int (*)(void *, float *, void *, void *, void *, int *);
constexpr size_t Tf2InterpRva = 0x1de9c0, Tf2MapTimeOffset = 0x4c, Tf2OriginOffset = 0x368;
#else
using Tf2BaseInterp = int (__fastcall *)(void *, void *, float *, void *, void *, void *, int *);
constexpr size_t Tf2InterpRva = 0x1c79b0, Tf2MapTimeOffset = 0x2c, Tf2OriginOffset = 0x2bc;
#endif
Tf2BaseInterp g_Tf2BaseInterp = nullptr;
DWORD g_Tf2InterpThread = 0;
struct Tf2InterpRow
{
    int Tick, Frame, Result, NoMoreChanges;
    double DemoTime, GlobalTime, Fraction;
    float Input, Effective, PreviousMapTime, MapTime;
    float Before[3], After[3];
};
struct Tf2InterpCapture
{
    std::atomic<bool> Active{false};
    void *Target = nullptr;
    int Entity = 0, LastTick = -1;
    std::vector<Tf2InterpRow> Rows;
} g_Tf2InterpCapture;
constexpr size_t Tf2InterpLimit = 65536;

int __fastcall Tf2ObserveBaseInterp(void *self,
#ifndef _WIN64
    void *edx,
#endif
    float *time, void *origin, void *angles, void *velocity, int *noMore)
{
    auto &p = g_Tf2InterpCapture;
    bool record = false;
    Tf2InterpRow row{};
    if (p.Active.load() && GetCurrentThreadId() == g_Tf2InterpThread && self == p.Target)
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
    const int result = g_Tf2BaseInterp(self,
#ifndef _WIN64
        edx,
#endif
        time, origin, angles, velocity, noMore);
    if (record && p.Active.load() && p.Rows.size() < Tf2InterpLimit)
    {
        row.Result = result;
        row.NoMoreChanges = *noMore;
        row.Effective = *time;
        const auto *bytes = static_cast<const unsigned char *>(self);
        std::memcpy(&row.MapTime, bytes + Tf2MapTimeOffset, sizeof(float));
        std::memcpy(row.After, bytes + Tf2OriginOffset, sizeof(row.After));
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
    return matches(0x1de9c0, entry, sizeof(entry)) &&
           matches(0x1dea90, predicted, sizeof(predicted)) &&
           matches(0x1deb24, loop, sizeof(loop)) &&
           matches(0x1deb82, watcher, sizeof(watcher));
#else
    // All ranges exclude absolute addresses subject to PE relocation.
    const unsigned char entry[] = {0x55,0x8b,0xec,0x8b,0x45,0x18,0x56,0x8b,0xf1,0xc7,0x00,0x01,0x00,0x00,0x00,0xf6,0x46,0x7c,0x01,0x74,0x40,0x80,0xbe,0xa4};
    const unsigned char predicted[] = {0x8b,0xca,0xe8,0x3b,0x1f,0x01,0x00,0xd9,0x55,0x08,0xf3,0x0f,0x10,0x4d,0x08,0xd9,0x1f};
    const unsigned char loop[] = {0xf3,0x0f,0x10,0x0f,0x8b,0xf9,0xf3,0x0f,0x10,0x46,0x2c,0x0f,0x2f,0xc1,0xf3,0x0f,0x11,0x4d,0x08,0x76,0x1c,0x33,0xd2,0x39};
    const unsigned char watcher[] = {0x8b,0x4f,0x08,0x51,0xd9,0x1c,0x24,0x8b,0x01,0xff,0x50,0x18,0xd9,0x45,0x08,0x85};
    return matches(0x1c79b0, entry, sizeof(entry)) &&
           matches(0x1c7a4e, predicted, sizeof(predicted)) &&
           matches(0x1c7ae3, loop, sizeof(loop)) &&
           matches(0x1c7b35, watcher, sizeof(watcher));
#endif
}

bool Tf2InstallInterpProbe()
{
    if (g_Tf2BaseInterp) return GetCurrentThreadId() == g_Tf2InterpThread;
    HMODULE module = nullptr;
    if (!Tf2VerifiedInterpImage(module)) return false;
    auto *address = reinterpret_cast<unsigned char *>(module) + Tf2InterpRva;
    g_Tf2BaseInterp = reinterpret_cast<Tf2BaseInterp>(address);
    LONG error = DetourTransactionBegin();
    if (error == NO_ERROR)
    {
        error = DetourUpdateThread(GetCurrentThread());
        if (error == NO_ERROR)
            error = DetourAttach(reinterpret_cast<PVOID *>(&g_Tf2BaseInterp),
                                 reinterpret_cast<PVOID>(Tf2ObserveBaseInterp));
        if (error == NO_ERROR) error = DetourTransactionCommit();
        else DetourTransactionAbort();
    }
    if (error != NO_ERROR)
    {
        g_Tf2BaseInterp = nullptr;
        return false;
    }
    g_Tf2InterpThread = GetCurrentThreadId();
    return true;
}
}
 
CON_COMMAND(mirv_interp_probe, "Read-only native interpolation diagnostic, verified TF2 builds")
{
    auto &p = g_Tf2InterpCapture;
    const int argc = args->ArgC();
    const char *cmd = argc > 1 ? args->ArgV(1) : "status";
    if (!_stricmp(cmd, "start") && argc == 3)
    {
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
        p.LastTick = -1;
        p.Active.store(true);
        Tier0_Msg("Native interpolation capture armed for player %d. Resume; stop before seeking/reloading.\n", p.Entity);
        return;
    }
    if (!_stricmp(cmd, "stop") && argc == 2)
    {
        p.Active.store(false);
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
                   "previous_map_time,map_time,result,no_more_changes,before_x,before_y,before_z,after_x,after_y,after_z\n";
            for (const auto &r : p.Rows)
            {
                out << r.Tick << ',' << r.Frame << ',' << p.Entity << ',' << r.DemoTime << ','
                    << r.GlobalTime << ',' << r.Fraction << ',' << r.Input << ',' << r.Effective << ','
                    << r.PreviousMapTime << ',' << r.MapTime << ',' << r.Result << ',' << r.NoMoreChanges;
                for (float v : r.Before) out << ',' << v;
                for (float v : r.After) out << ',' << v;
                out << '\n';
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
    Tier0_Msg("mirv_interp_probe start <entity> | stop | save <new.csv> | status\n"
              "No motion changes. Restart TF2 to remove installed detour. Stop before seeking/reloading.\n");
}
