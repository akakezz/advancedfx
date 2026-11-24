// Engine-side custom recording mode controller. No SourceRes code is copied.
// Source SDK IGameUIFuncs005::GetVideoModes is slot 3 (modes.h: four ints).
#include "RecordingResolution.h"
#include <shared/RenderResolution.h>
#include <mutex>

namespace RecordingResolution
{
namespace
{
using namespace advancedfx::RenderResolution;
struct Surface
{
    HWND Window = nullptr;
    bool Windowed = false;
    Size Back, Capture, Limit;
    unsigned long long Serial = 0;
};
std::recursive_mutex Mutex;
Surface Current;
Size Requested, Original;
bool Managed = false, Pending = false, WantFit = true, Fitted = false, Restoring = false, Confirmed = false;
unsigned long long WaitSerial = 0;
std::chrono::steady_clock::time_point Deadline;
std::string Message = "Select a resolution and apply it while recording is stopped.";
thread_local HWND ResizeWindow = nullptr;
RECT OriginalRect{};
HWND OriginalWindow = nullptr;
HMONITOR OriginalMonitor = nullptr;

bool Supported()
{
    return g_SourceSdkVer == SourceSdkVer_TF2 || g_SourceSdkVer == SourceSdkVer_TF2Classified;
}
bool Recording()
{
    return AfxHookSource::CameraOverlay::Recording::IsActive();
}
Size EngineSize()
{
    Size size;
    if (g_VEngineClient) g_VEngineClient->GetScreenSize(size.Width, size.Height);
    return size;
}
bool Writable(const void *p, size_t bytes)
{
    MEMORY_BASIC_INFORMATION m{};
    if (!p || !VirtualQuery(p, &m, sizeof(m)) || m.State != MEM_COMMIT ||
        (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(m.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return false;
    const auto start = reinterpret_cast<uintptr_t>(p);
    const auto base = reinterpret_cast<uintptr_t>(m.BaseAddress);
    return start >= base && start-base <= m.RegionSize && bytes <= m.RegionSize-(start-base);
}
bool Register(Size size)
{
    auto reject = [](const std::string &why) {
        Message = "Engine video-mode registration rejected: " + why;
        return false;
    };
    // The game UI interface is versioned independently of IVEngineClient,
    // and exposes the same native list with pointer/count arguments.
    HMODULE engine = GetModuleHandleW(L"engine.dll");
    if (!engine) return reject("engine.dll unavailable.");
    using Factory = void *(__cdecl *)(const char *, int *);
    auto factory = reinterpret_cast<Factory>(GetProcAddress(engine,"CreateInterface"));
    void *iface = factory ? factory("VENGINE_GAMEUIFUNCS_VERSION005",nullptr) : nullptr;
    if (!iface && g_AppSystemFactory)
        iface = g_AppSystemFactory("VENGINE_GAMEUIFUNCS_VERSION005",nullptr);
    if (!iface) return reject("IGameUIFuncs005 unavailable.");
    auto table = *reinterpret_cast<void ***>(iface);
    MEMORY_BASIC_INFORMATION code{};
    if (!table || !VirtualQuery(table[3], &code, sizeof(code)) ||
        code.AllocationBase != engine || code.State != MEM_COMMIT ||
        (code.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(code.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return reject("IGameUIFuncs GetVideoModes entry is not executable engine code.");
    using GetModes = void (__thiscall *)(void *, Mode **, int *);
    auto getModes = reinterpret_cast<GetModes>(table[3]);
    int count = 0;
    Mode *native = nullptr;
    getModes(iface, &native, &count);
    if (!native || count < 1 || count > 512)
        return reject("invalid list pointer or count (" + std::to_string(count) + ").");
    auto *counter = reinterpret_cast<int *>(native) - 1;
    int stored = 0;
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), counter, &stored, sizeof(stored), &read) ||
        read != sizeof(stored))
        return reject("cannot read internal mode count.");
    if (stored != count)
        return reject("internal count " + std::to_string(stored) +
                      " does not match reported count " + std::to_string(count) + ".");
    std::vector<Mode> modes(size_t(count), Mode{});
    if (!ReadProcessMemory(GetCurrentProcess(), native, modes.data(), modes.size()*sizeof(Mode), &read) ||
        read != modes.size()*sizeof(Mode))
        return reject("cannot read reported mode entries.");
    bool inserted = false;
    if (!Plan(modes, size, inserted))
        return reject("invalid mode entries, dimensions or full mode list (count " + std::to_string(count) + ").");
    if (!inserted) return true;
    // The SDK 2013 native list has 512 entries. Require the complete
    // expected storage to be writable before accepting an append. Unused
    // entries can retain old video modes after the engine rebuilds the list.
    if (!Writable(counter, sizeof(int)+512*sizeof(Mode)))
        return reject("full native mode storage is not writable.");
    Mode spare{};
    if (!ReadProcessMemory(GetCurrentProcess(), native+count, &spare, sizeof(spare), &read) ||
        read != sizeof(spare))
        return reject("cannot read spare mode slot.");
    const bool zero = !spare.Width && !spare.Height && !spare.Bits && !spare.Refresh;
    if (!zero && !Plausible(spare))
        return reject("spare entry does not look like zero or an old mode (" +
            std::to_string(spare.Width) + "x" + std::to_string(spare.Height) + ", " +
            std::to_string(spare.Bits) + " bpp, " + std::to_string(spare.Refresh) + " Hz).");
    std::vector<Mode> original(size_t(count)+1, Mode{});
    if (!ReadProcessMemory(GetCurrentProcess(), native, original.data(), original.size()*sizeof(Mode), &read) ||
        read != original.size()*sizeof(Mode))
        return reject("cannot save original list for rollback.");
    std::memcpy(native, modes.data(), modes.size()*sizeof(Mode));
    *counter = int(modes.size());
    int afterCount = 0;
    Mode *afterList = nullptr;
    getModes(iface, &afterList, &afterCount);
    if (afterList != native || afterCount != int(modes.size()))
    {
        std::memcpy(native, original.data(), original.size()*sizeof(Mode));
        *counter = count;
        return reject("engine did not report the updated mode list; rolled back.");
    }
    return true;
}
void QueueMode(Size size)
{
    char cmd[96];
    std::snprintf(cmd, sizeof(cmd), "mat_setvideomode %d %d 1", size.Width, size.Height);
    g_VEngineClient->ClientCmd_Unrestricted(cmd);
}
bool FitWindow(HWND hwnd, Size render)
{
    if (!hwnd || !IsWindow(hwnd) || IsIconic(hwnd) ||
        GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId()) return false;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR monitor = OriginalMonitor ? OriginalMonitor : MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    RECT wr{}, cr{};
    if (!GetMonitorInfoW(monitor, &mi) || !GetWindowRect(hwnd, &wr) || !GetClientRect(hwnd, &cr)) return false;
    const int borderW = (std::max)(0L, (wr.right-wr.left)-(cr.right-cr.left));
    const int borderH = (std::max)(0L, (wr.bottom-wr.top)-(cr.bottom-cr.top));
    const int workW = mi.rcWork.right-mi.rcWork.left, workH = mi.rcWork.bottom-mi.rcWork.top;
    const Size fit = advancedfx::RenderResolution::Fit(render, {workW-borderW,workH-borderH});
    if (fit.Width <= 0 || fit.Height <= 0) return false;
    const int w = fit.Width+borderW, h = fit.Height+borderH;
    const int x = mi.rcWork.left+(workW-w)/2, y = mi.rcWork.top+(workH-h)/2;
    ResizeWindow = hwnd;
    const bool ok = SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER) != FALSE;
    ResizeWindow = nullptr;
    RECT actual{};
    return ok && GetClientRect(hwnd, &actual) &&
           actual.right-actual.left == fit.Width && actual.bottom-actual.top == fit.Height;
}
bool RestoreWindow(HWND hwnd)
{
    if (!hwnd || hwnd != OriginalWindow || !IsWindow(hwnd) ||
        GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId()) return false;
    ResizeWindow = hwnd;
    const bool ok = SetWindowPos(hwnd, nullptr, OriginalRect.left, OriginalRect.top,
        OriginalRect.right-OriginalRect.left, OriginalRect.bottom-OriginalRect.top,
        SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER) != FALSE;
    ResizeWindow = nullptr;
    return ok;
}
void Begin(Size size, bool fit, bool restore)
{
    Requested = size; WantFit = fit; Restoring = restore;
    Pending = true; Confirmed = false; Fitted = false; Managed = true;
    WaitSerial = Current.Serial;
    Deadline = std::chrono::steady_clock::now()+std::chrono::seconds(8);
    Message = "Waiting for the engine and capture surfaces to confirm the requested resolution.";
}
bool Apply(Size size, bool fit)
{
    if (!Supported() || !g_VEngineClient || Recording() || Pending ||
        g_VEngineClient->IsDrawingLoadingImage() || !Current.Windowed || !Current.Window)
    {
        Message = "Requires TF2 in windowed mode, with recording stopped and no pending change.";
        return false;
    }
    if (!Valid(size) || size.Width > Current.Limit.Width || size.Height > Current.Limit.Height)
    {
        Message = "Resolution outside supported dimension or GPU texture limits.";
        return false;
    }
    if (!Managed)
    {
        const Size engine = EngineSize();
        if (!advancedfx::RenderResolution::Ready(engine,engine,Current.Back,Current.Capture) ||
            !GetWindowRect(Current.Window,&OriginalRect))
        {
            Message = "Cannot establish the original native resolution; wait for a rendered frame.";
            return false;
        }
        Original = engine; OriginalWindow = Current.Window;
        OriginalMonitor = MonitorFromWindow(Current.Window, MONITOR_DEFAULTTONEAREST);
    }
    if (!Register(size)) return false;
    Begin(size,fit,false);
    return true;
}
bool Restore()
{
    if (!Managed || !g_VEngineClient || Recording() || !Valid(Original))
    {
        Message = "Restore requires a saved original resolution and recording stopped.";
        return false;
    }
    Begin(Original,false,true);
    return true;
}
}
void Publish(void *window, bool windowed, int bw, int bh, int cw, int ch, int mw, int mh)
{
    std::lock_guard<std::recursive_mutex> lock(Mutex);
    Current = {static_cast<HWND>(window),windowed,{bw,bh},{cw,ch},{mw,mh},Current.Serial+1};
}
void Invalidate()
{
    std::lock_guard<std::recursive_mutex> lock(Mutex);
    Current.Back = Current.Capture = {};
}
bool SuppressSizeMessage(void *window)
{
    return ResizeWindow && ResizeWindow == window;
}
bool FitPresentation(void *window)
{
    std::lock_guard<std::recursive_mutex> lock(Mutex);
    return Managed && Fitted && Current.Windowed && Current.Window &&
        Current.Back == Requested && Current.Capture == Requested &&
        (!window || window == Current.Window);
}
void Frame()
{
    std::unique_lock<std::recursive_mutex> lock(Mutex);
    if (!Managed || !Supported() || !g_VEngineClient) return;
    const bool exact = Current.Windowed && advancedfx::RenderResolution::Ready(Requested, EngineSize(), Current.Back, Current.Capture);
    if (Recording())
    {
        if (Confirmed && !exact)
        {
            Confirmed = false;
            Message = "Resolution changed during recording; stopping to protect the output.";
            lock.unlock();
            g_VEngineClient->ClientCmd_Unrestricted("mirv_streams record end; host_framerate 0");
        }
        return;
    }
    if (Pending)
    {
        if (Current.Serial > WaitSerial && exact)
        {
            if (Restoring)
            {
                const HWND window = Current.Window;
                lock.unlock();
                const bool restored = RestoreWindow(window);
                lock.lock();
                Managed = Pending = Fitted = Restoring = false;
                Confirmed = true;
                Message = restored ? "Original render resolution and window restored." : "Original render resolution restored; window position could not be restored.";
                return;
            }
            if (WantFit && !Fitted)
            {
                const HWND window = Current.Window;
                const Size requested = Requested;
                lock.unlock();
                const bool fitted = FitWindow(window,requested);
                lock.lock();
                if (!fitted)
                {
                    Pending = false; Confirmed = false;
                    Message = "Preview fit failed. Restore original or apply again without fit.";
                    return;
                }
                Fitted = true;
                WaitSerial = Current.Serial;
                Deadline = std::chrono::steady_clock::now()+std::chrono::seconds(8);
                return; // require a new successful present AFTER shrinking the window
            }
            Pending = false; Confirmed = true;
            Message = WantFit ? "Native resolution verified; fitted preview active." : "Native resolution verified.";
        }
        else if (std::chrono::steady_clock::now() > Deadline)
        {
            Pending = false; Confirmed = false;
            Message = "Resolution not confirmed. Restore original or retry; recording is blocked.";
        }
    }
    else if (Confirmed && !exact)
    {
        Confirmed = false;
        Message = "Native resolution changed unexpectedly. Restore or apply again before recording.";
    }
}
State GetState()
{
    std::lock_guard<std::recursive_mutex> lock(Mutex);
    State state;
    state.Supported=Supported(); state.Windowed=Current.Windowed;
    state.RenderWidth=Current.Back.Width; state.RenderHeight=Current.Back.Height;
    state.CaptureWidth=Current.Capture.Width; state.CaptureHeight=Current.Capture.Height;
    state.MaxWidth=Current.Limit.Width; state.MaxHeight=Current.Limit.Height;
    RECT rc{};
    if (Current.Window && GetClientRect(Current.Window,&rc))
    { state.PreviewWidth=rc.right-rc.left; state.PreviewHeight=rc.bottom-rc.top; }
    state.Pending=Pending; state.Managed=Managed; state.Fit=Fitted; state.Ready=Confirmed;
    state.Message=Message;
    return state;
}
bool CanStart()
{
    std::lock_guard<std::recursive_mutex> lock(Mutex);
    if (!Managed) return true;
    return !Pending && Confirmed && Current.Windowed &&
        advancedfx::RenderResolution::Ready(Requested, EngineSize(), Current.Back, Current.Capture);
}
void Console(IWrpCommandArgs *args)
{
    std::unique_lock<std::recursive_mutex> lock(Mutex);
    bool queue = false;
    const int argc = args->ArgC();
    if (argc >= 2 && !_stricmp(args->ArgV(1),"apply") && (argc==4 || argc==5))
    {
        auto parse = [](const char *s, int &out) {
            char *end=nullptr; errno=0; long n=std::strtol(s,&end,10);
            if (end==s || *end || errno || n<1 || n>16384) return false;
            out=int(n); return true;
        };
        Size size;
        if (!parse(args->ArgV(2),size.Width) || !parse(args->ArgV(3),size.Height) ||
            (argc==5 && std::strcmp(args->ArgV(4),"0") && std::strcmp(args->ArgV(4),"1")))
        { Tier0_Warning("mirv_resolution apply <width> <height> [fit 0|1]\n"); return; }
        queue = Apply(size,argc==4 || args->ArgV(4)[0]=='1');
    }
    else if (argc==2 && !_stricmp(args->ArgV(1),"restore")) queue = Restore();
    else if (argc!=1 && !(argc==2 && !_stricmp(args->ArgV(1),"status")))
    { Tier0_Msg("mirv_resolution apply <width> <height> [fit 0|1] | restore | status\n"); return; }
    const Size requested = Requested;
    lock.unlock();
    if (queue) QueueMode(requested);
    const auto s = GetState();
    Tier0_Msg("Recording resolution: backbuffer %dx%d, capture %dx%d, preview %dx%d, GPU limit %dx%d.\n%s\n",
        s.RenderWidth,s.RenderHeight,s.CaptureWidth,s.CaptureHeight,s.PreviewWidth,s.PreviewHeight,
        s.MaxWidth,s.MaxHeight,s.Message.c_str());
}
}
CON_COMMAND(mirv_resolution, "Native TF2 windowed recording resolution and fitted preview")
{
    RecordingResolution::Console(args);
}
