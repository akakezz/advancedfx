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
std::atomic_bool NativePreview{false};
std::atomic_bool SourceUiCursor{false};
std::atomic_bool MapPreviewCursor{true};
Size Requested, Original;
bool Managed = false, Pending = false, WantFit = true, Fitted = false, Restoring = false, Confirmed = false;
unsigned long long WaitSerial = 0;
unsigned FitRetries = 0;
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
struct ModeList
{
    void *Interface = nullptr;
    using Getter = void (__thiscall *)(void *, Mode **, int *);
    Getter Get = nullptr;
    Mode *Data = nullptr;
    int Count = 0;
};
struct BorrowedMode
{
    Mode *List = nullptr;
    int Count = 0, Index = -1;
    Mode Saved{}, Custom{};
} Borrowed;

bool RejectMode(const std::string &why)
{
    Message = "Engine video-mode registration rejected: " + why;
    return false;
}
bool QueryModes(ModeList &list)
{
    HMODULE engine = GetModuleHandleW(L"engine.dll");
    if (!engine) return RejectMode("engine.dll unavailable.");
    using Factory = void *(__cdecl *)(const char *, int *);
    auto factory = reinterpret_cast<Factory>(GetProcAddress(engine,"CreateInterface"));
    void *iface = factory ? factory("VENGINE_GAMEUIFUNCS_VERSION005",nullptr) : nullptr;
    if (!iface && g_AppSystemFactory)
        iface = g_AppSystemFactory("VENGINE_GAMEUIFUNCS_VERSION005",nullptr);
    if (!iface) return RejectMode("IGameUIFuncs005 unavailable.");
    auto table = *reinterpret_cast<void ***>(iface);
    MEMORY_BASIC_INFORMATION code{};
    if (!table || !VirtualQuery(table[3], &code, sizeof(code)) ||
        code.AllocationBase != engine || code.State != MEM_COMMIT ||
        (code.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(code.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return RejectMode("IGameUIFuncs GetVideoModes entry is not executable engine code.");
    list.Interface = iface;
    list.Get = reinterpret_cast<ModeList::Getter>(table[3]);
    list.Get(iface, &list.Data, &list.Count);
    if (!list.Data || list.Count < 1 || list.Count > 512)
        return RejectMode("invalid list pointer or count ("+std::to_string(list.Count)+").");
    return true;
}
bool ReadMode(const Mode *p, Mode &mode)
{
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),p,&mode,sizeof(mode),&read) && read==sizeof(mode);
}
bool StillBorrowed(const ModeList &list)
{
    Mode value{};
    return Borrowed.Index>0 && list.Data==Borrowed.List && list.Count==Borrowed.Count &&
        Borrowed.Index<list.Count && ReadMode(list.Data+Borrowed.Index,value) &&
        SameMode(value,Borrowed.Custom);
}
bool ReleaseBorrowed()
{
    if (!Borrowed.List) return true;
    ModeList list;
    // Never write into a rebuilt list or an entry that another owner changed.
    if (!QueryModes(list) || !StillBorrowed(list) ||
        !Writable(list.Data+Borrowed.Index,sizeof(Mode))) return false;
    std::memcpy(list.Data+Borrowed.Index,&Borrowed.Saved,sizeof(Mode));
    Borrowed = {};
    return true;
}
bool Register(Size size)
{
    ModeList list;
    if (!QueryModes(list)) return false;
    if (Borrowed.List)
    {
        if (!StillBorrowed(list))
            return RejectMode("the borrowed mode changed; restore original resolution or restart TF2.");
        if (Dimensions(Borrowed.Custom)==size) return true;
        return RejectMode("restore original resolution before selecting another custom size.");
    }
    std::vector<Mode> modes(size_t(list.Count), Mode{});
    SIZE_T read=0;
    if (!ReadProcessMemory(GetCurrentProcess(), list.Data, modes.data(), modes.size()*sizeof(Mode), &read) ||
        read != modes.size()*sizeof(Mode))
        return RejectMode("cannot read reported mode entries.");
    const int index = BorrowIndex(modes,size,EngineSize(),Original);
    if (index==-1) return true;
    if (index<0)
        return RejectMode("no safe existing mode to borrow (count "+std::to_string(list.Count)+").");
    auto *slot=list.Data+index;
    Mode saved{};
    if (!Writable(slot,sizeof(Mode)) || !ReadMode(slot,saved) || !SameMode(saved,modes[index]))
        return RejectMode("selected existing mode is not writable or changed.");
    Mode custom{size.Width,size.Height,saved.Bits,saved.Refresh};
    // Do not append, shift entries or write the engine's internal count.
    std::memcpy(slot,&custom,sizeof(Mode));
    Borrowed={list.Data,list.Count,index,saved,custom};
    ModeList after;
    if (!QueryModes(after) || !StillBorrowed(after))
    {
        // If the list moved, do not dereference its former pointer.
        if (after.Data==list.Data && after.Count==list.Count &&
            ReadMode(slot,custom) && SameMode(custom,Borrowed.Custom) && Writable(slot,sizeof(Mode)))
            std::memcpy(slot,&saved,sizeof(Mode));
        Borrowed={};
        return RejectMode("engine did not report the substituted mode; restore or restart TF2.");
    }
    Tier0_Msg("Temporarily borrowed mode %d: %dx%d -> %dx%d (list count unchanged: %d).\n",
              index,saved.Width,saved.Height,size.Width,size.Height,list.Count);
    return true;
}
void QueueMode(Size size)
{
    char cmd[96];
    std::snprintf(cmd, sizeof(cmd), "mat_setvideomode %d %d 1", size.Width, size.Height);
    g_VEngineClient->ClientCmd_Unrestricted(cmd);
}
bool WorkArea(HWND hwnd, RECT &work)
{
    MONITORINFO mi{};
    mi.cbSize=sizeof(mi);
    if ((!OriginalMonitor || !GetMonitorInfoW(OriginalMonitor,&mi)) &&
        !GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&mi)) return false;
    work=mi.rcWork;
    return true;
}
bool PreviewFits(HWND hwnd, Size render)
{
    RECT work{}, wr{}, cr{};
    if (!hwnd || IsIconic(hwnd) || !WorkArea(hwnd,work) ||
        !GetWindowRect(hwnd,&wr) || !GetClientRect(hwnd,&cr)) return false;
    const Size actual{int(cr.right-cr.left),int(cr.bottom-cr.top)};
    return actual.Width>0 && actual.Height>0 && actual.Width<=render.Width && actual.Height<=render.Height &&
        wr.left>=work.left && wr.top>=work.top && wr.right<=work.right && wr.bottom<=work.bottom &&
        std::abs(std::int64_t(actual.Width)*render.Height-std::int64_t(actual.Height)*render.Width)
            <= (std::max)(render.Width,render.Height);
}
bool FitWindow(HWND hwnd, Size render)
{
    if (!hwnd || !IsWindow(hwnd) || IsIconic(hwnd) ||
        GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId()) return false;
    RECT work{}, wr{}, cr{};
    if (!WorkArea(hwnd,work)) return false;
    if (IsZoomed(hwnd))
    {
        ResizeWindow=hwnd;
        ShowWindow(hwnd,SW_RESTORE);
        ResizeWindow=nullptr;
    }
    if (!GetWindowRect(hwnd,&wr) || !GetClientRect(hwnd,&cr)) return false;
    const int borderW = (std::max)(0L, (wr.right-wr.left)-(cr.right-cr.left));
    const int borderH = (std::max)(0L, (wr.bottom-wr.top)-(cr.bottom-cr.top));
    const int workW = work.right-work.left, workH = work.bottom-work.top;
    const Size fit = advancedfx::RenderResolution::Fit(render, {workW-borderW,workH-borderH});
    if (fit.Width <= 0 || fit.Height <= 0) return false;
    const int w = fit.Width+borderW, h = fit.Height+borderH;
    const int x = work.left+(workW-w)/2, y = work.top+(workH-h)/2;
    ResizeWindow = hwnd;
    const bool ok = SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER) != FALSE;
    ResizeWindow = nullptr;
    RECT actual{};
    return ok && GetClientRect(hwnd, &actual) && PreviewFits(hwnd,render);
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
    FitRetries = 0;
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
void PublishPreviewPath(bool native) { NativePreview.store(native); }
void Invalidate()
{
    NativePreview.store(false);
    SourceUiCursor.store(false);
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
namespace
{
struct CursorSpace { HWND Window=nullptr; Size Preview, Render; POINT Origin{}; };
bool GetCursorSpace(CursorSpace &space, void *window)
{
    if (!MapPreviewCursor.load() || !SourceUiCursor.load()) return false;
    std::unique_lock<std::recursive_mutex> lock(Mutex,std::try_to_lock);
    if (!lock.owns_lock() || !Managed || !Fitted || !Current.Windowed ||
        !Current.Window || Current.Back!=Requested || Current.Capture!=Requested ||
        (window && window!=Current.Window)) return false;
    space.Window=Current.Window;
    space.Render=Requested;
    lock.unlock();
    if (GetForegroundWindow()!=space.Window || IsIconic(space.Window)) return false;
    RECT rc{};
    if (!GetClientRect(space.Window,&rc) || !ClientToScreen(space.Window,&space.Origin)) return false;
    space.Preview={int(rc.right-rc.left),int(rc.bottom-rc.top)};
    return space.Preview.Width>0 && space.Preview.Height>0 &&
           space.Preview.Width<=space.Render.Width && space.Preview.Height<=space.Render.Height &&
           space.Preview!=space.Render;
}
}
bool MapCursor(int &x, int &y, bool toRender, bool screen, void *window)
{
    CursorSpace space;
    if (!GetCursorSpace(space,window)) return false;
    const Size from=toRender ? space.Preview : space.Render;
    const Size to=toRender ? space.Render : space.Preview;
    x=MapAxis(x,screen ? space.Origin.x : 0,from.Width,to.Width);
    y=MapAxis(y,screen ? space.Origin.y : 0,from.Height,to.Height);
    return true;
}
bool MapCursorClip(int &left, int &top, int &right, int &bottom)
{
    CursorSpace space;
    if (!GetCursorSpace(space,nullptr)) return false;
    const int x=space.Origin.x,y=space.Origin.y;
    // Leave already-physical clip rectangles alone.
    if (left<x || top<y || right>x+space.Render.Width || bottom>y+space.Render.Height ||
        right<=left || bottom<=top ||
        (right<=x+space.Preview.Width && bottom<=y+space.Preview.Height)) return false;
    left=MapAxis(left,x,space.Render.Width,space.Preview.Width);
    right=MapAxis(right,x,space.Render.Width,space.Preview.Width);
    top=MapAxis(top,y,space.Render.Height,space.Preview.Height);
    bottom=MapAxis(bottom,y,space.Render.Height,space.Preview.Height);
    return true;
}
void Frame()
{
    // Sample on the engine thread; never reenter Source from cursor hooks.
    bool available=false, visible=false;
    if (Supported() && g_VEngineClient && FitPresentation(nullptr))
    {
        g_Hook_VClient_RenderView.UiCursorStatus(available,visible);
        SourceUiCursor.store((available && visible) || g_Hook_VClient_RenderView.IsSourceUiActive());
    }
    else SourceUiCursor.store(false);
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
                const bool released = ReleaseBorrowed();
                Managed = Pending = Fitted = Restoring = false;
                Confirmed = true;
                Message = !released ? "Original resolution restored; borrowed mode could not be safely restored. Restart TF2." :
                    restored ? "Original render resolution, window and borrowed mode restored." :
                    "Original render resolution restored; window position could not be restored.";
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
            if (WantFit && !PreviewFits(Current.Window,Requested))
            {
                if (++FitRetries <= 3)
                {
                    Fitted=false;
                    WaitSerial=Current.Serial;
                    Deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
                    return;
                }
                Pending=false; Confirmed=false;
                Message="Engine resized the fitted window again. Use mirv_resolution refit or restore.";
                return;
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
    else if (Confirmed && exact && WantFit && Fitted && Current.Window && !IsIconic(Current.Window) &&
             !PreviewFits(Current.Window,Requested))
    {
        if (++FitRetries > 3)
        {
            Confirmed=false;
            Message="Preview no longer fits monitor. Use mirv_resolution refit or restore.";
        }
        else
        {
            Pending=true; Confirmed=false; Fitted=false;
            WaitSerial=Current.Serial;
            Deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
            Message="Refitting preview after the game moved or resized its window.";
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
    state.NativePreview=NativePreview.load();
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
    else if (argc==3 && !_stricmp(args->ArgV(1),"cursor") &&
        (!std::strcmp(args->ArgV(2),"0") || !std::strcmp(args->ArgV(2),"1")))
        MapPreviewCursor.store(args->ArgV(2)[0]=='1');
    else if (argc==2 && !_stricmp(args->ArgV(1),"refit"))
    {
        if (!Managed || Recording() || !Current.Windowed ||
            !advancedfx::RenderResolution::Ready(Requested,EngineSize(),Current.Back,Current.Capture))
            Message="Refit requires a confirmed native mode and recording stopped.";
        else
        {
            WantFit=true; Pending=true; Confirmed=false; Fitted=false; Restoring=false;
            FitRetries=0; WaitSerial=Current.Serial;
            Deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
            Message="Refitting preview without changing the native mode.";
        }
    }
    else if (argc!=1 && !(argc==2 && !_stricmp(args->ArgV(1),"status")))
    { Tier0_Msg("mirv_resolution apply <width> <height> [fit 0|1] | restore | refit | cursor 0|1 | status\n"); return; }
    const Size requested = Requested;
    lock.unlock();
    if (queue) QueueMode(requested);
    const auto s = GetState();
    Tier0_Msg("Recording resolution: backbuffer %dx%d, capture %dx%d, preview %dx%d, GPU limit %dx%d.\n%s\nOverlay presentation: %s.\n",
        s.RenderWidth,s.RenderHeight,s.CaptureWidth,s.CaptureHeight,s.PreviewWidth,s.PreviewHeight,
        s.MaxWidth,s.MaxHeight,s.Message.c_str(),s.NativePreview ? "preview-resolution composite" : "ordinary/scaled backbuffer");
    Tier0_Msg("Preview cursor remapping: %s; Source UI cursor/menu: %s.\n",
        MapPreviewCursor.load() ? "enabled" : "disabled", SourceUiCursor.load() ? "active" : "inactive");
}
}
CON_COMMAND(mirv_resolution, "Native TF2 windowed recording resolution and fitted preview")
{
    RecordingResolution::Console(args);
}
