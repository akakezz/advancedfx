// Adapted from Paper (papesgit/advancedfx), shared/overlay/OverlayDx9.cpp.
// MIT license; see shared/overlay/PORTING.md for provenance and scope.
#include "MirvCam.h"
#include "CameraOverlay.h"
#include "RecordingResolution.h"
#include "Tf2RenderPass.h"
#include "CampathDrawer.h"
#include "Gui.h"
#include "MirvCalcs.h"
#include "MirvCam.h"
#include "MirvTime.h"
#include "RenderView.h"
#include "WrpVEngineClient.h"
#include <cerrno>
#include <climits>
#include "hlaeFolder.h"
#include "stdafx.h"
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <future>
#include <cctype>
#include <shared/CommandSystem.h>
#include <shared/DemoTimeline.h>
#include <shared/PlayerRepair.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <shared/AfxConsole.h>
#include <shared/RecordingLog.h>
#include <shared/overlay/third_party/imgui_filebrowser/imfilebrowser.h>
#include <shared/overlay/third_party/imgui_neo_sequencer/imgui_neo_sequencer.h>
#include <shared/overlay/third_party/imguizmo/ImGuizmo.h>
#include <shellapi.h>
#include <string>
#include <sstream>
#include <fstream>
#include <set>
#include <utility>
#include <vector>
extern WrpVEngineClient *g_VEngineClient;
extern CommandSystem g_CommandSystem;
extern bool GetCurrentDemoTick(int &);
extern bool GetCurrentDemoTime(double &);
extern bool GetDemoTickFromClientTime(double, double, int &);
namespace AfxHookSource
{
namespace CameraOverlay
{
// Informative text keeps the muted visual hierarchy but wraps in narrow panels.
static void DrawOverlayHint(const char *text)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextUnformatted(text);
	ImGui::PopTextWrapPos();
	ImGui::PopStyleColor();
}
static std::atomic_bool g_Open{false}, g_MouseLook{false};
static std::recursive_mutex g_SceneMutex;
static advancedfx::SceneCache g_SceneCache;
static bool g_SceneRecord = false, g_ScenePreview = false;
// Session-local opt-in; diagnostic capture remains the default.
static bool g_SceneRenderSnapshot = false, g_SceneSuppressLiveStudio = true;
// Separate bounded event journal; no simulation or live-effect suppression yet.
static bool g_SceneEffectsEnabled = false;
static advancedfx::SceneEffects::Journal g_SceneEffects;
static bool g_SceneNativeParticlesEnabled=false, g_SceneNativeParticleMarkers=false;
static bool g_SceneNativeParticleAppearance=false, g_SceneNativeParticleStyled=false;
static bool g_SceneNativeParticleReplay=false;
static bool g_SceneSuppressLiveParticles=false; // opt-in, not persisted
static std::string g_SceneParticleIsolationStatus="Not evaluated yet.";
static advancedfx::NativeParticles::ReplayResult g_SceneParticleReplayResult;
static int g_SceneParticleReplayTick=-1;
static advancedfx::NativeParticles::History g_SceneNativeParticles;
// Derived recorded-only replay metadata is reused while the same decoded
// lease is active. Native factory, allocation and renderer checks stay live.
static std::shared_ptr<const advancedfx::NativeParticles::ReplayPreparation> g_SceneParticlePrepared;
static auto ScenePrepareParticles(const std::shared_ptr<const advancedfx::NativeParticles::Frame> &sample){
    if(!sample)return std::shared_ptr<const advancedfx::NativeParticles::ReplayPreparation>{};
    if(!g_SceneParticlePrepared||!g_SceneParticlePrepared->Matches(*sample))
        g_SceneParticlePrepared=std::make_shared<const advancedfx::NativeParticles::ReplayPreparation>(sample);
    return g_SceneParticlePrepared;
}
// The decoded cache has only one entry. A diagnostic lookup or cursor move
// must not erase a known submission failure for an unchanged history.
struct SceneParticleFailureHistory {
    std::uint64_t Revision=0;
    std::set<int> Ticks;
    void Reset(){Ticks.clear();Revision=0;}
    bool Contains(std::uint64_t revision,int tick)const{return Revision==revision&&Ticks.count(tick)!=0;}
    void Mark(std::uint64_t revision,int tick){
        if(Revision!=revision){Ticks.clear();Revision=revision;}
        // At most one entry per retained history tick (32768).
        Ticks.insert(tick);
    }
};
static SceneParticleFailureHistory g_SceneIsolationFailed;
static std::string g_SceneIsolationEligibility="Not evaluated.";
static double g_SceneNativeParticleMs=0, g_SceneNativeParticleTotalMs=0, g_SceneNativeParticlePeakMs=0;
static std::size_t g_SceneNativeParticleSamples=0;
static int g_SceneDetachedTick=-1, g_SceneDetachedIndex=-1;
static bool g_SceneLegacyAuditPending=false;
static bool g_SceneLegacyCapture=false,g_SceneLegacyReplay=false,g_SceneLegacyAll=false,g_SceneLegacyQueued=false;
static bool g_SceneReplayModulation=false,g_SceneHideShadows=false,g_SceneHideDecals=false,g_SceneLegacyMarkers=false;
static bool g_SceneLightingCapture=false,g_SceneReplayLighting=false,g_SceneLightingObserverReady=false;
static std::string g_SceneLightingStatus="Historical model lighting not captured.";
static bool g_SceneSurfaceCapture=false,g_SceneHistoricalShadows=false,g_SceneHistoricalDecals=false,g_SceneRemovedDecals=false,g_SceneDecalOpacity=false,g_SceneQueuedDecals=false;
static advancedfx::SceneSurfaces::History g_SceneSurfaces;
static std::string g_SceneHistoricalSurfaceStatus="Not evaluated.";
static std::string g_SceneSurfaceStatus[2]={"Not evaluated.","Not evaluated."};
static advancedfx::NativeParticles::ReplayResult g_SceneLegacyReplayResult;
static int g_SceneLegacyReplayTick=-1;
static advancedfx::LegacyParticles::History g_SceneLegacyHistory;
static SceneParticleFailureHistory g_SceneLegacyIsolationFailed;
static std::string g_SceneLegacyCaptureStatus="Not captured.";
static std::string g_SceneDetachedStatus="Not run.";
static unsigned long long g_SceneReplayCallbacks=0,g_SceneReplayAttempted=0,g_SceneReplaySubmitted=0,g_SceneReplayQueueSkips=0,g_SceneReplayOtherSkips=0;
static int g_SceneReplayFrame=-1;
static unsigned g_SceneReplayFrameCallbacks=0,g_SceneReplayMaxCallbacks=0;
static std::string g_SceneReplayLastFailure;
// CPU wall-clock timings (not GPU completion). Access under g_SceneMutex.
struct ScenePerfStat {
    unsigned long long Count=0;
    double Last=0,Total=0,Peak=0;
    int PeakTick=-1;
    void Add(std::chrono::steady_clock::time_point begin,int tick=-1) {
        Last=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        Total+=Last;if(Last>Peak){Peak=Last;PeakTick=tick;}++Count;
    }
    void Print(const char *label) const {
        advancedfx::Message("  %s: last %.3f ms, mean %.3f ms, peak %.3f ms at tick %d (%llu calls)\n",label,Last,Count?Total/Count:0,Peak,PeakTick,Count);
    }
};
struct ScenePerfCounters {
    ScenePerfStat Sample,ModelDraw,ParticleAcquire,ParticleDraw,LegacyDraw,SurfaceBegin,SurfaceEnd,LegacyCapture,SurfaceCapture;
    unsigned long long ParticleMain=0,ParticleRejected=0,ParticleAttempts=0,ParticlePreflightRejected=0,LegacyRejected=0;
    ScenePerfStat MainModelDraw,OtherModelDraw,UnknownModelDraw,MainParticleDraw;
    unsigned long long NonMainModelSkipped=0,NonMainParticleSkipped=0,ModelsCulled=0,ActorsCulled=0;
    int LastModelFailureTick=-1;
    std::string LastModelFailureAsset;
    advancedfx::SceneCache::ReplayTimings StudioStages;
    double SlowestModelMs=0;
    std::string SlowestModelAsset;
    int SlowestModelTick=-1;
    double ParticleFactoryMs=0,ParticlePlanMs=0,ParticleCopyMs=0,ParticleRenderMs=0,ParticleDestroyMs=0;
    double ParticleGateMs=0,ParticleActivationMs=0,ParticleResetMs=0;
    unsigned long long FastWrites=0,SystemWrites=0,WriteFaults=0,CleanupFailures=0;
    unsigned long long FastReads=0,SystemReads=0,ReadFaults=0;
    int LastParticleFailureTick=-1;
    std::string LastParticleFailure;
};
static ScenePerfCounters g_ScenePerf;
static bool g_SceneMainViewOnly=false; // optional until skybox fidelity is checked
static bool g_ScenePreviewCull=false; // opt-in until animated bounds/edge coverage verified
static bool g_SceneActorCull=false; // optional conservative actor/attachment group culling
static bool g_SceneKeepAssets=true; // bounded preview-only references
static bool g_SceneBatchStudio=true; // reversible per-view batching
static bool g_SceneBatchQueued=true; // ordered per-draw tokens, reversible
static bool g_SceneFastParticleWrites=true; // verified detached allocations only
static bool g_SceneFastParticleReads=true; // verified detached validation only
static void SceneClearNativeParticles() {
    g_SceneNativeParticles.Clear(); g_SceneNativeParticleSamples=0;
    g_SceneParticlePrepared.reset();g_SceneIsolationFailed.Reset();g_SceneIsolationEligibility="Not evaluated.";
    g_SceneDetachedTick=g_SceneDetachedIndex=-1;g_SceneDetachedStatus="Not run.";
    g_SceneLegacyAuditPending=false;
    g_SceneLegacyHistory.Clear();g_SceneLegacyIsolationFailed.Reset();g_SceneLegacyCaptureStatus="Not captured.";
    g_SceneSurfaces.Clear();Mirv_Tf2_ResetSurfaces();g_SceneHistoricalSurfaceStatus="Not evaluated.";
    g_SceneLegacyReplayResult={};g_SceneLegacyReplayTick=-1;
    g_SceneParticleReplayResult={};g_SceneParticleReplayTick=-1;
    g_SceneNativeParticleMs=g_SceneNativeParticleTotalMs=g_SceneNativeParticlePeakMs=0;
}
static void SceneParticleCaptureEnabled(bool enabled) {
    if(enabled&&!g_SceneNativeParticlesEnabled)SceneClearNativeParticles();
    g_SceneNativeParticlesEnabled=enabled;
    if(enabled) {
        g_SceneEffectsEnabled=false;
        g_SceneNativeParticleAppearance=true;
        g_SceneNativeParticleReplay=true;
        g_SceneNativeParticleMarkers=false;
        g_SceneNativeParticleStyled=true;
    } else {
        g_SceneNativeParticleReplay=false;g_SceneNativeParticleMarkers=false;
    }
}
static void SceneParticlePreviewMode(int mode) {
    g_SceneNativeParticleReplay=mode==1;
    g_SceneNativeParticleMarkers=mode==2||mode==3;
    g_SceneNativeParticleStyled=mode==2;
    g_SceneParticleReplayResult={};g_SceneParticleReplayTick=-1;
}
static std::array<bool,6> g_SceneEffectsTraceAttempted{};
static std::array<std::vector<std::uintptr_t>,6> g_SceneEffectsCallsites;
static void SceneClearEffects() {
    g_SceneEffects.Clear(); g_SceneEffectsTraceAttempted.fill(false);
    for (auto &v:g_SceneEffectsCallsites) v.clear();
}
static int g_SceneAuditFrame = -1;
static std::map<const void *, int> g_SceneAuditDraws;
static std::string g_SceneAuditStatus = "No same-frame native model observation yet.";
static double g_SceneCursor = 0;
static bool g_SceneDrivingCamera = false;
static bool g_ScenePlayerModels = false, g_ScenePlayerFilter = false;
struct ScenePlayerSelection
{
	int Index;
	std::string Name;
};
static std::map<int, ScenePlayerSelection>
    g_ScenePlayers; // Demo-local user ID -> identity, never only a slot.
static bool g_SceneSmoothAnimation = true;
static bool g_SceneProjectiles = false, g_SceneWeapons = false, g_SceneCosmetics = false;
static bool g_SceneRagdolls = false, g_SceneGibs = false;
static bool g_SceneBuildings = false, g_SceneProps = false;
static std::string g_ScenePoseStatus = "Pose capture disabled.";
static std::shared_ptr<const std::string> g_SceneModelStatus;
static void SceneModelStatus(const std::string &value)
{
	auto previous = std::atomic_load(&g_SceneModelStatus);
	if (!previous || *previous != value)
		std::atomic_store(&g_SceneModelStatus, std::make_shared<const std::string>(value));
}
static std::atomic_bool g_SceneDrawModels{true};
static advancedfx::SceneCache::DrawingOptions g_SceneDrawing;
static std::atomic_ullong g_SceneDrawEpoch{0};

// Window/device callbacks only publish state; engine resources stay on the engine thread.
static std::atomic_bool g_SceneFocused{true}, g_SceneDeviceReady{false};
static std::atomic_ullong g_SceneLifecycleEpoch{0};
static std::atomic_ullong g_SceneReadyEpoch{0};
static std::atomic_uint g_SceneReadyFrames{0};
void SceneCacheFocus(bool focused)
{
	if (g_SceneFocused.exchange(focused) != focused)
	{
		++g_SceneLifecycleEpoch;
		++g_SceneDrawEpoch;
	}
}
void SceneCacheDevicePresent(bool success)
{
	if (g_SceneDeviceReady.exchange(success) != success)
	{
		++g_SceneLifecycleEpoch;
		++g_SceneDrawEpoch;
	}
}
static bool SceneCanDrawModels()
{
	return g_SceneFocused.load() && g_SceneDeviceReady.load() &&
	       g_SceneReadyEpoch == g_SceneLifecycleEpoch.load() && g_SceneReadyFrames >= 2;
}

static unsigned long long g_ScenePoseCaptured = 0, g_ScenePoseFailures = 0;
static bool SceneResolvePlayer(int index, int &user, ScenePlayerSelection &selection)
{
	char name[128];
	if (index < 1 || index > Mirv_Tf2_GetMaxPlayers() ||
	    !Mirv_Tf2_GetPlayerInfo(index, name, sizeof(name), &user) || user <= 0)
		return false;
	selection = {index, name};
	return true;
}
static bool ScenePlayerMatches(std::uint64_t user, int index = -1)
{
	if (!g_ScenePlayerFilter)
		return true;
	auto found = g_ScenePlayers.find(int(user));
	return found != g_ScenePlayers.end() && (index < 0 || found->second.Index == index);
}
static bool SceneSelectPosePlayer(int index)
{
	std::map<int, ScenePlayerSelection> selected;
	if (index > 0)
	{
		int user;
		ScenePlayerSelection player;
		if (!SceneResolvePlayer(index, user, player))
			return false;
		selected.emplace(user, std::move(player));
	}
	else if (index < -1)
		return false;
	g_ScenePlayerModels = index != 0;
	g_ScenePlayerFilter = index > 0;
	g_ScenePlayers = std::move(selected);
	g_ScenePoseStatus = "Capture preferences updated; changes affect future frames.";
	return true;
}
static ImGui::FrameIndexType g_SequencerFrame = 0;
// Session-local demo tick work range. Video/playback uses [In, Out); cache
// capture includes the Out sample so a cached loop can interpolate to it.
static int g_WorkIn = -1, g_WorkOut = -1;
static bool g_WorkLoop = false;
static double g_WorkLoopFirst = 0, g_WorkLoopLast = 0;
static std::uint64_t g_WorkLoopRevision = 0;
enum class WorkTransport { Idle, Playback, Armed, Starting, Recording };
static WorkTransport g_WorkTransport = WorkTransport::Idle;
static bool g_WorkDispatching = false;
static int g_WorkPreviousTick = -1;
static std::chrono::steady_clock::time_point g_WorkStartDeadline;
static std::string g_WorkStatus;
static bool WorkRangeValid() { return g_WorkIn >= 0 && g_WorkOut > g_WorkIn; }
static bool WorkRangeBusy() { return g_WorkTransport != WorkTransport::Idle || g_WorkLoop; }
static int g_SceneLastTick = -1;
static bool g_SceneCaptureResetPending=false;
static bool g_SceneRangeEnabled = false;
static int g_SceneRangeFirst = 0, g_SceneRangeLast = 660;
static unsigned long long g_SceneCaptureFrames = 0;
static double g_SceneCaptureMs = 0, g_SceneCaptureTotalMs = 0, g_SceneCapturePeakMs = 0;
static double g_SceneCaptureFirstTime = 0, g_SceneCaptureLastTime = 0;
static void SceneResetProfile()
{
	g_SceneCaptureFrames = 0;
	g_SceneCaptureMs = g_SceneCaptureTotalMs = g_SceneCapturePeakMs = 0;
	g_SceneCaptureFirstTime = g_SceneCaptureLastTime = 0;
}
static bool SceneCaptureTick(int tick)
{
	if (g_SceneRangeEnabled)
	{
		if (g_SceneRangeFirst < 0 || g_SceneRangeLast < g_SceneRangeFirst)
		{
			g_SceneRecord = false;
			g_ScenePoseStatus = "Invalid capture range: end must be at or after start.";
			return false;
		}
		if (tick > g_SceneRangeLast)
		{
			g_SceneRecord = false;
			g_ScenePoseStatus = "Capture range passed; recording stopped. Only observed ticks were retained.";
			return false;
		}
		if (tick < g_SceneRangeFirst)
			return false;
	}
	return tick != g_SceneLastTick;
}
static int g_SceneScrubRate = 0;
static auto g_SceneScrubLast = std::chrono::steady_clock::now();
static double SceneTickInterval();
static void ScenePreviewTick(int tick);
static void ScenePreviewTime(double time);

// Check the actual recorded clock, including internal gaps. A first sample
// within the In tick is acceptable; Out needs coverage for interpolation.
static bool WorkCacheBounds(double &first, double &last)
{
	const auto &frames = g_SceneCache.Frames();
	const double interval = SceneTickInterval();
	if (!WorkRangeValid() || frames.empty() || !std::isfinite(interval) || interval <= 0) return false;
	first = g_WorkIn * interval; last = g_WorkOut * interval;
	if (frames.front().DemoTime >= first + interval || frames.back().DemoTime < last - 1e-9) return false;
	first = (std::max)(first, frames.front().DemoTime);
	last = (std::min)(last, frames.back().DemoTime);
	if (!(last > first)) return false;
	auto it = std::lower_bound(frames.begin(), frames.end(), first,
		[](const auto &frame, double time){ return frame.DemoTime < time; });
	if (it != frames.begin()) --it;
	for (auto next = std::next(it); next != frames.end() && it->DemoTime < last; ++it, ++next)
		if (next->DemoTime > first && next->DemoTime - it->DemoTime > .1) return false;
	return true;
}
static double WorkWrapTime(double time, double first, double last)
{
	const double length = last - first;
	if (time >= first && time < last) return time;
	double offset = std::fmod(time - first, length);
	return first + (offset < 0 ? offset + length : offset);
}

static std::shared_ptr<const advancedfx::SceneCache::Frame> g_SceneView;
static bool g_SceneSampleReady = false, g_SceneSampleSmooth = false, g_SceneSamplePreview = false;
static std::uint64_t g_SceneSampleRevision = 0;
static double g_SceneSampleCursor = 0;
// Commit the model timeline first. A rejected transition cannot clear the
// other histories or allow later samples to accidentally join the old take.
static bool SceneCommitCaptureFrame(advancedfx::SceneCache::Frame frame,int tick){
    const bool reset=g_SceneCaptureResetPending||(g_SceneLastTick>=0&&tick<g_SceneLastTick)||
        (!g_SceneCache.Frames().empty()&&frame.DemoTime<g_SceneCache.Frames().back().DemoTime);
    if(!g_SceneCache.Append(std::move(frame),reset)){
        g_SceneCaptureResetPending=reset;
        return false;
    }
    if(reset){
        SceneClearEffects();SceneClearNativeParticles();
        ++g_SceneDrawEpoch;g_SceneSampleReady=false;SceneResetProfile();
    }
    g_SceneCaptureResetPending=false;
    return true;
}
// Single-run benchmark: engine-frame wall time, not GPU timestamps.
 // All mode changes are temporary; recording and demo transport are untouched.
struct SceneBenchState {
    bool Active=false, Measuring=false, ModelsOnly=false, Immediate=false;
    bool AutoQueue=false,Switching=false,SwitchWritten=false;
    int Suite=0,OriginalQueue=0,OwnedQueue=0,TargetQueue=0;
    std::vector<double> FrameMs;
    bool Audited=false;
    std::set<std::uint32_t> RendererTargets,PostRenderTargets;
    std::chrono::steady_clock::time_point PreviousRender;
    int Phase=0,Frames=0,OriginalSequencer=0;
    unsigned long long RenderCallbacks=0;
    bool Record=false,Preview=false,Models=false,MainOnly=false,Cull=false,Native=false,Legacy=false,Markers=false,LegacyMarkers=false,Residency=false,FastWrites=false,BatchStudio=false,FastReads=false,BatchQueued=false,ActorCull=false;
    bool Shadows=false,Decals=false,HideShadows=false,HideDecals=false,QueuedDecals=false,LegacyQueued=false;
    double Cursor=0,Late=0;
    std::chrono::steady_clock::time_point Start,LastRender;
    ScenePerfCounters Before;
    unsigned long long Submissions=0;
};
static SceneBenchState g_SceneBench;
static std::string g_SceneBenchReport;
static std::filesystem::path g_SceneBenchFile;
static bool g_SceneBenchRestoreQueue=false;
static int g_SceneBenchRestoreValue=0,g_SceneBenchExpectedQueue=0;
static void SceneBenchSave(){
    if(g_SceneBenchFile.empty())return;
    std::ofstream file(g_SceneBenchFile,std::ios::binary|std::ios::trunc);
    if(file){file.write(g_SceneBenchReport.data(),static_cast<std::streamsize>(g_SceneBenchReport.size()));file.flush();}
    if(!file)advancedfx::Message("Benchmark report file write failed; console report remains available.\n");
}
static void SceneBenchQueueRestore(){
    if(!g_SceneBenchRestoreQueue)return;
    g_SceneBenchRestoreQueue=false;
    float current=0;
    if(!Mirv_Tf2_SceneDrawVisibility("mat_queue_mode",current,false)){
        g_SceneBenchReport+="WARNING: queue restoration could not read cvar; restore manually.\n";
    }else if(current!=float(g_SceneBenchExpectedQueue)){
        g_SceneBenchReport+="Queue cvar changed externally; preserved current value.\n";
    }else{
        float saved=float(g_SceneBenchRestoreValue);
        if(Mirv_Tf2_SceneDrawVisibility("mat_queue_mode",saved,true))g_SceneBenchReport+="Original mat_queue_mode restored.\n";
        else g_SceneBenchReport+="WARNING: queue restoration failed; restore manually.\n";
    }
    SceneBenchSave();
}
static bool SceneBenchQueuedDecals(const SceneBenchState &b){return b.ModelsOnly&&!b.Immediate&&b.Decals&&b.QueuedDecals;}
static bool SceneBenchQueuedLegacy(const SceneBenchState &b){return b.ModelsOnly&&!b.Immediate&&b.Legacy&&b.LegacyQueued;}
static int SceneBenchLegacyPhase(const SceneBenchState &b){return 7+int(SceneBenchQueuedDecals(b));}
static int SceneBenchPhaseCount(){const auto &b=g_SceneBench;return b.ModelsOnly?7+int(SceneBenchQueuedDecals(b))+int(SceneBenchQueuedLegacy(b)):18;}
static const char *SceneBenchLabel(int phase){
    if(g_SceneBench.ModelsOnly){
        if(SceneBenchQueuedLegacy(g_SceneBench)&&phase==SceneBenchLegacyPhase(g_SceneBench))
            return SceneBenchQueuedDecals(g_SceneBench)?"Queued models + decals + legacy overlay":"Queued models + legacy overlay";
        static const char *names[]={"Live baseline (live effects)","Historical models, all views","Historical models, main only","Historical models, main + cull","Historical models, batching off","Historical models, batching on (repeat)","Historical models, actor-group cull","Historical models + queued decals"};
        return names[phase];
    }
    static const char *names[]={"Live baseline","Full cached preview","Main view only","Main + cull","Main + cull, historical particles off","Historical models and surfaces off","Optimized at latest tick","Earlier tick, previous memory path","Earlier tick, both optimizations","Earlier tick, residency only","Earlier tick, fast writes only","Earlier tick, batching off (both memory optimizations)","Earlier tick, batching on (repeat both)","Earlier tick, guarded reads off","Earlier tick, modern particles off","Earlier tick, legacy particles off","Earlier tick, historical surfaces off","Earlier tick, actor-group cull"};
    return names[phase];
}
static void SceneBenchApply(int phase,std::chrono::steady_clock::time_point now){
    auto &b=g_SceneBench;b.Phase=phase;b.Measuring=false;b.Frames=0;b.Start=now;b.LastRender=now;b.PreviousRender=now;b.FrameMs.clear();b.Audited=false;
    g_SceneRecord=false;g_ScenePreview=phase!=0;g_SceneDrawModels=b.ModelsOnly||phase!=5;
    g_SceneActorCull=b.ModelsOnly?phase>=6:phase+1==SceneBenchPhaseCount();
    g_SceneQueuedDecals=b.QueuedDecals;g_SceneLegacyQueued=b.LegacyQueued;
    if(b.ModelsOnly){
        g_SceneMainViewOnly=phase>=2;g_ScenePreviewCull=phase>=3;
        g_SceneKeepAssets=true;g_SceneBatchStudio=phase!=4;g_SceneBatchQueued=true;
        g_SceneNativeParticleReplay=g_SceneLegacyReplay=g_SceneNativeParticleMarkers=g_SceneLegacyMarkers=false;
        g_SceneHistoricalShadows=g_SceneHideShadows=g_SceneHideDecals=false;
        g_SceneHistoricalDecals=phase>=7&&SceneBenchQueuedDecals(b);
        g_SceneLegacyReplay=SceneBenchQueuedLegacy(b)&&phase==SceneBenchLegacyPhase(b);
        if(g_SceneHistoricalDecals)g_SceneHistoricalSurfaceStatus="No queued decal scope evaluated yet.";
        if(g_SceneLegacyReplay){g_SceneLegacyReplayTick=-1;g_SceneLegacyReplayResult={};g_SceneLegacyReplayResult.Status="No queued legacy draw evaluated yet.";}
        g_SceneCursor=b.Cursor;g_SequencerFrame=int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9));
        g_SceneSampleReady=false;++g_SceneDrawEpoch;return;
    }
    g_SceneMainViewOnly=phase>=2;g_ScenePreviewCull=phase>=3;
    g_SceneKeepAssets=phase!=7&&phase!=10;g_SceneFastParticleWrites=phase!=7&&phase!=9;
    g_SceneBatchStudio=phase!=11;
    g_SceneFastParticleReads=phase!=13;
    g_SceneNativeParticleReplay=b.Native&&phase!=4&&phase!=14;
    g_SceneLegacyReplay=b.Legacy&&phase!=4&&phase!=15;
    g_SceneHistoricalShadows=b.Shadows&&phase!=16;g_SceneHistoricalDecals=b.Decals&&phase!=16;
    g_SceneHideShadows=b.HideShadows&&phase!=16;g_SceneHideDecals=b.HideDecals&&phase!=16;
    g_SceneNativeParticleMarkers=false;g_SceneLegacyMarkers=false;
    g_SceneCursor=phase==6?b.Late:b.Cursor;
    g_SequencerFrame=int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9));
    g_SceneSampleReady=false;++g_SceneDrawEpoch;
}
static void SceneBenchStop(const char *reason){
    auto &b=g_SceneBench;if(!b.Active)return;b.Active=false;
    g_SceneRecord=b.Record;g_ScenePreview=b.Preview;g_SceneDrawModels=b.Models;
    g_SceneMainViewOnly=b.MainOnly;g_ScenePreviewCull=b.Cull;
    g_SceneKeepAssets=b.Residency;g_SceneFastParticleWrites=b.FastWrites;g_SceneBatchStudio=b.BatchStudio;g_SceneFastParticleReads=b.FastReads;
    g_SceneNativeParticleReplay=b.Native;g_SceneLegacyReplay=b.Legacy;
    g_SceneBatchQueued=b.BatchQueued;g_SceneActorCull=b.ActorCull;
    g_SceneHistoricalShadows=b.Shadows;g_SceneHistoricalDecals=b.Decals;g_SceneHideShadows=b.HideShadows;g_SceneHideDecals=b.HideDecals;g_SceneQueuedDecals=b.QueuedDecals;g_SceneLegacyQueued=b.LegacyQueued;
    g_SceneNativeParticleMarkers=b.Markers;g_SceneLegacyMarkers=b.LegacyMarkers;
    g_SceneCursor=b.Cursor;g_SequencerFrame=b.OriginalSequencer;
    g_SceneSampleReady=false;++g_SceneDrawEpoch;
    g_SceneBenchReport+=reason;g_SceneBenchReport+="\n";
    auto appendTargets=[&](const char *label,const std::set<std::uint32_t>& targets){
        g_SceneBenchReport+=label;char value[24]{};
        for(auto rva:targets){std::snprintf(value,sizeof(value)," 0x%x",unsigned(rva));g_SceneBenchReport+=value;}
        g_SceneBenchReport+="\n";
    };
    appendTargets("Observed client renderer targets (warm-up, not exhaustive):",b.RendererTargets);
    appendTargets("Observed client post-render targets (warm-up, not exhaustive):",b.PostRenderTargets);
    if(b.AutoQueue&&b.OwnedQueue!=b.OriginalQueue){
        g_SceneBenchRestoreQueue=true;g_SceneBenchRestoreValue=b.OriginalQueue;g_SceneBenchExpectedQueue=b.OwnedQueue;
        g_SceneBenchReport+="Queue cvar restoration pending at engine FRAME_START.\n";
    }
    SceneBenchSave();
    advancedfx::Message("%s\nUse mirv_cache perf bench report for all completed measurements.\n",reason);
}
static void SceneBenchSwitch(int suite,int mode,std::chrono::steady_clock::time_point now){
    auto &b=g_SceneBench;b.Suite=suite;b.TargetQueue=mode;b.Switching=true;b.SwitchWritten=false;
    b.Start=now;b.LastRender=now;b.Measuring=false;b.Frames=0;
    // Only live rendering while the material system changes contexts.
    g_SceneRecord=false;g_ScenePreview=false;g_SceneSampleReady=false;++g_SceneDrawEpoch;
    g_SceneBenchReport+="Switching benchmark to mat_queue_mode "+std::to_string(mode)+".\n";SceneBenchSave();
}
static void SceneBenchService(){
    std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
    SceneBenchQueueRestore();
    auto &b=g_SceneBench;if(!b.Active)return;
    const auto now=std::chrono::steady_clock::now();
    if(b.Switching&&!b.SwitchWritten){
        float current=0;
        if(!Mirv_Tf2_SceneDrawVisibility("mat_queue_mode",current,false)||current!=float(b.OwnedQueue)){
            SceneBenchStop("Benchmark cancelled: queue cvar unavailable or changed externally.");return;
        }
        float target=float(b.TargetQueue);
        // Claim a requested change even if its setter cannot verify it.
        b.OwnedQueue=b.TargetQueue;
        if(!Mirv_Tf2_SceneDrawVisibility("mat_queue_mode",target,true)){
            SceneBenchStop("Benchmark cancelled: queue cvar change could not be verified.");return;
        }
        b.SwitchWritten=true;b.Start=now;
    }
    if(now-b.LastRender>std::chrono::seconds(10)){
        SceneBenchStop("Benchmark cancelled: no ordinary render callback for ten seconds.");SceneCacheFrame(false);return;
    }
}
static bool SceneBenchTransportReady(bool modelsOnly){
    if(!modelsOnly)return Mirv_Tf2_ParticleIsolationReady();
    if(!g_VEngineClient||!g_VEngineClient->IsPlayingDemo()||g_VEngineClient->IsSkippingDemo()||
       g_VEngineClient->IsDrawingLoadingImage()||!MaterialSystem_CanExecuteOnRenderThread()||MirvCampathPreview_IsSeeking())return false;
    if(g_VEngineClient->IsPaused())return true;
    auto *demo=g_VEngineClient->GetDemoInfoEx();
    return demo&&demo->GetDemoPlaybackTimeScale()==0.0f;
}
// Called after the ordinary outer View_Render returns, not from demo time.
static void SceneBenchFrame(std::chrono::steady_clock::time_point now){
    auto &b=g_SceneBench;if(!b.Active)return;
    if(b.Switching){
        b.LastRender=now;
        if(!b.SwitchWritten){if(now-b.Start>std::chrono::seconds(10))SceneBenchStop("Benchmark stopped: no engine FRAME_START to request queue mode.");return;}
        const bool wantedImmediate=b.TargetQueue==0;
        if(MaterialSystem_IsImmediate()==wantedImmediate&&SceneCanDrawModels()){
            b.Switching=false;b.Immediate=wantedImmediate;b.ModelsOnly=b.Suite!=0;
            g_SceneBenchReport+=wantedImmediate?"Actual material context: immediate.\n":"Actual material context: queued.\n";
            SceneBenchApply(0,now);return;
        }
        if(now-b.Start>std::chrono::seconds(10))SceneBenchStop("Benchmark stopped: requested material context did not become active.");
        return;
    }
    if(!g_VEngineClient||!g_VEngineClient->IsPlayingDemo()||g_VEngineClient->IsSkippingDemo()||
       g_VEngineClient->IsDrawingLoadingImage()||!SceneCanDrawModels()||Recording::IsActive()||
       !SceneBenchTransportReady(b.ModelsOnly)||MaterialSystem_IsImmediate()!=b.Immediate||g_SceneScrubRate||g_WorkLoop){
        SceneBenchStop("Benchmark cancelled: playback, focus, recording, queue mode or transport changed.");return;
    }
    ++b.RenderCallbacks;
    b.LastRender=now;
    const double elapsed=std::chrono::duration<double>(now-b.Start).count();
    if(!b.Measuring){
        if(elapsed<0.5)return;
        b.Measuring=true;b.Start=now;b.PreviousRender=now;b.Before=g_ScenePerf;b.Submissions=g_SceneReplaySubmitted;b.Frames=0;return;
    }
    ++b.Frames;
    if(b.FrameMs.size()<8192)b.FrameMs.push_back(std::chrono::duration<double,std::milli>(now-b.PreviousRender).count());
    b.PreviousRender=now;
    if(elapsed<2.0)return;
    auto ms=[&](const ScenePerfStat &end,const ScenePerfStat &begin){return (end.Total-begin.Total)/b.Frames;};
    char line[512]{};
    std::snprintf(line,sizeof(line),"%s | tick %.2f | %.1f FPS (%.2f ms/frame; %d frames) | studio %.2f ms, particles %.2f ms, legacy %.2f ms, surfaces %.2f ms | models/frame %.1f, culled/frame %.1f\n",
        SceneBenchLabel(b.Phase),g_SceneCursor/SceneTickInterval(),b.Frames/elapsed,elapsed*1000/b.Frames,b.Frames,
        ms(g_ScenePerf.ModelDraw,b.Before.ModelDraw),ms(g_ScenePerf.ParticleDraw,b.Before.ParticleDraw),ms(g_ScenePerf.LegacyDraw,b.Before.LegacyDraw),
        ms(g_ScenePerf.SurfaceBegin,b.Before.SurfaceBegin)+ms(g_ScenePerf.SurfaceEnd,b.Before.SurfaceEnd),
        double(g_SceneReplaySubmitted-b.Submissions)/b.Frames,double(g_ScenePerf.ModelsCulled-b.Before.ModelsCulled)/b.Frames);
    g_SceneBenchReport+=line;
    advancedfx::Message("[Scene bench %d/%d] %s",b.Phase+1,SceneBenchPhaseCount(),line);
    if(!b.FrameMs.empty()){
        std::sort(b.FrameMs.begin(),b.FrameMs.end());
        const auto percentile=[&](double p){return b.FrameMs[static_cast<std::size_t>(p*(b.FrameMs.size()-1))];};
        std::snprintf(line,sizeof(line),"  Frame interval ms: median %.2f, p95 %.2f, p99 %.2f, max %.2f (%zu samples)\n",percentile(.5),percentile(.95),percentile(.99),b.FrameMs.back(),b.FrameMs.size());
        g_SceneBenchReport+=line;
    }
    if(b.Phase!=0){
        auto delta=[&](double end,double begin){return (end-begin)/b.Frames;};
        unsigned resident=0;unsigned long long hits=0,adds=0;Mirv_Tf2_SceneAssetStatus(resident,hits,adds);
        std::snprintf(line,sizeof(line),"  Particle stages ms/frame: factory %.2f, validation %.2f, copy %.2f, render %.2f, destroy %.2f | resident assets %u | writes direct %llu, system %llu, faults %llu, cleanup failures %llu\n",
            delta(g_ScenePerf.ParticleFactoryMs,b.Before.ParticleFactoryMs),delta(g_ScenePerf.ParticlePlanMs,b.Before.ParticlePlanMs),
            delta(g_ScenePerf.ParticleCopyMs,b.Before.ParticleCopyMs),delta(g_ScenePerf.ParticleRenderMs,b.Before.ParticleRenderMs),
            delta(g_ScenePerf.ParticleDestroyMs,b.Before.ParticleDestroyMs),resident,
            g_ScenePerf.FastWrites-b.Before.FastWrites,g_ScenePerf.SystemWrites-b.Before.SystemWrites,
            g_ScenePerf.WriteFaults-b.Before.WriteFaults,g_ScenePerf.CleanupFailures-b.Before.CleanupFailures);
        g_SceneBenchReport+=line;
        std::snprintf(line,sizeof(line),"  Particle extra stages ms/frame: gates %.2f, activation %.2f, active reset %.2f\n",
            delta(g_ScenePerf.ParticleGateMs,b.Before.ParticleGateMs),delta(g_ScenePerf.ParticleActivationMs,b.Before.ParticleActivationMs),delta(g_ScenePerf.ParticleResetMs,b.Before.ParticleResetMs));
        g_SceneBenchReport+=line;
        std::snprintf(line,sizeof(line),"  Detached validation reads: direct %llu, system %llu, faults %llu\n",
            g_ScenePerf.FastReads-b.Before.FastReads,g_ScenePerf.SystemReads-b.Before.SystemReads,g_ScenePerf.ReadFaults-b.Before.ReadFaults);
        g_SceneBenchReport+=line;
        std::snprintf(line,sizeof(line),"  Actor-group culled submissions/frame %.1f\n",double(g_ScenePerf.ActorsCulled-b.Before.ActorsCulled)/b.Frames);
        g_SceneBenchReport+=line;
        using T=advancedfx::SceneCache::ReplayTimings;
        const auto &a=g_ScenePerf.StudioStages.Ms;const auto &z=b.Before.StudioStages.Ms;
        std::snprintf(line,sizeof(line),"  Studio stages ms/frame: preflight/retire %.2f, asset %.2f, bones %.2f, state %.2f, draw %.2f, enqueue/restore %.2f\n",
            delta(a[T::Preflight],z[T::Preflight]),delta(a[T::Asset],z[T::Asset]),delta(a[T::Bones],z[T::Bones]),
            delta(a[T::State],z[T::State]),delta(a[T::Draw],z[T::Draw]),delta(a[T::Retirement],z[T::Retirement]));
        g_SceneBenchReport+=line;
        unsigned pending=0,peak=0;Mirv_Tf2_SceneRenderStatus(pending,peak);
        std::snprintf(line,sizeof(line),"  Queue owners outstanding %u (session peak %u); particle attempts/frame %.1f rejected/frame %.1f; legacy rejected/frame %.1f; sampled model CPU %.3f ms/frame; particle acquire %.3f ms/frame\n",pending,peak,
            double(g_ScenePerf.ParticleAttempts-b.Before.ParticleAttempts)/b.Frames,double(g_ScenePerf.ParticleRejected-b.Before.ParticleRejected)/b.Frames,double(g_ScenePerf.LegacyRejected-b.Before.LegacyRejected)/b.Frames,ms(g_ScenePerf.Sample,b.Before.Sample),ms(g_ScenePerf.ParticleAcquire,b.Before.ParticleAcquire));
        g_SceneBenchReport+=line;
        if(auto view=SceneCacheView()){
            std::size_t lit=0,total=0,lights=0;for(const auto &e:view->Entities)if(e.ModelPose&&e.ModelPose->ModelInfo->WireEdges.empty()){
                ++total;if(e.ModelLighting){++lit;lights+=e.ModelLighting->Count;}
            }
            g_SceneBenchReport+="  Historical model lighting requested "+std::to_string(int(g_SceneReplayLighting))+"; sampled "+std::to_string(lit)+"/"+std::to_string(total)+" studio poses, "+std::to_string(lights)+" local descriptors\n";
        }
        if(!b.ModelsOnly){
            const int tick=int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9));
            if(const auto *p=g_SceneNativeParticles.ForPreview(tick)){
                std::snprintf(line,sizeof(line),"  Particle coverage: requested %d resolved %d complete %d appearance %d appearance complete %d collections %zu\n",tick,p->Tick,int(p->Complete),int(p->Appearance),int(p->AppearanceComplete),p->Collections.size());
                g_SceneBenchReport+=line;
            }
            if(const auto surface=g_SceneSurfaces.ResolvePreview(tick)){
                g_SceneBenchReport+="  Historical surface ticks: shadows "+std::to_string(surface.ShadowTick)+", decals "+std::to_string(surface.DecalTick)+" (-1 unavailable)\n";
                if(surface.DecalTick>=0){
                    std::size_t geometry=0;if(surface.Value.Decals)for(const auto &d:*surface.Value.Decals)geometry+=d.HasGeometry();
                    g_SceneBenchReport+="  Historical decal exclusions: "+surface.Value.UnsupportedDecals.Describe()+"; clipped geometry "+std::to_string(geometry)+"/"+std::to_string(surface.Value.Decals?surface.Value.Decals->size():0)+"; detached replay "+std::to_string(int(g_SceneRemovedDecals))+"; surviving opacity "+std::to_string(int(g_SceneDecalOpacity))+"\n";
                }
            }
            g_SceneBenchReport+="  Recorded preflight factories avoided: "+std::to_string(g_ScenePerf.ParticlePreflightRejected-b.Before.ParticlePreflightRejected)+"\n";
            g_SceneBenchReport+="  Isolation eligibility: "+g_SceneIsolationEligibility+"\n";
            g_SceneBenchReport+="  Native particle status: "+g_SceneParticleReplayResult.Status+"\n  Legacy status: "+g_SceneLegacyReplayResult.Status+"\n  Surface status: "+g_SceneHistoricalSurfaceStatus+"\n";
        }
    }
    if(SceneBenchQueuedDecals(b)&&b.Phase>=7)g_SceneBenchReport+="  Queued decal status: "+g_SceneHistoricalSurfaceStatus+"\n";
    if(SceneBenchQueuedLegacy(b)&&b.Phase==SceneBenchLegacyPhase(b))g_SceneBenchReport+="  Queued legacy overlay status: "+g_SceneLegacyReplayResult.Status+
        "; submitted "+std::to_string(g_SceneLegacyReplayResult.Particles)+", rejected "+std::to_string(g_SceneLegacyReplayResult.Rejected)+" (live effects retained).\n";
    if(!b.RendererTargets.empty()||!b.PostRenderTargets.empty()){
        auto append=[&](const char *label,const std::set<std::uint32_t>& targets){
            g_SceneBenchReport+=label;char value[24]{};
            for(auto rva:targets){std::snprintf(value,sizeof(value)," 0x%x",unsigned(rva));g_SceneBenchReport+=value;}
            g_SceneBenchReport+="\n";
        };
        append("  Cumulative observed renderer RVAs:",b.RendererTargets);
        append("  Cumulative observed post-render RVAs:",b.PostRenderTargets);
    }
    SceneBenchSave();
    if(b.Phase+1==SceneBenchPhaseCount()){
        if(b.AutoQueue&&b.Suite==0){
            b.Suite=1;b.ModelsOnly=true;
            g_SceneBenchReport+="\nModel-only immediate comparison; live effects retained.\n";
            SceneBenchApply(0,now);
        }else if(b.AutoQueue&&b.Suite==1){
            SceneBenchSwitch(2,2,now);
            g_SceneBenchReport+="\nModel-only queued comparison; live effects retained.\n";
        }else SceneBenchStop("Benchmark complete; original scene settings restored.");
    }else SceneBenchApply(b.Phase+1,now);
}
void SceneCacheBenchmarkRenderEnd(){
    std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
    if(!g_SceneBench.Active)return;
    const int phase=g_SceneBench.Phase,suite=g_SceneBench.Suite;const bool switching=g_SceneBench.Switching;
    SceneBenchFrame(std::chrono::steady_clock::now());
    if(!g_SceneBench.Active||phase!=g_SceneBench.Phase||suite!=g_SceneBench.Suite||switching!=g_SceneBench.Switching)SceneCacheFrame(false);
}
static void SceneClearHistory()
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	SceneBenchStop("Benchmark cancelled: history cleared.");
	++g_SceneDrawEpoch;
	g_SceneSampleReady = false;
	g_SceneCache.Clear();
	SceneClearEffects();
	SceneClearNativeParticles();
	g_SceneDrawing.BoundEntities.clear();
	g_SceneAuditFrame = -1; g_SceneAuditDraws.clear();
    Mirv_Tf2_BeginSceneLightingAudit(false);g_SceneLightingObserverReady=false;g_SceneLightingStatus="Historical model lighting not captured.";
	g_SceneAuditStatus = "No same-frame native model observation yet.";
    g_SceneReplayCallbacks=g_SceneReplayAttempted=g_SceneReplaySubmitted=g_SceneReplayQueueSkips=g_SceneReplayOtherSkips=0;
    g_SceneReplayFrame=-1;g_SceneReplayFrameCallbacks=g_SceneReplayMaxCallbacks=0;g_SceneReplayLastFailure.clear();
	g_ScenePoseCaptured = g_ScenePoseFailures = 0;
	SceneResetProfile();
	g_SceneLastTick = -1;g_SceneCaptureResetPending=false;
	g_ScenePerf={};
	g_SceneScrubRate = 0;
	if (g_WorkLoop) g_WorkStatus = "Cached loop stopped: cache cleared.";
	g_WorkLoop = false;
	std::atomic_store(&g_SceneModelStatus, std::shared_ptr<const std::string>{});
	std::atomic_store(&g_SceneView, std::shared_ptr<const advancedfx::SceneCache::Frame>{});
}
std::shared_ptr<const advancedfx::SceneCache::Frame> SceneCacheView()
{
	return std::atomic_load(&g_SceneView);
}
bool SceneCachePreviewActive() { std::lock_guard<std::recursive_mutex> lock(g_SceneMutex); return g_ScenePreview; }
bool SceneCacheAuditActive()
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	const bool active=g_SceneRenderSnapshot && g_SceneRecord && !g_ScenePreview;
    g_SceneLightingObserverReady=Mirv_Tf2_BeginSceneLightingAudit(active&&g_SceneLightingCapture);
    if(!g_SceneLightingObserverReady&&active&&g_SceneLightingCapture)
        g_SceneLightingStatus="Historical lighting observer unavailable (exact TF2 x64 ABI or studio hooks).";
    return active;
}
int SceneCacheParticleMessage(const char *name, const void *state)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	if (!g_SceneEffectsEnabled || !g_SceneRecord || g_ScenePreview || !g_VEngineClient ||
	    !g_VEngineClient->IsPlayingDemo() || g_VEngineClient->IsDrawingLoadingImage()) return -1;
	if (g_VEngineClient->IsSkippingDemo()) { g_SceneEffects.Interrupt(); return -1; }
	int tick;
	if (!GetCurrentDemoTick(tick) || tick < 0 ||
	    (g_SceneRangeEnabled && (tick < g_SceneRangeFirst || tick > g_SceneRangeLast))) return -1;
	// Events may precede model acceptance. Keep pending-take messages out
    // of the old diagnostic journal; resume at the first accepted frame.
    if(g_SceneCaptureResetPending||(g_SceneLastTick>=0&&tick<g_SceneLastTick)||
       (g_SceneEffects.Last>=0&&tick<g_SceneEffects.Last)){
        g_SceneCaptureResetPending=true;return -1;
    }
	// Keep creation/destruction while paused, but ignore redundant setter traffic
	// only through value compaction; genuine paused updates can affect the sample.
	g_SceneEffects.Observe(tick);
	advancedfx::SceneEffects::Event e;
	auto result = advancedfx::SceneEffects::Decode(name, state, tick, e);
	if (result != advancedfx::SceneEffects::DecodeResult::Valid) {
	    g_SceneEffects.Note(result); return -1;
	}
	if (g_VEngineClient->IsPaused()) ++g_SceneEffects.Paused;
	g_SceneEffects.Push(e);
	const auto kind=static_cast<unsigned>(e.Type);
	if (kind>=g_SceneEffectsTraceAttempted.size() || g_SceneEffectsTraceAttempted[kind]) return -1;
	g_SceneEffectsTraceAttempted[kind]=true;
	return int(kind);
}
void SceneCacheParticleCallsites(int kind, std::vector<std::uintptr_t> rvas)
{
    std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
    if (kind>=0 && static_cast<std::size_t>(kind)<g_SceneEffectsCallsites.size() &&
        g_SceneEffectsTraceAttempted[kind])
        g_SceneEffectsCallsites[kind]=std::move(rvas);
}
static void SceneEffectsStatus(int tick, bool detail)
{
	const auto &j = g_SceneEffects;
	advancedfx::Message("Particle event probe: enabled %d; %zu events, %.2f MiB event payload; ticks %d .. %d; "
	    "create %zu, destroy %zu, stop %zu, object %zu, position %zu, orientation %zu; "
	    "invalid %zu, unknown %zu, dropped %zu; interrupted %d.\n",
	    int(g_SceneEffectsEnabled), j.Events.size(), j.Events.size() * sizeof(advancedfx::SceneEffects::Event) / 1048576.0,
	    j.First, j.Last, j.Counts[0], j.Counts[1], j.Counts[2], j.Counts[3], j.Counts[4], j.Counts[5],
	    j.Invalid, j.Unknown, j.Dropped, int(j.Interrupted));
	advancedfx::Message("Particle compaction: raw create %zu, destroy %zu, stop %zu, object %zu, position %zu, orientation %zu; "
	    "unchanged %zu, same-tick replaced %zu, paused messages %zu; time-like destroy IDs %zu; "
	    "last retained event tick %d, first overflow tick %d.\n", j.RawCounts[0], j.RawCounts[1], j.RawCounts[2],
	    j.RawCounts[3], j.RawCounts[4], j.RawCounts[5], j.Unchanged, j.Coalesced, j.Paused, j.SuspectDestroys, j.LastRetained, j.FirstDrop);
	static const char *names[]={"Create","Destroy","Stop","Object","Position","Orientation"};
	for (std::size_t i=0;i<g_SceneEffectsCallsites.size();++i) if (g_SceneEffectsTraceAttempted[i]) {
	    std::string line=std::string("Particle client return RVAs [")+names[i]+"]:";
	    for (auto rva:g_SceneEffectsCallsites[i]) {
	        static const char digits[]="0123456789abcdef";
	        char buf[2*sizeof(rva)]; std::size_t n=0;
	        do { buf[n++]=digits[rva & 15]; rva >>= 4; } while(rva);
	        line+=" 0x"; while(n) line+=buf[--n];
	    }
	    if (g_SceneEffectsCallsites[i].empty()) line+=" none (stack unavailable)";
	    advancedfx::Message("%s\n",line.c_str());
	}
	if (tick < 0) return;
	const auto sample = j.At(tick);
	if (!sample.InWindow) { advancedfx::Message("Particle tick %d outside observed window.\n", tick); return; }
	std::size_t points = 0, positioned = 0, stopped = 0;
	for (const auto &row : sample.Systems) {
	    stopped += row.second.StopReported; points += row.second.Points.size();
	    for (const auto &p : row.second.Points) positioned += p.second.HasPosition;
	}
	advancedfx::Message("Particle tick %d: %zu reported systems, %zu emission stops, %zu control points (%zu positioned); "
	    "%zu retained unknown updates, %zu unmatched destroys, %zu recreated IDs; %zu ambiguous destroys; truncated %d, interrupted %d. "
	    "These are event-derived states, not live particle counts.\n", tick, sample.Systems.size(), stopped, points, positioned,
	    sample.UnknownUpdates, sample.UnmatchedDestroys, sample.RecreatedIds, sample.SuspectDestroys, int(sample.Truncated), int(sample.Interrupted));
	if (sample.Truncated) advancedfx::Message("WARNING: requested tick is at/after first overflow; this is an incomplete event-derived sample, not current state.\n");
	if (!detail) return;
	std::size_t shown = 0;
	for (const auto &row : sample.Systems) {
	    if (shown++ >= 16) { advancedfx::Message("Additional systems omitted.\n"); break; }
	    const auto &system = row.second;
	    advancedfx::Message("  id %d: %s; owner %d; created %d; CPs %zu; stop %d\n", row.first,
	        system.Name.data(), system.Owner, system.CreatedTick, system.Points.size(), int(system.StopReported));
	    for (const auto &p : system.Points) if (p.second.HasPosition) {
	        const auto &v = p.second.Position;
	        advancedfx::Message("    CP%d: %.2f %.2f %.2f\n", p.first, v[0], v[1], v[2]); break;
	    }
	}
}
void SceneCachePublishModelAudit(int frame, std::map<const void *, int> submissions)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	g_SceneAuditFrame = frame;
	g_SceneAuditDraws = std::move(submissions);
    Mirv_Tf2_FinishSceneLightingAudit(frame);
}
bool SceneCacheReplayActive()
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	if (!g_SceneRenderSnapshot || !g_ScenePreview || !g_SceneDrawModels.load() || !SceneCanDrawModels() ||
	    !MaterialSystem_CanExecuteOnRenderThread()) return false;
	const auto view = SceneCacheView();
	// A genuinely empty historical frame must not show actors from the future.
	return view && view->RenderSnapshot;
}
bool SceneCacheSuppressLiveStudio()
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	if(!g_SceneSuppressLiveStudio)return false;
    if(SceneCacheReplayActive())return true;
    // Explicit benchmark model-off mode keeps requested live studio
    // suppression, but only with the same broad-history/device/queue guards.
    if(!g_SceneBench.Active||g_SceneBench.Phase!=5||!g_SceneBench.Models||
       !g_SceneRenderSnapshot||!g_ScenePreview||!SceneCanDrawModels()||
       !MaterialSystem_CanExecuteOnRenderThread())return false;
    const auto view=SceneCacheView();return view&&view->RenderSnapshot;
}
void VisitCameraSceneFrames(
	const std::function<void(const std::deque<advancedfx::SceneCache::Frame> &, std::uint64_t)> &visitor)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	visitor(g_SceneCache.Frames(), g_SceneCache.Revision());
}
bool SampleCameraScene(double clientTime, advancedfx::SceneCache::Frame &frame)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	const auto &frames = g_SceneCache.Frames();
	static auto revision = ~g_SceneCache.Revision();
	static bool sorted = false;
	if (revision != g_SceneCache.Revision())
	{
		revision = g_SceneCache.Revision();
		sorted = std::is_sorted(frames.begin(), frames.end(),
								[](const auto &a, const auto &b) { return a.ClientTime < b.ClientTime; });
	}
	if (sorted)
	{
		auto right = std::lower_bound(frames.begin(), frames.end(), clientTime,
									  [](const auto &f, double t) { return f.ClientTime < t; });
		if (right != frames.end() && std::abs(right->ClientTime - clientTime) < 1e-9)
		{
			frame = *right;
			return true;
		}
		if (right == frames.begin() || right == frames.end())
			return false;
		const auto &left = *std::prev(right);
		const double f = (clientTime - left.ClientTime) / (right->ClientTime - left.ClientTime);
		return g_SceneCache.Sample(left.DemoTime + f * (right->DemoTime - left.DemoTime), frame, .1, 256,
								   true);
	}
	for (size_t i = 0; i < frames.size(); ++i)
	{
		const auto &a = frames[i];
		if (std::abs(a.ClientTime - clientTime) < 1e-9)
		{
			frame = a;
			return true;
		}
		if (i + 1 < frames.size())
		{
			const auto &b = frames[i + 1];
			if (a.ClientTime < clientTime && clientTime < b.ClientTime)
			{
				const double f = (clientTime - a.ClientTime) / (b.ClientTime - a.ClientTime);
				return g_SceneCache.Sample(a.DemoTime + f * (b.DemoTime - a.DemoTime), frame, .1, 256, true);
			}
		}
	}
	return false;
}
double EditingClientTime(double liveTime)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	if (!g_ScenePreview) return liveTime;
	auto view = SceneCacheView();
	if (view)
		return view->ClientTime;
	// Missing entity coverage must not switch a cached sequence to the live clock.
	const auto &frames = g_SceneCache.Frames();
	if (frames.empty())
		return liveTime;
	auto right =
		std::lower_bound(frames.begin(), frames.end(), g_SceneCursor,
						 [](const advancedfx::SceneCache::Frame &f, double t) { return f.DemoTime < t; });
	if (right == frames.begin())
		return right->ClientTime + g_SceneCursor - right->DemoTime;
	if (right == frames.end())
		return frames.back().ClientTime + g_SceneCursor - frames.back().DemoTime;
	const auto &left = *std::prev(right);
	const double f = (g_SceneCursor - left.DemoTime) / (right->DemoTime - left.DemoTime);
	return left.ClientTime + f * (right->ClientTime - left.ClientTime);
}
void SceneCacheFrame(bool capture)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	if (capture)
	{
		Mirv_Tf2_SceneRenderFrame();
        Mirv_Tf2_SceneAssetResidency(g_SceneKeepAssets&&g_ScenePreview&&g_SceneDrawModels.load()&&g_SceneRenderSnapshot&&SceneCacheView()&&g_VEngineClient&&g_VEngineClient->IsPlayingDemo()&&!g_VEngineClient->IsSkippingDemo()&&!g_VEngineClient->IsDrawingLoadingImage()&&SceneCanDrawModels());
        Mirv_Tf2_SetSurfaceObservation(g_SceneSurfaceCapture&&g_SceneRecord&&!g_ScenePreview&&g_VEngineClient&&g_VEngineClient->IsPlayingDemo()&&!g_VEngineClient->IsSkippingDemo()&&!g_VEngineClient->IsDrawingLoadingImage());
        if(std::exchange(g_SceneLegacyAuditPending,false)) {
            int liveTick=-1;
            if(g_ScenePreview||!GetCurrentDemoTick(liveTick))
                advancedfx::Message("Legacy particle audit cancelled: requires live demo view.\n");
            else {
                const auto audit=Mirv_Tf2_InspectLegacyParticles();
                advancedfx::Message("Legacy particle audit at actual demo tick %d (not cached cursor):\n%s\n",liveTick,audit.c_str());
            }
        }
        if(g_SceneDetachedIndex>=0) {
            const int index=std::exchange(g_SceneDetachedIndex,-1);
            const int requested=std::exchange(g_SceneDetachedTick,-1);
            const auto *sample=g_SceneNativeParticles.At(requested);
            g_SceneDetachedStatus=g_SceneRecord?"Stop cache recording before preparing a detached collection.":sample?Mirv_Tf2_ProbeDetachedParticles(*sample,static_cast<std::size_t>(index)):"Requested sample no longer retained.";
            g_SceneDetachedStatus="Tick "+std::to_string(requested)+", collection "+std::to_string(index)+": "+g_SceneDetachedStatus;
            advancedfx::Message("Native detached probe: %s\n",g_SceneDetachedStatus.c_str());
        }
		const auto epoch = g_SceneLifecycleEpoch.load();
		if (g_SceneReadyEpoch != epoch)
		{
			g_SceneReadyEpoch = epoch;
			g_SceneReadyFrames = 0;
		}
		if (g_SceneFocused.load() && g_SceneDeviceReady.load())
			g_SceneReadyFrames = (std::min)(g_SceneReadyFrames.load() + 1, 2u);
		else
			g_SceneReadyFrames = 0;
	}
	int tick;
	double time;
	if (capture)
	{
		auto now = std::chrono::steady_clock::now();
		double elapsed = std::chrono::duration<double>(now - g_SceneScrubLast).count();
		g_SceneScrubLast = now;
		if (g_WorkLoop)
		{
			if (!g_ScenePreview || !g_VEngineClient || !g_VEngineClient->IsPlayingDemo() ||
				g_VEngineClient->IsDrawingLoadingImage())
				g_WorkLoop = false;
			else if (g_WorkLoopRevision != g_SceneCache.Revision())
			{
				g_WorkLoop = WorkCacheBounds(g_WorkLoopFirst, g_WorkLoopLast);
				g_WorkLoopRevision = g_SceneCache.Revision();
			}
			if (g_WorkLoop)
				ScenePreviewTime(WorkWrapTime(g_SceneCursor + std::clamp(elapsed, 0.0, .1),
					g_WorkLoopFirst, g_WorkLoopLast));
			else
				g_WorkStatus = "Cached loop stopped: preview or coverage changed.";
		}
		if (g_ScenePreview && g_SceneScrubRate && !g_SceneCache.Frames().empty() && g_VEngineClient &&
		    g_VEngineClient->IsPlayingDemo() && !g_VEngineClient->IsDrawingLoadingImage())
		{
			const double first = g_SceneCache.Frames().front().DemoTime;
			const double last = g_SceneCache.Frames().back().DemoTime;
			ScenePreviewTime(std::clamp(g_SceneCursor + g_SceneScrubRate * std::clamp(elapsed, 0.0, .1) *
			                                                SceneTickInterval(),
			                            first, last));
		}
	}
	if (!g_VEngineClient || !g_VEngineClient->IsPlayingDemo())
	{
		if (capture)
			Mirv_Tf2_ReleaseSceneMaterials();
		if (g_ScenePreview)
			++g_SceneDrawEpoch;
		g_SceneSampleReady = false;
		g_ScenePreview = false;
		g_SceneScrubRate = 0;
		g_WorkLoop = false;
		std::atomic_store(&g_SceneView, std::shared_ptr<const advancedfx::SceneCache::Frame>{});
		return;
	}
	if (capture && g_SceneDeviceReady.load() && g_SceneRecord && !(g_SceneRenderSnapshot && g_ScenePreview) && !g_VEngineClient->IsSkippingDemo() &&
	    GetCurrentDemoTick(tick) && GetCurrentDemoTime(time) && SceneCaptureTick(tick))
	{
		const auto captureStart = std::chrono::steady_clock::now();
		advancedfx::SceneCache::Frame frame;
		frame.DemoTime = time;
		frame.RenderSnapshot = g_SceneRenderSnapshot;
		frame.ClientTime = g_MirvTime.GetTime();
		if (auto globals = g_Hook_VClient_RenderView.GetGlobals())
			MirvCam_CaptureCameras(frame.ClientTime, globals->framecount_get(),
								   globals->absoluteframetime_get(), frame.Cameras);
		size_t selectedSeen = 0;
		std::vector<MirvTf2SceneEntity> entities;
		const bool broad = frame.RenderSnapshot;
		const auto globals = g_Hook_VClient_RenderView.GetGlobals();
		const bool audit = broad && globals && g_SceneAuditFrame == globals->framecount_get();
		size_t seen = 0, unseen = 0, matched = 0, nativeSkins = 0, networkSkins = 0, teamSkins = 0, unknownSkins = 0;
        size_t lightingCaptured=0,lightingModels=0,localLights=0;
		Mirv_Tf2_CollectSceneEntities(broad || g_SceneProjectiles, broad || g_SceneWeapons, broad || g_SceneCosmetics,
		                              broad || g_SceneRagdolls, broad || g_SceneGibs, broad || g_SceneBuildings,
		                              broad || g_SceneProps, entities, broad);
		unsigned int playerPoses = 0, projectilePoses = 0, weaponPoses = 0, cosmeticPoses = 0, failures = 0;
		unsigned int ragdollPoses = 0, gibPoses = 0, buildingPoses = 0, propPoses = 0, otherPoses = 0;
		std::string lastIssue;
		for (auto &row : entities)
		{
			auto &entity = row.Snapshot;
			const auto observed = audit ? g_SceneAuditDraws.find(row.ObservedRenderable) : g_SceneAuditDraws.end();
			if (observed != g_SceneAuditDraws.end())
			{
				++matched;
				if (observed->second >= 0) { entity.Skin = observed->second; entity.SkinFrom = advancedfx::SceneCache::Entity::SkinSource::Native; }
			}
			const bool player = entity.Category == MIRV_TF2_ENTITY_PLAYER;
			const bool selected = player && (broad || ScenePlayerMatches(entity.Identity, row.Index));
			selectedSeen += selected;
			if (player && !selected)
				continue; // A restrictive filter also excludes unselected player proxies.
			const bool gear =
			    entity.Category == MIRV_TF2_ENTITY_WEAPON || entity.Category == MIRV_TF2_ENTITY_COSMETIC;
			if (!broad && gear && entity.OwnerIdentity && !(entity.OwnerIdentity >> 63) &&
			    !ScenePlayerMatches(entity.OwnerIdentity))
				continue;

			if (broad || (player && g_ScenePlayerModels && selected) ||
			    (entity.Category == MIRV_TF2_ENTITY_PROJECTILE && g_SceneProjectiles) || gear ||
			    (entity.Category == MIRV_TF2_ENTITY_RAGDOLL && g_SceneRagdolls) ||
			    (entity.Category == MIRV_TF2_ENTITY_GIB && g_SceneGibs) ||
			    (entity.Category == MIRV_TF2_ENTITY_BUILDING && g_SceneBuildings) ||
			    (entity.Category == MIRV_TF2_ENTITY_PROP && g_SceneProps))
			{
				std::string status;
				if (Mirv_Tf2_CaptureScenePose(row.Index, entity, status, row.ObservedObject))
				{
					++g_ScenePoseCaptured;
					if (broad && entity.ModelPose && entity.ModelPose->ModelInfo->WireEdges.empty())
					{
						if (audit) (observed != g_SceneAuditDraws.end() ? seen : unseen)++;
                        if(g_SceneLightingCapture){
                            ++lightingModels;
                            if(audit&&observed!=g_SceneAuditDraws.end()&&Mirv_Tf2_CaptureSceneLighting(g_SceneAuditFrame,row.ObservedRenderable,entity)){
                                ++lightingCaptured;localLights+=entity.ModelLighting->Count;
                            }
                        }
						using Source = advancedfx::SceneCache::Entity::SkinSource;
						switch (entity.SkinFrom) {
						case Source::Native: ++nativeSkins; break;
						case Source::Network: ++networkSkins; break;
						case Source::TeamFallback: ++teamSkins; break;
						default: ++unknownSkins; break;
						}
					}
					if (player)
						++playerPoses;
					else if (entity.Category == MIRV_TF2_ENTITY_WEAPON)
						++weaponPoses;
					else if (entity.Category == MIRV_TF2_ENTITY_COSMETIC)
						++cosmeticPoses;
					else if (entity.Category == MIRV_TF2_ENTITY_RAGDOLL)
						++ragdollPoses;
					else if (entity.Category == MIRV_TF2_ENTITY_GIB)
						++gibPoses;
					else if (entity.Category == MIRV_TF2_ENTITY_BUILDING)
						++buildingPoses;
					else if (entity.Category == MIRV_TF2_ENTITY_PROP)
						++propPoses;
					else if (entity.Category == MIRV_TF2_ENTITY_PROJECTILE)
						++projectilePoses;
					else ++otherPoses;
				}
				else
				{
					++g_ScenePoseFailures;
					++failures;
					lastIssue = "Entity " + std::to_string(row.Index) + ": " + status;
				}
			}
			frame.Entities.push_back(std::move(entity));
		}
		if (broad || g_ScenePlayerModels || g_SceneProjectiles || g_SceneRagdolls || g_SceneGibs || g_SceneBuildings ||
		    g_SceneProps || g_SceneWeapons || g_SceneCosmetics)
		{
			g_ScenePoseStatus =
			    "Tick " + std::to_string(tick) + ": " + std::to_string(playerPoses) + " player poses, " +
			    std::to_string(projectilePoses) + " projectile poses, " + std::to_string(weaponPoses) +
			    " weapon poses, " + std::to_string(cosmeticPoses) + " cosmetic poses, " +
			    std::to_string(ragdollPoses) + " ragdoll poses, " + std::to_string(gibPoses) +
			    " gib poses, " + std::to_string(buildingPoses) + " building poses, " +
			    std::to_string(propPoses) + " prop poses, " + std::to_string(otherPoses) + " other poses; " + std::to_string(failures) + " unavailable.";
			if (broad) g_ScenePoseStatus += " Broad snapshot: " + std::to_string(entities.size()) + " registered renderables.";
			if (!lastIssue.empty())
				g_ScenePoseStatus += " " + lastIssue;
		}
		if (!broad && g_ScenePlayerFilter && g_ScenePlayerModels && selectedSeen < g_ScenePlayers.size())
			g_ScenePoseStatus += " " + std::to_string(selectedSeen) + "/" +
			                     std::to_string(g_ScenePlayers.size()) + " selected players available.";
		if (broad)
		{
			g_SceneAuditStatus = audit ? "Native model audit: " + std::to_string(g_SceneAuditDraws.size()) +
				" distinct observed submissions; " + std::to_string(matched) + " matched renderables; " +
				std::to_string(seen) + " captured studio poses seen by hook, " + std::to_string(unseen) +
				" not seen by hook (off-screen or other submission path)." : "No matching native view-frame audit; coverage unmeasured.";
			g_SceneAuditStatus += " Skins: " + std::to_string(nativeSkins) + " native, " +
				std::to_string(networkSkins) + " network, " + std::to_string(teamSkins) + " team fallback, " +
				std::to_string(unknownSkins) + " unknown.";
            if(g_SceneLightingCapture&&audit&&g_SceneLightingObserverReady){
                g_SceneLightingStatus="Tick "+std::to_string(tick)+": "+std::to_string(lightingCaptured)+"/"+std::to_string(lightingModels)+
                    " studio poses with same-view historical lighting; "+std::to_string(localLights)+" local light descriptors. Unobserved/static/ambiguous models use neutral fallback.";
            }
			g_SceneAuditDraws.clear(); g_SceneAuditFrame = -1;
            Mirv_Tf2_BeginSceneLightingAudit(false);
		}
		const bool modelAccepted=SceneCommitCaptureFrame(std::move(frame),tick);
        if(!modelAccepted)g_ScenePoseStatus=g_SceneCaptureResetPending?
            "New take rejected; previous model/effect histories preserved. Waiting for a valid new take.":
            "Scene frame rejected (invalid data or payload budget too small).";
        // Ordinary forward coverage remains independent, but never mix
        // samples from a rejected new take with the retained old interval.
        if(!g_SceneCaptureResetPending){
		if (g_SceneEffectsEnabled) g_SceneEffects.Observe(tick);
		if (g_SceneNativeParticlesEnabled && !g_ScenePreview) {
		    const auto begin=std::chrono::steady_clock::now();
		    auto particles=Mirv_Tf2_CaptureNativeParticles(tick,g_SceneNativeParticleAppearance);
		    g_SceneNativeParticles.Append(std::move(particles));
		    g_SceneNativeParticleMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
		    g_SceneNativeParticleTotalMs+=g_SceneNativeParticleMs;
		    g_SceneNativeParticlePeakMs=(std::max)(g_SceneNativeParticlePeakMs,g_SceneNativeParticleMs);
		    ++g_SceneNativeParticleSamples;
		}
        if(g_SceneLegacyCapture&&!g_ScenePreview) {
            const auto begin=std::chrono::steady_clock::now();
            advancedfx::LegacyParticles::Frame legacy;legacy.Tick=tick;
            g_SceneLegacyCaptureStatus=Mirv_Tf2_InspectLegacyParticles(&legacy,g_SceneLegacyAll);
            if(!g_SceneLegacyHistory.Append(std::move(legacy)))
                g_SceneLegacyCaptureStatus+="; historical sample rejected.";
            g_ScenePerf.LegacyCapture.Add(begin,tick);
        }
        if(g_SceneSurfaceCapture&&!g_ScenePreview){
            const auto begin=std::chrono::steady_clock::now();
            g_SceneSurfaces.Append(Mirv_Tf2_CaptureSurfaces(tick));
            g_ScenePerf.SurfaceCapture.Add(begin,tick);
        }
        }
		g_SceneLastTick = tick;
		g_SceneCaptureMs =
		    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - captureStart)
		        .count();
		g_SceneCaptureTotalMs += g_SceneCaptureMs;
		g_SceneCapturePeakMs = (std::max)(g_SceneCapturePeakMs, g_SceneCaptureMs);
		if (!g_SceneCaptureFrames++)
			g_SceneCaptureFirstTime = time;
		g_SceneCaptureLastTime = time;
		if (g_SceneRangeEnabled && tick == g_SceneRangeLast)
		{
			g_SceneRecord = false;
			g_ScenePoseStatus += " Capture range complete; recording stopped.";
		}
	}
	// Reuse the published sample when idle; capture and render retirement above still run.
	if (g_SceneSampleReady && g_SceneSampleRevision == g_SceneCache.Revision() &&
	    g_SceneSampleCursor == g_SceneCursor && g_SceneSampleSmooth == g_SceneSmoothAnimation &&
	    g_SceneSamplePreview == g_ScenePreview)
		return;
	g_SceneSampleReady = true;
	g_SceneSampleRevision = g_SceneCache.Revision();
	g_SceneSampleCursor = g_SceneCursor;
	g_SceneSampleSmooth = g_SceneSmoothAnimation;
	g_SceneSamplePreview = g_ScenePreview;
	advancedfx::SceneCache::Frame frame;
	bool valid = false;
	if (g_ScenePreview)
	{
		const auto begin=std::chrono::steady_clock::now();
        valid = g_SceneCache.Sample(g_SceneCursor, frame, .1, 256, g_SceneSmoothAnimation);
        g_ScenePerf.Sample.Add(begin,int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9)));
	}
	std::atomic_store(&g_SceneView,
	                  valid ? std::make_shared<const advancedfx::SceneCache::Frame>(std::move(frame))
	                        : std::shared_ptr<const advancedfx::SceneCache::Frame>{});
}
// IStudioRender is an engine-thread front end: let Source queue its immutable
// draw state. Calling that front end in a render-worker functor races the game.

std::shared_ptr<const std::vector<advancedfx::SceneCache::Line>> SceneCacheLines()
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	static std::shared_ptr<const advancedfx::SceneCache::Frame> last;
	static advancedfx::SceneCache::DrawingOptions previous;
	static std::uint64_t revision = ~0ull, particleRevision=~0ull;
	static bool previousMarkers=false, previousStyled=false,previousLegacyMarkers=false;
    static std::uint64_t legacyRevision=~0ull;
	static int previousParticleTick=-1;
	static std::shared_ptr<const std::vector<advancedfx::SceneCache::Line>> lines;
	const auto view = SceneCacheView();
	if (!view || !SceneCanDrawModels())
	{
		last.reset();
		lines.reset();
		return {};
	}
	auto options = g_SceneDrawing;
	options.Meshes = g_SceneDrawModels.load();
	const int particleTick=g_SceneNativeParticleMarkers?int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9)):-1;
	const auto particleRev=g_SceneNativeParticleMarkers?g_SceneNativeParticles.Revision:0;
    const int legacyTick=int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9));
    const auto legacyRev=g_SceneLegacyMarkers?g_SceneLegacyHistory.Revision:0;
    static int previousLegacyTick=-1;
	if (last != view || !(previous == options) || revision != g_SceneCache.Revision() ||
	    previousMarkers!=g_SceneNativeParticleMarkers || previousStyled!=g_SceneNativeParticleStyled || previousParticleTick!=particleTick || particleRevision!=particleRev ||
        previousLegacyMarkers!=g_SceneLegacyMarkers || legacyRevision!=legacyRev || (g_SceneLegacyMarkers&&previousLegacyTick!=legacyTick))
	{
		last = view;
		previous = options;
		revision = g_SceneCache.Revision();
		previousMarkers=g_SceneNativeParticleMarkers; previousStyled=g_SceneNativeParticleStyled; previousParticleTick=particleTick; particleRevision=particleRev;
		previousLegacyMarkers=g_SceneLegacyMarkers;legacyRevision=legacyRev;previousLegacyTick=legacyTick;
        auto result=g_SceneCache.Drawing(*view, options);
		if (g_SceneNativeParticleMarkers) if (const auto *sample=g_SceneNativeParticles.ForPreview(particleTick)) {
		    // A bounded diagnostic cross, not a textured particle or renderer.
		    const std::size_t step=(std::max)(std::size_t(1),(sample->Points.size()+8191)/8192);
		    std::size_t collection=0;
		    for(std::size_t i=0;i<sample->Points.size();i+=step) {
		        using namespace advancedfx::NativeParticles;
		        const auto &p=sample->Points[i];
		        float r=1.5f,a=220.0f/255;
		        auto rgb=advancedfx::SceneCache::CategoryColor(4);
		        bool custom=false;
		        if(g_SceneNativeParticleStyled&&sample->Appearance) {
		            while(collection<sample->Collections.size()&&
		                  i>=std::size_t(sample->Collections[collection].PointStart)+sample->Collections[collection].PointCount) ++collection;
		            if(collection<sample->Collections.size()) {
		                const auto &c=sample->Collections[collection];
		                // If position rejection changed indexing, do not associate wrong attributes.
		                if(i>=c.PointStart&&c.PointCount==c.Active) {
		                    const auto j=static_cast<unsigned>(i-c.PointStart);
		                    float v=0, a2=1;
		                    if(Float(*sample,c,Radius,j,0,v)&&v>=0) r=std::clamp(v,.35f,64.0f);
		                    if(Float(*sample,c,Alpha,j,0,v)) {
		                        Float(*sample,c,Alpha2,j,0,a2);
		                        a=std::clamp(v,0.0f,1.0f)*std::clamp(a2,0.0f,1.0f);
		                    }
		                    float tint[3]{};
		                    if(Float(*sample,c,Tint,j,0,tint[0])&&Float(*sample,c,Tint,j,1,tint[1])&&Float(*sample,c,Tint,j,2,tint[2])) {
		                        for(unsigned k=0;k<3;++k) rgb[k]=static_cast<unsigned char>(255*std::clamp(tint[k],0.0f,1.0f)+.5f);
		                        custom=true;
		                    }
		                }
		            }
		        }
		        const auto alpha=static_cast<unsigned char>(255*a+.5f);
		        if(!alpha) continue;
		        result.push_back({{p[0]-r,p[1],p[2]},{p[0]+r,p[1],p[2]},4,alpha,custom,rgb});
		        result.push_back({{p[0],p[1],p[2]-r},{p[0],p[1],p[2]+r},4,alpha,custom,rgb});
		    }
		}
        if(g_SceneLegacyMarkers)if(const auto *sample=g_SceneLegacyHistory.ForPreview(legacyTick)){
            for(const auto &p:sample->Particles){
                const float f=std::clamp(p.Lifetime/p.DieTime,0.f,1.f);
                const float r=std::clamp(p.StartSize+(float(p.EndSize)-p.StartSize)*f,.35f,64.f);
                const auto a=static_cast<unsigned char>(std::clamp(p.StartAlpha+(float(p.EndAlpha)-p.StartAlpha)*f,0.f,255.f));
                const auto &v=p.Position;
                if(a){result.push_back({{v[0]-r,v[1],v[2]},{v[0]+r,v[1],v[2]},4,a,true,p.Color});
                      result.push_back({{v[0],v[1],v[2]-r},{v[0],v[1],v[2]+r},4,a,true,p.Color});}
            }
        }
		lines = std::make_shared<const std::vector<advancedfx::SceneCache::Line>>(std::move(result));
	}
	return lines;
}
bool SceneCacheSuppressLiveParticles()
{
    std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
    const bool benchOff=g_SceneBench.Active&&!g_SceneBench.ModelsOnly&&(g_SceneBench.Phase==4||g_SceneBench.Phase==14)&&g_SceneBench.Native;
    auto deny=[](const char *why){g_SceneIsolationEligibility=why;return false;};
    if(!g_SceneSuppressLiveParticles||!g_ScenePreview||(!g_SceneNativeParticleReplay&&!benchOff)||
       !SceneCanDrawModels()||!SceneCacheView()||!Mirv_Tf2_ParticleIsolationReady())return deny("Off, no preview, or unsupported context.");
    const int tick=int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9));
    const auto sample=g_SceneNativeParticles.ShareForPreview(tick);
    if(!sample)return deny("No retained modern particle sample.");
    if(!ScenePrepareParticles(sample)->IsolationReady())return deny("Recorded modern prerequisites incomplete; live effects retained.");
    // Keep intentional benchmark ablations independent of previous phase outcomes.
    if(!g_SceneBench.Active&&g_SceneIsolationFailed.Contains(g_SceneNativeParticles.Revision,sample->Tick))
        return deny("Previous native submission for this retained sample was incomplete; live effects retained.");
    if(!g_SceneBench.Active&&g_SceneLegacyReplay){
        const auto legacy=g_SceneLegacyHistory.ShareForPreview(tick);
        if(!legacy||!legacy->RecordedIsolationReady())
            return deny("Requested legacy replay lacks complete manager-wide recorded coverage; live effects retained.");
        if(g_SceneLegacyIsolationFailed.Contains(g_SceneLegacyHistory.Revision,legacy->Tick))
            return deny("Previous legacy submission for this sample was incomplete; live effects retained.");
    }
    g_SceneIsolationEligibility="Recorded prerequisites satisfied (not pixel coverage).";
    return true;
}
void SceneCachePublishParticleIsolation(const char *status)
{
    std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
    if(g_SceneParticleIsolationStatus!=status)g_SceneParticleIsolationStatus=status;
}
unsigned SceneCacheBeginHistoricalSurfaces(){
    std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
    const unsigned requested=(g_SceneHistoricalShadows?1u:0u)|(g_SceneHistoricalDecals?(2u|(g_SceneRemovedDecals?4u:0u)|(g_SceneDecalOpacity?8u:0u)|((g_SceneBench.Active?g_SceneBench.QueuedDecals:g_SceneQueuedDecals)?16u:0u)):0u);
    if(!requested||!SceneCacheReplayActive()||
       (!Mirv_Tf2_ParticleIsolationReady()&&(!(requested&16)||!Mirv_Tf2_SceneStationaryReady()||!MaterialSystem_CanExecuteOnRenderThread())))return 0;
    const int tick=int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9));
    const auto sample=g_SceneSurfaces.ResolvePreview(tick);
    if(!sample){g_SceneHistoricalSurfaceStatus="No retained historical surface sample within two prior ticks.";return 0;}
    const auto begin=std::chrono::steady_clock::now();
    const auto active=Mirv_Tf2_BeginSurfaceReplay(sample.Value,requested,g_SceneHistoricalSurfaceStatus);
    g_ScenePerf.SurfaceBegin.Add(begin,tick);return active;
}
void SceneCacheEndHistoricalSurfaces(){
    const auto begin=std::chrono::steady_clock::now();
    auto status=Mirv_Tf2_EndSurfaceReplay();
    if(!status.empty()){std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);g_ScenePerf.SurfaceEnd.Add(begin,int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9)));g_SceneHistoricalSurfaceStatus=std::move(status);}
}
unsigned SceneCacheLiveSurfaceIsolation()
{
    std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
    if(!(g_SceneHideShadows||g_SceneHideDecals)||!SceneCacheReplayActive()||!Mirv_Tf2_ParticleIsolationReady())return 0;
    return (g_SceneHideShadows?1u:0u)|(g_SceneHideDecals?2u:0u);
}
void SceneCachePublishSurfaceIsolation(unsigned index,const char *status)
{
    std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
    if(index<2&&g_SceneSurfaceStatus[index]!=status)g_SceneSurfaceStatus[index]=status;
}
static void SceneCacheDrawLegacyParticles()
{
    std::shared_ptr<const advancedfx::LegacyParticles::Frame> sample;
    unsigned long long epoch=0;
    bool allowQueued=false, immediate=false;
    std::uint64_t revision=0;
    {
        std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
        // Phase 14 disables modern replay only, not its legacy companion.
        const bool legacyOnlyBench=g_SceneBench.Active&&!g_SceneBench.ModelsOnly&&g_SceneBench.Phase==14&&g_SceneBench.Native;
        allowQueued=g_SceneBench.Active?g_SceneBench.LegacyQueued:g_SceneLegacyQueued;
        immediate=MaterialSystem_IsImmediate();
        const bool queued=!immediate;
        if(!g_SceneLegacyReplay||!g_ScenePreview||(!g_SceneNativeParticleReplay&&!legacyOnlyBench&&!(queued&&allowQueued))||!SceneCanDrawModels()||!SceneCacheView())return;
        if(queued&&!allowQueued)return;
        const int tick=int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9));
        sample=g_SceneLegacyHistory.ShareForPreview(tick);
        if(!sample){g_SceneLegacyReplayTick=tick;g_SceneLegacyReplayResult={};g_SceneLegacyReplayResult.Status="No retained legacy sample within two prior ticks.";return;}
        epoch=g_SceneDrawEpoch.load();revision=g_SceneLegacyHistory.Revision;
    }
    const auto begin=std::chrono::steady_clock::now();
    auto result=Mirv_Tf2_RenderLegacyParticles(*sample,allowQueued);
    std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
    g_ScenePerf.LegacyDraw.Add(begin,sample->Tick);
    g_ScenePerf.LegacyRejected+=result.Rejected;
    if(!result.MainView&&result.Status.empty())return;
    if(epoch==g_SceneDrawEpoch.load()&&revision==g_SceneLegacyHistory.Revision&&g_SceneLegacyReplay){
        // Queued legacy is an intentionally partial overlay, not evidence
        // about immediate-mode blanket isolation.
        if(immediate&&!g_SceneBench.Active&&(!result.MainView||result.Rejected||result.CleanupFailures||result.Particles!=sample->Particles.size()))
            g_SceneLegacyIsolationFailed.Mark(revision,sample->Tick);
        g_SceneLegacyReplayTick=sample->Tick;g_SceneLegacyReplayResult=std::move(result);
    }
}
void SceneCacheDrawParticles()
{
    // Both native paths already reject non-main views internally. Avoid the
    // snapshot acquisition and expensive ABI checks on verified other views.
    {std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
     if(g_ScenePreview&&(g_SceneNativeParticleReplay||(g_SceneBench.Active&&!g_SceneBench.ModelsOnly&&g_SceneBench.Phase==14&&g_SceneLegacyReplay)||(g_SceneLegacyReplay&&(g_SceneBench.Active?g_SceneBench.LegacyQueued:g_SceneLegacyQueued)))&&Mirv_Tf2_SceneRenderViewId()>0){++g_ScenePerf.NonMainParticleSkipped;return;}}
    SceneCacheDrawLegacyParticles();
    // Main-view guard inside the native path. No persistent native objects,
    // and no native drawing in ordinary live demo playback.
    std::shared_ptr<const advancedfx::NativeParticles::Frame> sample;
    std::shared_ptr<const advancedfx::NativeParticles::ReplayPreparation> prepared;
    unsigned long long epoch=0;
    std::uint64_t revision=0;
    bool fastMemory=false,fastReads=false,auditRenderers=false;
    {
        std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
        if(!g_ScenePreview||!g_SceneNativeParticleReplay||!SceneCanDrawModels()||!SceneCacheView())return;
        const int tick=int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9));
        // Do not decode/copy appearance data every frame merely to fail the
        // modern native renderer's immediate-context gate.
        if(!MaterialSystem_IsImmediate()){
            g_SceneParticleReplayTick=tick;g_SceneParticleReplayResult={};
            g_SceneParticleReplayResult.Status="Modern native particles require immediate rendering; live effects retained.";
            return;
        }
        const auto begin=std::chrono::steady_clock::now();
        sample=g_SceneNativeParticles.ShareForPreview(tick);
        if(sample&&sample->Appearance)prepared=ScenePrepareParticles(sample);
        g_ScenePerf.ParticleAcquire.Add(begin,tick);
        if(!sample||!sample->Appearance) {
            g_SceneParticleReplayTick=tick;g_SceneParticleReplayResult={};
            g_SceneParticleReplayResult.Status="No retained appearance sample at cursor";
            return;
        }
        epoch=g_SceneDrawEpoch.load();revision=g_SceneNativeParticles.Revision;fastMemory=g_SceneFastParticleWrites;fastReads=g_SceneFastParticleReads;
        auditRenderers=g_SceneBench.Active&&!g_SceneBench.Measuring&&!g_SceneBench.Audited&&!g_SceneBench.ModelsOnly;
        if(auditRenderers)g_SceneBench.Audited=true;
    }
    const auto begin=std::chrono::steady_clock::now();
    auto result=Mirv_Tf2_RenderNativeParticles(*sample,fastMemory,fastReads,auditRenderers,prepared.get());
    std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
    if(auditRenderers&&g_SceneBench.Active){
        for(auto target:result.RendererTargets)if(g_SceneBench.RendererTargets.size()<256)g_SceneBench.RendererTargets.insert(target);
        for(auto target:result.PostRenderTargets)if(g_SceneBench.PostRenderTargets.size()<256)g_SceneBench.PostRenderTargets.insert(target);
    }
    g_ScenePerf.ParticleDraw.Add(begin,sample->Tick);
    g_ScenePerf.ParticleMain+=result.MainView;
    if(result.MainView)g_ScenePerf.MainParticleDraw.Add(begin,sample->Tick);
    g_ScenePerf.ParticleAttempts+=result.Attempts;g_ScenePerf.ParticleRejected+=result.Rejected;g_ScenePerf.ParticlePreflightRejected+=result.PreflightRejected;
    g_ScenePerf.ParticleFactoryMs+=result.FactoryMs;g_ScenePerf.ParticlePlanMs+=result.PlanMs;g_ScenePerf.ParticleCopyMs+=result.CopyMs;
    g_ScenePerf.ParticleRenderMs+=result.RenderMs;g_ScenePerf.ParticleDestroyMs+=result.DestroyMs;
    g_ScenePerf.ParticleGateMs+=result.GateMs;g_ScenePerf.ParticleActivationMs+=result.ActivationMs;g_ScenePerf.ParticleResetMs+=result.ResetMs;
    g_ScenePerf.FastWrites+=result.FastWrites;g_ScenePerf.SystemWrites+=result.SystemWrites;g_ScenePerf.WriteFaults+=result.WriteFaults;g_ScenePerf.CleanupFailures+=result.CleanupFailures;
    g_ScenePerf.FastReads+=result.FastReads;g_ScenePerf.SystemReads+=result.SystemReads;g_ScenePerf.ReadFaults+=result.ReadFaults;
    if(result.Rejected||result.CleanupFailures||(!result.MainView&&!result.Status.empty())){
        g_ScenePerf.LastParticleFailureTick=sample->Tick;
        g_ScenePerf.LastParticleFailure=result.Status;
    }
    // Non-main callbacks must not overwrite the last main-view diagnostic.
    if(!result.MainView&&result.Status.empty())return;
    if(epoch==g_SceneDrawEpoch.load()&&revision==g_SceneNativeParticles.Revision&&g_SceneNativeParticleReplay) {
        if(!g_SceneBench.Active&&(!result.MainView||result.Rejected||result.CleanupFailures||result.Particles!=sample->Active))
            g_SceneIsolationFailed.Mark(revision,sample->Tick);
        g_SceneParticleReplayTick=sample->Tick;g_SceneParticleReplayResult=std::move(result);
    }
}
void SceneCacheDrawModels()
{
	if (!g_SceneDrawModels.load() || !SceneCanDrawModels())
		return;
	std::shared_ptr<const advancedfx::SceneCache::Frame> view;
	unsigned long long epoch;
    bool modulation=false,lighting=false,modelDecals=false;
	{
		// Read the frame and its invalidation epoch together. Release the lock
		// before queueing: render-thread callbacks must never wait on UI state.
		std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
		view = SceneCacheView();
		epoch = g_SceneDrawEpoch.load();
        modulation=g_SceneReplayModulation;lighting=g_SceneReplayLighting;modelDecals=g_SceneHistoricalDecals;
	}
	if (!view)
		return;
	if (std::none_of(view->Entities.begin(), view->Entities.end(), [](const auto &entity) {
		    return entity.ModelPose && entity.ModelPose->ModelInfo->WireEdges.empty();
	    }))
	{
		const bool brush = std::any_of(view->Entities.begin(), view->Entities.end(), [](const auto &entity) {
			return entity.ModelPose && !entity.ModelPose->ModelInfo->WireEdges.empty();
		});
		SceneModelStatus(brush ? "Drawing cached brush meshes through the shared line renderer."
		                       : "No cached mesh at this cursor. Enable a model category and record this "
		                         "interval again. Optional bounds remain available.");
		return;
	}
	const auto begin=std::chrono::steady_clock::now();
    std::string status = "No cached model could be submitted.";
	const bool textured = SceneCacheReplayActive();
    const int nativeView=textured?Mirv_Tf2_SceneRenderViewId():-1;
    {std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
     if(g_SceneMainViewOnly&&nativeView>0){++g_ScenePerf.NonMainModelSkipped;return;}}
	float projection[4][4]{};
    const bool cull=g_ScenePreviewCull&&nativeView==0&&!Recording::IsActive()&&Mirv_Tf2_SceneMainProjection(projection);
    const advancedfx::SceneCache::PreviewFrustum frustum(projection);
    const auto culledActors=cull&&g_SceneActorCull?advancedfx::SceneCache::OutsideActorGroups(*view,frustum):std::set<std::uint64_t>{};
    unsigned submitted = 0, attempted = 0, queueSkips=0, otherSkips=0, culled=0,actorCulled=0;
    std::string lastFailure,lastFailureAsset;
    advancedfx::SceneCache::ReplayTimings stages;
    using Timing=advancedfx::SceneCache::ReplayTimings;
    struct BatchScope {
        bool Active;
        void Finish(){if(Active){Mirv_Tf2_EndSceneStudioBatch();Active=false;}}
        ~BatchScope(){Finish();}
    };
    const auto batchStart=std::chrono::steady_clock::now();
    BatchScope batch{textured&&g_SceneBatchStudio&&Mirv_Tf2_BeginSceneStudioBatch(g_SceneBatchQueued)};
    stages.Ms[Timing::Preflight]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-batchStart).count();
    double slowest=0;
    const std::string *slowAsset=nullptr;
	for (const auto &entity : view->Entities)
	{
		if (!g_SceneDrawModels.load() || !SceneCanDrawModels() || epoch != g_SceneDrawEpoch.load())
			return;
		if (entity.ModelPose && entity.ModelPose->ModelInfo->WireEdges.empty())
		{
            if(cull&&frustum.OutsideOrdinary(entity)){++culled;continue;}
            if(!culledActors.empty()&&culledActors.count(entity.Identity)){++culled;++actorCulled;continue;}
            const advancedfx::SceneCache::Entity *owner=nullptr;
            if(entity.OwnerIdentity){
                const auto found=std::lower_bound(view->Entities.begin(),view->Entities.end(),entity.OwnerIdentity,
                    [](const auto &row,std::uint64_t id){return row.Identity<id;});
                if(found!=view->Entities.end()&&found->Identity==entity.OwnerIdentity)owner=&*found;
            }
            ++attempted;
            const auto *drawEntity=&entity;
            advancedfx::SceneCache::Entity unmodulated;
            if((!modulation&&entity.HasModulation)||(!lighting&&entity.ModelLighting)||(!modelDecals&&entity.ModelDecalData)){
                unmodulated=entity;if(!modulation)unmodulated.HasModulation=false;if(!lighting)unmodulated.ModelLighting.reset();
                if(!modelDecals)unmodulated.ModelDecalData.reset();drawEntity=&unmodulated;
            }
            advancedfx::SceneCache::ReplayTimings modelStages;
            if (Mirv_Tf2_DrawScenePose(*drawEntity, status, owner, textured, view,&modelStages)) ++submitted;
            else {lastFailure=status;lastFailureAsset=entity.ModelPose->ModelInfo->Name;if(status.find("queue is behind")!=std::string::npos) ++queueSkips;else ++otherSkips;}
            stages.DecalBatchesRequested+=modelStages.DecalBatchesRequested;stages.DecalBatchesSubmitted+=modelStages.DecalBatchesSubmitted;
            double total=0;
            for(std::size_t i=0;i<modelStages.Ms.size();++i){stages.Ms[i]+=modelStages.Ms[i];total+=modelStages.Ms[i];}
            if(total>slowest){slowest=total;slowAsset=&entity.ModelPose->ModelInfo->Name;}
		}
	}
    const auto batchEnd=std::chrono::steady_clock::now();
    batch.Finish();
    stages.Ms[Timing::Retirement]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-batchEnd).count();
	{
        std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
        const int tick=int(std::floor(view->DemoTime/SceneTickInterval()+1e-9));
        g_ScenePerf.ModelDraw.Add(begin,tick);
        for(std::size_t i=0;i<stages.Ms.size();++i)g_ScenePerf.StudioStages.Ms[i]+=stages.Ms[i];
        g_ScenePerf.StudioStages.DecalBatchesRequested+=stages.DecalBatchesRequested;g_ScenePerf.StudioStages.DecalBatchesSubmitted+=stages.DecalBatchesSubmitted;
        if(slowAsset&&slowest>g_ScenePerf.SlowestModelMs){g_ScenePerf.SlowestModelMs=slowest;g_ScenePerf.SlowestModelAsset=*slowAsset;g_ScenePerf.SlowestModelTick=tick;}
        (nativeView==0?g_ScenePerf.MainModelDraw:nativeView>0?g_ScenePerf.OtherModelDraw:g_ScenePerf.UnknownModelDraw).Add(begin,tick);
        if(!lastFailure.empty()){g_ScenePerf.LastModelFailureTick=tick;g_ScenePerf.LastModelFailureAsset=lastFailureAsset;}
        ++g_SceneReplayCallbacks;g_SceneReplayAttempted+=attempted;g_SceneReplaySubmitted+=submitted;
        g_SceneReplayQueueSkips+=queueSkips;g_SceneReplayOtherSkips+=otherSkips;g_ScenePerf.ModelsCulled+=culled;g_ScenePerf.ActorsCulled+=actorCulled;
        if(!lastFailure.empty()) g_SceneReplayLastFailure=lastFailure;
        if(auto gl=g_Hook_VClient_RenderView.GetGlobals()) {
            const int frame=gl->framecount_get();
            if(frame!=g_SceneReplayFrame) {g_SceneReplayFrame=frame;g_SceneReplayFrameCallbacks=0;}
            ++g_SceneReplayFrameCallbacks;
            g_SceneReplayMaxCallbacks=(std::max)(g_SceneReplayMaxCallbacks,g_SceneReplayFrameCallbacks);
        }
    }
	if (epoch == g_SceneDrawEpoch.load())
		SceneModelStatus(textured ? "Experimental replay: " + std::to_string(submitted) + "/" + std::to_string(attempted) + " studio submissions; model decal batches "+std::to_string(stages.DecalBatchesSubmitted)+"/"+std::to_string(stages.DecalBatchesRequested)+". " + status : status);
}
// Diagnostics report the actual sampled representation, not just the toggle.
static std::string SceneAnimationSummary(const advancedfx::SceneCache::Entity &entity)
{
	if (!entity.ModelPose)
		return "Player/entity " + std::to_string(entity.Identity) + ": no cached model pose (bounds only)";
	const auto &pose = *entity.ModelPose;
	if (!pose.ModelInfo->WireEdges.empty())
		return "Brush mesh " + pose.ModelInfo->Name + ": " +
		       std::to_string(pose.ModelInfo->WireVertices.size()) + " vertices, " +
		       std::to_string(pose.ModelInfo->WireEdges.size()) + " wire edges.";
	std::string text =
	    (entity.Category == 0 ? "Player user " : "Entity handle ") +
	    std::to_string(entity.Category == 0 ? entity.Identity : (entity.Identity & 0xffffffff)) + " | " +
	    pose.ModelInfo->Name + ": ";
	if (pose.Local.empty())
		text += "world-only pose (nearest); capture a fresh interval";
	else if (pose.BlendFraction < 0)
		text += "recorded/nearest pose; " + std::to_string(pose.Fixed.size()) + " preserved bone exceptions";
	else
	{
		char fraction[32];
		std::snprintf(fraction, sizeof(fraction), "%.3f", pose.BlendFraction);
		text += "blend " + std::string(fraction) + "; " + std::to_string(pose.BlendedBones) + " blended, " +
		        std::to_string(pose.HeldBones) + " preserved bones";
	}
	if (pose.BlendFraction >= 0 && pose.HeldBones > pose.Fixed.size())
		text += "\n  Additional preserved bones cross an adjacent exception or scale-handedness boundary.";
	for (size_t i = 0; i < (std::min)(size_t(8), pose.Fixed.size()); ++i)
	{
		const auto &fixed = pose.Fixed[i];
		text += "\n  Bone " + std::to_string(fixed.Index);
		if (!pose.ModelInfo->BoneNames.empty() && !pose.ModelInfo->BoneNames[fixed.Index].empty())
			text += " (" + pose.ModelInfo->BoneNames[fixed.Index] + ")";
		text += ": ";
		text += advancedfx::SceneCache::FallbackName(fixed.Reason);
		text += fixed.World ? "; recorded world transform" : "; recorded parent-relative transform";
	}
	if (pose.Fixed.size() > 8)
		text += "\n  More preserved bones: " + std::to_string(pose.Fixed.size() - 8);
	return text;
}
static double SceneTickInterval()
{
	auto gl = g_Hook_VClient_RenderView.GetGlobals();
	return gl && gl->interval_per_tick_get() > 0 ? gl->interval_per_tick_get() : 1.0 / 66.0;
}
static void ScenePreviewTime(double time)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	const double interval = SceneTickInterval();
	const int tick = int(std::floor(time / interval + 1e-9));
	g_SequencerFrame = tick;
	g_SceneCursor = time;
	const auto &frames = g_SceneCache.Frames();
	if (!frames.empty() && tick >= int(std::floor(frames.front().DemoTime / interval)) &&
	    tick <= int(std::floor(frames.back().DemoTime / interval)))
		g_SceneCursor = std::clamp(g_SceneCursor, frames.front().DemoTime, frames.back().DemoTime);
	SceneCacheFrame(false);
	auto view = SceneCacheView();
	auto &path = g_Hook_VClient_RenderView.m_CamPath;
	if (view && !MirvCam_RigPreviewActive() && MirvCampathPreview_IsActive() && path.CanEval())
	{
		const bool driving = std::exchange(g_SceneDrivingCamera, true);
		MirvCampathPreview_SetTime(view->ClientTime - path.GetOffset());
		g_SceneDrivingCamera = driving;
	}
}
static void ScenePreviewTick(int tick)
{
	ScenePreviewTime(tick * SceneTickInterval());
}
// Native campath navigation retains its exact keyframe time and selection. Map
// its clock back into recorded scene time without evaluating the camera again.
void SceneCacheFollowCameraTime(double clientTime)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	const auto &frames = g_SceneCache.Frames();
	if (!g_ScenePreview || g_SceneDrivingCamera || MirvCam_RigPreviewActive() || frames.empty() ||
		!std::isfinite(clientTime))
		return;
	const auto current = SceneCacheView();
	if (current && current->ClientTime == clientTime)
		return;
	if (g_WorkLoop) { g_WorkLoop = false; g_WorkStatus = "Cached loop paused."; }
	const auto *nearest = &frames.front();
	double demoTime = 0;
	bool mapped = false;
	for (size_t i = 0; i < frames.size(); ++i)
	{
		const auto &a = frames[i];
		if (std::abs(a.ClientTime - clientTime) < std::abs(nearest->ClientTime - clientTime))
			nearest = &a;
		if (a.ClientTime == clientTime)
		{
			demoTime = a.DemoTime;
			mapped = true;
			break;
		}
		if (i + 1 == frames.size())
			continue;
		const auto &b = frames[i + 1];
		if (a.ClientTime < clientTime && clientTime < b.ClientTime)
		{
			const double f = (clientTime - a.ClientTime) / (b.ClientTime - a.ClientTime);
			demoTime = a.DemoTime + f * (b.DemoTime - a.DemoTime);
			mapped = true;
			break;
		}
	}
	// Outside recorded coverage, keep the requested cursor but publish no scene;
	// never clamp the camera to a different key or use the live playback clock.
	if (!mapped)
		demoTime = nearest->DemoTime + clientTime - nearest->ClientTime;
	g_SceneCursor = demoTime;
	g_SequencerFrame = int(std::clamp(std::round(demoTime / SceneTickInterval()), 0.0, double(INT_MAX)));
	SceneCacheFrame(false);
}
void SceneCacheStopScrub()
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	g_SceneScrubRate = 0;
	if (g_WorkLoop) { g_WorkLoop = false; g_WorkStatus = "Cached loop paused."; }
}
bool SceneCacheNavigationKey(unsigned int key, bool down)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	if (!g_ScenePreview || (key != VK_LEFT && key != VK_RIGHT && key != VK_UP && key != VK_DOWN &&
	                        key != VK_HOME && key != VK_END))
		return false;
	if (down && !g_SceneCache.Frames().empty())
	{
		SceneCacheStopScrub();
		int first = int(std::floor(g_SceneCache.Frames().front().DemoTime / SceneTickInterval()));
		int last = int(std::floor(g_SceneCache.Frames().back().DemoTime / SceneTickInterval()));
		int64_t target = g_SequencerFrame;
		if (key == VK_HOME)
			target = first;
		else if (key == VK_END)
			target = last;
		else if (key == VK_LEFT || key == VK_RIGHT)
			target += key == VK_LEFT ? -1 : 1;
		else
			target += (key == VK_DOWN ? -1 : 1) * int64_t(std::llround(1 / SceneTickInterval()));
		ScenePreviewTick(int(std::clamp(target, int64_t(first), int64_t(last))));
	}
	return true;
}
// Frame-driven held-key scrubbing keeps a fractional cache cursor. Taps still
// use SceneCacheNavigationKey and land on exact recorded demo ticks.
bool SceneCacheScrubKey(unsigned int key, double seconds)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	if (!g_ScenePreview || g_SceneCache.Frames().empty() || !std::isfinite(seconds) || seconds <= 0 ||
	    (key != VK_LEFT && key != VK_RIGHT && key != VK_UP && key != VK_DOWN))
		return false;
	const double interval = SceneTickInterval();
	const double step = (key == VK_LEFT    ? -1
	                     : key == VK_RIGHT ? 1
	                                       : (key == VK_UP ? 1 : -1) * std::llround(1 / interval));
	SceneCacheStopScrub();
	ScenePreviewTime(std::clamp(g_SceneCursor + step * (std::min)(seconds, .1) / .033 * interval,
	                            g_SceneCache.Frames().front().DemoTime,
	                            g_SceneCache.Frames().back().DemoTime));
	return true;
}
static void DrawSceneCoverage()
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	if (!ImGui::BeginNeoTimelineEx("Scene Cache", nullptr, ImGuiNeoTimelineFlags_None)) return;
	const auto &frames = g_SceneCache.Frames();
	const double interval = SceneTickInterval();
	for (size_t i = 0; i < frames.size();)
	{
		size_t last = i;
		while (last+1 < frames.size() && frames[last+1].DemoTime-frames[last].DemoTime <= .1) ++last;
		int firstTick = int(std::floor(frames[i].DemoTime/interval));
		int lastTick = int(std::floor(frames[last].DemoTime/interval));
		if (lastTick >= firstTick) ImGui::NeoTimelineSpan(firstTick,lastTick,IM_COL32(70,190,220,180),"Recorded scene coverage");
		i = last+1;
	}
	int actualTick = 0;
	if (g_ScenePreview && GetCurrentDemoTick(actualTick))
		ImGui::NeoTimelineSpan(actualTick,actualTick,IM_COL32(180,180,180,180),"Actual demo position");
	ImGui::EndNeoTimeLine();
}
static bool g_Docking = true;
// Recording options remain independent of the active overlay tab.
static char g_RecordFps[64] = "60";
static bool g_RecordAutoResume = true, g_RecordAutoPause = true;
static bool g_RecordAutoClose = true, g_RecordAutoStopOnOpen = false;
static unsigned int g_RecordHotkey = 0; // Disabled until explicitly chosen.
static unsigned int g_RecordHotkeyMods = 0; // Shift=1, Ctrl=2, Alt=4
static bool g_RecordCapturingHotkey = false;
static bool RecordModifierKey(unsigned int key)
{
	return key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
		key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
		key == VK_MENU || key == VK_LMENU || key == VK_RMENU ||
		key == VK_LWIN || key == VK_RWIN;
}
static unsigned int RecordModifiers()
{
	return (GetKeyState(VK_SHIFT) & 0x8000 ? 1u : 0u) |
		(GetKeyState(VK_CONTROL) & 0x8000 ? 2u : 0u) |
		(GetKeyState(VK_MENU) & 0x8000 ? 4u : 0u);
}
static std::string RecordHotkeyLabel()
{
	if (!g_RecordHotkey) return "Not bound";
	std::string result;
	if (g_RecordHotkeyMods & 2) result += "Ctrl+";
	if (g_RecordHotkeyMods & 1) result += "Shift+";
	if (g_RecordHotkeyMods & 4) result += "Alt+";
	char name[80] = {};
	const UINT scan = MapVirtualKeyA(g_RecordHotkey, MAPVK_VK_TO_VSC);
	const LONG code = LONG(scan) << 16;
	if (!scan || !GetKeyNameTextA(code, name, sizeof(name)))
		std::snprintf(name, sizeof(name), "VK %u", g_RecordHotkey);
	return result + name;
}
static std::atomic_bool g_RecordTogglePending{false};
static bool g_CurveKeyDrag = false, g_CancelGesture = false;
static std::unique_ptr<CamPathCollection::State> g_GestureStart;
static unsigned long long g_GestureOwner = 0, g_GestureRevision = 0;
static bool CancelEditGesture();
struct SpeedDrag
{
	int Index = -1;
	unsigned long long PathId = 0, Revision = 0;
	std::vector<CamPath::SpeedKey> Keys;
	double Duration = 0;
	float Maximum = 1;
};
static SpeedDrag g_SpeedDrag;
struct GizmoDrag
{
	bool HasModel = false, WasUsing = false, Changed = false;
	double KeyTime = 0;
	float Model[16] = {};
};
static GizmoDrag g_GizmoDrag;
struct TangentDrag
{
	struct Handle
	{
		size_t Index;
		CamPath::Channel Component;
		double Duration, Slope, Weight, ValuePerPixel;
	};
	std::vector<Handle> Handles;
	bool Outgoing = false, Group = false, MoveHandle = false, LengthOnly = false, Linked = true,
		 EditWeight = false;
	ImVec2 Mouse, LastMouse;
	double SecondsPerPixel = 0;
};
static TangentDrag g_TangentDrag;
static std::map<double, unsigned int> g_GraphChannels;
static unsigned long long g_GraphSelectionOwner = 0, g_GraphSelectionRevision = ~0ull;
static bool GraphChannelSelected(double time, int channel)
{
	const auto found = g_GraphChannels.find(time);
	return found != g_GraphChannels.end() && (found->second & (1u << channel));
}
static void SyncGraphSelection(CamPath &path)
{
	auto &document = g_Hook_VClient_RenderView.m_CamPaths;
	const auto owner = document.GetId(document.GetActive());
	if (owner == g_GraphSelectionOwner && g_GraphSelectionRevision == path.GetRevision())
		return;
	if (owner != g_GraphSelectionOwner)
		g_GraphChannels.clear();
	std::map<double, unsigned int> next;
	for (auto it = path.GetBegin(); it != path.GetEnd(); ++it)
		if (it.GetValue().Selected)
		{
			const auto found = g_GraphChannels.find(it.GetTime());
			next[it.GetTime()] = found == g_GraphChannels.end() ? 0x7f : found->second;
		}
	g_GraphChannels = std::move(next);
	g_GraphSelectionOwner = owner;
	g_GraphSelectionRevision = path.GetRevision();
}
static void PublishGraphSelection(CamPath &path)
{
	auto keys = path.CaptureState().Keys;
	for (auto &key : keys)
		key.second.Selected = g_GraphChannels.count(key.first) && g_GraphChannels.at(key.first) != 0;
	path.ReplaceKeys(keys);
	g_GraphSelectionRevision = path.GetRevision();
}

static void ResetSpeedDrag()
{
	g_SpeedDrag = SpeedDrag{};
}

static void EnsureEditGesture()
{
	if (g_GestureStart || !ImGui::IsMouseDown(0))
		return;
	auto &document = g_Hook_VClient_RenderView.m_CamPaths;
	g_GestureStart = std::make_unique<CamPathCollection::State>(document.CaptureState());
	g_GestureOwner = document.GetId(document.GetActive());
	g_GestureRevision = g_Hook_VClient_RenderView.m_CamPath.GetRevision();
}
static bool CancelEditGesture()
{
	if (!g_GestureStart && !g_CurveKeyDrag && g_SpeedDrag.Index < 0 && !g_GizmoDrag.HasModel &&
		!g_GizmoDrag.WasUsing && g_TangentDrag.Handles.empty())
		return false;
	g_GestureStart.reset();
	g_CurveKeyDrag = false;
	g_GizmoDrag = GizmoDrag{};
	g_TangentDrag = {};
	ResetSpeedDrag();
	g_CancelGesture = true;
	if (ImGui::GetCurrentContext())
		ImGui::ClearActiveID();
	g_Hook_VClient_RenderView.m_CamPaths.FinishEdit();
	return true;
}
static bool g_GraphRightCtrlHeld = false, g_GraphRightCtrlChord = false;
static bool g_Autosave = true;
static double g_AutosaveSeconds = 30;
static std::filesystem::path g_RecoveryFile;
static CamPathCollection::State g_RecoverySnapshot;
static bool g_RecoveryWritten = false;
static double g_RecoveryNextSave = 0;
static std::vector<std::filesystem::path> g_RecoveryCandidates;
static std::string g_DocumentStatus;
static void UpdateRecovery(bool force);
static void CancelCommandMarkerDrag();
static void SaveOverlayPreferences();

bool IsOpen()
{
	return g_Open;
}
void SetOpen(bool value)
{
	if (value && !g_Open && g_RecordAutoStopOnOpen && Recording::IsActive())
		g_RecordTogglePending.store(true);
	const bool wasOpen = g_Open.load();
	if (!value)
	{
		FinishEdit();
		g_CurveKeyDrag = false;
		g_GestureStart.reset();
		g_GraphRightCtrlHeld = false;
	}
	g_Open = value;
	if (!value)
		g_MouseLook = false;
	if (!value)
		UpdateRecovery(true);
	if (wasOpen && !value)
		SaveOverlayPreferences();
}
bool IsMouseLook()
{
	return g_MouseLook;
}
void SetMouseLook(bool value)
{
	if (value && !g_MouseLook)
		CancelEditGesture();
	g_MouseLook = value;
}
static void HistoryObserve()
{
	g_Hook_VClient_RenderView.m_CamPaths.Observe();
}
void BeginEdit()
{
	g_Hook_VClient_RenderView.m_CamPaths.BeginEdit();
}
void FinishEdit()
{
	CancelCommandMarkerDrag();
	if (!CancelEditGesture())
		g_Hook_VClient_RenderView.m_CamPaths.FinishEdit();
	UpdateRecovery(false);
}
void UpdateHistory()
{
	HistoryObserve();
	if (!IsOpen())
		FinishEdit();
}
void HistoryUndo(bool redo)
{
	CancelEditGesture();
	ResetSpeedDrag();
	g_CurveKeyDrag = false;
	g_GestureStart.reset();
	g_CancelGesture = ImGui::GetCurrentContext() && ImGui::IsMouseDown(0);
	auto &paths = g_Hook_VClient_RenderView.m_CamPaths;
	// Commit pending path gestures before comparing their edit index to camera edits.
	paths.FinishEdit();
	const auto pathIndex = paths.LastHistoryIndex(redo);
	const auto cameraIndex = MirvCam_HistoryIndex(redo);
	if (cameraIndex && (!pathIndex || (redo ? cameraIndex < pathIndex : cameraIndex > pathIndex)))
		MirvCam_HistoryUndo(redo);
	else if (pathIndex)
		paths.Undo(redo);
}
static std::filesystem::path RecoveryDirectory()
{
#ifdef _WIN32
	const wchar_t *local = _wgetenv(L"LOCALAPPDATA");
	if (local && *local) return std::filesystem::path(local) / L"advancedfx" / L"camera-recovery";
#else
	const char *local = std::getenv("LOCALAPPDATA");
	if (local && *local) return std::filesystem::u8path(local) / "advancedfx" / "camera-recovery";
#endif
	return std::filesystem::path(GetHlaeFolder()) / "camera-recovery";
}
static void ScanRecovery()
{
	g_RecoveryCandidates.clear();
	std::error_code error;
	const auto directory = RecoveryDirectory();
	if (!std::filesystem::is_directory(directory, error))
		return;
	for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
		 it.increment(error))
		if (it->is_regular_file(error) && it->path().extension() == ".xml" && it->path() != g_RecoveryFile)
			g_RecoveryCandidates.push_back(it->path());
	std::sort(g_RecoveryCandidates.begin(), g_RecoveryCandidates.end(),
			  [](const auto &a, const auto &b)
			  {
				  std::error_code ea, eb;
				  return std::filesystem::last_write_time(a, ea) > std::filesystem::last_write_time(b, eb);
			  });
}
static void NewRecoverySession()
{
	g_RecoverySnapshot = {};
	g_RecoveryWritten = false;
	g_RecoveryFile = RecoveryDirectory() /
					 ("campath-recovery-" +
					  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".xml");
	ScanRecovery();
}
static void UpdateRecovery(bool force)
{
	if (!g_Autosave && !force)
		return;
	const double now =
		std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	// Idle frames must not touch the filesystem or traverse unchanged camera data.
	if (!force && (now < g_RecoveryNextSave || (ImGui::GetCurrentContext() && ImGui::IsMouseDown(0))))
		return;
	g_RecoveryNextSave = now + g_AutosaveSeconds;
	auto &paths = g_Hook_VClient_RenderView.m_CamPaths;
	std::error_code error;
	if (!paths.IsDirty())
	{
		if (g_RecoveryWritten)
		{
			std::filesystem::remove(g_RecoveryFile, error);
			if (!error)
			{
				g_RecoveryWritten = false;
				g_RecoverySnapshot = {};
			}
		}
		return;
	}
	auto snapshot = paths.CaptureState();
	if (g_RecoveryWritten && CamPathCollection::SameContent(snapshot, g_RecoverySnapshot))
		return;
	if (g_RecoveryFile.empty())
		NewRecoverySession();
	std::filesystem::create_directories(RecoveryDirectory(), error);
	if (error || !paths.SaveAll(g_RecoveryFile.wstring().c_str(), true))
		g_DocumentStatus = "Recovery autosave could not be written. Save your work to a writable folder.";
	else
	{
		g_RecoveryWritten = true;
		g_RecoverySnapshot = std::move(snapshot);
	}
}
void ResetProjectRecovery()
{
	NewRecoverySession();
}
void FlushRecovery()
{
	UpdateRecovery(true);
}
bool SaveDocument(const wchar_t *fileName, bool all)
{
	auto &paths = g_Hook_VClient_RenderView.m_CamPaths;
	const bool success = all ? paths.SaveAll(fileName) : paths.SaveOne(fileName);
	g_DocumentStatus =
		success ? "Saved camera paths." : "Could not save camera paths; the existing file was retained.";
	if (success)
		UpdateRecovery(true);
	return success;
}
bool LoadDocument(const wchar_t *fileName, bool all)
{
	UpdateRecovery(true);
	auto &paths = g_Hook_VClient_RenderView.m_CamPaths;
	const bool success = all ? paths.LoadAll(fileName) : paths.LoadOne(fileName);
	if (success)
		NewRecoverySession();
	g_DocumentStatus = success ? "Opened camera paths. Previous unsaved work remains recoverable."
							   : "Could not open camera paths; the current document was retained.";
	return success;
}
static double TickInterval()
{
	auto *globals = g_Hook_VClient_RenderView.GetGlobals();
	return globals ? globals->interval_per_tick_get() : 0;
}
static double UnwrapAngleNear(double value, double reference)
{
	double delta = std::fmod(value-reference, 360.0);
	if (delta > 180) delta -= 360;
	if (delta < -180) delta += 360;
	return reference+delta;
}
static inline void Afx_GetLastCameraData(double &x, double &y, double &z, double &rX, double &rY, double &rZ,
										 float &fov)
{
	x = g_Hook_VClient_RenderView.LastCameraOrigin[0];
	y = g_Hook_VClient_RenderView.LastCameraOrigin[1];
	z = g_Hook_VClient_RenderView.LastCameraOrigin[2];
	rX = g_Hook_VClient_RenderView.LastCameraAngles[0];
	rY = g_Hook_VClient_RenderView.LastCameraAngles[1];
	rZ = g_Hook_VClient_RenderView.LastCameraAngles[2];
	fov = (float)g_Hook_VClient_RenderView.LastCameraFov;
}

// Helpers mirroring the DX11 overlay API (Source 1 equivalents)
static inline float GetLastCameraFov()
{
	return (float)g_Hook_VClient_RenderView.LastCameraFov;
}
static inline float GetLastCameraRoll()
{
	return (float)g_Hook_VClient_RenderView.LastCameraAngles[2];
}

static inline void Afx_ExecClientCmd(const char *cmd)
{
	if (g_VEngineClient && cmd)
		g_VEngineClient->ClientCmd_Unrestricted(cmd);
}

static inline void Afx_GotoDemoTick(int tick)
{
	MirvCampathPreview_SeekTick(tick);
}

static void StopWorkTransport(bool pause, const char *status);
namespace Recording
{
bool Toggle()
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	if (!g_WorkDispatching && g_WorkTransport != WorkTransport::Idle)
	{
		StopWorkTransport(true, "Range stopped by recording toggle.");
		return true;
	}
	if (!g_VEngineClient)
		return false;
	if (IsActive())
	{
		std::string command = "mirv_streams record end; host_framerate 0";
		if (g_RecordAutoPause)
			command += "; demo_pause";
		g_VEngineClient->ClientCmd_Unrestricted(command.c_str());
	}
	else
	{
		if (!RecordingResolution::CanStart())
		{
			Tier0_Warning("Custom recording resolution is not verified. Apply or restore before recording.\n");
			return false;
		}
		if(SeparatePassSupported() && !SeparateConfigured() && !ScreenEnabled())g_VEngineClient->ClientCmd_Unrestricted("mirv_streams record screen enabled 1");
		const double fps = std::strtod(g_RecordFps, nullptr);
		const double rate = std::isfinite(fps) && fps >= 1 && fps <= 10000 ? fps : 60.0;
		char command[256];
		std::snprintf(command, sizeof(command),
			"host_framerate %.7g; mirv_streams record fps %.7g; mirv_streams record start%s",
			rate, rate, g_RecordAutoResume ? "; demo_resume" : "");
		g_VEngineClient->ClientCmd_Unrestricted(command);
		if (g_RecordAutoClose)
			AfxHookSource::Gui::SetOverlayOpen(false);
	}
	return true;
}
bool CaptureHotkey(unsigned int key)
{
	if (!g_RecordCapturingHotkey)
		return false;
	if (key == VK_ESCAPE)
	{
		g_RecordCapturingHotkey = false;
		return true;
	}
	if (key == VK_BACK)
	{
		g_RecordHotkey = 0;
		g_RecordHotkeyMods = 0;
		g_RecordCapturingHotkey = false;
		if (ImGui::GetCurrentContext()) ImGui::MarkIniSettingsDirty();
		return true;
	}
	// Do not consume the operating system's window-management chords.
	if ((GetKeyState(VK_MENU) & 0x8000) &&
		(key == VK_TAB || key == VK_F4 || key == VK_ESCAPE))
		return false;
	if (key == VK_LWIN || key == VK_RWIN)
		return false;
	if (RecordModifierKey(key))
		return true;
	// F8 owns the overlay toggle, and Windows keys/system shortcuts are not
	// suitable single-key recording binds. Other keyboard keys are accepted.
	if (key == VK_F8 || key == VK_SNAPSHOT || key == VK_CANCEL)
		return true;
	g_RecordHotkey = key;
	g_RecordHotkeyMods = RecordModifiers();
	g_RecordCapturingHotkey = false;
	if (ImGui::GetCurrentContext()) ImGui::MarkIniSettingsDirty();
	return true;
}
bool HandleHotkey(unsigned int key)
{
	if (g_RecordCapturingHotkey || !g_RecordHotkey || key != g_RecordHotkey ||
		RecordModifiers() != g_RecordHotkeyMods || !g_VEngineClient ||
		g_VEngineClient->Con_IsVisible() ||
		(ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput))
		return false;
	g_RecordTogglePending.store(true);
	return true;
}
} // namespace Recording

static void StopWorkTransport(bool pause, const char *status)
{
	const bool ownedRecording = g_WorkTransport == WorkTransport::Starting ||
		g_WorkTransport == WorkTransport::Recording;
	g_WorkTransport = WorkTransport::Idle;
	g_WorkPreviousTick = -1;
	g_WorkStatus = status;
	// An explicit end also cancels a start command that has not become active yet.
	if (ownedRecording)
		Afx_ExecClientCmd(pause ? "mirv_streams record end; host_framerate 0; demo_pause"
			: "mirv_streams record end; host_framerate 0");
	else if (pause) Afx_ExecClientCmd("demo_pause");
}
static bool StartWorkTransport(bool record)
{
	int tick = -1;
	if (WorkRangeBusy() || !WorkRangeValid() || g_ScenePreview || Recording::IsActive() ||
		!g_VEngineClient || !g_VEngineClient->IsPlayingDemo() || g_VEngineClient->IsDrawingLoadingImage() ||
		g_VEngineClient->IsSkippingDemo() || MirvCampathPreview_IsSeeking() || !GetCurrentDemoTick(tick) ||
		tick > g_WorkIn)
	{
		g_WorkStatus = "Use actual demo playback, seek to or before In, and finish any active recording or seek.";
		return false;
	}
	if (auto *demo = g_VEngineClient->GetDemoInfoEx())
		if (demo->GetDemoPlaybackTotalTicks() > 0 && g_WorkOut > demo->GetDemoPlaybackTotalTicks())
		{
			g_WorkStatus = "Out is beyond the demo.";
			return false;
		}
	if (record && !RecordingResolution::CanStart())
	{
		g_WorkStatus = "Recording resolution is not verified.";
		return false;
	}
	g_WorkTransport = record ? WorkTransport::Armed : WorkTransport::Playback;
	g_WorkPreviousTick = tick;
	g_WorkStatus = record ? "Recording armed: waiting for In." : "Playing actual demo to Out.";
	Afx_ExecClientCmd("demo_timescale 1; demo_resume");
	return true;
}
// Engine FRAME_START, even with the overlay closed. Never seek implicitly;
// abort on a demo change or discontinuous backwards playback.
static void WorkTransportFrame()
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	if (g_WorkTransport == WorkTransport::Idle) return;
	int tick = -1;
	if (!g_VEngineClient || !g_VEngineClient->IsPlayingDemo() ||
		g_VEngineClient->IsDrawingLoadingImage() || g_VEngineClient->IsSkippingDemo() ||
		MirvCampathPreview_IsSeeking() || g_ScenePreview || !GetCurrentDemoTick(tick) ||
		tick < g_WorkPreviousTick)
	{
		StopWorkTransport(false, "Range cancelled: demo or playback clock changed.");
		return;
	}
	g_WorkPreviousTick = tick;
	if ((g_WorkTransport == WorkTransport::Playback || g_WorkTransport == WorkTransport::Armed) &&
		Recording::IsActive())
	{
		g_WorkTransport = WorkTransport::Idle;
		g_WorkStatus = "Range cancelled: another recording started.";
		return;
	}
	if (g_WorkTransport == WorkTransport::Recording && !Recording::IsActive())
	{
		g_WorkTransport = WorkTransport::Idle;
		g_WorkStatus = "Range recording stopped manually.";
		return;
	}
	if (tick >= g_WorkOut)
	{
		StopWorkTransport(true, g_WorkTransport == WorkTransport::Armed
			? "Out reached before recording could start." : "Out reached; range finished.");
		return;
	}
	if (g_WorkTransport == WorkTransport::Armed && tick >= g_WorkIn && !g_VEngineClient->IsPaused())
	{
		g_WorkDispatching = true;
		const bool started = Recording::Toggle();
		g_WorkDispatching = false;
		if (started)
		{
			g_WorkTransport = WorkTransport::Starting;
			g_WorkStartDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
			g_WorkStatus = "Starting range recording...";
		}
		else StopWorkTransport(true, "Range recording could not start.");
	}
	if (g_WorkTransport == WorkTransport::Starting)
	{
		if (Recording::IsActive())
		{
			g_WorkTransport = WorkTransport::Recording;
			g_WorkStatus = "Recording selected range.";
		}
		else if (std::chrono::steady_clock::now() >= g_WorkStartDeadline)
			StopWorkTransport(true, "Recording did not become active; range cancelled.");
	}
}

static inline const char *AfxStreams_GetRecordNameUtf8()
{
	return Recording::GetName();
}
static inline void AfxStreams_SetRecordNameUtf8(const char *name)
{
	if (name)
		Recording::SetName(name);
}

// Overlay UI state (parity with DX11)
struct CampathCtx
{
	bool active = false;
	double time = 0.0;
	CamPathValue value{};
};
static CampathCtx g_LastCampathCtx;
static bool g_ShowSequencer = false;
static bool g_ShowCameraControl = false;
static bool g_ShowGizmo = false;
static bool g_PreviewHudEnabled = true;
static bool g_CameraClipsEditing = false;
static std::uint64_t g_SelectedCameraClip = ~std::uint64_t(0);
static std::uint64_t g_SelectedSequenceModifier = ~std::uint64_t(0);
static std::uint64_t g_SelectedModifierDefinition = 0;
static bool g_FocusModifierLibrary = false;
// One-shot inspector focus requests from the timeline context menus.
enum class ClipInspectorFocus { None, CameraStart, CameraTransition, AdjustmentStart, AdjustmentEnd };
static ClipInspectorFocus g_ClipInspectorFocus = ClipInspectorFocus::None;
static bool g_AdjustmentDuplicateDrag = false;
static void FocusClipField(ClipInspectorFocus field)
{
 if (g_ClipInspectorFocus == field) { ImGui::SetKeyboardFocusHere(); g_ClipInspectorFocus = ClipInspectorFocus::None; }
}
static bool g_ShowTransitionGuide = true;
static std::atomic<std::uint64_t> g_TransitionGuidePublished{~std::uint64_t(0)};
static std::atomic_bool g_TransitionGuidesPublished{true};
static bool g_OpenCameraClips = false;
static bool g_ShowCameraGuides = false, g_CameraFrustums = true, g_FocusCameras = false,
			g_CamerasTabActive = false;
static std::atomic<bool> g_CameraGuidesPublished{false},g_CameraFrustumsPublished{true};
static ImGuizmo::OPERATION g_GizmoOp = ImGuizmo::TRANSLATE;
static ImGuizmo::MODE g_GizmoMode = ImGuizmo::LOCAL;

// Sequencer/curve editor shared state (parity with DX11)
static bool g_SequencerNeedsRefresh = false;
static void SceneNativeParticleStatus(int tick)
{
    const auto &history=g_SceneNativeParticles;
    advancedfx::Message("Native particle appearance capture %d, styled markers %d, native replay %d.\n",int(g_SceneNativeParticleAppearance),int(g_SceneNativeParticleStyled),int(g_SceneNativeParticleReplay));
    advancedfx::Message("Live particle isolation requested %d; last ordinary render: %s\n",int(g_SceneSuppressLiveParticles),g_SceneParticleIsolationStatus.c_str());
    const auto &replay=g_SceneParticleReplayResult;
    advancedfx::Message("Native particle replay tick %d: %zu attempts, %zu trees, %zu nonempty drawable collections, %zu submitted particles, %zu rejected attempts. %s\n",g_SceneParticleReplayTick,replay.Attempts,replay.Trees,replay.Nodes,replay.Particles,replay.Rejected,replay.Status.c_str());
    advancedfx::Message("Native particles: enabled %d, markers %d; %zu frames, %.2f MiB; ticks %d .. %d; capture last %.3f ms, mean %.3f ms, peak %.3f ms.\n",
        int(g_SceneNativeParticlesEnabled),int(g_SceneNativeParticleMarkers),history.Frames.size(),history.Bytes/1048576.0,
        history.Frames.empty()?-1:history.Frames.front().Tick,history.Frames.empty()?-1:history.Frames.back().Tick,
        g_SceneNativeParticleMs,g_SceneNativeParticleSamples?g_SceneNativeParticleTotalMs/g_SceneNativeParticleSamples:0,g_SceneNativeParticlePeakMs);
    advancedfx::Message("Particle history budget %.0f MiB; evicted %zu frames, rejected %zu frames.\n",history.BudgetBytes()/1048576.0,history.Evicted,history.Rejected);
    advancedfx::Message("Particle storage: %.2f MiB original snapshot payload, %.2f MiB packed before compression, %.2f MiB retained (%.2fx original/retained); decoded scratch %.2f MiB.\n",history.RawBytes/1048576.0,history.PlainBytes/1048576.0,history.Bytes/1048576.0,history.Bytes?double(history.RawBytes)/history.Bytes:0.0,history.DecodedBytes()/1048576.0);
    advancedfx::Message("Native detached probe: %s\n",g_SceneDetachedStatus.c_str());
    if(tick<0) return;
    const auto *preview=history.ForPreview(tick);
    advancedfx::Message("Particle preview requested tick %d; resolved tick %d (bounded prior sample, -1 unavailable).\n",tick,preview?preview->Tick:-1);
    const auto *f=history.At(tick);
    if(!f){advancedfx::Message("Native particles tick %d: no exact retained sample.\n",tick);return;}
    advancedfx::Message("Native particles tick %d: %zu root effects, %zu child collections, %zu active particles, %zu captured positions, %zu empty collections, %zu rejected reads/values; complete %d. %s\n",
        tick,f->Roots,f->Children,f->Active,f->Points.size(),f->Empty,f->Rejected,int(f->Complete),f->Status.c_str());
    if(!f->Points.empty()) {
        const auto &p=f->Points.front();
        advancedfx::Message("Native particles first XYZ: %.3f %.3f %.3f\n",p[0],p[1],p[2]);
    }
    if(f->Appearance) {
        using namespace advancedfx::NativeParticles;
        std::array<std::size_t,Layout::AttributeCount> coverage{},varying{};
        std::size_t nonempty=0,clocks=0;
        for(const auto &c:f->Collections) {
            clocks+=c.HasTime;
            if(!c.Active) continue;
            ++nonempty;
            for(unsigned a=0;a<Layout::AttributeCount;++a) if(c.Mask&(1u<<a)) {
                ++coverage[a]; if(c.Attributes[a].Stride) ++varying[a];
            }
        }
        advancedfx::Message("Native particle attributes: %zu collection records, %zu named, %zu clocks, %zu nonempty; %.3f MiB packed words; %zu rejected appearance reads/layouts; complete %d.\n",
            f->Collections.size(),f->NamedCollections,clocks,nonempty,f->Words.size()*sizeof(std::uint32_t)/1048576.0,f->AppearanceRejected,int(f->AppearanceComplete));
        advancedfx::Message("Shared constant attribute words: %zu (%.2f KiB saved in this sample).\n",f->SharedConstantWords,f->SharedConstantWords*4/1024.0);
        std::string line="Attribute coverage (collections captured/varying):";
        for(unsigned a=0;a<Layout::AttributeCount;++a)
            line+=" "+std::string(AttributeNames[a])+"="+std::to_string(coverage[a])+"/"+std::to_string(varying[a]);
        advancedfx::Message("%s\n",line.c_str());
        std::size_t shown=0;
        for(const auto &c:f->Collections) {
            if(!c.Active||shown++>=8) continue;
            const auto index=static_cast<std::size_t>(&c-f->Collections.data());
            float radius=0,alpha=0,tint[3]{},rotation=0;
            const bool radiusOk=Float(*f,c,Radius,0,0,radius), alphaOk=Float(*f,c,Alpha,0,0,alpha),
                tintOk=Float(*f,c,Tint,0,0,tint[0])&&Float(*f,c,Tint,0,1,tint[1])&&Float(*f,c,Tint,0,2,tint[2]),
                rotOk=Float(*f,c,Rotation,0,0,rotation);
            advancedfx::Message("  [%zu] %s; parent %d; particles %u; time %.3f; first radius %.3f (%d), alpha %.3f (%d), RGB %.3f %.3f %.3f (%d), rotation %.3f (%d).\n",
                index,c.Name[0]?c.Name.data():"[unavailable]",c.Parent,c.Active,c.Time,radius,int(radiusOk),alpha,int(alphaOk),tint[0],tint[1],tint[2],int(tintOk),rotation,int(rotOk));
        }
    }
}
// Read-only exact-tick inspection: retain original collection indices, including
// empty parents, so a reported index can also be used by the detached probe.
static void SceneLegacyParticleStatus(int tick,bool previewSample=false)
{
    advancedfx::Message("Queued anchored-page legacy overlay %d (live effects remain; other named pages immediate-only).\n",int(g_SceneLegacyQueued));
    advancedfx::Message("Legacy textured replay %d; tick %d: %zu submitted, %zu rejected (particles or batches); %s\n",int(g_SceneLegacyReplay),g_SceneLegacyReplayTick,g_SceneLegacyReplayResult.Particles,g_SceneLegacyReplayResult.Rejected,g_SceneLegacyReplayResult.Status.c_str());
    using namespace advancedfx::LegacyParticles;
    const auto &history=g_SceneLegacyHistory;
    advancedfx::Message("Legacy dissolve capture %d; %zu frames, %.2f MiB / 64 MiB payload; ticks %d .. %d; evicted %zu, rejected %zu.\n",
        int(g_SceneLegacyCapture),history.Frames.size(),history.Bytes/1048576.0,
        history.Frames.empty()?-1:history.Frames.front().Tick,history.Frames.empty()?-1:history.Frames.back().Tick,
        history.Evicted,history.Rejected);
    if(tick<0)return;
    const auto *sample=previewSample?history.ForPreview(tick):history.At(tick);
    if(previewSample)advancedfx::Message("Legacy preview requested tick %d; resolved tick %d (bounded prior sample).\n",tick,sample?sample->Tick:-1);
    if(!sample){advancedfx::Message("Legacy dissolve tick %d: %s. Last capture: %s\n",tick,previewSample?"no bounded prior sample":"no exact retained sample",g_SceneLegacyCaptureStatus.c_str());return;}
    std::size_t spark=0,glow=0;
    for(const auto &p:sample->Particles){spark+=p.Texture==Material::Spark;glow+=p.Texture==Material::CombineMuzzle;}
    advancedfx::Message("Legacy dissolve tick %d: %zu emitters, %zu expected, %zu captured; spark %zu, combinemuzzle2 %zu, unknown material %zu; complete %d.\n",
        sample->Tick,sample->Emitters,sample->Expected,sample->Particles.size(),spark,glow,sample->UnknownMaterials,int(sample->Complete));
    advancedfx::Message("Additional exact simple emitters %zu; skipped additional emitters %zu; registered material names %zu.\n",sample->OtherEmitters,sample->OtherRejected,sample->Materials.size());
    advancedfx::Message("Observed manager active %zu (known %d); uncaptured %zu; manager-wide isolation prerequisites %d.\n",sample->ObservedActive,int(sample->ObservedActiveKnown),sample->UncapturedActive(),int(sample->RecordedIsolationReady()));
    if(!sample->Particles.empty()){
        const auto &p=sample->Particles.front();
        advancedfx::Message("First XYZ %.3f %.3f %.3f; RGB %u %u %u; alpha %u -> %u; size %u -> %u; lifetime %.3f / %.3f; roll %.3f; UV %.3f %.3f %.3f %.3f.\n",
            p.Position[0],p.Position[1],p.Position[2],unsigned(p.Color[0]),unsigned(p.Color[1]),unsigned(p.Color[2]),unsigned(p.StartAlpha),unsigned(p.EndAlpha),
            unsigned(p.StartSize),unsigned(p.EndSize),p.Lifetime,p.DieTime,p.Roll,p.UV[0],p.UV[1],p.UV[2],p.UV[3]);
    }
}
static void SceneNativeParticleList(int tick,std::size_t start,const char *filter)
{
    const auto *f=g_SceneNativeParticles.At(tick);
    if(!f||!f->Appearance) {
        advancedfx::Message("Particle list: no exact retained appearance snapshot at tick %d.\n",tick);
        return;
    }
    auto lower=[](std::string value) {
        for(char &ch:value)ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return value;
    };
    const std::string needle=lower(filter?filter:"");
    advancedfx::Message("Particle list tick %d: %zu collections, position complete %d, appearance complete %d; start index %zu, filter '%s'.\n",
        tick,f->Collections.size(),int(f->Complete),int(f->AppearanceComplete),start,needle.c_str());
    std::size_t matches=0,active=0,shown=0,next=f->Collections.size();
    for(std::size_t i=0;i<f->Collections.size();++i) {
        const auto &c=f->Collections[i];
        const std::string name(c.Name.begin(),std::find(c.Name.begin(),c.Name.end(),'\0'));
        if(!needle.empty()&&lower(name).find(needle)==std::string::npos)continue;
        ++matches;active+=c.Active;
        if(i<start)continue;
        if(shown>=64){if(next==f->Collections.size())next=i;continue;}
        ++shown;
        advancedfx::Message("  [%zu] %s; parent %d; active %u; time %.3f (%d); mask 0x%08x\n",
            i,name.empty()?"[unnamed]":name.c_str(),c.Parent,c.Active,c.Time,int(c.HasTime),c.Mask);
    }
    advancedfx::Message("Particle list: %zu matching collections, %zu active particles; %zu shown.\n",matches,active,shown);
    if(next<f->Collections.size())advancedfx::Message("Next page: mirv_cache particles list %d %zu \"%s\"\n",tick,next,needle.c_str());
}
// Explicit fresh validation profile: no automatic seek, cvar change or capture.
static const char *SceneValidationSetup(){
    if(g_SceneBench.Active||g_SceneBenchRestoreQueue||g_SceneRecord||g_ScenePreview||g_SceneScrubRate||g_WorkLoop||Recording::IsActive())
        return "Stop recording, preview, transport and benchmark first.";
    if(g_SceneCache.Count()||!g_SceneNativeParticles.Frames.empty()||!g_SceneLegacyHistory.Frames.empty()||
       !g_SceneSurfaces.Frames.empty()||g_SceneEffects.First>=0)
        return "Existing history preserved. Use mirv_cache clear explicitly for a fresh take.";
    if(!Mirv_Tf2_SceneStationaryReady()||!MaterialSystem_IsImmediate())
        return "Requires supported TF2 x64 demo paused/frozen in actual immediate context (mat_queue_mode 0).";
    g_SceneRenderSnapshot=true;g_SceneDrawModels.store(true);g_SceneSuppressLiveStudio=true;
    SceneParticleCaptureEnabled(true);g_SceneNativeParticleAppearance=g_SceneNativeParticleReplay=true;
    g_SceneLegacyCapture=g_SceneLegacyReplay=g_SceneLegacyAll=g_SceneLegacyQueued=true;g_SceneLegacyMarkers=false;
    g_SceneReplayModulation=g_SceneLightingCapture=g_SceneReplayLighting=true;
    g_SceneSurfaceCapture=g_SceneHistoricalShadows=g_SceneHistoricalDecals=g_SceneRemovedDecals=g_SceneDecalOpacity=g_SceneQueuedDecals=true;
    g_SceneHideShadows=g_SceneHideDecals=false;g_SceneSuppressLiveParticles=true;
    g_SceneMainViewOnly=g_ScenePreviewCull=g_SceneActorCull=false;
    g_SceneKeepAssets=g_SceneBatchStudio=g_SceneBatchQueued=g_SceneFastParticleWrites=g_SceneFastParticleReads=true;
    g_SceneSmoothAnimation=true;g_SceneRangeEnabled=false;
    g_SceneBenchReport.clear();g_SceneBenchFile.clear();g_ScenePerf={};
    g_SceneSampleReady=false;++g_SceneDrawEpoch;return nullptr;
}
static std::string SceneValidationReport(){
    std::ostringstream out;
    out<<"Scene Cache consolidated validation (native submission/coverage, NOT pixel validation)\n";
    float queue=0;const bool queueKnown=Mirv_Tf2_SceneDrawVisibility("mat_queue_mode",queue,false);
    int live=-1;const bool liveKnown=GetCurrentDemoTick(live);
    out<<"Live tick "<<(liveKnown?live:-1)<<"; preview "<<g_ScenePreview<<"; recording "<<g_SceneRecord<<"; immediate "<<MaterialSystem_IsImmediate()<<"; queue cvar ";
    if(queueKnown)out<<queue;else out<<"unknown";
    out<<"; stationary eligibility "<<Mirv_Tf2_SceneStationaryReady()<<"\n";
    const int tick=g_ScenePreview?int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9)):
        g_SceneCache.Frames().empty()?-1:int(std::floor(g_SceneCache.Frames().back().DemoTime/SceneTickInterval()+1e-9));
    out<<"Requested cached tick "<<tick<<"; pending new take "<<g_SceneCaptureResetPending<<"\n";
    out<<"Settings: broad "<<g_SceneRenderSnapshot<<", models "<<g_SceneDrawModels.load()<<", studio suppression "<<g_SceneSuppressLiveStudio
       <<", modulation "<<g_SceneReplayModulation<<", lighting capture/replay "<<g_SceneLightingCapture<<"/"<<g_SceneReplayLighting
       <<", modern capture/replay/isolate "<<g_SceneNativeParticlesEnabled<<"/"<<g_SceneNativeParticleReplay<<"/"<<g_SceneSuppressLiveParticles
       <<", legacy capture/replay/all/queued "<<g_SceneLegacyCapture<<"/"<<g_SceneLegacyReplay<<"/"<<g_SceneLegacyAll<<"/"<<g_SceneLegacyQueued
       <<", surface capture/shadows/decals/removed/opacity/queued "<<g_SceneSurfaceCapture<<"/"<<g_SceneHistoricalShadows<<"/"<<g_SceneHistoricalDecals<<"/"<<g_SceneRemovedDecals<<"/"<<g_SceneDecalOpacity<<"/"<<g_SceneQueuedDecals
       <<", blanket hide shadows/decals "<<g_SceneHideShadows<<"/"<<g_SceneHideDecals<<"\n";
    auto history=[&](const char *name,const auto &frames,std::size_t bytes,std::size_t evicted,std::size_t rejected){
        out<<name<<": "<<frames.size()<<" frames, "<<bytes<<" bytes, evicted "<<evicted<<", rejected "<<rejected;
        if(!frames.empty())out<<", ticks "<<frames.front().Tick<<".."<<frames.back().Tick;
        out<<"\n";
    };
    out<<"Models: "<<g_SceneCache.Count()<<" frames, "<<g_SceneCache.Bytes()<<" bytes, "<<g_SceneCache.PoseCount()<<" unique poses";
    if(!g_SceneCache.Frames().empty())out<<", ticks "<<int(std::floor(g_SceneCache.Frames().front().DemoTime/SceneTickInterval()+1e-9))<<".."<<int(std::floor(g_SceneCache.Frames().back().DemoTime/SceneTickInterval()+1e-9));
    out<<"\n";
    history("Modern",g_SceneNativeParticles.Frames,g_SceneNativeParticles.Bytes,g_SceneNativeParticles.Evicted,g_SceneNativeParticles.Rejected);
    history("Legacy",g_SceneLegacyHistory.Frames,g_SceneLegacyHistory.Bytes,g_SceneLegacyHistory.Evicted,g_SceneLegacyHistory.Rejected);
    history("Surfaces",g_SceneSurfaces.Frames,g_SceneSurfaces.Bytes,g_SceneSurfaces.Evicted,g_SceneSurfaces.Rejected);
    if(auto view=SceneCacheView()){
        std::size_t studio=0,brush=0,missing=0,lit=0,local=0,modulated=0,decalKnown=0,decalModels=0,decalBatches=0,decalVertices=0,decalIndices=0,decalGeometry=0,decalGeometryBytes=0,decalSkinBatches=0;
        for(const auto &e:view->Entities){
            if(!e.ModelPose){++missing;continue;}
            if(!e.ModelPose->ModelInfo->WireEdges.empty()){++brush;continue;}
            ++studio;if(e.ModelLighting){++lit;local+=e.ModelLighting->Count;}modulated+=e.HasModulation;
            if(e.ModelDecals.Known()){++decalKnown;decalModels+=e.ModelDecals.Batches!=0;decalBatches+=e.ModelDecals.Batches;decalVertices+=e.ModelDecals.Vertices;decalIndices+=e.ModelDecals.Indices;}
            if(e.ModelDecalData){++decalGeometry;decalGeometryBytes+=e.ModelDecalData->Bytes();
                for(const auto &batch:e.ModelDecalData->Batches)decalSkinBatches+=!batch.Skin.empty();}
        }
        out<<"Published model sample: tick "<<view->DemoTime/SceneTickInterval()<<"; studio "<<studio<<", brush wire "<<brush<<", missing pose "<<missing<<", lit "<<lit<<", local lights "<<local<<", known modulation "<<modulated<<"\n";
        out<<"Studio decal inventory (capture only): known models "<<decalKnown<<"/"<<studio<<", affected "<<decalModels<<", batches "<<decalBatches<<", vertices "<<decalVertices<<", indices "<<decalIndices<<", geometry models "<<decalGeometry<<", geometry bytes "<<decalGeometryBytes<<", directly skinnable batches "<<decalSkinBatches<<"\n";
    }else out<<"Published model sample: unavailable\n";
    out<<"Studio decal replay (rigid/direct-skinned immediate only): submitted "<<g_ScenePerf.StudioStages.DecalBatchesSubmitted<<"/"<<g_ScenePerf.StudioStages.DecalBatchesRequested<<" material batches\n";
    if(auto p=g_SceneNativeParticles.ShareForPreview(tick))
        out<<"Modern sample: "<<p->Tick<<", complete "<<p->Complete<<", appearance complete "<<p->AppearanceComplete<<", collections "<<p->Collections.size()<<", active "<<p->Active<<", rejected "<<p->Rejected<<", appearance rejected "<<p->AppearanceRejected<<"\n";
    else out<<"Modern sample: unavailable\n";
    if(auto p=g_SceneLegacyHistory.ShareForPreview(tick))
        out<<"Legacy sample: "<<p->Tick<<", complete "<<p->Complete<<", particles "<<p->Particles.size()<<", rejected "<<p->Rejected<<", unknown material "<<p->UnknownMaterials<<", other rejected "<<p->OtherRejected<<", manager active "<<p->ObservedActive<<" (known "<<p->ObservedActiveKnown<<"), uncaptured "<<p->UncapturedActive()<<", manager-wide isolation ready "<<p->RecordedIsolationReady()<<"\n";
    else out<<"Legacy sample: unavailable\n";
    const auto surface=g_SceneSurfaces.ResolvePreview(tick);
    if(surface){
        const auto &f=surface.Value;std::size_t geometry=0,clipped=0,simple=0;
        if(f.Decals)for(const auto &d:*f.Decals)geometry+=d.HasGeometry();
        if(f.Shadows)for(const auto &s:*f.Shadows){clipped+=s.ClipKnown;simple+=s.HasSimpleTexture();}
        out<<"Surface sample: shadows tick "<<surface.ShadowTick<<", decals tick "<<surface.DecalTick
           <<"; shadows "<<(f.Shadows?f.Shadows->size():0)<<", clip-known "<<clipped<<", verified simple "<<simple<<", unobserved "<<f.ShadowsUnobserved
           <<"; decals "<<(f.Decals?f.Decals->size():0)<<", geometry "<<geometry<<", unsupported "<<f.DecalsUnsupported<<" ("<<f.UnsupportedDecals.Describe()<<")\n";
    }else out<<"Surface sample: unavailable\n";
    unsigned pending=0,peak=0;Mirv_Tf2_SceneRenderStatus(pending,peak);
    out<<"Studio submissions "<<g_SceneReplaySubmitted<<"/"<<g_SceneReplayAttempted<<", queue skips "<<g_SceneReplayQueueSkips<<", other skips "<<g_SceneReplayOtherSkips<<", outstanding owners "<<pending<<", peak "<<peak<<"\n"
       <<"Last studio failure: "<<g_SceneReplayLastFailure<<"\n"
       <<"Lighting: "<<g_SceneLightingStatus<<"\n"
       <<"Native particle replay tick "<<g_SceneParticleReplayTick<<": "<<g_SceneParticleReplayResult.Status<<"\n"
       <<"Legacy replay tick "<<g_SceneLegacyReplayTick<<": "<<g_SceneLegacyReplayResult.Status<<"\n"
       <<"Live particle isolation: "<<g_SceneParticleIsolationStatus<<"\n"
       <<"Isolation eligibility: "<<g_SceneIsolationEligibility<<"\n"
       <<"Historical surfaces: "<<g_SceneHistoricalSurfaceStatus<<"\n"
       <<"Benchmark active "<<g_SceneBench.Active<<", queue restoration pending "<<g_SceneBenchRestoreQueue<<"\n";
    if(!g_SceneBenchReport.empty())out<<"\n--- Latest benchmark (settings/ticks at benchmark time) ---\n"<<g_SceneBenchReport;
    else out<<"No benchmark results in this validation session.\n";
    return out.str();
}
void SceneCacheConsole(advancedfx::ICommandArgs *args)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	const char *op = args->ArgC() > 1 ? args->ArgV(1) : "help";
	auto parseInteger = [&](int index, int &value) {
		if (args->ArgC() <= index)
			return false;
		char *end = nullptr;
		errno = 0;
		long long parsed = std::strtoll(args->ArgV(index), &end, 10);
		if (end == args->ArgV(index) || *end || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX)
			return false;
		value = int(parsed);
		return true;
	};
	auto integer = [&](int &value) { return args->ArgC() == 3 && parseInteger(2, value); };
	int value = 0;
	bool ok = false;
    if(!_stricmp(op,"validate")){
        if(args->ArgC()==3&&!_stricmp(args->ArgV(2),"setup")){
            if(const auto *reason=SceneValidationSetup())advancedfx::Warning("Validation setup rejected: %s\n",reason);
            else advancedfx::Message("Validation profile enabled. Record one fresh interval; stop, pause, preview an earlier tick, then mirv_cache perf bench. Use mirv_cache validate report for one combined report.\n");
        }else if(args->ArgC()==3&&!_stricmp(args->ArgV(2),"report")){
            const auto report=SceneValidationReport();
            std::error_code ec;const auto folder=std::filesystem::temp_directory_path(ec);
            if(!ec){
                const auto path=folder/("hlae-scene-validation-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+".txt");
                std::ofstream file(path,std::ios::binary|std::ios::trunc);
                if(file){file.write(report.data(),static_cast<std::streamsize>(report.size()));file.close();}
                if(file){advancedfx::Message("Validation report: %s\n",path.u8string().c_str());return;}
            }
            advancedfx::Warning("Validation report file unavailable; printing to console.\n");
            advancedfx::Message("%s",report.c_str());
        }else advancedfx::Message("mirv_cache validate <setup|report>\n");
        return;
    }
    if(g_SceneBench.Active&&_stricmp(op,"perf")&&_stricmp(op,"status")){
        SceneBenchStop("Benchmark cancelled by another cache command.");SceneCacheFrame(false);
    }
    if(!_stricmp(op,"perf")){
        if(args->ArgC()>=3&&!_stricmp(args->ArgV(2),"bench")){
            if(args->ArgC()==4&&!_stricmp(args->ArgV(3),"report")){
                if(g_SceneBench.Active){
                    auto &b=g_SceneBench;
                    const double idle=std::chrono::duration<double>(std::chrono::steady_clock::now()-b.LastRender).count();
                    if(idle>10.0){SceneBenchStop("Benchmark cancelled: no ordinary render callback for ten seconds.");SceneCacheFrame(false);}
                    else if(b.Switching)advancedfx::Message("Benchmark switching to mat_queue_mode %d (%s).\n",b.TargetQueue,b.SwitchWritten?"waiting for actual context":"waiting for engine FRAME_START");
                    else advancedfx::Message("Benchmark phase %d/%d (%s), %s, %d measured renders, %llu total render callbacks.\n",
                        b.Phase+1,SceneBenchPhaseCount(),SceneBenchLabel(b.Phase),b.Measuring?"measuring":"warming up",b.Frames,b.RenderCallbacks);
                }
                if(!g_SceneBenchFile.empty())advancedfx::Message("Report file: %s\n",g_SceneBenchFile.u8string().c_str());
                if(g_SceneBenchReport.empty())advancedfx::Message("No benchmark report yet.\n");
                else {
                    std::istringstream report(g_SceneBenchReport);
                    std::string row;while(std::getline(report,row))advancedfx::Message("%s\n",row.c_str());
                }
                return;
            }
            if(args->ArgC()==4&&!_stricmp(args->ArgV(3),"cancel")){SceneBenchStop("Benchmark cancelled.");SceneCacheFrame(false);return;}
            const bool modelsOnly=args->ArgC()==4&&!_stricmp(args->ArgV(3),"models");
            const bool fullOnly=args->ArgC()==4&&!_stricmp(args->ArgV(3),"full");
            const bool automatic=args->ArgC()==3;
            float queueMode=0;
            const bool queueKnown=Mirv_Tf2_SceneDrawVisibility("mat_queue_mode",queueMode,false)&&
                (queueMode==-1||queueMode==0||queueMode==1||queueMode==2);
            if((!automatic&&!modelsOnly&&!fullOnly)||g_SceneBench.Active||g_SceneBenchRestoreQueue||!SceneCacheReplayActive()||
               !SceneBenchTransportReady(modelsOnly||automatic)||(automatic&&!queueKnown)||
               g_SceneScrubRate||g_WorkLoop||Recording::IsActive()||MirvCampathPreview_IsActive()||MirvCam_RigPreviewActive()||
               g_SceneCache.Frames().empty()){
                advancedfx::Message("Benchmark requires stationary broad cached preview, paused/frozen demo, no active camera path/rig preview or video recording. Use perf bench [full|models|report|cancel].\n");return;
            }
            auto &b=g_SceneBench;b={};b.Active=true;b.ModelsOnly=modelsOnly;b.AutoQueue=automatic;b.OriginalQueue=b.OwnedQueue=int(queueMode);b.Immediate=MaterialSystem_IsImmediate();b.Record=g_SceneRecord;b.Preview=g_ScenePreview;b.Models=g_SceneDrawModels;
            b.MainOnly=g_SceneMainViewOnly;b.Cull=g_ScenePreviewCull;b.Native=g_SceneNativeParticleReplay;b.Legacy=g_SceneLegacyReplay;
            b.Residency=g_SceneKeepAssets;b.FastWrites=g_SceneFastParticleWrites;b.BatchStudio=g_SceneBatchStudio;b.FastReads=g_SceneFastParticleReads;b.BatchQueued=g_SceneBatchQueued;b.ActorCull=g_SceneActorCull;
            b.Shadows=g_SceneHistoricalShadows;b.Decals=g_SceneHistoricalDecals;b.HideShadows=g_SceneHideShadows;b.HideDecals=g_SceneHideDecals;b.QueuedDecals=g_SceneQueuedDecals;b.LegacyQueued=g_SceneLegacyQueued;
            b.Markers=g_SceneNativeParticleMarkers;b.LegacyMarkers=g_SceneLegacyMarkers;b.Cursor=g_SceneCursor;
            b.Late=g_SceneCache.Frames().back().DemoTime;b.OriginalSequencer=int(g_SequencerFrame);
            g_SceneBenchReport="Scene cache benchmark (engine wall time, CPU subsystem totals/frame; camera must remain fixed; requested live model/particle isolation retained where eligible in isolation modes; live surfaces may return; memory tests are sequential, not cold-cache controlled).\n";
            if(modelsOnly)g_SceneBenchReport+="Models-only comparison: historical particles/surfaces and live-effect hiding temporarily off; live effects remain.\n";
            if(automatic)g_SceneBenchReport+="Automatic suite: full immediate, models-only immediate, models-only queued; original queue cvar "+std::to_string(b.OriginalQueue)+".\n";
            char config[512]{};
            std::snprintf(config,sizeof(config),"Requested preview: native %d legacy %d historical shadows %d decals %d; live studio suppression %d particles %d; hide shadows %d decals %d. Disabled effects remain disabled in the full suite. Model history %zu, particle %zu, legacy %zu, surface %zu frames.\n",
                int(b.Native),int(b.Legacy),int(b.Shadows),int(b.Decals),int(g_SceneSuppressLiveStudio),int(g_SceneSuppressLiveParticles),int(b.HideShadows),int(b.HideDecals),g_SceneCache.Frames().size(),g_SceneNativeParticles.Frames.size(),g_SceneLegacyHistory.Frames.size(),g_SceneSurfaces.Frames.size());
            g_SceneBenchReport+=config;
            std::error_code fileError;const auto folder=std::filesystem::temp_directory_path(fileError);
            g_SceneBenchFile.clear();
            if(!fileError)g_SceneBenchFile=folder/("hlae-scene-benchmark-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+".txt");
            const auto now=std::chrono::steady_clock::now();
            if(automatic)SceneBenchSwitch(0,0,now);
            else {g_SceneBenchReport+=b.Immediate?"Actual material context: immediate.\n":"Actual material context: queued.\n";SceneBenchApply(0,now);}
            SceneBenchSave();SceneCacheFrame(false);
            advancedfx::Message("Benchmark started. Close console and keep camera still; results saved after each phase to %s. Use perf bench report for partial results.\n",g_SceneBenchFile.u8string().c_str());return;
        }
        if(g_SceneBench.Active&&args->ArgC()>2){advancedfx::Message("Benchmark active; use perf bench cancel first.\n");return;}
        if(args->ArgC()==3&&!_stricmp(args->ArgV(2),"reset"))g_ScenePerf={};
        else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"mainOnly")&&parseInteger(3,value)&&(value==0||value==1))g_SceneMainViewOnly=value!=0;
        else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"cull")&&parseInteger(3,value)&&(value==0||value==1))g_ScenePreviewCull=value!=0;
        else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"actorCull")&&parseInteger(3,value)&&(value==0||value==1))g_SceneActorCull=value!=0;
        else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"residency")&&parseInteger(3,value)&&(value==0||value==1))g_SceneKeepAssets=value!=0;
        else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"fastWrites")&&parseInteger(3,value)&&(value==0||value==1))g_SceneFastParticleWrites=value!=0;
        else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"fastReads")&&parseInteger(3,value)&&(value==0||value==1))g_SceneFastParticleReads=value!=0;
        else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"batchQueued")&&parseInteger(3,value)&&(value==0||value==1))g_SceneBatchQueued=value!=0;
        else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"batchStudio")&&parseInteger(3,value)&&(value==0||value==1))g_SceneBatchStudio=value!=0;
        else if(args->ArgC()!=2){advancedfx::Message("mirv_cache perf [reset|mainOnly <0|1>|cull <0|1>|actorCull <0|1>|residency <0|1>|fastWrites <0|1>|fastReads <0|1>|batchStudio <0|1>|batchQueued <0|1>]\n");return;}
        advancedfx::Message("Scene cache CPU timing (not GPU completion), cursor %.3f ticks; capture %d, preview %d.\n",g_SceneCursor/SceneTickInterval(),int(g_SceneRecord),int(g_ScenePreview));
        g_ScenePerf.Sample.Print("Historical model sampling");
        g_ScenePerf.ModelDraw.Print("Studio replay submission");
        {using T=advancedfx::SceneCache::ReplayTimings;const auto &a=g_ScenePerf.StudioStages.Ms;
         const double n=g_ScenePerf.ModelDraw.Count?double(g_ScenePerf.ModelDraw.Count):1;
         advancedfx::Message("Studio CPU mean/callback: preflight/retire %.3f, asset %.3f, bones %.3f, state %.3f, draw %.3f, enqueue/restore %.3f ms; slowest individual submission %.3f ms at tick %d: %s.\n",
            a[T::Preflight]/n,a[T::Asset]/n,a[T::Bones]/n,a[T::State]/n,a[T::Draw]/n,a[T::Retirement]/n,g_ScenePerf.SlowestModelMs,g_ScenePerf.SlowestModelTick,g_ScenePerf.SlowestModelAsset.empty()?"none":g_ScenePerf.SlowestModelAsset.c_str());}
        g_ScenePerf.MainModelDraw.Print("Studio main view");g_ScenePerf.OtherModelDraw.Print("Studio other views");g_ScenePerf.UnknownModelDraw.Print("Studio unverified view");
        g_ScenePerf.MainParticleDraw.Print("Native particle main view");
        const double views=g_ScenePerf.ParticleMain?double(g_ScenePerf.ParticleMain):1;
        advancedfx::Message("Native particle CPU mean/main view: factory %.3f, validation %.3f, attribute copy %.3f, render %.3f, destruction %.3f ms; fast writes %d: %llu direct, %llu system, %llu faults; active cleanup failures %llu.\n",
            g_ScenePerf.ParticleFactoryMs/views,g_ScenePerf.ParticlePlanMs/views,g_ScenePerf.ParticleCopyMs/views,g_ScenePerf.ParticleRenderMs/views,g_ScenePerf.ParticleDestroyMs/views,
            int(g_SceneFastParticleWrites),g_ScenePerf.FastWrites,g_ScenePerf.SystemWrites,g_ScenePerf.WriteFaults,g_ScenePerf.CleanupFailures);
        advancedfx::Message("Native particle extra CPU mean/main view: gates %.3f, activation %.3f, active reset %.3f ms.\n",
            g_ScenePerf.ParticleGateMs/views,g_ScenePerf.ParticleActivationMs/views,g_ScenePerf.ParticleResetMs/views);
        advancedfx::Message("Detached validation fast reads %d: %llu direct, %llu system, %llu faults.\n",int(g_SceneFastParticleReads),g_ScenePerf.FastReads,g_ScenePerf.SystemReads,g_ScenePerf.ReadFaults);
        unsigned resident=0;unsigned long long hits=0,adds=0;Mirv_Tf2_SceneAssetStatus(resident,hits,adds);
        advancedfx::Message("Model residency %d; %u/256 retained asset references; %llu reuse hits, %llu additions (session). Studio batching %d, queued batching %d.\n",int(g_SceneKeepAssets),resident,hits,adds,int(g_SceneBatchStudio),int(g_SceneBatchQueued));
        advancedfx::Message("Preview frustum cull %d, actor-group cull %d; rejected model submissions %llu (actor group %llu).\n",int(g_ScenePreviewCull),int(g_SceneActorCull),g_ScenePerf.ModelsCulled,g_ScenePerf.ActorsCulled);
        advancedfx::Message("Main-only studio %d; verified non-main skips: studio %llu, particles %llu. Last studio failure tick %d, asset %s.\n",int(g_SceneMainViewOnly),g_ScenePerf.NonMainModelSkipped,g_ScenePerf.NonMainParticleSkipped,g_ScenePerf.LastModelFailureTick,g_ScenePerf.LastModelFailureAsset.empty()?"none":g_ScenePerf.LastModelFailureAsset.c_str());
        g_ScenePerf.ParticleAcquire.Print("Particle snapshot lookup/decode/preparation");
        g_ScenePerf.ParticleDraw.Print("Native particle replay (all eligible callbacks)");
        g_ScenePerf.LegacyDraw.Print("Legacy replay");
        g_ScenePerf.SurfaceBegin.Print("Surface replay setup");
        g_ScenePerf.SurfaceEnd.Print("Surface replay cleanup");
        g_ScenePerf.LegacyCapture.Print("Legacy capture");
        g_ScenePerf.SurfaceCapture.Print("Surface capture");
        advancedfx::Message("Particle main views %llu; attempts %llu; rejected %llu; legacy rejected %llu. Last particle failure tick %d: %s\n",
            g_ScenePerf.ParticleMain,g_ScenePerf.ParticleAttempts,g_ScenePerf.ParticleRejected,g_ScenePerf.LegacyRejected,g_ScenePerf.LastParticleFailureTick,g_ScenePerf.LastParticleFailure.empty()?"none":g_ScenePerf.LastParticleFailure.c_str());
        advancedfx::Message("Studio session callbacks %llu; attempted %llu; submitted %llu; queue skips %llu; other skips %llu; max callbacks per frame %u. Last failure: %s\n",
            g_SceneReplayCallbacks,g_SceneReplayAttempted,g_SceneReplaySubmitted,g_SceneReplayQueueSkips,g_SceneReplayOtherSkips,g_SceneReplayMaxCallbacks,g_SceneReplayLastFailure.empty()?"none":g_SceneReplayLastFailure.c_str());
        return;
    }
    if(!_stricmp(op,"surfaces")){
        if(args->ArgC()==4&&parseInteger(3,value)&&(value==0||value==1)){
            if(!_stricmp(args->ArgV(2),"capture")){if(value&&!g_SceneSurfaceCapture){g_SceneSurfaces.Clear();Mirv_Tf2_ResetSurfaces();}g_SceneSurfaceCapture=value!=0;}
            else if(!_stricmp(args->ArgV(2),"shadows"))g_SceneHistoricalShadows=value!=0;
            else if(!_stricmp(args->ArgV(2),"decals"))g_SceneHistoricalDecals=value!=0;
            else if(!_stricmp(args->ArgV(2),"removed"))g_SceneRemovedDecals=value!=0;
            else if(!_stricmp(args->ArgV(2),"opacity"))g_SceneDecalOpacity=value!=0;
            else if(!_stricmp(args->ArgV(2),"queued"))g_SceneQueuedDecals=value!=0;
            else if(!_stricmp(args->ArgV(2),"batch"))g_SceneHistoricalShadows=g_SceneHistoricalDecals=g_SceneRemovedDecals=g_SceneDecalOpacity=g_SceneQueuedDecals=value!=0;
        }
        const int tick=g_ScenePreview?int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9)):g_SceneSurfaces.Frames.empty()?-1:g_SceneSurfaces.Frames.back().Tick;
        advancedfx::Message("Historical surfaces capture %d, shadows %d, decals %d, removed decal replay %d, surviving opacity %d, queued visibility %d; %zu frames, %.2f MiB / 64 MiB; evicted %zu, rejected %zu. %s\n",int(g_SceneSurfaceCapture),int(g_SceneHistoricalShadows),int(g_SceneHistoricalDecals),int(g_SceneRemovedDecals),int(g_SceneDecalOpacity),int(g_SceneQueuedDecals),g_SceneSurfaces.Frames.size(),g_SceneSurfaces.Bytes/1048576.0,g_SceneSurfaces.Evicted,g_SceneSurfaces.Rejected,g_SceneHistoricalSurfaceStatus.c_str());
        if(const auto resolved=g_SceneSurfaces.ResolvePreview(tick)){
            const auto &sample=resolved.Value;
            advancedfx::Message("Surface requested tick %d; resolved shadows %d, decals %d (-1 unavailable, bounded prior samples). Shadows ready %d, observed %zu, unobserved %zu; decals ready %d, ordinary world %zu, unsupported %zu.\n",tick,resolved.ShadowTick,resolved.DecalTick,int(sample.ShadowsReady),sample.Shadows?sample.Shadows->size():0,sample.ShadowsUnobserved,int(sample.DecalsReady),sample.Decals?sample.Decals->size():0,sample.DecalsUnsupported);
            if(resolved.DecalTick>=0){
                std::size_t geometry=0;if(sample.Decals)for(const auto &d:*sample.Decals)geometry+=d.HasGeometry();
                advancedfx::Message("Historical decal rejection breakdown: %s. Clipped ordinary main-world geometry %zu/%zu; detached removed-mark replay experimental; unsupported types and unreplaced surviving opacity remain incomplete.\n",sample.UnsupportedDecals.Describe().c_str(),geometry,sample.Decals?sample.Decals->size():0);
            }
        }
        else advancedfx::Message("Surface tick %d: no bounded prior sample.\n",tick);
        std::size_t shadowFrames=0,decalFrames=0,nonemptyDecals=0;
        for(const auto &f:g_SceneSurfaces.Frames){shadowFrames+=f.ShadowsReady;decalFrames+=f.DecalsReady;nonemptyDecals+=f.DecalsReady&&f.Decals&&!f.Decals->empty();}
        advancedfx::Message("Surface coverage: %zu shadow-ready, %zu decal-ready (%zu nonempty) of %zu frames. Last decal capture: %s.\n",shadowFrames,decalFrames,nonemptyDecals,g_SceneSurfaces.Frames.size(),g_SceneSurfaces.Frames.empty()?"none":advancedfx::SceneSurfaces::DecalReadLabel(g_SceneSurfaces.Frames.back().DecalStatus));
        advancedfx::Message("mirv_cache surfaces <capture|shadows|decals|removed|opacity|queued|batch> <0|1> | status\n");return;
    }
    if(!_stricmp(op,"visuals")) {
        if(args->ArgC()==4&&parseInteger(3,value)&&(value==0||value==1)) {
            if(!_stricmp(args->ArgV(2),"modulation"))g_SceneReplayModulation=value!=0;
            else if(!_stricmp(args->ArgV(2),"lightingCapture"))g_SceneLightingCapture=value!=0;
            else if(!_stricmp(args->ArgV(2),"lighting"))g_SceneReplayLighting=value!=0;
            else if(!_stricmp(args->ArgV(2),"hideShadows"))g_SceneHideShadows=value!=0;
            else if(!_stricmp(args->ArgV(2),"hideDecals"))g_SceneHideDecals=value!=0;
            else if(!_stricmp(args->ArgV(2),"batch")){g_SceneReplayModulation=g_SceneLightingCapture=g_SceneReplayLighting=g_SceneHideShadows=g_SceneHideDecals=g_SceneLegacyReplay=value!=0;g_SceneLegacyMarkers=false;}
        }
        advancedfx::Message("mirv_cache visuals <modulation|lightingCapture|lighting|hideShadows|hideDecals|batch> <0|1>; or visuals status. Modulation %d; hide live shadows %d (%s); hide live decals %d (%s).\n",int(g_SceneReplayModulation),int(g_SceneHideShadows),g_SceneSurfaceStatus[0].c_str(),int(g_SceneHideDecals),g_SceneSurfaceStatus[1].c_str());
        advancedfx::Message("Model lighting capture %d, replay %d. %s\n",int(g_SceneLightingCapture),int(g_SceneReplayLighting),g_SceneLightingStatus.c_str());
        if(auto view=SceneCacheView()){
            std::size_t lit=0,lights=0;for(const auto &e:view->Entities)if(e.ModelLighting){++lit;lights+=e.ModelLighting->Count;}
            advancedfx::Message("Sampled historical model lighting: %zu models, %zu local descriptors.\n",lit,lights);
            std::size_t decalKnown=0,decalModels=0,decalIndices=0;for(const auto &e:view->Entities)if(e.ModelDecals.Known()){++decalKnown;decalModels+=e.ModelDecals.Batches!=0;decalIndices+=e.ModelDecals.Indices;}
            advancedfx::Message("Studio decal inventory (capture only): %zu known models, %zu affected, %zu indices.\n",decalKnown,decalModels,decalIndices);
            std::size_t known=0,changed=0;
            for(const auto &e:view->Entities)if(e.ModelPose&&e.HasModulation){
                ++known;
                if(e.Modulation!=std::array<float,4>{1,1,1,1}){
                    if(changed++<8)advancedfx::Message("  model %s RGBA %.3f %.3f %.3f %.3f\n",e.ModelPose->ModelInfo->Name.c_str(),e.Modulation[0],e.Modulation[1],e.Modulation[2],e.Modulation[3]);
                }
            }
            advancedfx::Message("Cached model modulation: %zu known, %zu nonneutral.\n",known,changed);
        }
        return;
    }
	if (!_stricmp(op, "particles"))
	{
	    if(integer(value)&&(value==0||value==1)) {
	        SceneParticleCaptureEnabled(value!=0);
	        SceneNativeParticleStatus(-1);
	    } else if(args->ArgC()==3&&!_stricmp(args->ArgV(2),"status")) {
	        SceneNativeParticleStatus(g_ScenePreview?int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9)):g_SceneNativeParticles.Frames.empty()?-1:g_SceneNativeParticles.Frames.back().Tick);
	    } else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"legacyCapture")&&parseInteger(3,value)&&(value==0||value==1)) {
            if(value&&!g_SceneLegacyCapture){g_SceneLegacyHistory.Clear();g_SceneLegacyIsolationFailed.Reset();g_SceneLegacyCaptureStatus="Not captured.";}
            g_SceneLegacyCapture=value!=0;SceneLegacyParticleStatus(-1);
        } else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"legacyAll")&&parseInteger(3,value)&&(value==0||value==1)) {
            if(g_SceneLegacyAll!=(value!=0)){g_SceneLegacyAll=value!=0;g_SceneLegacyHistory.Clear();}
            advancedfx::Message("Broader exact simple-emitter capture %d. Record fresh history.\n",int(g_SceneLegacyAll));
        } else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"legacyReplay")&&parseInteger(3,value)&&(value==0||value==1)) {
            g_SceneLegacyReplay=value!=0;g_SceneLegacyReplayResult={};g_SceneLegacyReplayTick=-1;SceneLegacyParticleStatus(-1);
        } else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"legacyQueued")&&parseInteger(3,value)&&(value==0||value==1)) {
            g_SceneLegacyQueued=value!=0;g_SceneLegacyReplayResult={};g_SceneLegacyReplayTick=-1;SceneLegacyParticleStatus(-1);
        } else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"legacyMarkers")&&parseInteger(3,value)&&(value==0||value==1)) {
            g_SceneLegacyMarkers=value!=0;SceneLegacyParticleStatus(-1);
        } else if(args->ArgC()==3&&!_stricmp(args->ArgV(2),"legacyStatus")) {
            SceneLegacyParticleStatus(g_ScenePreview?int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9)):g_SceneLegacyHistory.Frames.empty()?-1:g_SceneLegacyHistory.Frames.back().Tick,g_ScenePreview);
        } else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"legacyAt")&&parseInteger(3,value)&&value>=0) {
            SceneLegacyParticleStatus(value);
        } else if(args->ArgC()==3&&!_stricmp(args->ArgV(2),"legacy")) {
            if(g_ScenePreview)advancedfx::Message("Disable cached preview before auditing live legacy particles.\n");
            else {g_SceneLegacyAuditPending=true;advancedfx::Message("Legacy particle audit requested for next engine frame end.\n");}
        } else if(args->ArgC()==3&&!_stricmp(args->ArgV(2),"clear")) {
	        SceneClearNativeParticles();SceneNativeParticleStatus(-1);
	    } else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"at")&&parseInteger(3,value)&&value>=0) {
	        SceneNativeParticleStatus(value);
	    } else if((args->ArgC()>=4&&args->ArgC()<=6)&&!_stricmp(args->ArgV(2),"list")&&parseInteger(3,value)&&value>=0) {
            int start=0;
            if(args->ArgC()>=5&&(!parseInteger(4,start)||start<0)) {
                advancedfx::Message("Invalid collection start index.\n");return;
            }
            SceneNativeParticleList(value,static_cast<std::size_t>(start),args->ArgC()==6?args->ArgV(5):"");
        } else if(args->ArgC()==5&&!_stricmp(args->ArgV(2),"find")&&parseInteger(3,value)&&value>=0) {
            SceneNativeParticleList(value,0,args->ArgV(4));
        } else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"budget")&&parseInteger(3,value)&&value>=64&&value<=2048) {
            g_SceneNativeParticles.SetBudget(std::size_t(value)*1024u*1024u);
            SceneNativeParticleStatus(-1);
        } else if((args->ArgC()==4||args->ArgC()==5)&&!_stricmp(args->ArgV(2),"prepare")&&parseInteger(args->ArgC()-1,value)&&value>=0) {
            int tick=g_ScenePreview?int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9)):g_SceneNativeParticles.Frames.empty()?-1:g_SceneNativeParticles.Frames.back().Tick;
            if(args->ArgC()==5&&(!parseInteger(3,tick)||tick<0)) {
                advancedfx::Message("Invalid explicit particle tick.\n");return;
            }
            const auto *sample=g_SceneNativeParticles.At(tick);
            if(g_SceneRecord||!g_VEngineClient||!g_VEngineClient->IsPlayingDemo()||!g_VEngineClient->IsPaused()||g_VEngineClient->IsSkippingDemo()||
               !sample||!sample->Appearance||static_cast<std::size_t>(value)>=sample->Collections.size())
                advancedfx::Message("Detached probe rejected: stop cache recording, pause demo, and choose a named collection from an exact cached appearance tick.\n");
            else if(!sample->Collections[value].Active||!sample->Collections[value].Name[0])
                advancedfx::Message("Detached probe rejected: tick %d collection %d is empty or unnamed. Use particles at %d to select a nonempty entry.\n",tick,value,tick);
            else {
                g_SceneDetachedTick=tick;g_SceneDetachedIndex=value;g_SceneDetachedStatus="Requested for next engine frame end.";
                advancedfx::Message("Detached probe scheduled: tick %d collection %d (%s), %u historical particles.\n",tick,value,sample->Collections[value].Name.data(),sample->Collections[value].Active);
            }
	    } else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"markers")&&parseInteger(3,value)&&(value==0||value==1)) {
	        g_SceneNativeParticleMarkers=value!=0;if(value)g_SceneNativeParticleReplay=false;SceneNativeParticleStatus(-1);
	    } else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"native")&&parseInteger(3,value)&&(value==0||value==1)) {
	        SceneParticlePreviewMode(value?1:0);SceneNativeParticleStatus(-1);
	    } else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"isolate")&&parseInteger(3,value)&&(value==0||value==1)) {
	        g_SceneSuppressLiveParticles=value!=0;SceneNativeParticleStatus(-1);
	    } else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"appearance")&&parseInteger(3,value)&&(value==0||value==1)) {
	        g_SceneNativeParticleAppearance=value!=0;SceneNativeParticleStatus(-1);
	    } else if(args->ArgC()==4&&!_stricmp(args->ArgV(2),"styled")&&parseInteger(3,value)&&(value==0||value==1)) {
	        g_SceneNativeParticleStyled=value!=0;SceneNativeParticleStatus(-1);
	    } else advancedfx::Message("mirv_cache particles <0|1|status|clear|legacy|legacyStatus> | <legacyCapture|legacyReplay|legacyQueued|legacyMarkers|legacyAll> <0|1> | legacyAt <tick> | at <tick> | list <tick> [start [filter]] | find <tick> <filter> | prepare [tick] <collection index> | budget <MiB:64..2048> | <native|markers|appearance|styled|isolate> <0|1> - TF2 x64 native particle experiment.\n");
	    return;
	}
	if (!_stricmp(op, "effects"))
	{
		if (integer(value) && (value == 0 || value == 1)) {
			if (value && !g_SceneEffectsEnabled) SceneClearEffects();
			g_SceneEffectsEnabled = value != 0;
			if (value) advancedfx::Message("Particle event probe armed. Enable mirv_agr enabled 1 before loading the demo. "
			    "Use a fresh uninterrupted cache recording; effects already active may be missing.\n");
			SceneEffectsStatus(-1, false);
		} else if (args->ArgC() == 3 && !_stricmp(args->ArgV(2), "status")) {
			SceneEffectsStatus(g_SceneEffects.Last, false);
		} else if (args->ArgC() == 3 && !_stricmp(args->ArgV(2), "clear")) {
			SceneClearEffects(); SceneEffectsStatus(-1, false);
		} else if (args->ArgC() == 4 && !_stricmp(args->ArgV(2), "at") && parseInteger(3, value) && value >= 0) {
			SceneEffectsStatus(value, true);
		} else advancedfx::Message("mirv_cache effects <0|1|status|clear> | at <tick> - experimental particle event capture, no visual replay.\n");
		return;
	}
	if (!_stricmp(op, "record") && integer(value) && (value == 0 || value == 1))
	{
		if (value && !g_SceneRecord && g_SceneEffectsEnabled) g_SceneEffects.Interrupt();
		g_SceneRecord = value != 0;
		ok = true;
	}
	else if (!_stricmp(op, "range"))
	{
		int first, last;
		if (args->ArgC() == 3 && !_stricmp(args->ArgV(2), "off"))
		{
			g_SceneRangeEnabled = false;
			ok = true;
		}
		else if (args->ArgC() == 4 && parseInteger(2, first) && parseInteger(3, last) && first >= 0 &&
		         last >= first)
		{
			g_SceneRangeFirst = first;
			g_SceneRangeLast = last;
			g_SceneRangeEnabled = true;
			ok = true;
		}
	}
	else if (!_stricmp(op, "preview") && integer(value) && (value == 0 || value == 1))
	{
		if (!value)
		{
			++g_SceneDrawEpoch;
			g_ScenePreview = false;
			SceneCacheStopScrub();
			SceneCacheFrame(false);
			ok = true;
		}
		else if (!g_SceneCache.Frames().empty() && g_VEngineClient && g_VEngineClient->IsPlayingDemo())
		{
			bool wasActive = g_ScenePreview;
			g_ScenePreview = true;
			ScenePreviewTick(
			    wasActive ? int(g_SequencerFrame)
				          : int(std::floor(g_SceneCache.Frames().back().DemoTime / SceneTickInterval())));
			ok = true;
		}
		g_SequencerNeedsRefresh = true;
	}
	else if (!_stricmp(op, "rate") && integer(value) && value >= -10000 && value <= 10000)
	{
		if (!value)
		{
			SceneCacheStopScrub();
			ok = true;
		}
		else if (g_ScenePreview && !g_SceneCache.Frames().empty())
		{
			g_WorkLoop = false;
			g_SceneScrubRate = value;
			g_SceneScrubLast = std::chrono::steady_clock::now();
			ok = true;
		}
	}
	else if ((!_stricmp(op, "goto") || !_stricmp(op, "step")) && integer(value))
	{
		const auto &frames = g_SceneCache.Frames();
		if (!frames.empty() && g_VEngineClient && g_VEngineClient->IsPlayingDemo())
		{
			int64_t target = !_stricmp(op, "step") ? int64_t(g_SequencerFrame) + value : value;
			int first = int(std::floor(frames.front().DemoTime / SceneTickInterval()));
			int last = int(std::floor(frames.back().DemoTime / SceneTickInterval()));
			if (!_stricmp(op, "step"))
				target = std::clamp(target, int64_t(first), int64_t(last));
			if (target >= first && target <= last)
			{
				g_SceneScrubRate = 0;
				g_WorkLoop = false;
				g_ScenePreview = true;
				ScenePreviewTick(int(target));
				g_SequencerNeedsRefresh = true;
				ok = true;
			}
		}
	}
	else if (!_stricmp(op, "players") || !_stricmp(op, "filter"))
	{
		const bool bodies = !_stricmp(op, "players");
		const char *mode = args->ArgC() > 2 ? args->ArgV(2) : "list";
		if (!bodies && args->ArgC() == 2)
		{
			advancedfx::Message(
			    "Filter: %s; %zu selected. Use mirv_cache filter all|none|set|add|remove <indices...>.\n",
			    g_ScenePlayerFilter ? "selected players" : "all players", g_ScenePlayers.size());
			return;
		}
		if (args->ArgC() == 3 &&
		    (!_stricmp(mode, "all") || !_stricmp(mode, "none") || (bodies && !strcmp(mode, "0"))))
		{
			if (bodies)
				SceneSelectPosePlayer(!_stricmp(mode, "all") ? -1 : 0);
			else
			{
				g_ScenePlayerFilter = _stricmp(mode, "all") != 0;
				g_ScenePlayers.clear();
			}
			ok = true;
		}
		else
		{
			const bool set = bodies || !_stricmp(mode, "set"), add = !bodies && !_stricmp(mode, "add"),
			           remove = !bodies && !_stricmp(mode, "remove");
			const int first = bodies ? 2 : 3;
			std::map<int, ScenePlayerSelection> selected;
			bool valid = (set || add || remove) && args->ArgC() > first && args->ArgC() - first <= 128;
			for (int arg = first; valid && arg < args->ArgC(); ++arg)
			{
				int index, user;
				ScenePlayerSelection player;
				valid = parseInteger(arg, index) && SceneResolvePlayer(index, user, player);
				if (valid)
					selected.emplace(user, std::move(player));
			}
			if (valid)
			{
				if (!set && !g_ScenePlayerFilter)
				{
					g_ScenePlayers.clear();
					for (int index = 1; index <= Mirv_Tf2_GetMaxPlayers(); ++index)
					{
						int user;
						ScenePlayerSelection player;
						if (SceneResolvePlayer(index, user, player))
							g_ScenePlayers.emplace(user, std::move(player));
					}
				}
				if (set)
					g_ScenePlayers = std::move(selected);
				else
					for (const auto &entry : selected)
						if (add)
							g_ScenePlayers[entry.first] = entry.second;
						else
							g_ScenePlayers.erase(entry.first);
				g_ScenePlayerFilter = true;
				if (bodies)
					g_ScenePlayerModels = true;
				ok = true;
			}
		}
	}
	else if (!_stricmp(op, "projectiles") || !_stricmp(op, "weapons") || !_stricmp(op, "cosmetics") ||
	         !_stricmp(op, "ragdolls") || !_stricmp(op, "gibs") || !_stricmp(op, "buildings") ||
	         !_stricmp(op, "props"))
	{
		int enabled;
		if (integer(enabled) && (enabled == 0 || enabled == 1))
		{
			if (!_stricmp(op, "weapons"))
				g_SceneWeapons = enabled != 0;
			else if (!_stricmp(op, "cosmetics"))
				g_SceneCosmetics = enabled != 0;
			else if (!_stricmp(op, "ragdolls"))
				g_SceneRagdolls = enabled != 0;
			else if (!_stricmp(op, "gibs"))
				g_SceneGibs = enabled != 0;
			else if (!_stricmp(op, "buildings"))
				g_SceneBuildings = enabled != 0;
			else if (!_stricmp(op, "props"))
				g_SceneProps = enabled != 0;
			else
				g_SceneProjectiles = enabled != 0;
			ok = true;
		}
	}
	else if (!_stricmp(op, "animation") && args->ArgC() == 3)
	{
		if (!_stricmp(args->ArgV(2), "smooth") || !_stricmp(args->ArgV(2), "nearest"))
		{
			g_SceneSmoothAnimation = !_stricmp(args->ArgV(2), "smooth");
			SceneCacheFrame(false);
			ok = true;
		}
	}

	else if (!_stricmp(op, "bounds"))
	{
		if (integer(value) && (value == 0 || value == 1))
		{
			g_SceneDrawing.Bounds = value != 0;
			ok = true;
		}
		else if (args->ArgC() == 4 && _stricmp(args->ArgV(2), "entities") && parseInteger(3, value) &&
		         (value == 0 || value == 1))
		{
			if (!_stricmp(args->ArgV(2), "missing"))
			{
				g_SceneDrawing.BoundsMissingOnly = value != 0;
				ok = true;
			}
			const char *names[] = {"players",  "projectiles", "weapons",   "cosmetics", "other",
			                       "ragdolls", "gibs",        "buildings", "props"};
			for (int i = 0; i < advancedfx::SceneCache::CategoryCount; ++i)
				if (!_stricmp(args->ArgV(2), names[i]) || !_stricmp(args->ArgV(2), "all"))
				{
					g_SceneDrawing.BoundCategories[i] = value != 0;
					ok = true;
				}
		}
		else if (args->ArgC() >= 4 && !_stricmp(args->ArgV(2), "entities"))
		{
			if (args->ArgC() == 4 && (!_stricmp(args->ArgV(3), "all") || !_stricmp(args->ArgV(3), "none")))
			{
				g_SceneDrawing.BoundsSelectedOnly = _stricmp(args->ArgV(3), "all") != 0;
				g_SceneDrawing.BoundEntities.clear();
				ok = true;
			}
			else
			{
				std::set<std::uint64_t> selected;
				bool valid = args->ArgC() <= 131;
				for (int i = 3; valid && i < args->ArgC(); ++i)
				{
					const char *word = args->ArgV(i);
					char *end = nullptr;
					errno = 0;
					auto id = std::strtoull(word, &end, 10);
					valid = *word >= '0' && *word <= '9' && end != word && !*end && !errno && id;
					if (valid)
						selected.insert(id);
				}
				if (valid)
				{
					g_SceneDrawing.BoundsSelectedOnly = true;
					g_SceneDrawing.BoundEntities = std::move(selected);
					ok = true;
				}
			}
		}
	}

	else if (!_stricmp(op, "trails"))
	{
		if (args->ArgC() == 3 && integer(value) && (value == 0 || value == 1))
		{
			g_SceneDrawing.Trails = value != 0;
			ok = true;
		}
		else if (args->ArgC() == 4 && !_stricmp(args->ArgV(2), "seconds"))
		{
			char *end = nullptr;
			const double seconds = std::strtod(args->ArgV(3), &end);
			if (end != args->ArgV(3) && !*end && std::isfinite(seconds) && seconds >= .05 && seconds <= 30)
			{
				g_SceneDrawing.TrailSeconds = seconds;
				ok = true;
			}
		}
		else if (args->ArgC() == 4 && parseInteger(3, value) && (value == 0 || value == 1))
		{
			const char *categories[] = {"players",  "projectiles", "weapons",   "cosmetics", "other",
			                            "ragdolls", "gibs",        "buildings", "props"};
			for (int i = 0; i < advancedfx::SceneCache::CategoryCount; ++i)
				if (!_stricmp(args->ArgV(2), categories[i]) || !_stricmp(args->ArgV(2), "all"))
				{
					g_SceneDrawing.TrailCategories[i] = value != 0;
					ok = true;
				}
		}
	}
	else if (!_stricmp(op, "models"))
	{
		int enabled;
		if (integer(enabled) && (enabled == 0 || enabled == 1))
		{
			if (g_SceneDrawModels.exchange(enabled != 0) != (enabled != 0))
				++g_SceneDrawEpoch;
			SceneModelStatus(enabled ? "Cached model drawing enabled." : "Cached model drawing disabled.");
			ok = true;
		}
	}
	else if (!_stricmp(op, "clear") && args->ArgC() == 2)
	{
		SceneClearHistory();
		ok = true;
	}
	else if (!_stricmp(op, "status") && args->ArgC() <= 2)
		ok = true;
	else if (!_stricmp(op, "help") && args->ArgC() <= 2)
	{
		advancedfx::Message(
		    "Scene cache commands:\n"
		    "  mirv_cache record <0|1> - Record while the actual demo plays forward.\n"
		    "  mirv_cache players <all|none|indices...> - Player model capture.\n"
		    "  mirv_cache filter <all|none|set|add|remove> [indices...] - Body/held-gear scope.\n"
		    "  mirv_cache <projectiles|weapons|cosmetics|ragdolls|gibs|buildings|props> <0|1>\n"
		    "  mirv_cache range <start tick> <end tick>|off - Inclusive capture bounds.\n"
		    "  mirv_cache clear - Clear recorded history.\n"
		    "  mirv_cache preview <0|1> - Preview retained history without seeking the demo.\n"
		    "  mirv_cache goto <tick> | step <signed ticks>\n"
		    "  mirv_cache rate <-10000..10000> - Cached ticks/sec; 0 stops.\n"
		    "  mirv_cache animation <smooth|nearest>\n"
		    "  mirv_cache models <0|1> | bounds <0|1>\n"
		    "  mirv_cache bounds <category|all|missing> <0|1>\n"
		    "  mirv_cache bounds entities <all|none|stable identities...>\n"
		    "  mirv_cache trails <0|1> | trails seconds <0.05..30>\n"
		    "  mirv_cache trails "
		    "<all|players|projectiles|weapons|cosmetics|other|ragdolls|gibs|buildings|props> <0|1>\n"
		    "  mirv_cache particles <0|1|status|clear|legacy|legacyStatus> | <legacyCapture|legacyReplay|legacyQueued|legacyMarkers|legacyAll> <0|1> | legacyAt <tick> | at <tick> | list <tick> [start [filter]] | find <tick> <filter> | prepare [tick] <collection index> | budget <MiB:64..2048> | <native|markers|appearance|styled|isolate> <0|1> - Native particle experiment.\n"
		    "  mirv_cache surfaces <capture|shadows|decals|removed|opacity|queued|batch> <0|1> | status\n"
		    "  mirv_cache visuals <modulation|lightingCapture|lighting|hideShadows|hideDecals|batch> <0|1> | status\n"
		    "  mirv_cache effects <0|1|status|clear> | at <tick> - Particle event probe (no visual replay).\n"
		    "  mirv_cache perf [reset|mainOnly <0|1>|cull <0|1>|actorCull <0|1>|residency <0|1>|fastWrites <0|1>|fastReads <0|1>|batchStudio <0|1>|batchQueued <0|1>|bench [full|models|report|cancel]] - CPU replay timings and failure diagnostics.\n"
            "  mirv_cache validate <setup|report> - Fresh consolidated validation profile or combined report.\n"
            "  mirv_cache status - Detailed capture/render diagnostics.\n"
		    "Cached navigation: Right Ctrl + arrows / Home / End.\n");
		return;
	}
	if (!ok)
	{
		advancedfx::Warning("Scene cache command rejected. Use mirv_cache help for syntax.\n");
		return;
	}
	advancedfx::Message("Scene cache: record %d, preview %d; rate %d ticks/s; tick %d; %zu frames, %.2f MiB "
	                    "payload; coverage at cursor %s.\n",
	                    int(g_SceneRecord), int(g_ScenePreview), g_SceneScrubRate, int(g_SequencerFrame),
	                    g_SceneCache.Count(), g_SceneCache.Bytes() / 1048576.0,
	                    SceneCacheView() ? "yes" : "no");
	if (_stricmp(op, "status"))
	{
		if (ImGui::GetCurrentContext())
			ImGui::MarkIniSettingsDirty();
		return;
	}

	advancedfx::Message("Scene pose: players %d; filter %s (%zu selected); captured %llu, unavailable %llu; "
	                    "models %d; projectiles %d; weapons %d; cosmetics %d; ragdolls %d; gibs %d; "
	                    "buildings %d; props %d; animation %s. %s\n",
	                    int(g_ScenePlayerModels), g_ScenePlayerFilter ? "selected" : "all",
	                    g_ScenePlayers.size(), g_ScenePoseCaptured, g_ScenePoseFailures,
	                    int(g_SceneDrawModels.load()), int(g_SceneProjectiles), int(g_SceneWeapons),
	                    int(g_SceneCosmetics), int(g_SceneRagdolls), int(g_SceneGibs), int(g_SceneBuildings),
	                    int(g_SceneProps), g_SceneSmoothAnimation ? "smooth" : "nearest",
	                    g_ScenePoseStatus.c_str());
	if (g_SceneRenderSnapshot) advancedfx::Message("%s\n", g_SceneAuditStatus.c_str());
	if (g_SceneEffectsEnabled || g_SceneEffects.First >= 0)
		SceneEffectsStatus(g_ScenePreview ? int(g_SequencerFrame) : g_SceneEffects.Last, false);
	if(g_SceneNativeParticlesEnabled||!g_SceneNativeParticles.Frames.empty())
	    SceneNativeParticleStatus(g_ScenePreview?int(std::floor(g_SceneCursor/SceneTickInterval()+1e-9)):g_SceneNativeParticles.Frames.empty()?-1:g_SceneNativeParticles.Frames.back().Tick);
	if (g_SceneRangeEnabled)
		advancedfx::Message("Capture range: %d .. %d inclusive%s.\n", g_SceneRangeFirst, g_SceneRangeLast,
		                    g_SceneRecord ? " (armed)" : " (stopped)");
	advancedfx::Message(
	    "Capture cost: last %.3f ms, mean %.3f ms, peak %.3f ms over %llu observed frames; "
	    "%zu unique retained poses, %zu unchanged poses reused.\n",
	    g_SceneCaptureMs, g_SceneCaptureFrames ? g_SceneCaptureTotalMs / g_SceneCaptureFrames : 0,
	    g_SceneCapturePeakMs, g_SceneCaptureFrames, g_SceneCache.PoseCount(), g_SceneCache.ReusedPoses());
	if (g_ScenePreview)
		if (auto status = std::atomic_load(&g_SceneModelStatus))
			advancedfx::Message("%s\n", status->c_str());
	if (!_stricmp(op, "status"))
	{
		unsigned pending = 0, peak = 0;
		Mirv_Tf2_SceneRenderStatus(pending, peak);
		advancedfx::Message(
		    "Cached render: pending %u, peak %u; focus %d, device ready %d, drawing ready %d.\n", pending,
		    peak, int(g_SceneFocused.load()), int(g_SceneDeviceReady.load()), int(SceneCanDrawModels()));
        advancedfx::Message("Replay audit: callbacks %llu, attempted %llu, submitted %llu, queue skips %llu, other skips %llu; max callbacks per engine frame %u; last failure %s.\n",g_SceneReplayCallbacks,g_SceneReplayAttempted,g_SceneReplaySubmitted,g_SceneReplayQueueSkips,g_SceneReplayOtherSkips,g_SceneReplayMaxCallbacks,g_SceneReplayLastFailure.empty()?"none":g_SceneReplayLastFailure.c_str());
		if (auto view = SceneCacheView())
		{
			advancedfx::Message("Scene animation at tick %.3f (%s):\n", g_SceneCursor / SceneTickInterval(),
			                    g_SceneSmoothAnimation ? "smooth" : "nearest");
			for (const auto &entity : view->Entities)
				advancedfx::Message("%s\n", SceneAnimationSummary(entity).c_str());
		}
	}
	if (!g_SceneCache.Frames().empty())
		advancedfx::Message("Cached tick bounds: %d .. %d (gaps may exist).\n",
		                    int(std::floor(g_SceneCache.Frames().front().DemoTime / SceneTickInterval())),
		                    int(std::floor(g_SceneCache.Frames().back().DemoTime / SceneTickInterval())));
}
static bool g_ShowCurveEditor = false, g_FocusGraph = false;
static float g_CurvePadding = 26.0f;
static float g_CurveValueScale = 1.0f;
// View transforms are independent of camera data and undo history.
static float g_CurveValueOffset = 0.0f;
static double g_CurveTimeZoom = 1.0, g_CurveTimeCenter = 0.5;
static std::vector<int> g_CurveSelection;
static bool g_CurveShow[7] = {true, false, false, false, false, false, false};
static double g_DragFirst = 0, g_DragLast = 1;
static double g_DragCenters[7], g_DragHalves[7];
static bool g_CurveLocked[7] = {}, g_LockKeyTimes = false, g_FrameSelectedValues = false;
static bool g_ModifierRangeTool = false;
static double g_ModifierBegin = 0, g_ModifierEnd = 1;
static int g_ModifierSelection = -1;
static int g_CurveFocusChannel = -1; // -1 = none
struct CurveCache
{
	bool valid = false;
	std::vector<double> times;
	std::vector<CamPathValue> values;
} g_CurveCache;
static int g_CurveCtxKeyIndex = -1;
static int g_CurveCtxChannel = -1;
static unsigned long long g_CurveCtxRevision = 0;
static bool g_CurveNormalize = true; // normalize each channel to its own range

// UI SCALE
static float g_UiScale = 1.0f;

// Fonts
// Pointers to fonts we load at startup. We always have Default; the others
// may be null if the system TTF file isn't found.
static ImFont *g_FontDefault = nullptr;
static ImFont *g_FontMono = nullptr;
static ImFont *g_FontSans = nullptr;
static ImFont *g_FontSilly = nullptr;

// Current choice in Settings tab: 0=Default, 1=Monospace, 2=Sans Serif, 3=Silly.
static int g_FontChoice = 0;

// Guard to run font loading once.
static bool g_FontsLoaded = false;

// Persistent demo file dialog (Settings → Open demo)
static ImGui::FileBrowser g_DemoOpenDialog(ImGuiFileBrowserFlags_CloseOnEsc |
										   ImGuiFileBrowserFlags_EditPathString |
										   ImGuiFileBrowserFlags_CreateNewDir);
static bool g_DemoDialogInit = false;

// UI state mirrored for Mirv camera sliders so external changes (e.g., wheel in passthrough)
// stay in sync with the controls below.
static bool g_uiFovInit = false;
static float g_uiFov = 90.0f;
static float g_uiFovDefault = 90.0f;
static bool g_uiRollInit = false;
static float g_uiRoll = 0.0f;
static float g_uiRollDefault = 0.0f;
static bool g_uiKsensInit = false;
static float g_uiKsens = 1.0f;
static float g_uiKsensDefault = 1.0f;

static void CurveCache_Rebuild()
{
	g_CurveCache.valid = true;
	g_CurveCache.times.clear();
	g_CurveCache.values.clear();
	g_CurveCache.times.reserve(g_Hook_VClient_RenderView.m_CamPath.GetSize());
	g_CurveCache.values.reserve(g_Hook_VClient_RenderView.m_CamPath.GetSize());
	for (CamPathIterator it = g_Hook_VClient_RenderView.m_CamPath.GetBegin();
		 it != g_Hook_VClient_RenderView.m_CamPath.GetEnd(); ++it)
	{
		g_CurveCache.times.push_back(it.GetTime());
		g_CurveCache.values.push_back(it.GetValue());
	}
}

// Evaluated display samples are shared by all channels and rebuilt only when
// camera data or the visible time range changes. Never resample the whole path
// inside the per-key/per-channel drawing loops.
struct CurveSamples
{
	unsigned long long Revision = ~0ull;
	double Begin = 0, End = 0;
	int Resolution = 0;
	std::vector<double> Times;
	std::vector<CamPathValue> Values, Base;
	void Update(CamPath &path, double begin, double end, int resolution)
	{
		if (Revision == path.GetGeometryRevision() && Begin == begin && End == end &&
			Resolution == resolution)
			return;
		Revision = path.GetGeometryRevision();
		Begin = begin;
		End = end;
		Resolution = resolution;
		Times.clear();
		Values.clear();
		Base.clear();
		for (int i = 0; i <= resolution; ++i)
			Times.push_back(begin + (end - begin) * i / resolution);
		auto add = [&](double time)
		{
			if (begin <= time && time <= end)
				Times.push_back(time);
		};
		for (auto key = path.GetBegin(); key != path.GetEnd(); ++key)
		{
			add(key.GetTime());
			add(key.GetTime() + key.GetValue().HoldDuration);
		}
		for (const auto &modifier : path.GetModifiers())
		{
			add(modifier.Begin);
			add(modifier.End);
			add(modifier.Begin + modifier.FadeIn);
			add(modifier.End - modifier.FadeOut);
			for (const auto &key : modifier.Envelope)
				add(modifier.Begin + (modifier.End - modifier.Begin) * key.Time);
		}
		std::sort(Times.begin(), Times.end());
		Times.erase(std::unique(Times.begin(), Times.end()), Times.end());
		for (double time : Times)
		{
			Values.push_back(path.Eval(time));
			if (!path.GetModifiers().empty())
				Base.push_back(path.EvalUnmodified(time));
		}
	}
};
static CurveSamples g_CurveRangeSamples, g_CurveViewSamples;

// File dialogs for campath open/save
static ImGui::FileBrowser s_campathOpenDialog(ImGuiFileBrowserFlags_CloseOnEsc |
											  ImGuiFileBrowserFlags_EditPathString |
											  ImGuiFileBrowserFlags_CreateNewDir);
static ImGui::FileBrowser s_campathSaveDialog(ImGuiFileBrowserFlags_EnterNewFilename |
											  ImGuiFileBrowserFlags_CloseOnEsc |
											  ImGuiFileBrowserFlags_EditPathString |
											  ImGuiFileBrowserFlags_CreateNewDir);
static ImGui::FileBrowser s_campathFolderDialog(
	ImGuiFileBrowserFlags_SelectDirectory | ImGuiFileBrowserFlags_ConfirmOnEnter |
	ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_EditPathString |
	ImGuiFileBrowserFlags_CreateNewDir);
static bool s_campathDialogsInit = false;
static bool g_OpenAll = false, g_SaveAll = false, g_ExportClassic = false;
static double g_ExportFps = 120;

// Preferences share the existing ImGui settings file. Demo identities and running
// operations deliberately stay in memory; saved fixes are armed only for compatible demos.
static bool g_OverlayPreferencesReady = false;
static bool g_NoStaticPropFade = false;
static bool g_DemoMute = false, g_DemoDeathcam = false, g_DemoSmooth = false;
static double g_DemoInterpolation = .1;
static auto g_DemoFixAttempt = std::chrono::steady_clock::time_point{};
static std::pair<std::uint64_t, size_t> g_OverlayPreferenceStamp{};
static double g_SavedTrailSeconds = 2;
static std::pair<std::uint64_t, size_t> OverlayPreferenceStamp()
{
	std::uint64_t bits = 0;
	unsigned bit = 0;
	for (bool value : {g_ScenePlayerModels, g_SceneProjectiles, g_SceneWeapons, g_SceneCosmetics,
	                   g_SceneRagdolls, g_SceneGibs, g_SceneBuildings, g_SceneProps, g_SceneSmoothAnimation,
	                   g_SceneDrawModels.load(), g_ShowSequencer, g_ShowCameraControl, g_ShowCurveEditor,
	                   g_ShowGizmo, g_PreviewHudEnabled, g_ShowCameraGuides, g_CameraFrustums, g_DemoMute, g_DemoDeathcam, g_NoStaticPropFade, g_DemoSmooth})
	{
		if (value)
			bits |= 1ull << bit;
		++bit;
	}
	for (bool value : g_SceneDrawing.TrailCategories)
	{
		if (value)
			bits |= 1ull << bit;
		++bit;
	}
	if (g_SceneDrawing.Bounds)
		bits |= 1ull << bit++;
	else
		++bit;
	if (g_SceneDrawing.Trails)
		bits |= 1ull << bit;
	++bit;
	for (bool value : g_SceneDrawing.BoundCategories)
	{
		if (value)
			bits |= 1ull << bit;
		++bit;
	}
	if (g_SceneDrawing.BoundsMissingOnly)
		bits |= 1ull << bit;
	++bit;
	if (g_SceneDrawing.BoundsSelectedOnly)
		bits |= 1ull << bit;
	return {bits, g_SceneCache.Limit()};
}
static void CheckOverlayPreferences()
{
	if (!g_OverlayPreferencesReady || !ImGui::GetCurrentContext())
		return;
	const auto stamp = OverlayPreferenceStamp();
	if (stamp != g_OverlayPreferenceStamp || g_SavedTrailSeconds != g_SceneDrawing.TrailSeconds)
	{
		g_OverlayPreferenceStamp = stamp;
		g_SavedTrailSeconds = g_SceneDrawing.TrailSeconds;
		ImGui::MarkIniSettingsDirty();
	}
}
void RememberDemoFixPreference(DemoFixPreference setting, double value)
{
	if (setting == DemoFixPreference::RecorderMute)
	{
		g_DemoMute = value != 0;
		if (!g_DemoMute)
			MirvFix_ArmRecorderSoundMute(false);
	}
	else if (setting == DemoFixPreference::NoStaticPropFade)
		g_NoStaticPropFade = value != 0;
	else if (setting == DemoFixPreference::Deathcam)
		g_DemoDeathcam = value != 0;
	else
	{
		g_DemoSmooth = value >= .001 && value <= 1 && std::isfinite(value);
		if (g_DemoSmooth)
			g_DemoInterpolation = value;
	}
	g_DemoFixAttempt = {};
	if (ImGui::GetCurrentContext())
		ImGui::MarkIniSettingsDirty();
}
void PreferencesFrame()
{
    SceneBenchService();
	if (g_RecordTogglePending.exchange(false))
		Recording::Toggle();
	WorkTransportFrame();
	if (!g_OverlayPreferencesReady)
		return;
	CheckOverlayPreferences();
	const auto now = std::chrono::steady_clock::now();
	if (now - g_DemoFixAttempt < std::chrono::seconds(1))
		return;
	g_DemoFixAttempt = now;
	if (MirvFix_StaticPropFadeSupported() && g_NoStaticPropFade != MirvFix_GetNoStaticPropFade())
		MirvFix_SetNoStaticPropFade(g_NoStaticPropFade);
	MirvFix_ArmRecorderSoundMute(g_DemoMute);
	if (g_DemoDeathcam && !MirvFix_GetPovDeathcam())
		MirvFix_SetPovDeathcam(true);
	if (!g_VEngineClient || !g_VEngineClient->IsPlayingDemo() || g_VEngineClient->IsDrawingLoadingImage())
		return;
	if (g_DemoMute && !g_VEngineClient->IsHLTV() && !MirvFix_GetRecorderSoundMute())
		MirvFix_SetRecorderSoundMute(true);
	if (g_DemoSmooth && MirvFix_PlayerInterpolationAvailable() &&
	    std::abs(MirvFix_GetPlayerInterpolation() - g_DemoInterpolation) > 1e-6)
		MirvFix_SetPlayerInterpolation(g_DemoInterpolation);
}
// -1: automatic (hide empty), 0: hidden, 1: shown. Events are opt-in.
static int g_SequencerTracks[7] = {-1, -1, -1, -1, -1, 0, -1};
static int g_SequencerPathMode = 0;
static bool g_SequencerSourcesOpen = true, g_SequencerPathsOpen = true;
static bool SequencerTrackVisible(int track, bool populated)
{
	return g_SequencerTracks[track] > 0 || (g_SequencerTracks[track] < 0 && populated);
}
static bool SequencerRepairPopulated()
{
	auto &model = MirvFix_PlayerRepairModel();
	return !model.Samples().empty() || !model.GetEdits().Ranges.empty() || !model.GetEdits().Cuts.empty();
}
static bool SequencerPathVisible(size_t index)
{
	auto &paths = g_Hook_VClient_RenderView.m_CamPaths;
	if (!SequencerTrackVisible(0, paths.GetPath(index)->GetSize() != 0))
		return false;
	if (index == paths.GetActive() || g_SequencerPathMode == 2)
		return true;
	if (g_SequencerPathMode == 1)
		for (auto key = paths.GetPath(index)->GetBegin(); key != paths.GetPath(index)->GetEnd(); ++key)
			if (key.GetValue().Selected)
				return true;
	return false;
}
// Persisted overlay paths in imgui.ini
struct OverlayPathsSettings
{
	std::string campathDir, recordBrowseDir, demoDir;
};
static OverlayPathsSettings g_OverlayPaths;
static void *OverlayPaths_ReadOpen(ImGuiContext *, ImGuiSettingsHandler *, const char *name)
{
	return (0 == strcmp(name, "Paths")) ? (void *)&g_OverlayPaths : nullptr;
}
static void OverlayPaths_ReadLine(ImGuiContext *, ImGuiSettingsHandler *, void *entry, const char *line)
{
	OverlayPathsSettings *s = (OverlayPathsSettings *)entry;
	const char *eq = strchr(line, '=');
	if (!eq)
		return;
	std::string key(line, eq - line);
	std::string val(eq + 1);

	if (key == "SequencerTracks")
	{
		int values[7];
		if (std::sscanf(val.c_str(), "%d %d %d %d %d %d %d", &values[0], &values[1], &values[2], &values[3],
						&values[4], &values[5], &values[6]) == 7)
			for (int i = 0; i < 7; ++i)
				g_SequencerTracks[i] = std::clamp(values[i], -1, 1);
		return;
	}
	if (key == "SequencerSourcesOpen" || key == "SequencerPathsOpen")
	{
		if (val == "0" || val == "1")
			(key == "SequencerSourcesOpen" ? g_SequencerSourcesOpen : g_SequencerPathsOpen) = val == "1";
		return;
	}
	if (key == "SequencerPathMode")
	{
		g_SequencerPathMode = std::clamp(std::atoi(val.c_str()), 0, 2);
		return;
	}
	if (key == "CacheTrailSeconds")
	{
		char *end = nullptr;
		const double seconds = std::strtod(val.c_str(), &end);
		if (end != val.c_str() && !*end && std::isfinite(seconds) && seconds >= .05 && seconds <= 30)
			g_SceneDrawing.TrailSeconds = seconds;
		return;
	}
	bool *option = nullptr;
	if (key == "CacheBoundsMissingOnly")
		option = &g_SceneDrawing.BoundsMissingOnly;
	for (int i = 0; i < advancedfx::SceneCache::CategoryCount; ++i)
		if (key == "CacheBoundCategory" + std::to_string(i))
			option = &g_SceneDrawing.BoundCategories[i];
	if (key == "CacheBounds")
		option = &g_SceneDrawing.Bounds;
	if (key == "CacheTrails")
		option = &g_SceneDrawing.Trails;
	for (int i = 0; i < advancedfx::SceneCache::CategoryCount; ++i)
		if (key == "CacheTrailCategory" + std::to_string(i))
			option = &g_SceneDrawing.TrailCategories[i];
	if (key == "CachePlayers")
		option = &g_ScenePlayerModels;
	if (key == "CacheProjectiles")
		option = &g_SceneProjectiles;
	if (key == "CacheWeapons")
		option = &g_SceneWeapons;
	if (key == "CacheCosmetics")
		option = &g_SceneCosmetics;
	if (key == "CacheRagdolls")
		option = &g_SceneRagdolls;
	if (key == "CacheGibs")
		option = &g_SceneGibs;
	if (key == "CacheBuildings")
		option = &g_SceneBuildings;
	if (key == "CacheProps")
		option = &g_SceneProps;
	if (key == "CacheSmooth")
		option = &g_SceneSmoothAnimation;
	if (key == "ShowSequencer")
		option = &g_ShowSequencer;
	if (key == "ShowCameraControl")
		option = &g_ShowCameraControl;
	if (key == "ShowCurveEditor")
		option = &g_ShowCurveEditor;
	if (key == "ShowGizmo")
		option = &g_ShowGizmo;
	if (key == "CameraGuides") option=&g_ShowCameraGuides;
	if (key == "CameraFrustums") option=&g_CameraFrustums;
	if (key == "PreviewHud")
		option = &g_PreviewHudEnabled;
	if (key == "FixRecorderMute")
		option = &g_DemoMute;
	if (key == "FixNoStaticPropFade")
		option = &g_NoStaticPropFade;
	if (key == "FixDeathcam")
		option = &g_DemoDeathcam;
	if (key == "FixSmoothMovement")
		option = &g_DemoSmooth;
	if (option)
	{
		if (val == "0" || val == "1")
			*option = val == "1";
		return;
	}
	if (key == "CacheModels")
	{
		if (val == "0" || val == "1")
			g_SceneDrawModels.store(val == "1");
		return;
	}
	if (key == "CacheBudgetMiB")
	{
		char *end = nullptr;
		errno = 0;
		const auto budget = std::strtol(val.c_str(), &end, 10);
		if (!errno && end != val.c_str() && !*end && budget >= 1 && budget <= 1024)
			g_SceneCache.SetLimit(size_t(budget) * 1048576);
		return;
	}
	if (key == "FixInterpolationSeconds")
	{
		char *end = nullptr;
		const double seconds = std::strtod(val.c_str(), &end);
		if (end != val.c_str() && !*end && std::isfinite(seconds) && seconds >= .001 && seconds <= 1)
			g_DemoInterpolation = seconds;
		return;
	}
	if (key == "CampathDir")
	{
		s->campathDir = val;
		if (!val.empty())
		{
			s_campathOpenDialog.SetDirectory(val);
			s_campathSaveDialog.SetDirectory(val);
		}
	}
	else if (key == "RecordBrowseDir")
		s->recordBrowseDir = val;
	else if (key == "DemoDir")
		s->demoDir = val;
	else if (key == "UiScale")
	{
		float v = std::strtof(val.c_str(), nullptr);
		if (std::isfinite(v))
			g_UiScale = std::clamp(v, 0.5f, 2.0f);
	}
	else if (key == "Font")
		g_FontChoice = std::clamp(atoi(val.c_str()), 0, 3);
	else if (key == "DrawNames")
		g_CampathDrawer.SetDrawNames(atoi(val.c_str()) != 0);
	else if (key == "DrawHolds")
		g_CampathDrawer.SetDrawHolds(atoi(val.c_str()) != 0);
	else if (key == "DrawDurations")
		g_CampathDrawer.SetDrawDurations(atoi(val.c_str()) != 0);
	else if (key == "SeekMode")
		MirvCampathPreview_SetSeekOptions(atoi(val.c_str()), MirvCampathPreview_GetSeekScale());
	else if (key == "SeekScale")
		MirvCampathPreview_SetSeekOptions(MirvCampathPreview_GetSeekMode(),
										  std::strtod(val.c_str(), nullptr));
	else if (key == "Autosave")
		g_Autosave = atoi(val.c_str()) != 0;
	else if (key == "AutosaveSeconds")
	{
		double v = std::strtod(val.c_str(), nullptr);
		if (std::isfinite(v))
			g_AutosaveSeconds = std::clamp(v, 5.0, 3600.0);
	}
	else if (key == "Docking")
		g_Docking = atoi(val.c_str()) != 0;
	else if (key == "RecordFps")
	{
		if (!val.empty() && val.size() < sizeof(g_RecordFps))
			std::snprintf(g_RecordFps, sizeof(g_RecordFps), "%s", val.c_str());
	}
	else if (key == "RecordAutoResume") g_RecordAutoResume = atoi(val.c_str()) != 0;
	else if (key == "RecordAutoPause") g_RecordAutoPause = atoi(val.c_str()) != 0;
	else if (key == "RecordAutoClose") g_RecordAutoClose = atoi(val.c_str()) != 0;
	else if (key == "RecordAutoStopOnOpen") g_RecordAutoStopOnOpen = atoi(val.c_str()) != 0;
	else if (key == "RecordHotkeyMods")
		g_RecordHotkeyMods = (unsigned int)atoi(val.c_str()) & 7u;
	else if (key == "RecordHotkey")
	{
		int value = atoi(val.c_str());
		if (value >= 0 && value <= 255 && value != VK_F8 && !RecordModifierKey((unsigned int)value))
			g_RecordHotkey = (unsigned int)value;
	}
}
static void OverlayPaths_WriteAll(ImGuiContext *, ImGuiSettingsHandler *handler, ImGuiTextBuffer *out_buf)
{
	out_buf->appendf("[%s][%s]\n", handler->TypeName, "Paths");
	if (!g_OverlayPaths.campathDir.empty())
		out_buf->appendf("CampathDir=%s\n", g_OverlayPaths.campathDir.c_str());
	if (!g_OverlayPaths.recordBrowseDir.empty())
		out_buf->appendf("RecordBrowseDir=%s\n", g_OverlayPaths.recordBrowseDir.c_str());
	if (!g_OverlayPaths.demoDir.empty())
		out_buf->appendf("DemoDir=%s\n", g_OverlayPaths.demoDir.c_str());
	out_buf->appendf("DrawNames=%d\nDrawHolds=%d\nDrawDurations=%d\n", g_CampathDrawer.GetDrawNames(),
					 g_CampathDrawer.GetDrawHolds(), g_CampathDrawer.GetDrawDurations());
	out_buf->appendf("SeekMode=%d\nSeekScale=%.6f\n", MirvCampathPreview_GetSeekMode(),
					 MirvCampathPreview_GetSeekScale());
	out_buf->appendf("Autosave=%d\nAutosaveSeconds=%.3f\n", g_Autosave ? 1 : 0, g_AutosaveSeconds);
	out_buf->appendf("UiScale=%.3f\nFont=%d\nDocking=%d\n", g_UiScale, g_FontChoice, g_Docking ? 1 : 0);
	out_buf->appendf("RecordFps=%s\nRecordAutoResume=%d\nRecordAutoPause=%d\n"
		"RecordAutoClose=%d\nRecordAutoStopOnOpen=%d\nRecordHotkey=%u\nRecordHotkeyMods=%u\n\n",
		g_RecordFps, (int)g_RecordAutoResume, (int)g_RecordAutoPause,
		(int)g_RecordAutoClose, (int)g_RecordAutoStopOnOpen, g_RecordHotkey, g_RecordHotkeyMods);
	out_buf->appendf("SequencerSourcesOpen=%d\nSequencerPathsOpen=%d\n", int(g_SequencerSourcesOpen),
					 int(g_SequencerPathsOpen));
	out_buf->appendf("SequencerTracks=%d %d %d %d %d %d %d\nSequencerPathMode=%d\n", g_SequencerTracks[0],
					 g_SequencerTracks[1], g_SequencerTracks[2], g_SequencerTracks[3], g_SequencerTracks[4],
					 g_SequencerTracks[5], g_SequencerTracks[6], g_SequencerPathMode);
	out_buf->appendf("CachePlayers=%d\n", int(g_ScenePlayerModels));
	out_buf->appendf("CacheProjectiles=%d\n", int(g_SceneProjectiles));
	out_buf->appendf("CacheWeapons=%d\n", int(g_SceneWeapons));
	out_buf->appendf("CacheCosmetics=%d\n", int(g_SceneCosmetics));
	out_buf->appendf("CacheRagdolls=%d\n", int(g_SceneRagdolls));
	out_buf->appendf("CacheGibs=%d\n", int(g_SceneGibs));
	out_buf->appendf("CacheBuildings=%d\n", int(g_SceneBuildings));
	out_buf->appendf("CacheProps=%d\n", int(g_SceneProps));
	out_buf->appendf("CacheSmooth=%d\n", int(g_SceneSmoothAnimation));
	out_buf->appendf("CacheModels=%d\n", int(g_SceneDrawModels.load()));
	out_buf->appendf("CacheBounds=%d\nCacheTrails=%d\nCacheTrailSeconds=%.3f\n",
	                 int(g_SceneDrawing.Bounds && !g_SceneDrawing.BoundsSelectedOnly),
	                 int(g_SceneDrawing.Trails), g_SceneDrawing.TrailSeconds);
	for (int i = 0; i < advancedfx::SceneCache::CategoryCount; ++i)
		out_buf->appendf("CacheTrailCategory%d=%d\n", i, int(g_SceneDrawing.TrailCategories[i]));
	out_buf->appendf("CacheBoundsMissingOnly=%d\n", int(g_SceneDrawing.BoundsMissingOnly));
	for (int i = 0; i < advancedfx::SceneCache::CategoryCount; ++i)
		out_buf->appendf("CacheBoundCategory%d=%d\n", i, int(g_SceneDrawing.BoundCategories[i]));
	out_buf->appendf("ShowSequencer=%d\n", int(g_ShowSequencer));
	out_buf->appendf("ShowCameraControl=%d\n", int(g_ShowCameraControl));
	out_buf->appendf("ShowCurveEditor=%d\n", int(g_ShowCurveEditor));
	out_buf->appendf("ShowGizmo=%d\n", int(g_ShowGizmo));
	out_buf->appendf("PreviewHud=%d\n", int(g_PreviewHudEnabled));
	out_buf->appendf("CameraGuides=%d\nCameraFrustums=%d\n",int(g_ShowCameraGuides),int(g_CameraFrustums));
	out_buf->appendf("FixRecorderMute=%d\n", int(g_DemoMute));
	out_buf->appendf("FixNoStaticPropFade=%d\n", int(g_NoStaticPropFade));
	out_buf->appendf("FixDeathcam=%d\n", int(g_DemoDeathcam));
	out_buf->appendf("FixSmoothMovement=%d\n", int(g_DemoSmooth));
	out_buf->appendf("CacheBudgetMiB=%zu\nFixInterpolationSeconds=%.6f\n\n", g_SceneCache.Limit() / 1048576,
	                 g_DemoInterpolation);
}

static void SaveOverlayPreferences()
{
	if (!g_OverlayPreferencesReady || !ImGui::GetCurrentContext())
		return;
	CheckOverlayPreferences();
	const char *file = ImGui::GetIO().IniFilename;
	if (file && *file)
		ImGui::SaveIniSettingsToDisk(file);
}

static void PathChanged(void *)
{
	g_SequencerNeedsRefresh = true;
	g_CurveCache.valid = false;
	if (g_LastCampathCtx.active)
	{
		bool found = false;
		for (auto it = g_Hook_VClient_RenderView.m_CamPath.GetBegin();
			 it != g_Hook_VClient_RenderView.m_CamPath.GetEnd(); ++it)
			if (it.GetTime() == g_LastCampathCtx.time)
			{
				found = true;
				g_LastCampathCtx.value = it.GetValue();
				break;
			}
		if (!found)
			g_LastCampathCtx.active = false;
	}
}
bool PreviewInsert()
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	if (g_ScenePreview)
		SceneCacheFrame(false);
	const bool cachedMode = SceneCachePreviewActive();
	auto cachedView = cachedMode ? SceneCacheView() : nullptr;
	if (cachedMode && !cachedView)
	{
		g_DocumentStatus = "Cached time has no coverage. Choose a recorded frame before inserting.";
		return false;
	}
	CancelEditGesture();
	BeginEdit();
	auto &path = g_Hook_VClient_RenderView.m_CamPath;
	const double time = cachedMode ? cachedView->ClientTime - path.GetOffset() : MirvCampathPreview_GetTime();
	const bool ok = path.Insert(time);
	FinishEdit();
	if (!ok)
	{
		g_DocumentStatus = "Insert requires an unoccupied interior time and Custom or Linear channels. The "
		                   "path was retained.";
		advancedfx::Warning("%s\n", g_DocumentStatus.c_str());
	}
	return ok;
}
bool PreviewCapture(bool replace)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	if (g_ScenePreview)
	{
		SceneCacheFrame(false);
		if (!SceneCacheView())
		{
			g_DocumentStatus = "Cached time has no coverage. Choose a recorded frame before capture.";
			return false;
		}
	}
	CancelEditGesture();
	BeginEdit();
	auto &path = g_Hook_VClient_RenderView.m_CamPath;
	double x, y, z, p, yaw, roll;
	float fov;
	Afx_GetLastCameraData(x, y, z, p, yaw, roll, fov);
	CamPathValue pose(x, y, z, p, yaw, roll, fov);
	bool ok = false;
	if (replace)
	{
		auto keys = path.CaptureState().Keys;
		auto selected = keys.end();
		size_t count = 0;
		for (auto it = keys.begin(); it != keys.end(); ++it)
			if (it->second.Selected)
			{
				selected = it;
				++count;
			}
		if (count == 1)
		{
			unsigned int channels = 0;
			for (int ch = 0; ch < 7; ++ch)
				if (!g_CurveLocked[ch])
					channels |= 1u << ch;
			ok = path.ReplacePose(selected->first, pose, channels);
		}
	}
	else
	{
		if (!path.GetSize())
		{
			path.PositionInterpMethod_set(CamPath::DI_CUSTOM);
			path.RotationInterpMethod_set(CamPath::QI_CUSTOM);
			path.FovInterpMethod_set(CamPath::DI_CUSTOM);
		}
		ok = path.AddPose(EditingClientTime(g_MirvTime.GetTime()) - path.GetOffset(), pose);
	}
	if (!ok)
	{
		g_DocumentStatus =
			replace ? "Replace requires one selected key and a pose compatible with neighbouring holds."
					: "Capture conflicts with an existing hold.";
		advancedfx::Warning("%s\n", g_DocumentStatus.c_str());
	}
	if (!IsOpen())
		FinishEdit();
	return ok;
}
bool PreviewHold(bool clear)
{
	CancelEditGesture();
	if (g_LockKeyTimes)
	{
		g_DocumentStatus = "Key times are locked; unlock them to edit holds.";
		return false;
	}
	BeginEdit();
	auto &path = g_Hook_VClient_RenderView.m_CamPath;
	auto keys = path.CaptureState().Keys;
	bool any = false;
	for (auto &key : keys)
		if (key.second.Selected)
		{
			any = true;
			key.second.HoldDuration = clear ? 0 : g_MirvTime.GetTime() - path.GetOffset() - key.first;
		}
	const bool ok = any && path.ReplaceKeys(keys);
	if (!ok)
	{
		g_DocumentStatus = "Select keys and capture a release after arrival, before the next different pose.";
		advancedfx::Warning("%s\n", g_DocumentStatus.c_str());
	}
	if (!IsOpen())
		FinishEdit();
	return ok;
}
void PreviewSelectAll()
{
	g_GraphChannels.clear();
	g_GraphSelectionRevision = ~0ull;
	CancelEditGesture();
	g_Hook_VClient_RenderView.m_CamPath.SelectAll();
}
bool PreviewAnchor()
{
	CancelEditGesture();
	for (bool locked : g_CurveLocked)
		if (locked)
		{
			g_DocumentStatus = "Unlock pose channels before anchoring the selection.";
			return false;
		}
	size_t index = 0;
	auto &path = g_Hook_VClient_RenderView.m_CamPath;
	for (auto it = path.GetBegin(); it != path.GetEnd(); ++it, ++index)
		if (it.GetValue().Selected)
		{
			char command[128];
			snprintf(command, sizeof(command), "mirv_campath edit anchor #%u current", (unsigned)index);
			Afx_ExecClientCmd(command);
			return true;
		}
	g_DocumentStatus = "Select an anchor key first (Ctrl+A anchors from the first key).";
	return false;
}
static void DrawSeekSettings()
{
	int mode = MirvCampathPreview_GetSeekMode();
	const char *items[] = {"Timescale", "Pause", "Continue (1x)"};
	if (ImGui::Combo("After seeking", &mode, items, 3))
	{
		MirvCampathPreview_SetSeekOptions(mode, MirvCampathPreview_GetSeekScale());
		ImGui::MarkIniSettingsDirty();
	}
	if (mode == 0)
	{
		double scale = MirvCampathPreview_GetSeekScale();
		if (ImGui::InputDouble("After-seek timescale", &scale, 0, 0, "%.3f") &&
			MirvCampathPreview_SetSeekOptions(mode, scale))
			ImGui::MarkIniSettingsDirty();
		ImGui::TextDisabled("0 freezes models and particles after the seek completes.");
	}
}
static void SetKeySelection(const std::vector<int> &indices)
{
	auto &path = g_Hook_VClient_RenderView.m_CamPath;
	auto keys = path.CaptureState().Keys;
	g_GraphChannels.clear();
	g_GraphSelectionRevision = ~0ull;
	size_t index = 0;
	bool changed = false;
	for (auto &key : keys)
	{
		const bool selected = std::binary_search(indices.begin(), indices.end(), (int)index++);
		changed |= key.second.Selected != selected;
		key.second.Selected = selected;
	}
	if (changed)
		path.ReplaceKeys(keys);
}
static void DrawDocumentControls()
{
	auto &paths = g_Hook_VClient_RenderView.m_CamPaths;
	ImGui::TextUnformatted(paths.IsDirty() ? "Unsaved changes *" : "All changes saved");
	if (ImGui::BeginCombo("Path", paths.GetName(paths.GetActive()).c_str()))
	{
		for (size_t i = 0; i < paths.GetCount(); ++i)
		{
			ImGui::PushID((int)i);
			const auto label = paths.GetName(i) + (paths.IsDirty(i) ? " *" : "");
			if (ImGui::Selectable(label.c_str(), i == paths.GetActive()))
			{
				const double clientTime =
					MirvCampathPreview_GetTime() + g_Hook_VClient_RenderView.m_CamPath.GetOffset();
				g_CurveKeyDrag = false;
				paths.Select(i);
				if (MirvCampathPreview_IsActive())
					MirvCampathPreview_SetTime(clientTime - g_Hook_VClient_RenderView.m_CamPath.GetOffset());
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	if (ImGui::Button("New path"))
		paths.NewPath();
	ImGui::SameLine();
	if (ImGui::Button("Duplicate path"))
		paths.NewPath({}, true);
	ImGui::SameLine();
	if (ImGui::Button("Remove path"))
		paths.Remove(paths.GetActive());
	static char rename[129] = {};
	static unsigned long long lastId = 0;
	const auto state = paths.CaptureState();
	if (lastId != state.Paths[state.Active].Id || !ImGui::IsAnyItemActive())
	{
		lastId = state.Paths[state.Active].Id;
		snprintf(rename, sizeof(rename), "%s", paths.GetName(paths.GetActive()).c_str());
	}
	if (ImGui::InputText("Path name", rename, sizeof(rename), ImGuiInputTextFlags_EnterReturnsTrue))
		if (!paths.Rename(rename))
			g_DocumentStatus = "Use a nonempty, unique path name (up to 128 bytes).";
	bool muted = paths.IsMuted(paths.GetActive()), solo = paths.IsSolo(paths.GetActive()),
		 visible = paths.IsVisible(paths.GetActive());
	bool flagsChanged = ImGui::Checkbox("Disable playback", &muted);
	ImGui::SameLine();
	flagsChanged |= ImGui::Checkbox("Play only marked paths", &solo);
	ImGui::SameLine();
	flagsChanged |= ImGui::Checkbox("Visible in world", &visible);
	if (flagsChanged)
		paths.SetFlags(paths.GetActive(), muted, solo, visible);
	bool hold = g_Hook_VClient_RenderView.m_CamPath.GetHold();
	if (ImGui::Checkbox("Hold first / last pose outside path", &hold))
		g_Hook_VClient_RenderView.m_CamPath.SetHold(hold);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Classic mirv_campath hold: keep the endpoint pose before the first key and after "
						  "the last. Dim sequencer spans show this extension.");
	const auto winner = paths.GetPlaybackIndex(g_MirvTime.GetTime(), false);
	ImGui::Text("Playback: %s%s",
				winner < paths.GetCount() ? paths.GetName(winner).c_str() : "No eligible path",
				g_Hook_VClient_RenderView.m_CamPath.Enabled_get() ? "" : " (disabled)");
	DrawOverlayHint("Latest start wins overlaps. Duplicates start disabled. Marked paths exclude "
						"unmarked paths. Visibility only affects drawing.");
	if (!g_DocumentStatus.empty())
		ImGui::TextWrapped("%s", g_DocumentStatus.c_str());
	if (!g_RecoveryCandidates.empty())
	{
		ImGui::Separator();
		ImGui::Text("%u recovery backup(s) available", (unsigned)g_RecoveryCandidates.size());
		if (ImGui::Button("Recover latest backup"))
		{
			const auto backup = g_RecoveryCandidates.front();
			UpdateRecovery(true);
			if (paths.LoadAll(backup.wstring().c_str(), true))
			{
				NewRecoverySession();
				g_DocumentStatus = "Recovered unsaved work. Save it with Save As or Save all.";
			}
			else
				g_DocumentStatus = "The recovery backup could not be read; current work was retained.";
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard latest backup"))
		{
			std::error_code error;
			std::filesystem::remove(g_RecoveryCandidates.front(), error);
			ScanRecovery();
		}
	}
}
static void DrawKeyInspector()
{
	auto &path = g_Hook_VClient_RenderView.m_CamPath;
	auto keys = path.CaptureState().Keys;
	if (path.RotationInterpMethod_get() == CamPath::QI_CUSTOM && path.CanEval())
		for (auto &key : keys)
			if (key.second.Selected && !key.second.HasEuler)
				key.second.SetEulerAngles(path.EvalUnmodified(key.first).GetEulerAngles());
	std::vector<double> selected;
	for (const auto &key : keys)
		if (key.second.Selected)
			selected.push_back(key.first);
	ImGui::Checkbox("Lock key times", &g_LockKeyTimes);
	const char *lockNames[] = {"X", "Y", "Z", "FOV", "Pitch", "Yaw", "Roll"};
	for (int ch = 0; ch < 7; ++ch)
	{
		ImGui::PushID(ch);
		if (ch)
			ImGui::SameLine();
		ImGui::Checkbox(lockNames[ch], &g_CurveLocked[ch]);
		ImGui::PopID();
	}
	ImGui::TextDisabled("Checked channels are locked for editor pose edits.");
	if (selected.empty())
	{
		ImGui::TextDisabled("Click a graph key or box select to inspect. Ctrl adds to selection.");
		return;
	}
	ImGui::Text("%u selected key(s) | edits apply as an offset to all selected keys",
				(unsigned)selected.size());
	auto first = keys.find(selected.front());
	double time = first->first;
	const double ipt = TickInterval();
	int tick = 0;
	const bool haveTick = GetDemoTickFromClientTime(g_MirvTime.GetTime(), time + path.GetOffset(), tick);
	double newTime = time;
	ImGui::BeginDisabled(g_LockKeyTimes);
	bool changed = ImGui::InputDouble("Arrival (path seconds)", &newTime, 0, 0, "%.6f",
									  ImGuiInputTextFlags_EnterReturnsTrue);
	if (haveTick &&
		ImGui::InputInt("Arrival (demo tick)", &tick, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue))
	{
		int oldTick = 0;
		GetDemoTickFromClientTime(g_MirvTime.GetTime(), time + path.GetOffset(), oldTick);
		newTime = time + (tick - (double)oldTick) * ipt;
		changed = true;
	}
	ImGui::EndDisabled();
	if (changed && std::isfinite(newTime))
	{
		std::vector<double> arrivals;
		for (const auto &key : keys) arrivals.push_back(key.first + (key.second.Selected ? newTime - time : 0));
		if (path.Retime(arrivals))
		{
			keys = path.CaptureState().Keys;
			first = keys.find(newTime);
			time = newTime;
		}
		else
			g_DocumentStatus = "Arrival conflicts with another key or hold release; edit rejected.";
	}
	const char *labels[] = {"X (units)",	   "Y (units)",		"Z (units)",	 "FOV (degrees)",
							"Pitch (degrees)", "Yaw (degrees)", "Roll (degrees)"};
	for (int ch = 0; ch < 7; ++ch)
	{
		ImGui::BeginDisabled(g_CurveLocked[ch]);
		double value = CamPath::GetChannelValue(first->second, (CamPath::Channel)ch), newValue = value;
		if (ImGui::InputDouble(labels[ch], &newValue, 0, 0, "%.6f", ImGuiInputTextFlags_EnterReturnsTrue) &&
			std::isfinite(newValue))
		{
			for (auto &key : keys)
				if (key.second.Selected)
					CamPath::SetChannelValue(key.second, (CamPath::Channel)ch,
											 CamPath::GetChannelValue(key.second, (CamPath::Channel)ch) +
												 newValue - value);
			if (!path.ReplaceKeys(keys))
				g_DocumentStatus = "Pose edit conflicts with a neighbouring hold.";
		}
		ImGui::EndDisabled();
	}

	ImGui::BeginDisabled(g_LockKeyTimes);
	double duration = first->second.HoldDuration;
	bool setHold =
		ImGui::InputDouble("Hold (seconds)", &duration, 0, 0, "%.6f", ImGuiInputTextFlags_EnterReturnsTrue);
	if (ImGui::Button("Capture hold release"))
	{
		duration = g_MirvTime.GetTime() - path.GetOffset() - time;
		setHold = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear hold"))
	{
		duration = 0;
		setHold = true;
	}
	if (setHold)
	{
		for (auto &key : keys)
			if (key.second.Selected)
				key.second.HoldDuration = duration;
		if (!path.ReplaceKeys(keys))
			g_DocumentStatus = "Hold must end after arrival and before the next different pose.";
	}
	ImGui::EndDisabled();
	if (ImGui::Button("Reset selected tangents to Auto"))
		for (int ch = 0; ch < 7; ++ch)
			if (!g_CurveLocked[ch] && g_CurveShow[ch] &&
				(g_CurveFocusChannel < 0 || g_CurveFocusChannel == ch))
			{
				path.SetTangentMode((CamPath::Channel)ch, true, true, CamPath::TM_AUTO);
				path.SetTangentWeight((CamPath::Channel)ch, true, true, 1, 1);
			}
	DrawOverlayHint("Graph: right-click a channel checkbox to focus; drag keys; Shift locks an axis; Alt "
						"snaps time to ticks. Handles: Shift changes weight; Alt breaks alignment.");
}
static void DrawSpeedProfile(CamPath &path)
{
	auto &document = g_Hook_VClient_RenderView.m_CamPaths;
	const auto owner = document.GetId(document.GetActive());
	if (g_SpeedDrag.Index >= 0 &&
		(g_SpeedDrag.PathId != owner || g_SpeedDrag.Revision != path.GetRevision() || g_LockKeyTimes ||
		 g_CancelGesture || IsMouseLook()))
	{
		ResetSpeedDrag();
		// An external edit must not be undone by Escape using the old drag snapshot.
		g_GestureStart.reset();
	}
	ImGui::BeginDisabled(g_LockKeyTimes);
	if (ImGui::Button("Constant speed"))
	{
		ResetSpeedDrag();
		if (!path.ApplySpeedProfile({}, path.GetDuration()))
			g_DocumentStatus = "This path cannot be retimed; existing data retained.";
	}
	ImGui::SameLine();
	if (ImGui::Button("Ease start / end"))
	{
		ResetSpeedDrag();
		if (!path.ApplySpeedProfile({{0, 0}, {0.25, 1}, {0.75, 1}, {1, 0}}, path.GetDuration()))
			g_DocumentStatus = "This path cannot be retimed; existing data retained.";
	}
	auto profile = path.GetSpeedProfile();
	if (profile.empty())
		profile = {{0, 1}, {1, 1}};
	const ImVec2 origin = ImGui::GetCursorScreenPos(),
				 size((std::max)(32.0f, ImGui::GetContentRegionAvail().x), 110);
	ImGui::InvisibleButton("##speed_profile", size);
	auto *draw = ImGui::GetWindowDrawList();
	auto &io = ImGui::GetIO();
	float maximum = 1;
	for (const auto &key : profile)
		maximum = (std::max)(maximum, (float)key.Speed * 1.15f);
	if (g_SpeedDrag.Index >= 0)
		maximum = g_SpeedDrag.Maximum;
	auto point = [&](const CamPath::SpeedKey &key)
	{
		return ImVec2(origin.x + 8 + (size.x - 16) * (float)key.Time,
					  origin.y + size.y - 8 - (size.y - 16) * (float)key.Speed / maximum);
	};
	draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
	draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(18, 22, 26, 255));
	const auto keyProgress = path.GetSpeedKeyProgress();
	size_t keyIndex = 0;
	float lastLabelRight = -FLT_MAX;
	int lastPixel = -1;
	for (auto key = path.GetBegin(); key != path.GetEnd(); ++key, ++keyIndex)
	{
		const float x = point({keyProgress[keyIndex], 0}).x;
		if ((int)x != lastPixel || key.GetValue().Selected)
			draw->AddLine(ImVec2(x, origin.y + 2), ImVec2(x, origin.y + size.y - 2),
						  key.GetValue().Selected ? IM_COL32(255, 210, 90, 180)
												  : IM_COL32(160, 180, 200, 100));
		lastPixel = (int)x;
		const auto label = std::to_string(keyIndex);
		const float width = ImGui::CalcTextSize(label.c_str()).x;
		const float left = std::clamp(x - width * .5f, origin.x + 2,
									  (std::max)(origin.x + 2, origin.x + size.x - width - 2));
		if (left > lastLabelRight + 4)
		{
			draw->AddText(ImVec2(left, origin.y + 2), IM_COL32(210, 220, 235, 255), label.c_str());
			lastLabelRight = left + width;
		}
		if (ImGui::IsItemHovered() && std::abs(io.MousePos.x - x) < 5)
		{
			int tick = 0;
			if (GetDemoTickFromClientTime(g_MirvTime.GetTime(), key.GetTime() + path.GetOffset(), tick))
				ImGui::SetTooltip("Key %u | tick %d | path %.3f s | moving time %.2f%%", (unsigned)keyIndex,
								  tick, key.GetTime(), keyProgress[keyIndex] * 100);
			else
				ImGui::SetTooltip("Key %u | path %.3f s | moving time %.2f%%", (unsigned)keyIndex,
								  key.GetTime(), keyProgress[keyIndex] * 100);
		}
	}
	ImVec2 last = point({0, path.GetProfileSpeed(0)});
	for (int i = 1; i <= 128; ++i)
	{
		ImVec2 next = point({i / 128.0, path.GetProfileSpeed(i / 128.0)});
		draw->AddLine(last, next, IM_COL32(90, 210, 240, 255), 2);
		last = next;
	}
	bool hitPoint = false;
	for (size_t i = 0; i < profile.size(); ++i)
	{
		const auto position = point(profile[i]);
		draw->AddCircleFilled(position, 5, IM_COL32(255, 210, 90, 255));
		if (ImGui::IsItemHovered() && std::abs(io.MousePos.x - position.x) < 8 &&
			std::abs(io.MousePos.y - position.y) < 8)
		{
			hitPoint = true;
			if (ImGui::IsMouseClicked(0) && !g_LockKeyTimes)
			{
				EnsureEditGesture();
				g_SpeedDrag.Index = (int)i;
				g_SpeedDrag.PathId = owner;
				g_SpeedDrag.Revision = path.GetRevision();
				g_SpeedDrag.Keys = profile;
				g_SpeedDrag.Duration = path.GetDuration();
				g_SpeedDrag.Maximum = maximum;
			}
			if (ImGui::IsMouseClicked(1) && !g_LockKeyTimes && i > 0 && i + 1 < profile.size())
			{
				ResetSpeedDrag();
				profile.erase(profile.begin() + i);
				if (!path.ApplySpeedProfile(profile, path.GetDuration()))
					g_DocumentStatus = "Speed edit rejected; existing data retained.";
				break;
			}
		}
	}
	draw->PopClipRect();
	if (g_SpeedDrag.Index >= 0 && ImGui::IsMouseDragging(0) && !g_LockKeyTimes && !g_CancelGesture)
	{
		auto candidate = g_SpeedDrag.Keys;
		auto &key = candidate[g_SpeedDrag.Index];
		const float mouseY = std::clamp(io.MousePos.y, origin.y + 8, origin.y + size.y - 8);
		key.Speed = std::clamp(double(origin.y + size.y - 8 - mouseY) / (size.y - 16) * g_SpeedDrag.Maximum,
							   0.0, 100.0);
		const size_t index = (size_t)g_SpeedDrag.Index;
		if (index > 0 && index + 1 < candidate.size())
		{
			const double margin =
				(std::min)(0.001, (candidate[index + 1].Time - candidate[index - 1].Time) * 0.25);
			key.Time = std::clamp(double(io.MousePos.x - origin.x - 8) / (size.x - 16),
								  candidate[index - 1].Time + margin, candidate[index + 1].Time - margin);
		}
		if (candidate != path.GetSpeedProfile())
		{
			if (!path.ApplySpeedProfile(candidate, g_SpeedDrag.Duration))
				g_DocumentStatus = "Speed profile needs nonzero area and a retimable path.";
			g_SpeedDrag.Revision = path.GetRevision();
		}
	}
	if (!ImGui::IsMouseDown(0))
		ResetSpeedDrag();
	if (!hitPoint && !g_LockKeyTimes && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) &&
		profile.size() < 128)
	{
		const float y = std::clamp(io.MousePos.y, origin.y + 8, origin.y + size.y - 8);
		CamPath::SpeedKey key{
			std::clamp(double(io.MousePos.x - origin.x - 8) / (size.x - 16), 0.001, 0.999),
			std::clamp(double(origin.y + size.y - 8 - y) / (size.y - 16) * maximum, 0.0, 100.0)};
		auto at = std::lower_bound(profile.begin(), profile.end(), key.Time,
								   [](const CamPath::SpeedKey &a, double t) { return a.Time < t; });
		if (at != profile.end() && std::abs(at->Time - key.Time) > 0.001 &&
			(at == profile.begin() || key.Time - std::prev(at)->Time > 0.001))
		{
			profile.insert(at, key);
			if (!path.ApplySpeedProfile(profile, path.GetDuration()))
				g_DocumentStatus = "Speed edit rejected; existing data retained.";
		}
	}
	ImGui::EndDisabled();
	ImGui::TextWrapped("Drag speed points; double-click to add; right-click an interior point to remove. X "
					   "is moving time (0-100%%), Y is relative speed. Vertical lines mark camera keys; "
					   "hover for index/tick. Retiming moves arrivals and hold "
					   "releases, retaining hold durations. Camera keys do not restart this ramp.");
	if (g_LockKeyTimes)
		ImGui::TextDisabled("Unlock key times to edit the speed profile or retime this path.");
}
static bool DrawModifierEnvelope(CamPath::NoiseModifier &modifier, std::uint64_t sharedId = 0)
{
	if (!ImGui::TreeNode("Strength envelope"))
		return false;
	static int dragged = -1, ownerModifier = -1;
	static unsigned long long owner = 0;
 static bool ownerShared = false;
	const auto pathId = sharedId ? sharedId :
		g_Hook_VClient_RenderView.m_CamPaths.GetId(g_Hook_VClient_RenderView.m_CamPaths.GetActive());
 const bool canceled = sharedId ? ImGui::IsKeyPressed(ImGuiKey_Escape) : g_CancelGesture;
	if (ownerShared != bool(sharedId) || owner != pathId || canceled ||
  (!sharedId && (!g_GestureStart || ownerModifier != g_ModifierSelection)))
		dragged = -1;
	auto keys = modifier.Envelope;
	if (keys.empty())
		keys = {{0, 1}, {1, 1}};
	bool changed = false, reset = false;
	if (ImGui::Button("Reset envelope"))
	{
		modifier.Envelope.clear();
		keys = {{0, 1}, {1, 1}};
		dragged = -1;
		changed = true;
		reset = true;
	}
	const auto origin = ImGui::GetCursorScreenPos();
	const ImVec2 size((std::max)(32.0f, ImGui::GetContentRegionAvail().x), 90);
	ImGui::InvisibleButton("##modifier_envelope", size);
	auto &io = ImGui::GetIO();
	auto *draw = ImGui::GetWindowDrawList();
	auto point = [&](const CamPath::ModifierKey &key)
	{
		return ImVec2(origin.x + 8 + (size.x - 16) * (float)key.Time,
					  origin.y + size.y - 8 - (size.y - 16) * (float)key.Value);
	};
	draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
	draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(18, 22, 26, 255));
	for (size_t i = 1; i < keys.size(); ++i)
	{
		const auto a = point(keys[i - 1]), b = point(keys[i]);
		ImVec2 last = a;
		for (int sample = 1; sample <= 32; ++sample)
		{
			const double u = sample / 32.0, weight = u * u * u * (10 + u * (-15 + 6 * u));
			const ImVec2 next(a.x + (b.x - a.x) * (float)u, a.y + (b.y - a.y) * (float)weight);
			draw->AddLine(last, next, IM_COL32(160, 120, 240, 255), 2);
			last = next;
		}
	}
	bool hit = false;
	for (size_t i = 0; i < keys.size(); ++i)
	{
		const auto p = point(keys[i]);
		draw->AddCircleFilled(p, 4, IM_COL32(255, 210, 90, 255));
		if (ImGui::IsItemHovered() && std::abs(io.MousePos.x - p.x) < 8 && std::abs(io.MousePos.y - p.y) < 8)
		{
			hit = true;
			if (ImGui::IsMouseClicked(0))
			{
				if (!sharedId) EnsureEditGesture();
				dragged = (int)i;
				owner = pathId;
    ownerShared = bool(sharedId);
				ownerModifier = g_ModifierSelection;
			}
			if (ImGui::IsMouseClicked(1) && i && i + 1 < keys.size())
			{
				keys.erase(keys.begin() + i);
				changed = true;
				dragged = -1;
				break;
			}
		}
	}
	draw->PopClipRect();
	if (dragged >= 0 && ImGui::IsMouseDragging(0) && !canceled && (size_t)dragged < keys.size())
	{
		auto &key = keys[dragged];
		key.Value = std::clamp(double(origin.y + size.y - 8 - io.MousePos.y) / (size.y - 16), 0.0, 1.0);
		if (dragged && (size_t)dragged + 1 < keys.size())
		{
			const double margin = (std::min)(.001, (keys[dragged + 1].Time - keys[dragged - 1].Time) * .25);
			key.Time = std::clamp(double(io.MousePos.x - origin.x - 8) / (size.x - 16),
								  keys[dragged - 1].Time + margin, keys[dragged + 1].Time - margin);
		}
		changed = true;
	}
	if (!hit && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && keys.size() < 128)
	{
		CamPath::ModifierKey key{
			std::clamp(double(io.MousePos.x - origin.x - 8) / (size.x - 16), .001, .999),
			std::clamp(double(origin.y + size.y - 8 - io.MousePos.y) / (size.y - 16), 0.0, 1.0)};
		auto at = std::lower_bound(keys.begin(), keys.end(), key.Time,
								   [](const CamPath::ModifierKey &a, double t) { return a.Time < t; });
		if (at != keys.end() && at->Time - key.Time > .001 &&
			(at == keys.begin() || key.Time - std::prev(at)->Time > .001))
		{
			keys.insert(at, key);
			changed = true;
		}
	}
	if (!ImGui::IsMouseDown(0))
		dragged = -1;
	if (changed && !reset)
		modifier.Envelope = keys;
	ImGui::TextDisabled(
		"X: normalized range time | Y: strength. Drag, double-click to add, right-click to remove.");
	ImGui::TreePop();
	return changed;
}

// UI selection only: authored values continue through CamPath::SetModifiers validation/history.
struct ModifierFieldSelection
{
	unsigned long long PathId = 0, Revision = 0;
	int Index = -1, Type = -1, Edited = -1;
	bool Selected[23] = {}, Dragging = false;
	float StartY = 0;
	ImGuiID Input = 0;
};
static ModifierFieldSelection g_ModifierFields;
static double &ModifierValue(CamPath::Modifier &modifier, int index)
{
	double *fields[] = {&modifier.Strength,		&modifier.FadeIn,		&modifier.FadeOut,
						&modifier.Frequency,	&modifier.Roughness,	&modifier.Phase,
						&modifier.Window,		&modifier.Damping,		&modifier.Amplitude[0],
						&modifier.Amplitude[1], &modifier.Amplitude[2], &modifier.Amplitude[3],
						&modifier.Amplitude[4], &modifier.Amplitude[5], &modifier.Amplitude[6], &modifier.Power,
		&modifier.Lower, &modifier.Upper, &modifier.Softness, &modifier.Rate, &modifier.Scale, &modifier.Offset, &modifier.Pivot};
	return *fields[index];
}
static ImU32 ModifierColor(size_t index, bool enabled = true)
{
	const ImU32 colors[] = {IM_COL32(150, 130, 240, 255), IM_COL32(80, 190, 220, 255),
							IM_COL32(220, 155, 80, 255),  IM_COL32(100, 195, 130, 255),
							IM_COL32(220, 120, 170, 255), IM_COL32(180, 190, 90, 255)};
	return enabled ? colors[index % 6] : IM_COL32(135, 135, 135, 255);
}
static bool ModifierField(int index, const char *label, double *value)
{
	auto &selection = g_ModifierFields;
	auto &io = ImGui::GetIO();
	const ImVec2 rectMin = ImGui::GetCursorScreenPos();
	const ImVec2 rectMax(rectMin.x + ImGui::CalcItemWidth(), rectMin.y + ImGui::GetFrameHeight());
	if (io.KeyShift && ImGui::IsMouseClicked(0) && ImGui::IsMouseHoveringRect(rectMin, rectMax))
	{
		std::fill(std::begin(selection.Selected), std::end(selection.Selected), false);
		selection.Dragging = true;
		selection.StartY = io.MousePos.y + ImGui::GetScrollY();
		ImGui::ClearActiveID();
	}
	if (!io.KeyShift || !ImGui::IsMouseDown(0))
		selection.Dragging = false;
	if (selection.Dragging)
	{
		const float end = io.MousePos.y + ImGui::GetScrollY();
		const float top = (std::min)(selection.StartY, end), bottom = (std::max)(selection.StartY, end);
		selection.Selected[index] =
			rectMin.y + ImGui::GetScrollY() <= bottom && rectMax.y + ImGui::GetScrollY() >= top;
	}
	ImGui::BeginDisabled(selection.Dragging);
	// Commit on Enter, so typing an intermediate value never rebuilds a modifier stack.
	const bool changed = ImGui::InputDouble(label, value, 0, 0, "%.4f", ImGuiInputTextFlags_EnterReturnsTrue);
	if (ImGui::IsItemActivated() && !selection.Selected[index])
	{
		std::fill(std::begin(selection.Selected), std::end(selection.Selected), false);
		selection.Selected[index] = true;
	}
	if (ImGui::IsItemActive())
		selection.Input = ImGui::GetActiveID();
	ImGui::EndDisabled();
	if (selection.Selected[index])
		ImGui::GetWindowDrawList()->AddRect(rectMin, rectMax, IM_COL32(255, 225, 90, 255), 2, 0, 2);
	if (changed)
		selection.Edited = index;
	return changed;
}
static void ApplyModifierFields(CamPath::Modifier &modifier)
{
	const auto &selection = g_ModifierFields;
	if (selection.Edited < 0 || !selection.Selected[selection.Edited])
		return;
	const double value = ModifierValue(modifier, selection.Edited);
	for (int index = 0; index < 23; ++index)
		if (selection.Selected[index])
			ModifierValue(modifier, index) = value;
}
static void DrawModifierControls(CamPath &path)
{
	if (!ImGui::IsMouseDown(0) || !ImGui::GetIO().KeyShift)
		g_ModifierFields.Dragging = false;
	if (!ImGui::CollapsingHeader("Range modifiers"))
		return;
	ImGui::BeginChild("Modifier controls", ImVec2(0, 300), true);
	if (ImGui::Button(g_ModifierRangeTool ? "Finish range tool" : "Draw modifier range"))
		g_ModifierRangeTool = !g_ModifierRangeTool;
	ImGui::SameLine();
	if (ImGui::Button("Range from selected keys"))
	{
		bool first = true;
		for (auto it = path.GetBegin(); it != path.GetEnd(); ++it)
			if (it.GetValue().Selected)
			{
				if (first)
				{
					g_ModifierBegin = it.GetTime();
					first = false;
				}
				g_ModifierEnd = it.GetTime() + it.GetValue().HoldDuration;
			}
		if (g_ModifierEnd <= g_ModifierBegin && path.GetSize())
			g_ModifierEnd = path.GetUpperBound();
	}
	ImGui::InputDouble("Range begin (path s)", &g_ModifierBegin, 0, 0, "%.3f");
	ImGui::InputDouble("Range end (path s)", &g_ModifierEnd, 0, 0, "%.3f");
	int beginTick = 0, endTick = 0;
	if (GetDemoTickFromClientTime(g_MirvTime.GetTime(), g_ModifierBegin + path.GetOffset(), beginTick) &&
		GetDemoTickFromClientTime(g_MirvTime.GetTime(), g_ModifierEnd + path.GetOffset(), endTick))
	{
		const int oldBegin = beginTick, oldEnd = endTick;
		if (ImGui::InputInt("Range begin tick", &beginTick))
			g_ModifierBegin += (beginTick - double(oldBegin)) * TickInterval();
		if (ImGui::InputInt("Range end tick", &endTick))
			g_ModifierEnd += (endTick - double(oldEnd)) * TickInterval();
	}

	auto modifiers = path.GetModifiers();
	static int newType = 0;
	const char *types[] = {"Noise",          "Oscillation",        "Smooth / stabilize",
	                       "Spring",         "Expo timing",        "Bounce timing",
	                       "Elastic timing", "Value limits",       "Slew / rate limit",
	                       "Stepped",        "Repeat / ping-pong", "Value remap"};
	ImGui::Combo("New modifier type", &newType, types, (int)CamPath::ModifierType::Count);
	static int preset = 0;
	const char *presets[] = {"Handheld",
	                         "Impact shake",
	                         "Expo in",
	                         "Expo out",
	                         "Expo in-out",
	                         "Bounce out (Z)",
	                         "Elastic out (FOV)",
	                         "Stabilize angles",
	                         "FOV punch",
	                         "FOV soft bounds (35..110)",
	                         "Pitch soft bounds (-85..85)",
	                         "Pan rate (45 deg/s)",
	                         "Stepped camera (12 Hz)",
	                         "Loop position twice",
	                         "Ping-pong position",
	                         "Half roll",
	                         "Widen FOV (+10)"};
	const char *presetIds[] = {
	    "handheld",    "impact",      "expo-in",       "expo-out",      "expo-in-out",
	    "bounce-out",  "elastic-out", "stabilize",     "fov-punch",     "fov-safe",
	    "pitch-safe",  "slow-pan",    "stepped-12fps", "loop-position", "ping-pong-position",
	    "reduce-roll", "widen-fov"};
	ImGui::Combo("Modifier preset", &preset, presets, (int)(sizeof(presets) / sizeof(presets[0])));
	const bool presetHovered = ImGui::IsItemHovered();
	const char *controlDescriptions[] = {
	    "Softly bounds FOV without changing camera position or angles.",
	    "Softly bounds pitch; yaw and roll remain unchanged. This is not an Euler repair tool.",
	    "Limits yaw to 45 degrees/s in the full-strength region. Fades relax the cap; pitch and roll are "
		"untouched.",
	    "Samples the complete camera pose at 12 Hz. Recording FPS remains unchanged.",
	    "Repeats the selected position movement twice. Matching endpoint positions are needed to avoid a "
		"jump; angles/FOV are untouched.",
	    "Moves forward and backward once through the position curve. Angles and FOV keep their original "
		"timing.",
	    "Halves roll around zero, blending at the range edges. Pitch and yaw are unchanged.",
	    "Adds 10 degrees of FOV, blending at the range edges. Geometry is unchanged."};
	const char *effectDescriptions[] = {"Small irregular position and angle movement.",
										"Manually timed shake with a decaying envelope.",
										"Accelerates movement through the range.",
										"Decelerates movement through the range.",
										"Eases movement at both ends.",
										"Adds bounce timing to vertical movement.",
										"Adds elastic timing to FOV.",
										"Smooths authored angular movement.",
										"Adds an oscillating FOV accent."};
	const char *presetDescription =
		preset >= 9 ? controlDescriptions[preset - 9] : effectDescriptions[preset];
	ImGui::TextWrapped("%s", presetDescription);
	if (presetHovered || ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", presetDescription);
	if (ImGui::Button("Add preset to range"))
	{
		CamPath::Modifier m;
		if (CamPath::MakeModifierPreset(presetIds[preset], g_ModifierBegin, g_ModifierEnd, m))
		{
			modifiers.push_back(m);
			if (path.SetModifiers(modifiers))
				g_ModifierSelection = (int)modifiers.size() - 1;
			else
			{
				modifiers = path.GetModifiers();
				g_DocumentStatus = "Preset rejected: modifier stack limit reached.";
			}
		}
		else
			g_DocumentStatus = "Choose a finite range with begin before end.";
	}
	DrawOverlayHint("Presets add editable settings to the stack; existing modifiers are retained.");
	const std::string addLabel =
		std::string("Add ") + CamPath::ModifierTypeName((CamPath::ModifierType)newType) + " to range";
	if (ImGui::Button(addLabel.c_str()))
	{
		CamPath::NoiseModifier m;
		m.Type = (CamPath::ModifierType)newType;
		m.Begin = g_ModifierBegin;
		m.End = g_ModifierEnd;
		m.FadeIn = m.FadeOut = (std::max)(0.0, (m.End - m.Begin) * 0.15);
		if (m.Type == CamPath::ModifierType::Limits)
			m.FadeIn = m.FadeOut = 0;
		for (int ch = 0; ch < 7; ++ch)
			if (g_CurveShow[ch] && (g_CurveFocusChannel < 0 || g_CurveFocusChannel == ch))
				m.Amplitude[ch] = newType >= 7 ? 1 : newType >= 2 ? .75 : ch < 3 ? 3 : 1;
		modifiers.push_back(m);
		if (path.SetModifiers(modifiers))
			g_ModifierSelection = (int)modifiers.size() - 1;
		else
		{
			modifiers = path.GetModifiers();
			g_DocumentStatus =
				"Choose a finite range with begin before end (maximum 64 modifiers, eight active filters).";
		}
	}
	if (!modifiers.empty())
	{
		g_ModifierSelection = std::clamp(g_ModifierSelection, 0, (int)modifiers.size() - 1);
		ImGui::TextUnformatted("Modifier stack: top is applied first, bottom last (indices start at 0).");
		if (ImGui::BeginListBox(
				"##modifier_stack",
				ImVec2(-FLT_MIN,
					   ImGui::GetTextLineHeightWithSpacing() * (std::min)(4, (int)modifiers.size()) + 8)))
		{
			for (size_t i = 0; i < modifiers.size(); ++i)
			{
				const auto label = std::to_string(i) + ": " + CamPath::ModifierTypeName(modifiers[i].Type) +
								   (modifiers[i].Enabled ? "" : " (disabled)");
				ImGui::PushStyleColor(ImGuiCol_Text, ModifierColor(i, modifiers[i].Enabled));
				if (ImGui::Selectable(label.c_str(), (int)i == g_ModifierSelection))
					g_ModifierSelection = (int)i;
				ImGui::PopStyleColor();
			}
			ImGui::EndListBox();
		}
		auto &m = modifiers[g_ModifierSelection];
		int type = (int)m.Type;
		bool changed = ImGui::Combo("Modifier type", &type, types, (int)CamPath::ModifierType::Count);
		if (changed)
		{
			m.Type = (CamPath::ModifierType)type;
			if (CamPath::IsTimingModifier(m.Type))
				m.Frequency = (std::min)(m.Frequency, 16.0);
			if (type >= 2)
			{
				m.CameraSpace = false;
				for (auto &amplitude : m.Amplitude)
					amplitude = std::clamp(std::abs(amplitude), 0.0, 1.0);
			}
		}
		changed |= ImGui::Checkbox("Enabled##noise", &m.Enabled);
		ImGui::SameLine();
		ImGui::BeginDisabled(type >= 2);
		changed |= ImGui::Checkbox("Camera-space position", &m.CameraSpace);
		ImGui::EndDisabled();
		const auto pathId =
			g_Hook_VClient_RenderView.m_CamPaths.GetId(g_Hook_VClient_RenderView.m_CamPaths.GetActive());
		if (g_ModifierFields.PathId != pathId || g_ModifierFields.Index != g_ModifierSelection ||
			g_ModifierFields.Type != type)
		{
			if (g_ModifierFields.Input && ImGui::GetActiveID() == g_ModifierFields.Input)
				ImGui::ClearActiveID();
			g_ModifierFields = {};
			g_ModifierFields.PathId = pathId;
			g_ModifierFields.Index = g_ModifierSelection;
			g_ModifierFields.Type = type;
		}
		if (g_ModifierFields.Revision != path.GetRevision() && g_ModifierFields.Input &&
			ImGui::GetActiveID() == g_ModifierFields.Input)
			ImGui::ClearActiveID();
		g_ModifierFields.Revision = path.GetRevision();
		g_ModifierFields.Edited = -1;
		if (g_ModifierFields.Dragging && ImGui::IsMouseDown(0))
		{
			const auto *window = ImGui::GetCurrentWindow();
			const float direction = ImGui::GetIO().MousePos.y < window->InnerRect.Min.y + 12   ? -1.f
									: ImGui::GetIO().MousePos.y > window->InnerRect.Max.y - 12 ? 1.f
																							   : 0.f;
			ImGui::SetScrollY(ImGui::GetScrollY() + direction * ImGui::GetIO().DeltaTime * 240);
		}
		ImGui::TextDisabled(
			"Shift+drag across fields to select. Type into a selected field; Enter sets them all.");
		changed |= ModifierField(0, "Modifier strength", &m.Strength);
		changed |= ImGui::InputDouble("Begin##noise", &m.Begin, 0, 0, "%.4f");
		changed |= ImGui::InputDouble("End##noise", &m.End, 0, 0, "%.4f");
		changed |= ModifierField(1, "Fade in (s)", &m.FadeIn);
		changed |= ModifierField(2, "Fade out (s)", &m.FadeOut);
		if (type == 0 || type == 1 || type == 3 || type == 5 || type == 6 || type == 9 || type == 10)
			changed |= ModifierField(3,
			                         (type == 5 || type == 6 || type == 10) ? "Cycles across range"
			                         : type == 9                            ? "Sample rate (Hz)"
			                                                                : "Frequency (Hz)",
			                         &m.Frequency);
		if (CamPath::IsTimingModifier(m.Type))
		{
			int direction = (int)m.Direction;
			const char *directions[] = {"In", "Out", "In-out"};
			if (ImGui::Combo("Easing direction", &direction, directions, 3))
			{
				m.Direction = (CamPath::EaseDirection)direction;
				changed = true;
			}
			if (type == 4)
				changed |= ModifierField(15, "Expo power", &m.Power);
			if (type >= 5)
				changed |= ModifierField(7, "Easing damping", &m.Damping);
			ImGui::TextWrapped("Warps progress once across the range; does not restart at keys. Channel "
			                   "strengths are 0..1. Equal XYZ strengths keep the spatial path inside the "
			                   "range. Elastic overshoot extends its endpoint displacement. Timing effects "
			                   "need changing values; use noise or FOV punch on a stationary pose.");
		}
		if (type == 7)
		{
			changed |= ModifierField(16, "Lower bound", &m.Lower);
			changed |= ModifierField(17, "Upper bound", &m.Upper);
			changed |= ModifierField(18, "Soft limit width", &m.Softness);
		}
		if (type == 8)
		{
			changed |= ModifierField(19, "Maximum change per second", &m.Rate);
			ImGui::TextWrapped("Per-channel units/s or degrees/s; not total orientation angular speed. Full "
			                   "strength with no fade enforces the cap; blending relaxes it.");
		}
		if (type == 10)
			changed |= ImGui::Checkbox("Ping-pong", &m.PingPong);
		if (type == 11)
		{
			changed |= ModifierField(20, "Value scale", &m.Scale);
			changed |= ModifierField(21, "Value offset", &m.Offset);
			changed |= ModifierField(22, "Value pivot", &m.Pivot);
		}
		if (type >= 7)
			ImGui::TextWrapped("Use channel strengths to choose the affected values. Limits and rate caps "
			                   "are strict only at full strength, away from fades and range edges. Repeat "
			                   "needs matching endpoint poses for a seamless loop.");
		if (type == 0)
		{
			int octaves = (int)m.Octaves;
			if (ImGui::SliderInt("Noise octaves", &octaves, 1, 8))
			{
				m.Octaves = (unsigned)octaves;
				changed = true;
			}
			changed |= ModifierField(4, "Noise roughness", &m.Roughness);
		}
		if (type == 1)
			changed |= ModifierField(5, "Phase (cycles)", &m.Phase);
		if (type == 2)
			changed |= ModifierField(6, "Smoothing window (s)", &m.Window);
		if (type == 3)
			changed |= ModifierField(7, "Spring damping", &m.Damping);
		if (type == 0)
		{
			int seed = (int)m.Seed;
			if (ImGui::InputInt("Seed", &seed) && seed >= 0)
			{
				m.Seed = (unsigned)seed;
				changed = true;
			}
		}
		const char *names[] = {"X amplitude (units)", "Y amplitude (units)",   "Z amplitude (units)",
							   "FOV amplitude (deg)", "Pitch amplitude (deg)", "Yaw amplitude (deg)",
							   "Roll amplitude (deg)"};
		const char *filterNames[] = {"X strength",     "Y strength",   "Z strength",   "FOV strength",
									 "Pitch strength", "Yaw strength", "Roll strength"};
		for (int ch = 0; ch < 7; ++ch)
			changed |= ModifierField(8 + ch, type >= 2 ? filterNames[ch] : names[ch], &m.Amplitude[ch]);
		ApplyModifierFields(m);
		changed |= DrawModifierEnvelope(m);
		if (ImGui::Button("Remove modifier"))
		{
			modifiers.erase(modifiers.begin() + g_ModifierSelection);
			g_ModifierFields = {};
			changed = true;
		}
		else
		{
			ImGui::SameLine();
			if (ImGui::Button("Move up") && g_ModifierSelection > 0)
			{
				std::swap(modifiers[g_ModifierSelection], modifiers[g_ModifierSelection - 1]);
				--g_ModifierSelection;
				changed = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Move down") && g_ModifierSelection + 1 < (int)modifiers.size())
			{
				std::swap(modifiers[g_ModifierSelection], modifiers[g_ModifierSelection + 1]);
				++g_ModifierSelection;
				changed = true;
			}
		}
		if (changed && !path.SetModifiers(modifiers))
			g_DocumentStatus = "Modifier edit rejected: check range, fade lengths, frequency and amplitude.";
		g_ModifierFields.Revision = path.GetRevision();
	}
	ImGui::TextWrapped("Modifiers affect only their shaded range, including holds when covered. "
					   "Smooth/spring channel strengths use 0..1. The "
					   "dim graph is the authored curve; the bright graph includes modifiers. Seeds give "
					   "identical results when scrubbing or recording. Oscillation phase is in cycles; "
					   "spring damping 1 is critical damping.");
	ImGui::EndChild();
}

static void DrawSpeedControls()
{
	auto &path = g_Hook_VClient_RenderView.m_CamPath;
	if (!path.CanEval() || path.GetSize() < 2)
		return;
	if (ImGui::CollapsingHeader("Travel speed / distance timing"))
	{
		static float speed[192];
		static unsigned long long lastRevision = ~0ull;
		if (lastRevision != path.GetGeometryRevision())
		{
			lastRevision = path.GetGeometryRevision();
			for (int i = 0; i < 192; ++i)
				speed[i] = (float)path.GetSpeed(path.GetLowerBound() + path.GetDuration() * i / 191);
		}
		ImGui::PlotLines("Speed (units/s)", speed, 192, 0, nullptr, 0, FLT_MAX, ImVec2(0, 70));
		DrawSpeedProfile(path);
		bool enabled = path.GetDistanceTiming();
		if (ImGui::Checkbox("Distance timing", &enabled))
			path.SetDistanceTiming(enabled);
		bool poseTiming = path.GetDistanceTimingPose();
		if (ImGui::Checkbox("Apply distance timing to angles / FOV too", &poseTiming))
			path.SetDistanceTimingPose(poseTiming);
		ImGui::TextDisabled("Position only keeps angle / FOV easing on its own time axis.");
		static double duration = 10;
		ImGui::InputDouble("Total duration (seconds)", &duration, 0, 0, "%.3f");
		ImGui::BeginDisabled(g_LockKeyTimes);
		if (ImGui::Button("Retime by distance"))
			if (!path.RetimeByDistance(duration))
				g_DocumentStatus = "Retime requires Custom/Linear channels, nonzero travel and enough "
								   "duration for all holds.";
		ImGui::EndDisabled();
		ImGui::TextWrapped(
			"Retime distributes travel time by distance and keeps holds and stationary intervals. It "
			"preserves the spatial curve. Reset Auto or changing geometry may require retiming again.");
	}
}
static void DrawInterpolationSettings(CamPath &path)
{
	auto settings = path.GetAutoTangentSettings();
	int interp = path.PositionInterpMethod_get() == CamPath::DI_CUSTOM
					 ? (settings.Method == AutoTangentMethod::MinimumJerk ? 5 : (int)settings.Method)
				 : path.PositionInterpMethod_get() == CamPath::DI_LINEAR ? 4
																		 : 3;
	const char *modes[] = {"Spline (smooth start / stop)",
						   "Catmull-Rom (through keys)",
						   "Shape-preserving (Monotone)",
						   "Classic cubic (four keys)",
						   "Linear",
						   "Minimum jerk (global quintic)"};
	static bool resetTangents = false;
	ImGui::Checkbox("Recalculate Auto when changing profile", &resetTangents);
	if (ImGui::Combo("Interpolation", &interp, modes, 6))
	{
		if (interp < 3 || interp == 5)
		{
			settings.Method = interp == 5 ? AutoTangentMethod::MinimumJerk : (AutoTangentMethod)interp;
			path.SetAutoTangentSettings(settings);
		}
		path.PositionInterpMethod_set(interp < 3 || interp == 5 ? CamPath::DI_CUSTOM
									  : interp == 3				? CamPath::DI_DEFAULT
																: CamPath::DI_LINEAR);
		path.FovInterpMethod_set(interp < 3 || interp == 5 ? CamPath::DI_CUSTOM
								 : interp == 3			   ? CamPath::DI_DEFAULT
														   : CamPath::DI_LINEAR);
		if (resetTangents)
		{
			auto keys = path.CaptureState().Keys;
			for (auto &key : keys)
			{
				key.second.TxModeIn = key.second.TxModeOut = key.second.TyModeIn = key.second.TyModeOut =
					key.second.TzModeIn = key.second.TzModeOut = key.second.TfovModeIn =
						key.second.TfovModeOut = CamPath::TM_AUTO;
				key.second.AccelerationMask &= 0x70;
				key.second.TxWIn = key.second.TxWOut = key.second.TyWIn = key.second.TyWOut =
					key.second.TzWIn = key.second.TzWOut = key.second.TfovWIn = key.second.TfovWOut = 1;
			}
			if (path.RotationInterpMethod_get() == CamPath::QI_CUSTOM)
				for (auto &key : keys)
				{
					key.second.TRxModeIn = key.second.TRxModeOut = key.second.TRyModeIn =
						key.second.TRyModeOut = key.second.TRzModeIn = key.second.TRzModeOut =
							CamPath::TM_AUTO;
					key.second.TRxWIn = key.second.TRxWOut = key.second.TRyWIn = key.second.TRyWOut =
						key.second.TRzWIn = key.second.TRzWOut = 1;
					key.second.AccelerationMask = 0;
				}
			path.ReplaceKeys(keys);
		}
	}
	ImGui::TextDisabled("Position: %s | FOV: %s",
						CamPath::DoubleInterp_ToString(path.PositionInterpMethod_get()),
						CamPath::DoubleInterp_ToString(path.FovInterpMethod_get()));
	int rotation = path.RotationInterpMethod_get() == CamPath::QI_CUSTOM	? 0
				   : path.RotationInterpMethod_get() == CamPath::QI_SLINEAR ? 2
																			: 1;
	const char *rotations[] = {"Editable Euler (uses Auto profile)", "Classic quaternion spline",
							   "Quaternion linear"};
	if (ImGui::Combo("Rotation", &rotation, rotations, 3))
		path.RotationInterpMethod_set(rotation == 0	  ? CamPath::QI_CUSTOM
									  : rotation == 1 ? CamPath::QI_SCUBIC
													  : CamPath::QI_SLINEAR);
	ImGui::TextDisabled("Effective rotation: %s",
						CamPath::QuaternionInterp_ToString(path.RotationInterpMethod_get()));
	if (path.HasKeyHolds())
		ImGui::TextWrapped("Holds require editable endpoint derivatives: Classic channels use Custom while "
						   "holds exist. Linear stays linear.");
	if ((path.PositionInterpMethod_get() != CamPath::DI_CUSTOM &&
		 path.PositionInterpMethod_get() != CamPath::DI_LINEAR) ||
		(path.FovInterpMethod_get() != CamPath::DI_CUSTOM &&
		 path.FovInterpMethod_get() != CamPath::DI_LINEAR))
		if (ImGui::Button("Edit Classic position / FOV tangents"))
			if (!path.MakeScalarCurvesEditable())
				g_DocumentStatus = "Classic scalar conversion requires an evaluable path.";
	size_t manual = 0, total = 0;
	for (auto it = path.GetBegin(); it != path.GetEnd(); ++it)
	{
		const auto v = it.GetValue();
		total += 7;
		manual += bool(v.TxModeIn || v.TxModeOut) + bool(v.TyModeIn || v.TyModeOut) +
				  bool(v.TzModeIn || v.TzModeOut) + bool(v.TfovModeIn || v.TfovModeOut) +
				  bool(v.TRyModeIn || v.TRyModeOut) + bool(v.TRzModeIn || v.TRzModeOut) +
				  bool(v.TRxModeIn || v.TRxModeOut);
	}
	ImGui::Text("Automatic channels: %u | Manual channels: %u", (unsigned)(total - manual), (unsigned)manual);
	DrawOverlayHint("Authored / baked tangents survive profile changes unless reset to Auto.");
	ImGui::BeginDisabled(!manual);
	if (ImGui::Button("Reset whole path to Auto"))
	{
		auto before = path.CaptureState().Keys;
		path.SelectAll();
		for (int ch = 0; ch < 7; ++ch)
		{
			path.SetTangentMode((CamPath::Channel)ch, true, true, CamPath::TM_AUTO);
			path.SetTangentWeight((CamPath::Channel)ch, true, true, 1, 1);
		}
		auto keys = path.CaptureState().Keys;
		for (auto &key : keys)
			key.second.Selected = before.find(key.first)->second.Selected;
		path.ReplaceKeys(keys);
	}
	ImGui::EndDisabled();
	float tension = (float)(settings.Tension * 100);
	if (ImGui::SliderFloat("Tension", &tension, 0, 100, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
	{
		settings.Tension = tension / 100;
		path.SetAutoTangentSettings(settings);
	}
	if (settings.Method == AutoTangentMethod::MinimumJerk)
		ImGui::TextWrapped(
			"Minimum jerk shares velocity and acceleration through the whole channel. Linked slope edits "
			"remain smooth; broken or weighted handles use editable cubic on that channel.");
}

static bool ObservePlayer(int entity, int user)
{
 if (!g_VEngineClient || !g_VEngineClient->IsPlayingDemo()) return false;
 char cmd[192];
 if (g_VEngineClient->IsHLTV()) {
  MirvSpectate_ReleaseView();
  std::snprintf(cmd, sizeof(cmd), "spec_autodirector 0; spec_mode 4; spec_player \"#%d\"", user);
 } else std::snprintf(cmd, sizeof(cmd), "mirv_pov %d", entity);
 Afx_ExecClientCmd(cmd);
 return true;
}

static void DrawObserving()
{
	if (!ImGui::BeginTabItem("Observing"))
		return;
	ImGui::TextUnformatted("Click a player to spectate. Bone selection and camera attachment remain available below.");
	if (ImGui::Button("Detach"))
		Afx_ExecClientCmd("mirv_cam attach clear");
	ImGui::SameLine();
	if (ImGui::Button("Capture relative framing"))
		Afx_ExecClientCmd("mirv_cam capture; mirv_input end");
	static int bone = -1, selectedEntity = -1, selectedUser = 0;
	static std::string traceStatus;
	char selectedName[64] = {};
	int currentUser = 0;
	if (selectedUser &&
		(!Mirv_Tf2_GetPlayerInfo(selectedEntity, selectedName, sizeof(selectedName), &currentUser) ||
		 currentUser != selectedUser))
	{
		selectedEntity = -1;
		selectedUser = 0;
		bone = -1;
	}
	if (ImGui::Button("Trace entity"))
	{
		int index = -1;
		if (!Mirv_Tf2_TraceEntity(g_Hook_VClient_RenderView.LastCameraOrigin,
								g_Hook_VClient_RenderView.LastCameraAngles, index))
			traceStatus = "TF2 tracing interface is unavailable.";
		else if (index < 0)
			traceStatus = "No entity under the camera crosshair (world geometry blocks the trace).";
		else
		{
			selectedEntity = index;
			selectedUser = 0;
			int user = 0;
			if (Mirv_Tf2_GetPlayerInfo(index, selectedName, sizeof(selectedName), &user))
				selectedUser = user;
			bone = -1;
			traceStatus = "Selected entity " + std::to_string(index) + ".";
		}
	}
	ImGui::SameLine();
	DrawOverlayHint("Aim the camera first; trace selects the entity at the center of the view.");
	if (!traceStatus.empty())
		ImGui::TextWrapped("%s", traceStatus.c_str());
	static float offset[3] = {0, 0, 0};
	static ImGuiTextFilter filter;
	filter.Draw("Filter players");
	if (ImGui::BeginTable("tf2_players", 1,
						  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
						  ImVec2(500, 220)))
	{
		ImGui::TableSetupColumn("Player");
		ImGui::TableHeadersRow();
		for (int i = 1; i <= Mirv_Tf2_GetMaxPlayers(); ++i)
		{
			char name[64];
			int user = 0;
			if (!Mirv_Tf2_GetPlayerInfo(i, name, sizeof(name), &user) || !filter.PassFilter(name))
				continue;
			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			char label[100];
			sprintf_s(label, "%d: %s", i, name);
			if (ImGui::Selectable(label, selectedEntity == i))
			{
				if (selectedEntity != i || selectedUser != user)
					bone = -1;
				selectedEntity = i;
				selectedUser = user;
				traceStatus = ObservePlayer(i, user) ? "Spectating " + std::string(name) + "." : "Selected " + std::string(name) + ".";
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	if (selectedEntity > 0)
	{
		ImGui::Separator();
		const bool stv = g_VEngineClient && g_VEngineClient->IsPlayingDemo() && g_VEngineClient->IsHLTV();
  if (!stv && g_VEngineClient && g_VEngineClient->IsPlayingDemo()) {
   int view = MirvPovSpectate_ThirdPerson() ? 1 : 0;
   const char *views[] = {"First person", "Third person"};
   if (ImGui::Combo("POV view", &view, views, 2)) Afx_ExecClientCmd(view ? "mirv_pov mode third" : "mirv_pov mode first");
   if (ImGui::Button("Return to recorded POV")) Afx_ExecClientCmd("mirv_pov off");
   ImGui::TextWrapped("%s", MirvPovSpectate_Status());
  }
		ImGui::Separator();
		ImGui::Text("Selected entity: %d", selectedEntity);
		ImGui::InputFloat3("Forward / left / up", offset);
		MirvTf2BoneName_t names[MIRV_TF2_MAX_BONES] = {};
		const int count = Mirv_Tf2_GetBoneNames(selectedEntity, names);
		if (bone >= count)
			bone = -1;
		char preview[100];
		if (bone < 0)
			sprintf_s(preview, "Entity origin");
		else
			sprintf_s(preview, "%d: %s", bone, names[bone].name);
		if (ImGui::BeginCombo("Bone", preview))
		{
			if (ImGui::Selectable("Entity origin", bone < 0))
				bone = -1;
			for (int i = 0; i < count; ++i)
			{
				char label[100];
				sprintf_s(label, "%d: %s", i, names[i].name[0] ? names[i].name : "(unnamed)");
				if (ImGui::Selectable(label, bone == i))
					bone = i;
			}
			ImGui::EndCombo();
		}
		if (!count)
			ImGui::TextDisabled("No model bones available for this entity.");
		const char *attachModes[] = {"bone", "entity", "aligned", "world"};
		int attachMode = std::clamp(MirvCam_GetAttachMode(), 0, 3);
		if (ImGui::BeginCombo("Attach orientation", attachModes[attachMode]))
		{
			for (int i = 0; i < 4; ++i)
				if (ImGui::Selectable(attachModes[i], attachMode == i))
				{
					char command[80];
					std::snprintf(command, sizeof(command), "mirv_cam attachMode %s", attachModes[i]);
					Afx_ExecClientCmd(command);
				}
			ImGui::EndCombo();
		}
		DrawOverlayHint("World keeps the camera heading and offset direction. Mode applies on attachment.");
		if (ImGui::Button("Attach selected"))
		{
			char cmd[256];
			sprintf_s(cmd, "mirv_input end; mirv_cam attach %d %d %.6f %.6f %.6f", selectedEntity, bone,
					  offset[0], offset[1], offset[2]);
			Afx_ExecClientCmd(cmd);
		}
	}
	ImGui::EndTabItem();
}
void Initialize()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	auto &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	g_FontDefault = io.Fonts->AddFontDefault();
	io.FontDefault = g_FontDefault;
	char windows[MAX_PATH];
	if (GetWindowsDirectoryA(windows, MAX_PATH))
	{
		auto load = [&](const char *file) -> ImFont * {
			std::string path = std::string(windows) + "/Fonts/" + file;
			std::error_code error;
			return std::filesystem::exists(path, error) ? io.Fonts->AddFontFromFileTTF(path.c_str(), 16)
														: nullptr;
		};
		g_FontMono = load("consola.ttf");
		g_FontSans = load("segoeui.ttf");
		g_FontSilly = load("comic.ttf");
	}
	ImGuiSettingsHandler ini;
	ini.TypeName = "HLAEOverlayPaths";
	ini.TypeHash = ImHashStr(ini.TypeName);
	ini.ReadOpenFn = OverlayPaths_ReadOpen;
	ini.ReadLineFn = OverlayPaths_ReadLine;
	ini.WriteAllFn = OverlayPaths_WriteAll;
	ImGui::AddSettingsHandler(&ini);
	if (io.IniFilename)
		ImGui::LoadIniSettingsFromDisk(io.IniFilename);
	g_OverlayPreferenceStamp = OverlayPreferenceStamp();
	g_OverlayPreferencesReady = true;
	io.FontGlobalScale = g_UiScale;
	ImFont *choices[] = {g_FontDefault, g_FontMono, g_FontSans, g_FontSilly};
	io.FontDefault = choices[g_FontChoice] ? choices[g_FontChoice] : g_FontDefault;
}
static inline double S1_ClientTime()
{
	return g_MirvTime.GetTime();
}
static void DrawCurveEditor()
{
	if (!g_ShowCurveEditor)
		return;
	const auto size = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2((std::max)(20.0f, size.x - 1040), 50), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2((std::min)(1000.0f, size.x - 40), (std::min)(650.0f, size.y - 80)),
							 ImGuiCond_FirstUseEver);
	if (g_FocusGraph)
	{
		ImGui::SetNextWindowFocus();
		g_FocusGraph = false;
	}
	if (ImGui::Begin("Campath Graph", &g_ShowCurveEditor))
	{

		ImGui::Separator();
		ImGui::SeparatorText("Curve Editor");
		DrawInterpolationSettings(g_Hook_VClient_RenderView.m_CamPath);
		DrawSpeedControls();
		DrawModifierControls(g_Hook_VClient_RenderView.m_CamPath);
		if (ImGui::CollapsingHeader("Selected key inspector"))
		{
			ImGui::BeginChild("Key values", ImVec2(0, 180), true);
			DrawKeyInspector();
			ImGui::EndChild();
		}
		// Removed channel selector; editing is driven by visibility checkboxes and optional focus
		ImGui::Checkbox("Normalized", &g_CurveNormalize);
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		// Checkboxes with right-click focus toggle
		ImGui::SameLine();
		ImGui::Checkbox("X##show", &g_CurveShow[0]);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			g_CurveFocusChannel = (g_CurveFocusChannel == 0 ? -1 : 0);
			if (g_CurveFocusChannel == 0)
				g_CurveShow[0] = true;
		}
		if (!g_CurveShow[0] && g_CurveFocusChannel == 0)
			g_CurveFocusChannel = -1;
		ImGui::SameLine();
		ImGui::Checkbox("Y##show", &g_CurveShow[1]);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			g_CurveFocusChannel = (g_CurveFocusChannel == 1 ? -1 : 1);
			if (g_CurveFocusChannel == 1)
				g_CurveShow[1] = true;
		}
		if (!g_CurveShow[1] && g_CurveFocusChannel == 1)
			g_CurveFocusChannel = -1;
		ImGui::SameLine();
		ImGui::Checkbox("Z##show", &g_CurveShow[2]);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			g_CurveFocusChannel = (g_CurveFocusChannel == 2 ? -1 : 2);
			if (g_CurveFocusChannel == 2)
				g_CurveShow[2] = true;
		}
		if (!g_CurveShow[2] && g_CurveFocusChannel == 2)
			g_CurveFocusChannel = -1;
		ImGui::SameLine();
		ImGui::Checkbox("FOV##show", &g_CurveShow[3]);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			g_CurveFocusChannel = (g_CurveFocusChannel == 3 ? -1 : 3);
			if (g_CurveFocusChannel == 3)
				g_CurveShow[3] = true;
		}
		if (!g_CurveShow[3] && g_CurveFocusChannel == 3)
			g_CurveFocusChannel = -1;
		ImGui::SameLine();
		ImGui::Checkbox("Pitch##show", &g_CurveShow[4]);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			g_CurveFocusChannel = (g_CurveFocusChannel == 4 ? -1 : 4);
			if (g_CurveFocusChannel == 4)
				g_CurveShow[4] = true;
		}
		if (!g_CurveShow[4] && g_CurveFocusChannel == 4)
			g_CurveFocusChannel = -1;
		ImGui::SameLine();
		ImGui::Checkbox("Yaw##show", &g_CurveShow[5]);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			g_CurveFocusChannel = (g_CurveFocusChannel == 5 ? -1 : 5);
			if (g_CurveFocusChannel == 5)
				g_CurveShow[5] = true;
		}
		if (!g_CurveShow[5] && g_CurveFocusChannel == 5)
			g_CurveFocusChannel = -1;
		ImGui::SameLine();
		ImGui::Checkbox("Roll##show", &g_CurveShow[6]);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			g_CurveFocusChannel = (g_CurveFocusChannel == 6 ? -1 : 6);
			if (g_CurveFocusChannel == 6)
				g_CurveShow[6] = true;
		}
		if (!g_CurveShow[6] && g_CurveFocusChannel == 6)
			g_CurveFocusChannel = -1;
		if (ImGui::Button("Fit graph"))
		{
			g_CurveTimeZoom = 1.0;
			g_CurveTimeCenter = 0.5;
			g_CurveValueScale = 1.0f;
			g_CurveValueOffset = 0.0f;
		}
		ImGui::SameLine();
		if (ImGui::Button("Frame selected"))
		{
			auto &path = g_Hook_VClient_RenderView.m_CamPath;
			double a = 0, b = 0;
			bool found = false;
			for (auto key = path.GetBegin(); key != path.GetEnd(); ++key)
				if (key.GetValue().Selected)
				{
					if (!found)
					{
						a = key.GetTime();
						found = true;
					}
					b = key.GetTime() + key.GetValue().HoldDuration;
				}
			if (found && path.GetDuration() > 0)
			{
				const double span = (std::max)(path.GetDuration() * 0.02, b - a) * 1.3;
				g_CurveTimeZoom = std::clamp(path.GetDuration() / span, 1.0, 10000.0);
				g_CurveTimeCenter = ((a + b) * 0.5 - path.GetLowerBound()) / path.GetDuration();
				g_CurveValueScale = 1;
				g_CurveValueOffset = 0;
				g_FrameSelectedValues = true;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Delete selected keys"))
			g_Hook_VClient_RenderView.m_CamPath.DeleteSelected();
		DrawOverlayHint("Wheel: zoom time | Ctrl+wheel: zoom values | MMB: pan | Shift+handle: selected "
							"handles | Esc: cancel drag");
		DrawOverlayHint("Time axis: demo tick / path seconds. Circles: Auto. Squares: Manual / baked.");

		SyncGraphSelection(g_Hook_VClient_RenderView.m_CamPath);
		// Keep cache fresh (standalone)
		if (!g_CurveCache.valid || g_CurveCache.times.size() != g_Hook_VClient_RenderView.m_CamPath.GetSize())
			CurveCache_Rebuild();

		if (!g_CurveCache.times.empty() && g_Hook_VClient_RenderView.m_CamPath.CanEval() &&
			g_Hook_VClient_RenderView.m_CamPath.GetDuration() > 0)
		{
			ImVec2 canvasSize((std::max)(80.0f, ImGui::GetContentRegionAvail().x),
							  (std::max)(180.0f, ImGui::GetContentRegionAvail().y -
													 2 * ImGui::GetFrameHeightWithSpacing()));
			ImGui::BeginChild("##campath_curve2", canvasSize, true,
							  ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
			ImDrawList *dl = ImGui::GetWindowDrawList();
			ImVec2 p0 = ImGui::GetCursorScreenPos();
			// Use the child's content area, not its outer size (which includes padding).
			canvasSize = ImGui::GetContentRegionAvail();
			ImVec2 p1 = ImVec2(p0.x + canvasSize.x, p0.y + canvasSize.y);
			auto &io = ImGui::GetIO();
			const float plotWidth = (std::max)(1.0f, canvasSize.x - 2 * g_CurvePadding);
			const float plotHeight = (std::max)(1.0f, canvasSize.y - 2 * g_CurvePadding);
			const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(p0, p1);
			if (hovered && !ImGui::IsAnyItemActive() && io.MouseWheel != 0.0f)
			{
				ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetID("##graph_navigation"));
				if (io.KeyCtrl)
				{
					const float y = (p1.y - g_CurvePadding - io.MousePos.y) / plotHeight - 0.5f;
					const float oldScale = g_CurveValueScale;
					g_CurveValueScale = std::clamp(oldScale * std::pow(1.2f, io.MouseWheel), 0.02f, 100.0f);
					g_CurveValueOffset += y * (1.0f / oldScale - 1.0f / g_CurveValueScale);
				}
				else
				{
					const double x = (io.MousePos.x - p0.x - g_CurvePadding) / plotWidth - 0.5;
					const double oldZoom = g_CurveTimeZoom;
					g_CurveTimeZoom = std::clamp(oldZoom * std::pow(1.2, io.MouseWheel), 1.0, 10000.0);
					g_CurveTimeCenter += x * (1.0 / oldZoom - 1.0 / g_CurveTimeZoom);
				}
			}
			static bool panning = false;
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
				panning = true;
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
				panning = false;
			if (panning)
			{
				g_CurveTimeCenter -= io.MouseDelta.x / (plotWidth * g_CurveTimeZoom);
				g_CurveValueOffset += io.MouseDelta.y / (plotHeight * g_CurveValueScale);
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
			}
			g_CurveTimeCenter =
				std::clamp(g_CurveTimeCenter, 0.5 / g_CurveTimeZoom, 1.0 - 0.5 / g_CurveTimeZoom);
			dl->PushClipRect(p0, p1, true);

			// Background + border
			dl->AddRectFilled(p0, p1, IM_COL32(18, 22, 26, 255));
			dl->AddRect(p0, p1, IM_COL32(60, 70, 80, 255));

			// Screen-space helpers
			auto toX = [&](double t)
			{
				double firstTime = (g_CurveKeyDrag || !g_TangentDrag.Handles.empty())
									   ? g_DragFirst
									   : g_CurveCache.times.front();
				double duration = ((g_CurveKeyDrag || !g_TangentDrag.Handles.empty())
									   ? g_DragLast
									   : g_Hook_VClient_RenderView.m_CamPath.GetUpperBound()) -
								  firstTime;
				double tMin = firstTime + duration * (g_CurveTimeCenter - 0.5 / g_CurveTimeZoom);
				double tMax = tMin + duration / g_CurveTimeZoom;
				double u = (t - tMin) / (tMax - tMin + 1e-12);
				return p0.x + (float)u * (canvasSize.x - 2 * g_CurvePadding) + g_CurvePadding;
			};
			auto fromX = [&](float px) -> double
			{
				double firstTime = (g_CurveKeyDrag || !g_TangentDrag.Handles.empty())
									   ? g_DragFirst
									   : g_CurveCache.times.front();
				double duration = ((g_CurveKeyDrag || !g_TangentDrag.Handles.empty())
									   ? g_DragLast
									   : g_Hook_VClient_RenderView.m_CamPath.GetUpperBound()) -
								  firstTime;
				double tMin = firstTime + duration * (g_CurveTimeCenter - 0.5 / g_CurveTimeZoom);
				double tMax = tMin + duration / g_CurveTimeZoom;
				double u = (px - (p0.x + g_CurvePadding)) / (canvasSize.x - 2 * g_CurvePadding);
				if (u < 0.0)
					u = 0.0;
				if (u > 1.0)
					u = 1.0;
				return tMin + u * (tMax - tMin + 1e-12);
			};

			static bool rangeDragging = false;
			static double rangeStart = 0;
			if (g_ModifierRangeTool && hovered && ImGui::IsMouseClicked(0))
			{
				rangeDragging = true;
				rangeStart = fromX(io.MousePos.x);
			}
			if (rangeDragging && !g_CancelGesture)
			{
				const double end = fromX(io.MousePos.x);
				g_ModifierBegin = (std::min)(rangeStart, end);
				g_ModifierEnd = (std::max)(rangeStart, end);
			}
			if (!ImGui::IsMouseDown(0) || g_CancelGesture)
				rangeDragging = false;
			if (g_ModifierRangeTool)
			{
				dl->AddRectFilled(ImVec2(toX(g_ModifierBegin), p0.y), ImVec2(toX(g_ModifierEnd), p1.y),
								  IM_COL32(120, 150, 220, 35));
			}
			// Keep the range strip bounded. Selecting a stack row reveals its group of lanes.
			const int laneCount = std::clamp((int)(canvasSize.y / 4 / 15), 1, 4);
			const size_t firstModifier = (std::max)(0, g_ModifierSelection) / laneCount * laneCount;
			const auto &graphModifiers = g_Hook_VClient_RenderView.m_CamPath.GetModifiers();
			const size_t lastModifier = (std::min)(graphModifiers.size(), firstModifier + laneCount);
			for (size_t i = firstModifier; i < lastModifier; ++i)
			{
				// Only copy fields used by the range handles; no per-frame envelope allocations.
				const auto &source = graphModifiers[i];
				const double begin = source.Begin, end = source.End, fadeIn = source.FadeIn,
							 fadeOut = source.FadeOut;
				const auto color = ModifierColor(i, source.Enabled);
				const float x0 = toX(begin), x1 = toX(end);
				const float y = p0.y + 4 + (float)(i - firstModifier) * 15;
				auto clipX = [&](double time) { return std::clamp(toX(time), p0.x, p1.x); };
				if (x1 < p0.x || x0 > p1.x)
					continue;
				dl->AddRectFilled(ImVec2(clipX(begin), y), ImVec2(clipX(end), y + 12),
								  (color & 0x00ffffff) | 0x70000000);
				dl->AddLine(ImVec2(clipX(begin), y + 12), ImVec2(clipX(begin + fadeIn), y),
							IM_COL32(230, 210, 255, 220), 1);
				dl->AddLine(ImVec2(clipX(end - fadeOut), y), ImVec2(clipX(end), y + 12),
							IM_COL32(230, 210, 255, 220), 1);
				if (!g_ModifierRangeTool && hovered && ImGui::IsMouseClicked(0) &&
					ImGui::IsMouseHoveringRect(ImVec2(clipX(begin), y), ImVec2(clipX(end), y + 12)))
					g_ModifierSelection = (int)i;
				if ((int)i == g_ModifierSelection)
					dl->AddRect(ImVec2((std::max)(p0.x, x0), y), ImVec2((std::min)(p1.x, x1), y + 12),
								IM_COL32(255, 225, 90, 255));
				const auto caption = std::to_string(i) + ": " + CamPath::ModifierTypeName(source.Type);
				dl->AddText(ImVec2((std::max)(p0.x + 3, x0 + 5), y - 1), color, caption.c_str());
				if (g_ModifierRangeTool)
					continue;
				const double handles[] = {begin, end, begin + fadeIn, end - fadeOut};
				for (int edge = 0; edge < 4; ++edge)
				{
					const float x = toX(handles[edge]);
					const auto edgeId = "##modifier_edge_" + std::to_string(i) + "_" + std::to_string(edge);
					ImGui::PushID((int)i);
					ImGui::PushID(edge);
					const bool active = ImGui::GetActiveID() == ImGui::GetID(edgeId.c_str());
					if (!active && (x < p0.x - 4 || x > p1.x + 4))
					{
						ImGui::PopID();
						ImGui::PopID();
						continue;
					}
					dl->AddLine(ImVec2(x, y), ImVec2(x, y + 12), IM_COL32(230, 210, 255, 255), 2);
					ImGui::SetCursorScreenPos(ImVec2(std::clamp(x, p0.x, p1.x) - 4, y));
					ImGui::InvisibleButton(edgeId.c_str(), ImVec2(8, 12));
					if (ImGui::IsItemActivated())
						EnsureEditGesture();
					if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0) && !g_CancelGesture)
					{
						auto modifiers = g_Hook_VClient_RenderView.m_CamPath.GetModifiers();
						auto &edit = modifiers[i];
						const double t = fromX(io.MousePos.x);
						if (edge == 0)
							edit.Begin = (std::min)(t, edit.End - 1e-5);
						if (edge == 1)
							edit.End = (std::max)(t, edit.Begin + 1e-5);
						if (edge == 2)
							edit.FadeIn = std::clamp(t - edit.Begin, 0.0, edit.End - edit.Begin);
						if (edge == 3)
							edit.FadeOut = std::clamp(edit.End - t, 0.0, edit.End - edit.Begin);
						edit.FadeIn = (std::min)(edit.FadeIn, edit.End - edit.Begin);
						edit.FadeOut = (std::min)(edit.FadeOut, edit.End - edit.Begin);
						g_Hook_VClient_RenderView.m_CamPath.SetModifiers(modifiers);
						g_ModifierSelection = (int)i;
					}
					ImGui::PopID();
					ImGui::PopID();
				}
			}
			// Grid
			for (int i = 0; i <= 10; ++i)
			{
				float x = p0.x + i * (canvasSize.x - 2 * g_CurvePadding) / 10.0f + g_CurvePadding;
				dl->AddLine(ImVec2(x, p0.y + g_CurvePadding), ImVec2(x, p1.y - g_CurvePadding),
							IM_COL32(40, 48, 56, 255));
			}
			for (int i = 0; i <= 6; ++i)
			{
				float y = p0.y + i * (canvasSize.y - 2 * g_CurvePadding) / 6.0f + g_CurvePadding;
				dl->AddLine(ImVec2(p0.x + g_CurvePadding, y), ImVec2(p1.x - g_CurvePadding, y),
							IM_COL32(40, 48, 56, 255));
			}

			// Which channels to draw this frame
			bool drawMask[7] = {false, false, false, false, false, false, false};
			for (int c = 0; c < 7; ++c)
				drawMask[c] = g_CurveShow[c];

			// Determine anchor channel for non-normalized mapping when multiple channels are shown.
			int firstVisible = -1;
			for (int c = 0; c < 7; ++c)
			{
				if (g_CurveShow[c])
				{
					firstVisible = c;
					break;
				}
			}
			int anchorCh = (g_CurveFocusChannel >= 0 ? g_CurveFocusChannel : firstVisible);
			if (anchorCh < 0)
				anchorCh = 0; // fallback if none visible

			// Per-channel colors
			auto chColor = [&](int ch) -> ImU32
			{
				switch (ch)
				{
				case 0:
					return IM_COL32(236, 86, 86, 255); // X
				case 1:
					return IM_COL32(93, 201, 103, 255); // Y
				case 2:
					return IM_COL32(65, 156, 255, 255); // Z
				case 3:
					return IM_COL32(255, 200, 80, 255); // FOV
				case 4:
					return IM_COL32(200, 120, 255, 255); // Pitch
				case 5:
					return IM_COL32(120, 200, 255, 255); // Yaw
				case 6:
					return IM_COL32(255, 120, 200, 255); // Roll
				default:
					return IM_COL32(255, 255, 255, 255);
				}
			};
			auto chHandleColor = [&](int ch) -> ImU32
			{ return ImGui::GetColorU32(ImGuiCol_PlotLinesHovered); };

			auto &curvePath = g_Hook_VClient_RenderView.m_CamPath;
			if (!curvePath.GetModifiers().empty())
				g_CurveRangeSamples.Update(curvePath, curvePath.GetLowerBound(), curvePath.GetUpperBound(),
										   256);
			double vCenter[7] = {0, 0, 0, 0, 0, 0, 0}, vHalf[7] = {1, 1, 1, 1, 1, 1, 1};
			for (int ch = 0; ch < 7; ++ch)
			{
				if (!drawMask[ch])
					continue;
				bool first = true;
				double vMin = 0.0, vMax = 0.0;
				double prev = 0.0;
				bool havePrev = false;
				for (size_t i = 0; i < g_CurveCache.values.size(); ++i)
				{
					const CamPathValue &v = g_CurveCache.values[i];
					double val;
					if (ch <= 3)
					{
						val = (ch == 0 ? v.X : ch == 1 ? v.Y : ch == 2 ? v.Z : v.Fov);
					}
					else
					{
						QEulerAngles e = v.GetEulerAngles();
						val = (ch == 4 ? e.Pitch : ch == 5 ? e.Yaw : e.Roll);
						if (havePrev && !v.HasEuler)
						{
							val = UnwrapAngleNear(val, prev);
						}
						prev = val;
						havePrev = true;
					}

					if (first)
					{
						vMin = vMax = val;
						first = false;
					}
					else
					{
						vMin = (std::min)(vMin, val);
						vMax = (std::max)(vMax, val);
					}
				}
				if (!curvePath.GetModifiers().empty())
					for (const auto &sample : g_CurveRangeSamples.Values)
					{
						const double value = CamPath::GetChannelValue(sample, (CamPath::Channel)ch);
						vMin = (std::min)(vMin, value);
						vMax = (std::max)(vMax, value);
					}
				if (first)
				{
					vMin = 0;
					vMax = 1;
				}
				if (vMax - vMin < 1e-6)
					vMax = vMin + 1.0;
				vCenter[ch] = 0.5 * (vMin + vMax);
				vHalf[ch] = 0.5 * (vMax - vMin);
			}

			if (g_FrameSelectedValues)
			{
				double lo = 1e300, hi = -1e300;
				for (const auto &v : g_CurveCache.values)
					if (v.Selected)
						for (int ch = 0; ch < 7; ++ch)
							if (drawMask[ch] && (g_CurveFocusChannel < 0 || g_CurveFocusChannel == ch))
							{
								const int axis = g_CurveNormalize ? ch : anchorCh;
								const double n =
									(CamPath::GetChannelValue(v, (CamPath::Channel)ch) - vCenter[axis]) /
									vHalf[axis];
								lo = (std::min)(lo, n);
								hi = (std::max)(hi, n);
							}
				if (lo <= hi)
				{
					g_CurveValueOffset = (float)((lo + hi) / 4);
					g_CurveValueScale = (float)std::clamp(1.6 / (std::max)(0.04, hi - lo), 0.02, 100.0);
				}
				g_FrameSelectedValues = false;
			}

			if (g_CurveKeyDrag || !g_TangentDrag.Handles.empty())
				for (int ch = 0; ch < 7; ++ch)
				{
					vCenter[ch] = g_DragCenters[ch];
					vHalf[ch] = g_DragHalves[ch];
				}

			// Map value -> screen Y for a specific channel
			auto toY_ch = [&](int ch, double v) -> float
			{
				if (g_CurveNormalize)
				{
					double norm = (v - vCenter[ch]) / (vHalf[ch] > 1e-12 ? vHalf[ch] : 1e-12); // [-1..1]
					// Leave values unclamped; the canvas clips curves instead of flattening them.
					double yNorm = ((norm * 0.5 - g_CurveValueOffset) * (double)g_CurveValueScale + 0.5);
					return p1.y - (float)yNorm * (canvasSize.y - 2.0f * g_CurvePadding) - g_CurvePadding;
				}
				else
				{
					// Fall back to single-channel mapping using anchor channel's scale/center.
					// Anchor = focused channel if any, else first visible.
					int ach = anchorCh;
					double norm = (v - vCenter[ach]) / (vHalf[ach] > 1e-12 ? vHalf[ach] : 1e-12);
					double yNorm = ((norm * 0.5 - g_CurveValueOffset) * (double)g_CurveValueScale + 0.5);
					return p1.y - (float)yNorm * (canvasSize.y - 2.0f * g_CurvePadding) - g_CurvePadding;
				}
			};

			const char *channelNames[] = {"X", "Y", "Z", "FOV", "Pitch", "Yaw", "Roll"};
			auto fromYValue = [&](int ch, float y)
			{
				const int axis = g_CurveNormalize ? ch : anchorCh;
				const double n =
					((p1.y - g_CurvePadding - y) / plotHeight - 0.5) / g_CurveValueScale + g_CurveValueOffset;
				return vCenter[axis] + 2 * n * vHalf[axis];
			};
			for (int line = 0; line <= 4; ++line)
			{
				const float x = p0.x + g_CurvePadding + plotWidth * line / 4;
				const double time = fromX(x);
				int tick = 0;
				char label[96];
				if (GetDemoTickFromClientTime(g_MirvTime.GetTime(),
											  time + g_Hook_VClient_RenderView.m_CamPath.GetOffset(), tick))
					snprintf(label, sizeof(label), "%d / %.2fs", tick, time);
				else
					snprintf(label, sizeof(label), "%.3fs", time);
				dl->AddText(ImVec2(x - 20, p1.y - 13), IM_COL32(190, 200, 210, 255), label);
				const float y = p1.y - g_CurvePadding - plotHeight * line / 4;
				snprintf(label, sizeof(label), "%.2f", fromYValue(anchorCh, y));
				dl->AddText(ImVec2(p0.x + 2, y), IM_COL32(190, 200, 210, 255), label);
			}
			if (hovered)
			{
				dl->AddLine(ImVec2(io.MousePos.x, p0.y), ImVec2(io.MousePos.x, p1.y),
							IM_COL32(180, 190, 200, 100));
				const double time = fromX(io.MousePos.x);
				ImGui::BeginTooltip();
				ImGui::Text("Path %.6f s | %s axis: %.6f %s", time, channelNames[anchorCh],
							fromYValue(anchorCh, io.MousePos.y), anchorCh < 3 ? "units" : "degrees");
				if (g_Hook_VClient_RenderView.m_CamPath.CanEval())
				{
					auto value = g_Hook_VClient_RenderView.m_CamPath.Eval(time);
					for (int ch = 0; ch < 7; ++ch)
						if (drawMask[ch])
							ImGui::Text("%s curve: %.6f", channelNames[ch],
										CamPath::GetChannelValue(value, (CamPath::Channel)ch));
				}
				ImGui::EndTooltip();
			}

			const bool sampleAll =
				curvePath.GetDistanceTiming() || !curvePath.GetModifiers().empty() ||
				curvePath.GetAutoTangentSettings().Method == AutoTangentMethod::MinimumJerk;
			bool sampled[7];
			for (int ch = 0; ch < 7; ++ch)
				sampled[ch] =
					sampleAll || (ch < 3	? curvePath.PositionInterpMethod_get() != CamPath::DI_CUSTOM
								  : ch == 3 ? curvePath.FovInterpMethod_get() != CamPath::DI_CUSTOM
											: curvePath.RotationInterpMethod_get() != CamPath::QI_CUSTOM);
			bool needSamples = false;
			for (int ch = 0; ch < 7; ++ch)
				needSamples |= drawMask[ch] && sampled[ch];
			if (needSamples)
			{
				g_CurveViewSamples.Update(curvePath, fromX(p0.x + g_CurvePadding),
										  fromX(p1.x - g_CurvePadding),
										  std::clamp((int)plotWidth, 256, 2048));
				for (int ch = 0; ch < 7; ++ch)
					if (drawMask[ch] && sampled[ch])
					{
						double previous = 0, previousBase = 0;
						ImVec2 last, lastBase;
						for (size_t i = 0; i < g_CurveViewSamples.Times.size(); ++i)
						{
							const auto &pose = g_CurveViewSamples.Values[i];
							double value = CamPath::GetChannelValue(pose, (CamPath::Channel)ch);
							if (i && ch >= 4 && !pose.HasEuler)
								value = UnwrapAngleNear(value, previous);
							const ImVec2 point(toX(g_CurveViewSamples.Times[i]), toY_ch(ch, value));
							if (i)
								dl->AddLine(last, point, chColor(ch), 2);
							last = point;
							previous = value;
							if (!g_CurveViewSamples.Base.empty())
							{
								const auto &base = g_CurveViewSamples.Base[i];
								double b = CamPath::GetChannelValue(base, (CamPath::Channel)ch);
								if (i && ch >= 4 && !base.HasEuler)
									b = UnwrapAngleNear(b, previousBase);
								const ImVec2 pointBase(point.x, toY_ch(ch, b));
								if (i)
									dl->AddLine(lastBase, pointBase, (chColor(ch) & 0x00ffffff) | 0x50000000,
												1);
								lastBase = pointBase;
								previousBase = b;
							}
						}
					}
			}

			// Draw segments
			// Keep per-channel running continuity for Euler channels so adjacent segments align
			// visually
			auto unwrapNear = UnwrapAngleNear;
			bool prevValid[7] = {false, false, false, false, false, false, false};
			double prevYEnd[7] = {0, 0, 0, 0, 0, 0, 0};
			for (size_t i = 0; i + 1 < g_CurveCache.times.size(); ++i)
			{
				double t0 = g_CurveCache.times[i] + g_CurveCache.values[i].HoldDuration;
				double t1 = g_CurveCache.times[i + 1];
				const CamPathValue &cv0 = g_CurveCache.values[i];
				const CamPathValue &cv1 = g_CurveCache.values[i + 1];
				double h = (t1 - t0);
				if (h <= 0 || toX(t1) < p0.x || toX(g_CurveCache.times[i]) > p1.x)
					continue;

				// Per-channel draw
				for (int ch = 0; ch < 7; ++ch)
				{
					if (!drawMask[ch])
						continue;

					double y0, y1;
					if (ch <= 3)
					{
						y0 = (ch == 0 ? cv0.X : ch == 1 ? cv0.Y : ch == 2 ? cv0.Z : cv0.Fov);
						y1 = (ch == 0 ? cv1.X : ch == 1 ? cv1.Y : ch == 2 ? cv1.Z : cv1.Fov);
					}
					else
					{
						QEulerAngles e0 = cv0.GetEulerAngles();
						QEulerAngles e1 = cv1.GetEulerAngles();
						double y0raw = (ch == 4 ? e0.Pitch : ch == 5 ? e0.Yaw : e0.Roll);
						double y1raw = (ch == 4 ? e1.Pitch : ch == 5 ? e1.Yaw : e1.Roll);
						// Ensure continuity across segments relative to previous segment end
						if (prevValid[ch] && !cv0.HasEuler)
						{
							y0 = unwrapNear(y0raw, prevYEnd[ch]);
						}
						else
						{
							y0 = y0raw;
						}
						y1 = cv1.HasEuler ? y1raw : unwrapNear(y1raw, y0);
					}

					// Which interpolation is active for this channel?
					bool isCustom =
						(ch == 3)
							? (g_Hook_VClient_RenderView.m_CamPath.FovInterpMethod_get() ==
							   CamPath::DI_CUSTOM)
							: (ch <= 2 ? (g_Hook_VClient_RenderView.m_CamPath.PositionInterpMethod_get() ==
										  CamPath::DI_CUSTOM)
									   : (g_Hook_VClient_RenderView.m_CamPath.RotationInterpMethod_get() ==
										  CamPath::QI_CUSTOM));

					const bool showHandles =
						isCustom && !g_CurveLocked[ch] && !g_ModifierRangeTool &&
						!(curvePath.GetDistanceTiming() && (ch < 3 || curvePath.GetDistanceTimingPose())) &&
						(g_CurveFocusChannel < 0 || g_CurveFocusChannel == ch);
					if (sampled[ch] && !showHandles)
						continue;
					double mOut = 0, mIn = 0, wOut = 1, wIn = 1;
					if (isCustom)
					{
						auto &path = g_Hook_VClient_RenderView.m_CamPath;
						path.GetTangent((CamPath::Channel)ch, i, true, mOut, wOut);
						path.GetTangent((CamPath::Channel)ch, i + 1, false, mIn, wIn);
					}

					Afx::Math::ResolveHermiteWeights(wOut, wIn);
					// Control points for channel ch
					ImVec2 P0(toX(t0), toY_ch(ch, y0));
					ImVec2 P3(toX(t1), toY_ch(ch, y1));
					ImVec2 P1(toX(t0 + wOut * (h / 3.0)), toY_ch(ch, y0 + wOut * (h / 3.0) * mOut));
					ImVec2 P2(toX(t1 - wIn * (h / 3.0)), toY_ch(ch, y1 - wIn * (h / 3.0) * mIn));

					// Curve
					ImU32 c = chColor(ch);
					if (!sampled[ch])
						dl->AddBezierCubic(P0, P1, P2, P3, c, 2.0f);
					if (cv0.HoldDuration > 0)
						dl->AddLine(ImVec2(toX(g_CurveCache.times[i]), P0.y), P0, c, 3);

					// Remember continuity for next segment (Euler channels)
					if (ch >= 4)
					{
						prevValid[ch] = true;
						prevYEnd[ch] = y1;
					}

					// Handles + hit: show handles on all visible by default, or only focused channel
					// if focus is set
					if (showHandles)
					{
						float r = 4.0f;
						dl->AddCircleFilled(P0, r, IM_COL32(220, 220, 220, 255));
						dl->AddCircleFilled(P3, r, IM_COL32(220, 220, 220, 255));
						dl->AddLine(P0, P1, IM_COL32(180, 128, 64, 200));
						dl->AddLine(P3, P2, IM_COL32(180, 128, 64, 200));
						dl->AddCircleFilled(P1, r,
											GraphChannelSelected(g_CurveCache.times[i], ch)
												? IM_COL32(255, 225, 90, 255)
												: chHandleColor(ch));
						dl->AddCircleFilled(P2, r,
											GraphChannelSelected(g_CurveCache.times[i + 1], ch)
												? IM_COL32(255, 225, 90, 255)
												: chHandleColor(ch));

						for (int side = 0; side < 2; ++side)
						{
							const bool outgoing = side == 0;
							const ImVec2 handle = outgoing ? P1 : P2;
							ImGui::SetCursorScreenPos(ImVec2(handle.x - 6, handle.y - 6));
							char id[48];
							snprintf(id, sizeof(id), outgoing ? "##h1b_%zu_ch%d" : "##h2b_%zu_ch%d", i, ch);
							ImGui::InvisibleButton(id, ImVec2(12, 12));
							if (ImGui::IsItemHovered())
								ImGui::SetTooltip("Drag: slope | Shift+drag: move selected channel handles "
												  "| Ctrl: length only | Alt: unlink");
							if (ImGui::IsItemActivated())
							{
								EnsureEditGesture();
								auto &drag = g_TangentDrag;
								drag = {};
								const size_t clicked = outgoing ? i : i + 1;
								drag.Outgoing = outgoing;
								drag.Group =
									io.KeyShift && GraphChannelSelected(g_CurveCache.times[clicked], ch);
								drag.MoveHandle = io.KeyShift;
								drag.LengthOnly = io.KeyCtrl;
								drag.Linked = !io.KeyAlt;
								drag.EditWeight = (drag.MoveHandle || drag.LengthOnly) &&
												  curvePath.GetAutoTangentSettings().Method !=
													  AutoTangentMethod::MinimumJerk;
								drag.Mouse = drag.LastMouse = io.MousePos;
								drag.SecondsPerPixel =
									curvePath.GetDuration() / (g_CurveTimeZoom * plotWidth);
								if (!drag.Group)
								{
									g_GraphChannels.clear();
									g_GraphChannels[g_CurveCache.times[clicked]] = 1u << ch;
									PublishGraphSelection(curvePath);
								}
								for (size_t index = 0; index < g_CurveCache.times.size(); ++index)
									for (int component = 0; component < 7; ++component)
									{
										const bool selected =
											drag.Group
												? GraphChannelSelected(g_CurveCache.times[index], component)
												: index == clicked && component == ch;
										const bool custom =
											component < 3
												? curvePath.PositionInterpMethod_get() == CamPath::DI_CUSTOM
											: component == 3
												? curvePath.FovInterpMethod_get() == CamPath::DI_CUSTOM
												: curvePath.RotationInterpMethod_get() == CamPath::QI_CUSTOM;
										if (!selected || !drawMask[component] || g_CurveLocked[component] ||
											!custom ||
											(g_CurveFocusChannel >= 0 && g_CurveFocusChannel != component) ||
											(curvePath.GetDistanceTiming() &&
											 (component < 3 || curvePath.GetDistanceTimingPose())))
											continue;
										if (outgoing ? index + 1 == g_CurveCache.times.size() : index == 0)
											continue;
										const size_t interval = outgoing ? index : index - 1;
										const double duration = g_CurveCache.times[interval + 1] -
																g_CurveCache.times[interval] -
																g_CurveCache.values[interval].HoldDuration;
										double slope, weight, otherSlope, otherWeight;
										const auto channel = (CamPath::Channel)component;
										if (duration <= 0 ||
											!curvePath.GetTangent(channel, index, outgoing, slope, weight) ||
											!curvePath.GetTangent(channel, outgoing ? index + 1 : index - 1,
																  !outgoing, otherSlope, otherWeight))
											continue;
										Afx::Math::ResolveHermiteWeights(weight, otherWeight);
										const double scale = 2 *
															 vHalf[g_CurveNormalize ? component : anchorCh] /
															 (g_CurveValueScale * plotHeight);
										drag.Handles.push_back(
											{index, channel, duration, slope, weight, scale});
									}
								g_DragFirst = curvePath.GetLowerBound();
								g_DragLast = curvePath.GetUpperBound();
								for (int c = 0; c < 7; ++c)
								{
									g_DragCenters[c] = vCenter[c];
									g_DragHalves[c] = vHalf[c];
								}
							}
						}
					} // active-channel handles
				} // channels
			} // segments

			if (!g_TangentDrag.Handles.empty() && ImGui::IsMouseDragging(0) && !g_CancelGesture &&
				(io.MousePos.x != g_TangentDrag.LastMouse.x || io.MousePos.y != g_TangentDrag.LastMouse.y))
			{
				g_TangentDrag.LastMouse = io.MousePos;
				const auto &drag = g_TangentDrag;
				const double dt = (io.MousePos.x - drag.Mouse.x) * drag.SecondsPerPixel;
				std::vector<CamPath::TangentEdit> edits;
				for (const auto &handle : drag.Handles)
				{
					const double dv = -(io.MousePos.y - drag.Mouse.y) * handle.ValuePerPixel;
					const double direction = drag.Outgoing ? 1.0 : -1.0;
					const double offset = direction * handle.Weight * handle.Duration / 3;
					const double weight =
						drag.EditWeight
							? std::clamp((offset + dt) * direction * 3 / handle.Duration, .01, 5.0)
							: handle.Weight;
					const double slope =
						drag.LengthOnly ? handle.Slope
										: (offset * handle.Slope + dv) /
											  (direction * (std::max)(1e-6, weight) * handle.Duration / 3);
					edits.push_back({handle.Index, handle.Component, drag.Outgoing, slope, weight});
				}
				if (!(drag.LengthOnly && (!drag.EditWeight || dt == 0)) &&
					!curvePath.EditTangents(edits, drag.Linked, drag.EditWeight, !drag.Group))
					g_DocumentStatus = "Handle edit rejected; original keyframes retained.";
			}

			for (size_t index = 0; index < g_CurveCache.times.size(); ++index)
				if (g_CurveCache.values[index].HoldDuration > 0)
				{
					const double begin = g_CurveCache.times[index],
								 release = begin + g_CurveCache.values[index].HoldDuration;
					auto &path = g_Hook_VClient_RenderView.m_CamPath;
					const auto pose = path.EvalUnmodified(begin);
					for (int ch = 0; ch < 7; ++ch)
						if (drawMask[ch])
						{
							const float y = toY_ch(ch, CamPath::GetChannelValue(pose, (CamPath::Channel)ch));
							dl->AddLine(ImVec2(toX(begin), y), ImVec2(toX(release), y),
										(chColor(ch) & 0x00ffffff) | 0x60000000, 1);
							ImVec2 last(toX(begin), toY_ch(ch, CamPath::GetChannelValue(
																   path.Eval(begin), (CamPath::Channel)ch)));
							for (int sample = 1; sample <= 96; ++sample)
							{
								const double t = begin + (release - begin) * sample / 96;
								ImVec2 next(toX(t), toY_ch(ch, CamPath::GetChannelValue(
																   path.Eval(t), (CamPath::Channel)ch)));
								dl->AddLine(last, next, chColor(ch), 2);
								last = next;
							}
						}

					dl->AddLine(ImVec2(toX(release), p0.y + g_CurvePadding),
								ImVec2(toX(release), p1.y - g_CurvePadding), IM_COL32(255, 190, 70, 190), 1);
					int tick = 0;
					char caption[96];
					if (GetDemoTickFromClientTime(g_MirvTime.GetTime(),
												  release + g_Hook_VClient_RenderView.m_CamPath.GetOffset(),
												  tick))
						snprintf(caption, sizeof(caption), "Release %d / %.3fs", tick, release);
					else
						snprintf(caption, sizeof(caption), "Release %.3fs", release);
					dl->AddText(ImVec2(toX(release) + 3, p0.y + 3), IM_COL32(255, 190, 70, 255), caption);
				}

			// One hit target per key/channel. Drag from a frozen transform so changing the
			// first/last key or the value range cannot move the coordinate system under the mouse.
			bool &keyDrag = g_CurveKeyDrag;
			static bool boxDrag = false;
			static CamPath::State dragStart;
			static ImVec2 dragMouse, boxStart;
			static double secondsPerPixel, valuePerPixel[7];
			static std::map<double, unsigned int> dragChannels;
			struct ChannelPoint
			{
				size_t Index;
				int Channel;
				ImVec2 Position;
			};
			std::vector<ChannelPoint> points;
			for (size_t index = 0; index < g_CurveCache.times.size(); ++index)
				for (int ch = 0; ch < 7; ++ch)
					if (drawMask[ch] && (g_CurveFocusChannel < 0 || g_CurveFocusChannel == ch))
					{
						const float keyX = toX(g_CurveCache.times[index]);
						if (!keyDrag && (keyX < p0.x - 6 || keyX > p1.x + 6))
							continue;
						const auto &v = g_CurveCache.values[index];
						double component = CamPath::GetChannelValue(v, (CamPath::Channel)ch);
						if (ch >= 4 && g_Hook_VClient_RenderView.m_CamPath.RotationInterpMethod_get() ==
										   CamPath::QI_CUSTOM)
							component = CamPath::GetChannelValue(
								g_Hook_VClient_RenderView.m_CamPath.EvalUnmodified(g_CurveCache.times[index]),
								(CamPath::Channel)ch);
						ImVec2 point(keyX, toY_ch(ch, component));
						if (!keyDrag && (point.y < p0.y - 6 || point.y > p1.y + 6))
							continue;
						points.push_back({index, ch, point});
						const unsigned char inModes[] = {v.TxModeIn,  v.TyModeIn,  v.TzModeIn, v.TfovModeIn,
														 v.TRyModeIn, v.TRzModeIn, v.TRxModeIn};
						const unsigned char outModes[] = {v.TxModeOut,	 v.TyModeOut,  v.TzModeOut,
														  v.TfovModeOut, v.TRyModeOut, v.TRzModeOut,
														  v.TRxModeOut};
						const bool automatic =
							inModes[ch] == CamPath::TM_AUTO && outModes[ch] == CamPath::TM_AUTO;
						const bool channelSelected = GraphChannelSelected(g_CurveCache.times[index], ch);
						const ImU32 color = channelSelected ? IM_COL32(255, 225, 90, 255) : chColor(ch);
						if (automatic)
							dl->AddCircleFilled(point, channelSelected ? 5.5f : 4, color);
						else
							dl->AddRectFilled(ImVec2(point.x - 4, point.y - 4),
											  ImVec2(point.x + 4, point.y + 4), color);
						ImGui::SetCursorScreenPos(ImVec2(point.x - 6, point.y - 6));
						char id[64];
						snprintf(id, sizeof(id), "##curve_key_%zu_ch%d", index, ch);
						ImGui::InvisibleButton(id, ImVec2(12, 12));
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("Key %u / %s: %.6f\n%s tangents (circle: Auto, square: Manual)",
											  (unsigned)index, channelNames[ch], component,
											  automatic ? "Automatic" : "Authored / baked");
						if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
						{
							g_CurveCtxKeyIndex = (int)index;
							g_CurveCtxChannel = ch;
							g_CurveCtxRevision = g_Hook_VClient_RenderView.m_CamPath.GetGeometryRevision();
							ImGui::OpenPopup("campath_curve_kf_ctx");
						}
						if (!g_ModifierRangeTool && ImGui::IsItemClicked())
						{
							EnsureEditGesture();
							auto &path = g_Hook_VClient_RenderView.m_CamPath;
							if (!io.KeyCtrl && !channelSelected)
								g_GraphChannels.clear();
							auto &mask = g_GraphChannels[g_CurveCache.times[index]];
							if (io.KeyCtrl)
								mask ^= 1u << ch;
							else
								mask |= 1u << ch;
							if (!mask)
								g_GraphChannels.erase(g_CurveCache.times[index]);
							PublishGraphSelection(path);
							BeginEdit();
							dragStart = path.CaptureState();
							dragChannels = g_GraphChannels;
							for (auto &key : dragStart.Keys)
								if (key.second.Selected && (dragChannels[key.first] & 0x70) &&
									path.RotationInterpMethod_get() == CamPath::QI_CUSTOM)
									key.second.SetEulerAngles(
										path.EvalUnmodified(key.first).GetEulerAngles());
							dragMouse = io.MousePos;
							secondsPerPixel = (g_Hook_VClient_RenderView.m_CamPath.GetUpperBound() -
											   g_CurveCache.times.front()) /
											  (g_CurveTimeZoom * plotWidth);
							for (int c = 0; c < 7; ++c)
								valuePerPixel[c] = 2 * vHalf[g_CurveNormalize ? c : anchorCh] /
												   (g_CurveValueScale * plotHeight);
							g_DragFirst = g_CurveCache.times.front();
							g_DragLast = g_Hook_VClient_RenderView.m_CamPath.GetUpperBound();
							for (int c = 0; c < 7; ++c)
							{
								g_DragCenters[c] = vCenter[c];
								g_DragHalves[c] = vHalf[c];
							}
							keyDrag = true;
						}
					}
			if (!g_CancelGesture && keyDrag && ImGui::IsMouseDragging(0))
			{
				double dt = (io.MousePos.x - dragMouse.x) * secondsPerPixel;
				double pixels = -(io.MousePos.y - dragMouse.y);
				if (g_LockKeyTimes)
					dt = 0;

				if (io.KeyShift)
				{
					if (std::abs(io.MousePos.x - dragMouse.x) > std::abs(io.MousePos.y - dragMouse.y))
						pixels = 0;
					else
						dt = 0;
				}
				if (io.KeyAlt && TickInterval() > 0)
					dt = std::round(dt / TickInterval()) * TickInterval();
				CInterpolationMap<CamPathValue> keys;
				bool unique = true;
				unsigned int visibleMask = 0;
				for (int c = 0; c < 7; ++c)
					if (drawMask[c] && (g_CurveFocusChannel < 0 || g_CurveFocusChannel == c))
						visibleMask |= 1u << c;
				CamPath retimed;
				retimed.RestoreState(dragStart);
				std::vector<double> arrivals;
				for (const auto &key : dragStart.Keys)
				{
					const auto mask = dragChannels.count(key.first) ? dragChannels.at(key.first) & visibleMask : 0;
					arrivals.push_back(key.first + (mask ? dt : 0));
				}
				unique = retimed.Retime(arrivals);
				const auto shaped = retimed.CaptureState().Keys;
				for (auto key : dragStart.Keys)
				{
					const auto mask =
						dragChannels.count(key.first) ? dragChannels.at(key.first) & visibleMask : 0;
					if (unique) key.second = shaped.at(key.first + (mask ? dt : 0));
					if (pixels != 0)
						for (int c = 0; c < 7; ++c)
							if ((mask & (1u << c)) && !g_CurveLocked[c])
								CamPath::SetChannelValue(
									key.second, (CamPath::Channel)c,
									CamPath::GetChannelValue(key.second, (CamPath::Channel)c) +
										pixels * valuePerPixel[c]);
					unique &= keys.emplace(key.first + (mask ? dt : 0), key.second).second;
				}
				if (!unique || !retimed.ReplaceKeys(keys))
					g_DocumentStatus = "Keys cannot share an arrival time or cross a hold release.";
				else
				{
					auto &path = g_Hook_VClient_RenderView.m_CamPath;
					if (path.PositionInterpMethod_get() != retimed.PositionInterpMethod_get() ||
						path.FovInterpMethod_get() != retimed.FovInterpMethod_get())
						path.RestoreState(retimed.CaptureState());
					else
						path.ReplaceKeys(keys);
					g_GraphChannels.clear();
					for (const auto &entry : dragChannels)
						g_GraphChannels[entry.first + ((entry.second & visibleMask) ? dt : 0)] |=
							entry.second;
					g_GraphSelectionRevision = g_Hook_VClient_RenderView.m_CamPath.GetRevision();
				}
			}
			if (!ImGui::IsMouseDown(0))
				keyDrag = false;
			if (!g_ModifierRangeTool && hovered && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemActive() &&
				!keyDrag)
			{
				boxDrag = true;
				boxStart = io.MousePos;
				ImGui::SetActiveID(ImGui::GetID("##curve_box_select"), ImGui::GetCurrentWindow());
				ImGui::FocusWindow(ImGui::GetCurrentWindow());
			}
			if (g_CancelGesture)
				boxDrag = false;
			if (boxDrag)
			{
				ImGui::KeepAliveID(ImGui::GetID("##curve_box_select"));
				ImVec2 lo((std::min)(boxStart.x, io.MousePos.x), (std::min)(boxStart.y, io.MousePos.y));
				ImVec2 hi((std::max)(boxStart.x, io.MousePos.x), (std::max)(boxStart.y, io.MousePos.y));
				dl->AddRectFilled(lo, hi, IM_COL32(100, 170, 230, 40));
				dl->AddRect(lo, hi, IM_COL32(100, 170, 230, 220));
				if (!ImGui::IsMouseDown(0))
				{
					auto &path = g_Hook_VClient_RenderView.m_CamPath;
					if (!io.KeyCtrl)
						g_GraphChannels.clear();
					for (const auto &pt : points)
						if (pt.Position.x >= lo.x && pt.Position.x <= hi.x && pt.Position.y >= lo.y &&
							pt.Position.y <= hi.y)
							g_GraphChannels[g_CurveCache.times[pt.Index]] |= 1u << pt.Channel;
					PublishGraphSelection(path);
					ImGui::ClearActiveID();
					boxDrag = false;
				}
			}

			// Draw the curve-editor popup ONCE per frame (outside loops)
			dl->PopClipRect();
			if (ImGui::BeginPopup("campath_curve_kf_ctx"))
			{
				const bool contextValid =
					g_CurveCtxKeyIndex >= 0 && (size_t)g_CurveCtxKeyIndex < g_CurveCache.values.size() &&
					g_CurveCtxChannel >= 0 &&
					g_CurveCtxRevision == g_Hook_VClient_RenderView.m_CamPath.GetGeometryRevision();
				if (!contextValid)
					ImGui::CloseCurrentPopup();
				if (contextValid)
				{
					auto mapChToEnum = [&](int chX) -> CamPath::Channel
					{
						return (chX == 0   ? CamPath::CH_X
								: chX == 1 ? CamPath::CH_Y
								: chX == 2 ? CamPath::CH_Z
								: chX == 3 ? CamPath::CH_FOV
										   : (chX == 4	 ? CamPath::CH_RPITCH
											  : chX == 5 ? CamPath::CH_RYAW
														 : CamPath::CH_RROLL));
					};

					auto ApplyMode = [&](bool setIn, bool setOut, unsigned char mode)
					{
						bool prevDraw = g_CampathDrawer.Draw_get();
						g_CampathDrawer.Draw_set(false);
						if (!g_CurveCache.values[g_CurveCtxKeyIndex].Selected)
						{
							g_Hook_VClient_RenderView.m_CamPath.SelectNone();
							g_Hook_VClient_RenderView.m_CamPath.SelectAdd((size_t)g_CurveCtxKeyIndex,
																		  (size_t)g_CurveCtxKeyIndex);
						}
						g_Hook_VClient_RenderView.m_CamPath.SetTangentMode(mapChToEnum(g_CurveCtxChannel),
																		   setIn, setOut, mode);
						g_CampathDrawer.Draw_set(prevDraw);
						g_SequencerNeedsRefresh = true;
					};

					const auto &contextPath = g_Hook_VClient_RenderView.m_CamPath;
					bool contextCustom = g_CurveCtxChannel < 3
											 ? contextPath.PositionInterpMethod_get() == CamPath::DI_CUSTOM
										 : g_CurveCtxChannel == 3
											 ? contextPath.FovInterpMethod_get() == CamPath::DI_CUSTOM
											 : contextPath.RotationInterpMethod_get() == CamPath::QI_CUSTOM;
					if (!contextCustom)
						ImGui::TextDisabled("Choose Custom interpolation to edit tangents.");
					if (ImGui::BeginMenu("Tangent mode", contextCustom && !g_CurveLocked[g_CurveCtxChannel]))
					{
						if (ImGui::BeginMenu("OUT"))
						{
							if (ImGui::MenuItem("Auto"))
								ApplyMode(false, true, CamPath::TM_AUTO);
							if (ImGui::MenuItem("Flat"))
								ApplyMode(false, true, CamPath::TM_FLAT);
							if (ImGui::MenuItem("Linear"))
								ApplyMode(false, true, CamPath::TM_LINEAR);
							if (ImGui::MenuItem("Free"))
								ApplyMode(false, true, CamPath::TM_FREE);
							ImGui::EndMenu();
						}
						if (ImGui::BeginMenu("IN"))
						{
							if (ImGui::MenuItem("Auto"))
								ApplyMode(true, false, CamPath::TM_AUTO);
							if (ImGui::MenuItem("Flat"))
								ApplyMode(true, false, CamPath::TM_FLAT);
							if (ImGui::MenuItem("Linear"))
								ApplyMode(true, false, CamPath::TM_LINEAR);
							if (ImGui::MenuItem("Free"))
								ApplyMode(true, false, CamPath::TM_FREE);
							ImGui::EndMenu();
						}
						if (ImGui::BeginMenu("BOTH"))
						{
							if (ImGui::MenuItem("Auto"))
								ApplyMode(true, true, CamPath::TM_AUTO);
							if (ImGui::MenuItem("Flat"))
								ApplyMode(true, true, CamPath::TM_FLAT);
							if (ImGui::MenuItem("Linear"))
								ApplyMode(true, true, CamPath::TM_LINEAR);
							if (ImGui::MenuItem("Free"))
								ApplyMode(true, true, CamPath::TM_FREE);
							ImGui::EndMenu();
						}
						ImGui::EndMenu();
					}

					ImGui::Separator();
					ImGui::MenuItem("Close");
				}
				ImGui::EndPopup();
			}
			if (!ImGui::IsPopupOpen("campath_curve_kf_ctx"))
			{
				g_CurveCtxKeyIndex = -1;
				g_CurveCtxChannel = -1;
			}

			ImGui::EndChild();
		}
		else
		{
			ImGui::TextDisabled("Add evaluable keys, or a held pose. Classic Cubic needs four keys.");
		}
	}
	ImGui::End();
}
// Demo events and command-marker details share the existing sequencer renderer.
struct SequencerTimeline
{
	std::future<advancedfx::DemoTimeline::Index> Worker;
	std::shared_ptr<std::atomic_bool> Cancel;
	std::shared_ptr<std::atomic<float>> Progress;
	std::filesystem::path Pending;
	advancedfx::DemoTimeline::Index Index;
	std::vector<size_t> Filtered;
	bool Categories[4] = {true, true, true, false}, RecorderOnly = false, FilterDirty = true;
	char Search[128] = {};
	int SelectedTick = -1;
	unsigned long long DragCommand = 0;
	int DragCommandTick = 0, DragMouseTick = 0;
	unsigned long long SelectedCommand = 0, DraftRevision = ~0ull, DraftId = 0;
	CommandSystem::TickMarker Draft;
	char CommandName[129] = {}, CommandBody[16385] = {};
	std::filesystem::path CommandsFile;
	std::string Status;
	bool FocusEvents = false, FocusCommands = false;
	bool AutoIndex = true, MarkerContext = false, ContextEvent = false;
	int ContextTick = 0, EventLeadTicks = 132, GoToTick = 0;
	std::string RequestedDemo;
	std::filesystem::file_time_type IndexedStamp{};
	~SequencerTimeline()
	{
		if (Cancel)
			Cancel->store(true);
	}
} g_SequencerTimeline;
static void CancelCommandMarkerDrag()
{
	if (!g_SequencerTimeline.DragCommand)
		return;
	g_SequencerTimeline.DragCommand = 0;
	if (ImGui::GetCurrentContext())
		ImGui::ClearActiveID();
}


static void StartTimelineIndex(const std::filesystem::path &file)
{
	auto &state = g_SequencerTimeline;
	if (state.Worker.valid())
	{
		state.Pending = file;
		state.Cancel->store(true);
		return;
	}
	state.Index = {};
	state.Index.File = file;
	state.Filtered.clear();
	state.SelectedTick = -1;
	state.FilterDirty = true;
	state.Cancel = std::make_shared<std::atomic_bool>(false);
	state.Progress = std::make_shared<std::atomic<float>>(0);
	auto cancel = state.Cancel;
	auto progress = state.Progress;
	try
	{
		state.Worker = std::async(std::launch::async, [file, cancel, progress]() {
			return advancedfx::DemoTimeline::Read(file, *cancel, *progress);
		});
	}
	catch (const std::exception &e)
	{
		state.Index.Error = e.what();
	}
}
void NotifyDemoPlaybackFile(const char *file)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	if (file && *file)
	{
		CancelCommandMarkerDrag();
		if (g_WorkTransport != WorkTransport::Idle)
			StopWorkTransport(false, "Range cleared: new demo.");
		g_WorkIn = g_WorkOut = -1;
		g_WorkLoop = false;
		g_WorkStatus.clear();
		SceneClearHistory();
		if (g_SceneRangeEnabled)
			g_SceneRecord = false;
		g_SceneRangeEnabled = false;
		g_ScenePreview = false;
		if (g_ScenePlayerFilter)
			g_ScenePlayerModels = false;
		g_ScenePlayerFilter = false;
		g_ScenePlayers.clear();
		g_ScenePoseStatus = "New demo: record scene history to capture the enabled categories.";
		g_SequencerTimeline.RequestedDemo = file;
	}
}
static void AutoIndexRequestedDemo()
{
	auto &state = g_SequencerTimeline;
	if (state.RequestedDemo.empty())
		return;
	auto name = std::move(state.RequestedDemo);
	state.RequestedDemo.clear();
	if (!state.AutoIndex || !g_VEngineClient)
		return;
	try
	{
		std::filesystem::path file(name);
		if (!file.has_extension())
			file += ".dem";
		if (file.is_relative())
		{
			const char *directory = g_VEngineClient->GetGameDirectory();
			if (!directory || !*directory)
				return;
			file = file.lexically_normal();
			auto game = std::filesystem::absolute(directory).lexically_normal();
			while (game.filename().empty() && game.parent_path() != game)
				game = game.parent_path();
			std::vector<std::filesystem::path> candidates{game / file};
			// Source/UI may report tf/name.dem instead of name.dem. Avoid tf/tf.
			if (_stricmp(file.begin()->string().c_str(), game.filename().string().c_str()) == 0)
				candidates.insert(candidates.begin(), game.parent_path() / file);
			candidates.push_back(std::filesystem::absolute(file));
			std::error_code error;
			auto found = std::find_if(candidates.begin(), candidates.end(), [&](const auto &candidate) {
				error.clear();
				return std::filesystem::is_regular_file(candidate, error);
			});
			if (found == candidates.end())
			{
				state.Status = "Automatic event indexing could not find " + name +
					" in the game directory: " + game.string() + ". Use Open demo... to select it.";
				return;
			}
			file = *found;
		}
		file = std::filesystem::weakly_canonical(file);
		if (state.Worker.valid() && state.Index.File == file && !state.Cancel->load())
		{
			state.Pending.clear();
			return;
		}
		if (!state.Worker.valid() && state.Index.File == file && state.Index.Error.empty() &&
		    std::filesystem::last_write_time(file) == state.IndexedStamp)
			return;
		StartTimelineIndex(file);
	}
	catch (const std::exception &e)
	{
		state.Status = std::string("Automatic event indexing: ") + e.what();
	}
}
static void PollTimelineIndex()
{
	auto &state = g_SequencerTimeline;
	if (state.Worker.valid() && state.Worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		try
		{
			state.Index = state.Worker.get();
		}
		catch (const std::exception &e)
		{
			state.Index.Error = e.what();
		}
		state.FilterDirty = true;
		if (state.Index.Error.empty())
		{
			std::error_code error;
			state.IndexedStamp = std::filesystem::last_write_time(state.Index.File, error);
		}
		if (!state.Pending.empty())
		{
			auto next = state.Pending;
			state.Pending.clear();
			StartTimelineIndex(next);
		}
	}
}
static bool TimelineMatchesDemo()
{
	const auto &index = g_SequencerTimeline.Index;
	if (!g_VEngineClient || !g_VEngineClient->IsPlayingDemo())
		return false;
	auto demo = g_VEngineClient->GetDemoInfoEx();
	if (!demo || index.File.empty() || !index.Error.empty())
		return false;
	const char *level = g_VEngineClient->GetLevelName();
	if (!level)
		return false;
	return demo->GetDemoPlaybackTotalTicks() == index.TotalTicks && advancedfx::DemoTimeline::SameMap(level, index.Map);
}
static void FilterTimelineEvents()
{
	auto &state = g_SequencerTimeline;
	if (!state.FilterDirty)
		return;
	state.Filtered.clear();
	std::string search = state.Search;
	for (auto &c : search)
		if ('A' <= c && c <= 'Z')
			c = char(c - 'A' + 'a');
	for (size_t i = 0; i < state.Index.Events.size(); ++i)
	{
		const auto &event = state.Index.Events[i];
		if (!state.Categories[int(event.Kind)] || (state.RecorderOnly && !event.RecorderDeath))
			continue;
		if (!search.empty() && event.Search.find(search) == std::string::npos)
			continue;
		state.Filtered.push_back(i);
	}
	state.FilterDirty = false;
}
static void DrawSequencerExtraTracks(int visibleStart, int visibleEnd)
{
	auto &state = g_SequencerTimeline;
	state.MarkerContext = false;
	ImGui::FrameIndexType mouseTick = 0;
	const bool haveMouseTick = ImGui::GetNeoSequencerMouseFrame(
		&mouseTick, ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	const int buckets = (std::max)(1, (std::min)(2000, int(ImGui::GetContentRegionAvail().x)));
	auto bucket = [&](int tick) {
		return int((int64_t(tick) - visibleStart) * buckets /
		           (std::max)(int64_t(1), int64_t(visibleEnd) - visibleStart + 1));
	};
	if (SequencerTrackVisible(4, !g_CommandSystem.GetTickMarkers().empty()) &&
		ImGui::BeginNeoTimelineEx("Commands", nullptr, ImGuiNeoTimelineFlags_None))
	{
		int previous = -1;
		int64_t nearest = INT64_MAX;
		for (const auto &marker : g_CommandSystem.GetTickMarkers())
		{
			if (marker.Tick < visibleStart || marker.Tick > visibleEnd)
				continue;
			int pixel = bucket(marker.Tick);
			if (pixel == previous)
				continue;
			previous = pixel;
			std::string label = marker.Name.empty() ? "Command" : marker.Name;
			label += marker.Enabled ? "" : " [disabled]";
			bool hovered = false;
			const int64_t distance = std::abs(int64_t(marker.Tick) - mouseTick);
			if (ImGui::NeoTimelineSpan(marker.Tick, marker.Tick,
			                           marker.Id == state.SelectedCommand ? IM_COL32(255, 230, 110, 255)
			                           : marker.Enabled                   ? IM_COL32(190, 130, 255, 255)
			                                                              : IM_COL32(100, 100, 100, 255),
			                           label.c_str(), &hovered) && distance < nearest)
			{
				nearest = distance;
				state.SelectedCommand = marker.Id;
				state.DragCommand = marker.Id;
				state.DragCommandTick = marker.Tick;
				state.DragMouseTick = mouseTick;
				state.FocusCommands = true;
				ImGui::NeoCancelSelection();
			}
			if (hovered && distance < nearest && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				nearest = distance;
				state.ContextTick = marker.Tick;
				state.ContextEvent = false;
				state.MarkerContext = true;
				state.SelectedCommand = marker.Id;
				state.FocusCommands = true;
			}
		}
		if (state.DragCommand)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
			    !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
				state.DragCommand = 0;
			else
			{
				const auto &markers = g_CommandSystem.GetTickMarkers();
				auto marker = std::find_if(markers.begin(), markers.end(), [&](const auto &value) {
					return value.Id == state.DragCommand;
				});
				if (marker == markers.end())
					state.DragCommand = 0;
				else
				{
					auto moved = *marker;
					moved.Tick = int(std::clamp(int64_t(state.DragCommandTick) + mouseTick - state.DragMouseTick,
						int64_t(0), int64_t(INT_MAX)));
					if (haveMouseTick && moved.Tick != marker->Tick)
						g_CommandSystem.SetTickMarker(moved);
				}
				ImGui::NeoCancelSelection();
			}
		}
		ImGui::EndNeoTimeLine();
	}
	if (SequencerTrackVisible(5, !state.Index.Events.empty()) &&
		ImGui::BeginNeoTimelineEx("Demo events", nullptr, ImGuiNeoTimelineFlags_None))
	{
		FilterTimelineEvents();
		int previous = -1;
		int64_t nearest = INT64_MAX;
		auto first = std::lower_bound(state.Filtered.begin(), state.Filtered.end(), visibleStart,
		                              [&](size_t i, int tick) { return state.Index.Events[i].Tick < tick; });
		for (auto it = first; it != state.Filtered.end(); ++it)
		{
			const auto &event = state.Index.Events[*it];
			if (event.Tick > visibleEnd)
				break;
			int pixel = bucket(event.Tick);
			if (pixel == previous)
				continue;
			previous = pixel;
			const ImU32 colors[] = {IM_COL32(255, 100, 90, 255), IM_COL32(100, 180, 255, 255),
			                        IM_COL32(255, 190, 60, 255), IM_COL32(120, 210, 160, 255)};
			bool hovered = false;
			const int64_t distance = std::abs(int64_t(event.Tick) - mouseTick);
			if (ImGui::NeoTimelineSpan(event.Tick, event.Tick, colors[int(event.Kind)], event.Summary.c_str(),
			                           &hovered) && distance < nearest)
			{
				nearest = distance;
				state.SelectedTick = event.Tick;
				state.FocusEvents = true;
				ImGui::NeoCancelSelection();
			}
			if (hovered && distance < nearest && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				nearest = distance;
				state.ContextTick = event.Tick;
				state.ContextEvent = true;
				state.MarkerContext = true;
				state.SelectedTick = event.Tick;
				state.FocusEvents = true;
			}
		}
		ImGui::EndNeoTimeLine();
	}
}
static int EventSeekTick(int tick)
{
	return (std::max)(0, tick - (std::max)(0, g_SequencerTimeline.EventLeadTicks));
}
static void DrawSequencerSeekContext(bool keyframeContextOpened)
{
	auto &state = g_SequencerTimeline;
	ImGui::FrameIndexType tick;
	if (!keyframeContextOpened && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
	    ImGui::GetNeoSequencerMouseFrame(&tick))
	{
		if (!state.MarkerContext)
		{
			state.ContextTick = tick;
			state.ContextEvent = false;
		}
		ImGui::OpenPopup("sequencer_seek_context");
		ImGui::NeoCancelSelection();
	}
	if (ImGui::BeginPopup("sequencer_seek_context"))
	{
		ImGui::Text("Tick %d", state.ContextTick);
		ImGui::BeginDisabled(!g_VEngineClient || !g_VEngineClient->IsPlayingDemo() ||
		                     (state.ContextEvent && !TimelineMatchesDemo()));
		if (ImGui::MenuItem("Go to"))
		{
			if (SceneCachePreviewActive()) ScenePreviewTick(state.ContextTick);
			else Afx_GotoDemoTick(state.ContextTick);
		}
		if (ImGui::MenuItem("Go to with lead-in"))
		{
			if (SceneCachePreviewActive()) ScenePreviewTick(EventSeekTick(state.ContextTick));
			else Afx_GotoDemoTick(EventSeekTick(state.ContextTick));
		}
		ImGui::EndDisabled();
		ImGui::SetNextItemWidth(140);
		ImGui::InputInt("Lead-in (ticks)", &state.EventLeadTicks);
		state.EventLeadTicks = (std::max)(0, state.EventLeadTicks);
		ImGui::EndPopup();
	}
}
static void OpenDemoPicker()
{
	if (!g_DemoDialogInit)
	{
		g_DemoOpenDialog.SetTitle("Open demo");
		g_DemoOpenDialog.SetTypeFilters({".dem"});
		g_DemoDialogInit = true;
	}
	if (!g_OverlayPaths.demoDir.empty())
		g_DemoOpenDialog.SetDirectory(g_OverlayPaths.demoDir);
	g_DemoOpenDialog.Open();
}
static void DrawDemoPicker()
{
	g_DemoOpenDialog.Display();
	if (!g_DemoOpenDialog.HasSelected())
		return;
	const auto file = g_DemoOpenDialog.GetSelected();
	const auto path = file.string();
	if (path.find_first_of("\"\r\n") == std::string::npos)
	{
		Afx_ExecClientCmd(("playdemo \"" + path + "\"").c_str());
		NotifyDemoPlaybackFile(path.c_str());
		g_OverlayPaths.demoDir = file.parent_path().string();
		ImGui::MarkIniSettingsDirty();
	}
	g_DemoOpenDialog.ClearSelected();
}
struct RepairEditor
{
	int Entity = -1, Tick = 0, First = 0, Last = 0, Channels = 2;
	bool Smooth = true, Follow = true, Drag = false, DragMoved = false;
	float DragMouseX = 0;
	int DragStart = 0;
	unsigned long long ReportRevision = ~0ull;
	int ReportBridge = -1;
	std::vector<std::string> RangeReports;
	std::vector<unsigned> RangeReady;
	int PendingEntity = -1, PendingUser = 0;
	std::string Status;
} g_RepairEditor;
static void UpdatePlayerRepairReports()
{
	auto &model = MirvFix_PlayerRepairModel();
	auto &ui = g_RepairEditor;
	if (ui.ReportRevision == model.Revision() && ui.ReportBridge == model.MaxBridgeTicks)
		return;
	ui.ReportRevision = model.Revision();
	ui.ReportBridge = model.MaxBridgeTicks;
	ui.RangeReports.clear();
	ui.RangeReady.clear();
	for (const auto &range : model.GetEdits().Ranges)
	{
		std::array<double, 5> values{};
		std::string reason;
		unsigned ready = model.Evaluate(range.First, values, &reason) & range.Mask;
		ui.RangeReady.push_back(ready);
		if (ready == range.Mask)
			ui.RangeReports.push_back(
			    "Ready: reconstruction available. Compare repaired to replay and apply.");
		else
			ui.RangeReports.push_back(std::string(ready ? "Partly blocked: " : "Blocked: ") + reason);
	}
}
static void DrawPlayerRepairTrack(int visibleStart, int visibleEnd)
{
	UpdatePlayerRepairReports();
	auto &model = MirvFix_PlayerRepairModel();
	auto &ui = g_RepairEditor;
	std::string label = "Player repair";
	if (!model.Player.empty())
		label += ": " + model.Player;
	if (!ImGui::BeginNeoTimelineEx(label.c_str(), nullptr, ImGuiNeoTimelineFlags_None))
		return;
	const ImGuiID dragId = ImGui::GetID("player_repair_range_drag");
	ImGui::FrameIndexType tick;
	if (ImGui::GetNeoTimelineMouseFrame(&tick))
	{
		if (ImGui::IsMouseClicked(0))
		{
			ui.Drag = true;
			ui.DragMoved = false;
			ui.DragMouseX = ImGui::GetIO().MousePos.x;
			ui.DragStart = tick;
			ui.First = ui.Last = tick;
			ui.Follow = false;
		}
		if (ui.Drag && ImGui::IsMouseDown(0))
		{
			ui.DragMoved |= std::abs(ImGui::GetIO().MousePos.x - ui.DragMouseX) > 3;
			if (ui.DragMoved)
			{
				ui.First = (std::min)(ui.DragStart, int(tick));
				ui.Last = (std::max)(ui.DragStart, int(tick));
				ui.Tick = tick;
			}
			ImGui::NeoCancelSelection();
		}
	}
	if (!ImGui::IsMouseDown(0))
	{
		ui.Drag = false;
		if (ImGui::GetActiveID() == dragId)
			ImGui::ClearActiveID();
	}
	for (size_t i = 0; i < model.GetEdits().Ranges.size(); ++i)
	{
		const auto &r = model.GetEdits().Ranges[i];
		bool ready = ui.RangeReady[i] == r.Mask;
		ImGui::NeoTimelineSpan(r.First, r.Last,
		                       ready ? IM_COL32(255, 100, 80, 150) : IM_COL32(255, 190, 50, 190),
		                       ui.RangeReports[i].c_str());
	}
	if (ui.Drag)
		ImGui::NeoTimelineSpan(ui.First, ui.Last, IM_COL32(255, 220, 80, 140), "Repair selection");
	for (int cut : model.GetEdits().Cuts)
		if (cut >= visibleStart && cut <= visibleEnd)
			ImGui::NeoTimelineSpan(cut, cut, IM_COL32(255, 80, 255, 255),
			                       "Do not interpolate across this tick");
	const int width = (std::max)(1, int(ImGui::GetContentRegionAvail().x));
	for (auto i = model.Samples().lower_bound(visibleStart);
	     i != model.Samples().end() && i->first <= visibleEnd;)
	{
		int pixel =
		    int((int64_t(i->first) - visibleStart) * width / (int64_t(visibleEnd) - visibleStart + 1));
		if (ImGui::NeoTimelineSpan(i->first, i->first, IM_COL32(110, 210, 255, 255),
		                           "Original player sample"))
		{
			ui.Drag = true;
			ui.DragStart = i->first;
			ui.DragMouseX = ImGui::GetIO().MousePos.x;
			ui.DragMoved = false;
			ui.Tick = i->first;
			ui.First = ui.Last = i->first;
			ui.Follow = false;
		}
		const int64_t span = int64_t(visibleEnd) - visibleStart + 1;
		const int64_t next = int64_t(visibleStart) + ((int64_t(pixel) + 1) * span + width - 1) / width;
		const int64_t target = (std::max)(int64_t(i->first) + 1, next);
		if (target > INT_MAX)
			break;
		i = model.Samples().lower_bound(int(target));
	}
	if (ui.Drag)
	{
		if (ImGui::GetActiveID() != dragId)
		{
			ImGui::SetActiveID(dragId, ImGui::GetCurrentWindow());
			ImGui::FocusWindow(ImGui::GetCurrentWindow());
		}
		ImGui::KeepAliveID(dragId);
	}
	ImGui::EndNeoTimeLine();
}
static ImGui::FileBrowser g_RepairDemoDialog(ImGuiFileBrowserFlags_CloseOnEsc |
                                             ImGuiFileBrowserFlags_EditPathString);
static void OpenRepairDemoPicker(int entity = -1, int user = 0)
{
	auto &ui = g_RepairEditor;
	ui.PendingEntity = entity;
	ui.PendingUser = user;
	g_RepairDemoDialog.SetTitle("Choose the demo currently playing");
	g_RepairDemoDialog.SetTypeFilters({".dem"});
	if (!g_OverlayPaths.demoDir.empty())
		g_RepairDemoDialog.SetDirectory(g_OverlayPaths.demoDir);
	else if (g_VEngineClient)
	{
		auto directory = g_VEngineClient->GetGameDirectory();
		if (directory && *directory)
			g_RepairDemoDialog.SetDirectory(directory);
	}
	g_RepairDemoDialog.Open();
	ui.Status =
	    "Select the .dem currently playing. Playback will not restart; event indexing is not required.";
}
static void CompleteRepairDemoSelection(const std::filesystem::path &file)
{
	auto &ui = g_RepairEditor;
	if (ui.PendingEntity >= 0)
	{
		char name[64];
		int user = 0;
		if (!Mirv_Tf2_GetPlayerInfo(ui.PendingEntity, name, sizeof(name), &user) || user != ui.PendingUser)
		{
			ui.Status = "The selected player changed while choosing the demo. Choose the player again and "
			            "start collection.";
			ui.PendingEntity = -1;
			return;
		}
	}
	if (MirvFix_PlayerRepairBindPlayingDemo(file.string().c_str()))
	{
		g_OverlayPaths.demoDir = file.parent_path().string();
		ImGui::MarkIniSettingsDirty();
		ui.Status.clear();
		if (ui.PendingEntity >= 0)
			MirvFix_PlayerRepairBegin(ui.PendingEntity);
	}
	else
		ui.Status =
		    "The demo could not be identified. Check the message above and choose the playing file again.";
	ui.PendingEntity = -1;
	ui.PendingUser = 0;
}
// Comparison uses the marked interval, not a stale inspector tick. The native seek
// helper snapshots its after-seek options, so other sequencer seeks keep their settings.
static const advancedfx::PlayerRepair::Range *PlayerRepairComparisonRange()
{
	const auto &ranges = MirvFix_PlayerRepairModel().GetEdits().Ranges;
	const advancedfx::PlayerRepair::Range *first = nullptr;
	for (const auto &range : ranges)
	{
		if (g_RepairEditor.First == range.First && g_RepairEditor.Last == range.Last)
			return &range;
		if (!first || range.First < first->First)
			first = &range;
	}
	return first;
}
static void ComparePlayerRepair(bool repaired)
{
	auto &ui = g_RepairEditor;
	auto range = PlayerRepairComparisonRange();
	if (!range)
	{
		ui.Status = "Mark bad samples before comparing a repair.";
		return;
	}
	ui.First = range->First;
	ui.Last = range->Last;
	if (!MirvFix_PlayerRepairEnable(repaired))
	{
		ui.Status.clear(); // The native status explains the blocked repair.
		return;
	}
	int previousMode = MirvCampathPreview_GetSeekMode();
	double previousScale = MirvCampathPreview_GetSeekScale();
	MirvCampathPreview_SetSeekOptions(2, previousScale);
	bool seeking = MirvCampathPreview_SeekTick((std::max)(0, ui.First - 66));
	MirvCampathPreview_SetSeekOptions(previousMode, previousScale);
	if (!seeking && repaired)
		MirvFix_PlayerRepairEnable(false);
	ui.Status = seeking ? "Comparison will play at normal speed after seeking. Watch the sampled player."
	                    : "Comparison seek could not start. Load the matching demo and try again.";
}
static void DrawPlayerAngleDiagnostic(int playhead)
{
	if (!ImGui::CollapsingHeader("Investigate angle snapping"))
		return;
	static int first = 0, last = 0;
	static std::string status;
	static ImGui::FileBrowser saveDialog(ImGuiFileBrowserFlags_CloseOnEsc |
	                                     ImGuiFileBrowserFlags_EditPathString |
	                                     ImGuiFileBrowserFlags_EnterNewFilename);
	ImGui::TextWrapped("Record ORIGINAL playback around a glitch. This disables player repair, rewinds "
	                   "to the beginning, and plays automatically. No samples or marks are removed.");
	ImGui::BeginDisabled(MirvFix_PlayerAngleTraceActive());
	if (ImGui::Button("Use current tick +/- 66"))
	{
		first = (std::max)(0, playhead - 66);
		last = playhead <= INT_MAX - 66 ? playhead + 66 : INT_MAX;
	}
	ImGui::SetNextItemWidth(120);
	ImGui::InputInt("Diagnostic first tick", &first);
	ImGui::SetNextItemWidth(120);
	ImGui::InputInt("Diagnostic last tick", &last);
	if (ImGui::Button("Record original angle diagnostic"))
	{
		if (!MirvFix_PlayerRepairHasDemo() && TimelineMatchesDemo())
			MirvFix_PlayerRepairBindPlayingDemo(g_SequencerTimeline.Index.File.string().c_str());
		if (MirvFix_PlayerAngleTraceBegin(g_RepairEditor.Entity, first, last))
		{
			int mode = MirvCampathPreview_GetSeekMode();
			double scale = MirvCampathPreview_GetSeekScale();
			MirvCampathPreview_SetSeekOptions(2, scale);
			bool seeking = MirvCampathPreview_SeekTick(0);
			MirvCampathPreview_SetSeekOptions(mode, scale);
			if (!seeking)
				MirvFix_PlayerAngleTraceEnd();
			status = seeking ? "Wait for playback to pass the diagnostic end tick, then save CSV."
			                 : "Could not start the diagnostic seek.";
		}
		else
			status.clear();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Stop angle diagnostic"))
		MirvFix_PlayerAngleTraceEnd();
	ImGui::TextWrapped("%s", MirvFix_PlayerAngleTraceStatus());
	ImGui::Text("%zu diagnostic rows", MirvFix_PlayerAngleTraceRows());
	ImGui::BeginDisabled(MirvFix_PlayerAngleTraceActive() || !MirvFix_PlayerAngleTraceRows());
	if (ImGui::Button("Save angle diagnostic CSV..."))
	{
		saveDialog.SetTitle("Save angle diagnostic to a new file");
		saveDialog.SetTypeFilters({".csv"});
		saveDialog.SetInputName("player-angles.csv");
		saveDialog.Open();
	}
	ImGui::EndDisabled();
	saveDialog.Display();
	if (saveDialog.HasSelected())
	{
		status = MirvFix_PlayerAngleTraceSave(saveDialog.GetSelected().wstring().c_str())
		             ? "Angle diagnostic CSV saved."
		             : "Save failed. Stop capture and choose a new writable filename.";
		saveDialog.ClearSelected();
	}
	if (!status.empty())
		ImGui::TextWrapped("%s", status.c_str());
	ImGui::TextWrapped("Records receive angles, live poses, sequences/cycles, animation layers, movement/timing, "
	                   "model metadata, and natural bone matrices. Coverage counts and status codes identify "
	                   "unavailable or dropped data. Pose slots are normalized, not degrees. "
	                   "No additional bone evaluation is requested.");
}
static void DrawPlayerRepairEditor(int playhead)
{
	if (!ImGui::BeginTabItem("Player repair"))
		return;
	auto &model = MirvFix_PlayerRepairModel();
	auto &ui = g_RepairEditor;
	static ImGui::FileBrowser loadDialog(ImGuiFileBrowserFlags_CloseOnEsc |
	                                     ImGuiFileBrowserFlags_EditPathString);
	static ImGui::FileBrowser saveDialog(
	    ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_EditPathString |
	    ImGuiFileBrowserFlags_EnterNewFilename | ImGuiFileBrowserFlags_CreateNewDir);
	ImGui::TextWrapped("Experimental player repair changes position and eye angles during playback. "
	                   "It does not rewrite the demo or repair animation/bone poses.");
	ImGui::Separator();
	ImGui::TextUnformatted("1. Record the player's movement");
	ImGui::TextWrapped("Choose a player, start collection, and play through the glitch with good movement "
	                   "before and after it. Starting a new collection replaces previous samples and marks.");
	char previewName[64];
	int previewUser = 0;
	bool havePreview = Mirv_Tf2_GetPlayerInfo(ui.Entity, previewName, sizeof(previewName), &previewUser);
	if (ImGui::BeginCombo("Repair player", havePreview ? previewName : "Choose player"))
	{
		for (int entity = 1; entity <= Mirv_Tf2_GetMaxPlayers(); ++entity)
		{
			char name[64];
			int user = 0;
			if (!Mirv_Tf2_GetPlayerInfo(entity, name, sizeof(name), &user) || user <= 0)
				continue;
			std::string label = std::to_string(entity) + ": " + name;
			if (ImGui::Selectable(label.c_str(), entity == ui.Entity))
				ui.Entity = entity;
		}
		ImGui::EndCombo();
	}
	char chosen[64];
	int user = 0;
	bool validPlayer = Mirv_Tf2_GetPlayerInfo(ui.Entity, chosen, sizeof(chosen), &user) && user > 0;
	if (validPlayer)
		ImGui::Text("Selected player: %s (user %d)", chosen, user);
	else
		ImGui::TextWrapped("Choose a recorded player from Repair player before starting collection.");
	ImGui::BeginDisabled(!validPlayer);
	if (ImGui::Button("Start new player collection"))
	{
		if (!MirvFix_PlayerRepairHasDemo() && TimelineMatchesDemo())
			MirvFix_PlayerRepairBindPlayingDemo(g_SequencerTimeline.Index.File.string().c_str());
		if (MirvFix_PlayerRepairHasDemo())
		{
			ui.Status.clear();
			MirvFix_PlayerRepairBegin(ui.Entity);
		}
		else
			OpenRepairDemoPicker(ui.Entity, user);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!MirvFix_PlayerRepairCollecting());
	if (ImGui::Button("Stop player collection"))
		MirvFix_PlayerRepairEnd();
	ImGui::EndDisabled();

	if (ImGui::Button("Choose playing demo file..."))
		OpenRepairDemoPicker();
	g_RepairDemoDialog.Display();
	if (g_RepairDemoDialog.HasSelected())
	{
		CompleteRepairDemoSelection(g_RepairDemoDialog.GetSelected());
		g_RepairDemoDialog.ClearSelected();
	}
	else if (!g_RepairDemoDialog.IsOpened() && ui.PendingEntity >= 0)
	{
		ui.PendingEntity = -1;
		ui.PendingUser = 0;
		ui.Status = "Collection was not started; no playing demo file was selected.";
	}
	ImGui::TextWrapped("%s", MirvFix_PlayerRepairStatus());
	if (!model.Demo.empty())
		ImGui::TextWrapped(
		    "Sample demo: %s",
		    std::filesystem::path(model.Demo.substr(0, model.Demo.find('|'))).filename().string().c_str());
	ImGui::Text("%zu recorded samples | %zu marked repairs%s", model.Samples().size(),
	            model.GetEdits().Ranges.size(), MirvFix_PlayerRepairCollecting() ? " | collecting" : "");
	if (ImGui::Button("Load player repair"))
	{
		loadDialog.SetTitle("Load player repair");
		loadDialog.SetTypeFilters({".xml"});
		loadDialog.Open();
	}
	ImGui::SameLine();
	if (ImGui::Button("Save player repair"))
	{
		saveDialog.SetTitle("Save player repair");
		saveDialog.SetTypeFilters({".xml"});
		saveDialog.SetInputName("player.repair.xml");
		saveDialog.Open();
	}
	loadDialog.Display();
	saveDialog.Display();
	if (loadDialog.HasSelected())
	{
		MirvFix_PlayerRepairEnable(false);
		MirvFix_PlayerRepairEnd();
		bool loaded = model.Load(loadDialog.GetSelected().wstring().c_str());
		ui.Status = loaded ? "Repair loaded. Choose Compare repaired to test it."
		                   : "Invalid repair file; previous data retained.";
		if (loaded && !model.GetEdits().Ranges.empty())
		{
			ui.First = model.GetEdits().Ranges.front().First;
			ui.Last = model.GetEdits().Ranges.front().Last;
		}
		loadDialog.ClearSelected();
	}
	if (saveDialog.HasSelected())
	{
		ui.Status = model.Save(saveDialog.GetSelected().wstring().c_str())
		                ? "Repair and original samples saved."
		                : "Could not save repair.";
		saveDialog.ClearSelected();
	}
	ImGui::Separator();
	ImGui::TextUnformatted("2. Mark the bad samples");
	ImGui::TextWrapped("Drag on the Player repair timeline row, or select one sample below. "
	                   "Use separate small repairs for scattered glitches to preserve good movement.");
	ImGui::Checkbox("Follow demo tick in samples", &ui.Follow);
	if (ui.Follow)
		ui.Tick = playhead;
	ImGui::SetNextItemWidth(120);
	ImGui::InputInt("Inspect sample tick", &ui.Tick);
	ui.Tick = (std::max)(0, ui.Tick);
	auto selected = model.Samples().lower_bound(ui.Tick);
	if (selected == model.Samples().end() && !model.Samples().empty())
		selected = std::prev(model.Samples().end());
	if (ImGui::Button("Previous sample"))
	{
		if (selected != model.Samples().begin())
		{
			--selected;
			ui.Tick = selected->first;
			ui.Follow = false;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Next sample"))
	{
		if (selected != model.Samples().end())
		{
			auto next = std::next(selected);
			if (next != model.Samples().end())
			{
				selected = next;
				ui.Tick = selected->first;
				ui.Follow = false;
			}
		}
	}
	if (selected != model.Samples().end())
	{
		ImGui::Text("Sample tick %d | position %.3f %.3f %.3f | pitch/yaw %.3f %.3f", selected->first,
		            selected->second.Value[0], selected->second.Value[1], selected->second.Value[2],
		            selected->second.Value[3], selected->second.Value[4]);
		auto repaired = selected->second.Value;
		std::string reason;
		unsigned mask = model.Evaluate(selected->first, repaired, &reason);
		if (mask)
			ImGui::Text("Replacement: position %.3f %.3f %.3f | pitch/yaw %.3f %.3f", repaired[0],
			            repaired[1], repaired[2], repaired[3], repaired[4]);
		if (!reason.empty())
			ImGui::TextWrapped("%s", reason.c_str());
		if (ImGui::Button("Select this sample"))
		{
			ui.First = ui.Last = selected->first;
			ui.Follow = false;
			ui.Tick = selected->first;
		}
	}
	ImGui::BeginDisabled(MirvFix_PlayerRepairCollecting());
	ImGui::SetNextItemWidth(120);
	ImGui::InputInt("Exclude first tick", &ui.First);
	ImGui::SameLine();
	if (ImGui::Button("Start at playhead"))
		ui.First = playhead;
	ImGui::SetNextItemWidth(120);
	ImGui::InputInt("Exclude last tick", &ui.Last);
	ImGui::SameLine();
	if (ImGui::Button("End at playhead"))
		ui.Last = playhead;
	const char *channels[] = {"Position", "Eye angles", "Position + eye angles"};
	ImGui::Combo("Repair channels", &ui.Channels, channels, 3);
	ImGui::Checkbox("Smooth bounded reconstruction", &ui.Smooth);
	if (ImGui::Button("Exclude marked player samples"))
	{
		advancedfx::PlayerRepair::Range range;
		range.First = ui.First;
		range.Last = ui.Last;
		range.Mask = ui.Channels == 0 ? 7 : ui.Channels == 1 ? 24 : 31;
		range.Smooth = ui.Smooth;
		auto first = model.Samples().lower_bound(range.First);
		ui.Status = first != model.Samples().end() && first->first <= range.Last && model.Exclude(range)
		                ? "Marked for repair. Check Ready/Blocked below, then Compare repaired."
		                : "No recorded sample in range, overlapping channels, or invalid tick range.";
	}
	if (ImGui::Button("Undo player repair"))
		model.Undo(false);
	ImGui::SameLine();
	if (ImGui::Button("Redo player repair"))
		model.Undo(true);
	UpdatePlayerRepairReports();
	if (!model.GetEdits().Ranges.empty())
		DrawOverlayHint("Amber spans are blocked or partly blocked; hover for anchor details.");
	for (size_t i = 0; i < model.GetEdits().Ranges.size(); ++i)
	{
		const auto &r = model.GetEdits().Ranges[i];
		ImGui::PushID(int(i));
		if (ImGui::SmallButton("Use for comparison"))
		{
			ui.First = r.First;
			ui.Last = r.Last;
		}
		ImGui::SameLine();
		ImGui::Text("%zu: ticks %d..%d | %s | %s", i, r.First, r.Last,
		            r.Mask == 7    ? "position"
		            : r.Mask == 24 ? "eye angles"
		                           : "both",
		            r.Smooth ? "smooth" : "linear");
		ImGui::SameLine();
		if (ImGui::SmallButton("Remove exclusion"))
		{
			model.RemoveRange(i);
			ImGui::PopID();
			break;
		}
		ImGui::TextWrapped("%s", ui.RangeReports[i].c_str());
		ImGui::PopID();
	}
	if (ImGui::CollapsingHeader("Advanced repair boundaries"))
	{
		ImGui::TextWrapped("A discontinuity protects a real teleport or other intentional jump. "
		                   "It BLOCKS repair across that tick; it does not exclude a bad sample.");
		ImGui::SetNextItemWidth(120);
		ImGui::InputInt("Maximum bridge (ticks)", &model.MaxBridgeTicks);
		model.MaxBridgeTicks = (std::clamp)(model.MaxBridgeTicks, 1, 100000);
		ImGui::Text("Boundary tick: %d (Inspect sample tick above)", ui.Tick);
		if (ImGui::Button("Toggle discontinuity at sample"))
			model.ToggleCut(ui.Tick);
		for (int cut : model.GetEdits().Cuts)
		{
			ImGui::PushID(cut);
			ImGui::Text("Protected discontinuity at tick %d", cut);
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove boundary"))
			{
				model.ToggleCut(cut);
				ImGui::PopID();
				break;
			}
			ImGui::PopID();
		}
	}
	ImGui::EndDisabled();
	ImGui::Separator();
	ImGui::TextUnformatted("3. Compare during playback");
	if (auto range = PlayerRepairComparisonRange())
		ImGui::Text("Comparison: ticks %d..%d (starts 66 ticks earlier)", range->First, range->Last);
	ImGui::TextWrapped("Comparison seeks before the marked range and plays automatically. "
	                   "Use the same view of the sampled player for both versions.");
	bool enabled = MirvFix_PlayerRepairEnabled();
	if (ImGui::Checkbox("Apply marked player repairs", &enabled))
		MirvFix_PlayerRepairEnable(enabled);
	ImGui::Text("Repair: %s | %llu substituted updates | %llu actually changed",
	            MirvFix_PlayerRepairEnabled() ? "ON" : "OFF", MirvFix_PlayerRepairWrites(),
	            MirvFix_PlayerRepairChanged());
	if (MirvFix_PlayerRepairEnabled() && !MirvFix_PlayerRepairWrites())
		ImGui::TextWrapped(
		    "No repair updates received yet. Replay the marked interval with the sampled player present.");
	if (MirvFix_PlayerRepairEnabled() && MirvFix_PlayerRepairWrites() && !MirvFix_PlayerRepairChanged())
		ImGui::TextWrapped("Replacements so far equal the original values. No numeric repair effect yet.");
	if (ImGui::Button("Compare original from before range"))
		ComparePlayerRepair(false);
	ImGui::SameLine();
	if (ImGui::Button("Compare repaired from before range"))
		ComparePlayerRepair(true);
	ImGui::TextWrapped("Changing marks or switching repair ON/OFF does not rebuild past movement. "
	                   "Compare again after each edit. Changed updates confirm numeric substitution, "
	                   "not the quality of the rendered result.");
	if (!ui.Status.empty())
		ImGui::TextWrapped("%s", ui.Status.c_str());
	DrawPlayerAngleDiagnostic(playhead);
	ImGui::EndTabItem();
}
static bool SceneCategoryCheckbox(const char *label, bool &value, int category, const char *hint)
{
	const auto rgb = advancedfx::SceneCache::CategoryColor(category);
	ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(rgb[0] / 255.f, rgb[1] / 255.f, rgb[2] / 255.f, 1));
	const bool changed = ImGui::Checkbox(label, &value);
	ImGui::PopStyleColor();
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", hint);
	return changed;
}

static constexpr const char *SceneCategoryNames[] = {"Players",   "Projectiles", "Weapons",
                                                     "Cosmetics", "Other",       "Ragdolls",
                                                     "Gibs",      "Buildings",   "Props / objectives"};
static void SceneCategoryFilters(std::array<bool, advancedfx::SceneCache::CategoryCount> &values,
                                 const char *hint)
{
	if (ImGui::BeginTable("Category filters", 3, ImGuiTableFlags_SizingStretchSame))
	{
		for (int i = 0; i < advancedfx::SceneCache::CategoryCount; ++i)
		{
			ImGui::TableNextColumn();
			SceneCategoryCheckbox(SceneCategoryNames[i], values[i], i, hint);
		}
		ImGui::EndTable();
	}
}
static void DrawSceneCacheMenu(int playhead)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	ImGui::Text("%s | %zu frames | %.2f MiB", g_SceneRecord ? "Recording / armed" : "Stopped",
	            g_SceneCache.Count(), g_SceneCache.Bytes() / 1048576.0);
	if (g_SceneCache.Count() > 1)
		ImGui::TextDisabled("Retained: %.2f seconds (gaps may exist)",
		                    g_SceneCache.Frames().back().DemoTime - g_SceneCache.Frames().front().DemoTime);
	if (ImGui::CollapsingHeader("Capture", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Checkbox("Experimental full renderable capture / replay", &g_SceneRenderSnapshot))
		{
			++g_SceneDrawEpoch;
			if (g_SceneRenderSnapshot) g_SceneDrawModels.store(true);
		}
		if (g_SceneRenderSnapshot)
			DrawOverlayHint("Capture all available registered models in one demo pass. Category/player filters are bypassed. Preview uses native textures, neutral lighting and attempts to hide live studio models. Capture suspends while previewing. Record fresh history first.");
		bool particles=g_SceneNativeParticlesEnabled;
		if(ImGui::Checkbox("Capture particles (TF2 x64 experiment)",&particles))SceneParticleCaptureEnabled(particles);
		if(particles)DrawOverlayHint("Captures particle appearance automatically. Historical native preview uses the setting in Preview; record fresh history.");
        if(ImGui::Checkbox("Capture legacy dissolve particles (experimental)",&g_SceneLegacyCapture)&&g_SceneLegacyCapture){
            g_SceneLegacyHistory.Clear();g_SceneLegacyIsolationFailed.Reset();g_SceneLegacyCaptureStatus="Not captured.";
        }
		if(g_SceneLegacyCapture&&ImGui::Checkbox("Include other exact simple emitters (experimental)",&g_SceneLegacyAll))g_SceneLegacyHistory.Clear();
        if(ImGui::Checkbox("Capture shadow projections and world decals (experimental)",&g_SceneSurfaceCapture)&&g_SceneSurfaceCapture){g_SceneSurfaces.Clear();Mirv_Tf2_ResetSurfaces();}
        ImGui::TextDisabled(g_SceneRenderSnapshot ? "Category settings apply only to diagnostic capture." : "Choose categories; checked colors match the preview.");
		if (ImGui::BeginTable("Scene capture categories", 2, ImGuiTableFlags_SizingStretchSame))
		{
			bool players = g_ScenePlayerModels;
			ImGui::TableNextColumn();
			if (SceneCategoryCheckbox("Player models", players, 0,
			                          "Full player bodies; defaults to all players. Off retains lightweight "
			                          "player bounds. Optional filtering also scopes held gear."))
				g_ScenePlayerModels = players;
			ImGui::TableNextColumn();
			SceneCategoryCheckbox("Projectiles", g_SceneProjectiles, 1,
			                      "Visible projectile models; no particle trails.");
			ImGui::TableNextColumn();
			SceneCategoryCheckbox(
			    "Weapons", g_SceneWeapons, 2,
			    "Held and dropped weapons. Loose items capture independently of player filtering.");
			ImGui::TableNextColumn();
			SceneCategoryCheckbox(
			    "Cosmetics", g_SceneCosmetics, 3,
			    "Worn and dropped cosmetics. Corpse-attached items require their host to be captured.");
			ImGui::TableNextColumn();
			SceneCategoryCheckbox("Ragdolls", g_SceneRagdolls, 5,
			                      "All visible ragdoll models; independent of player filtering.");
			ImGui::TableNextColumn();
			SceneCategoryCheckbox("Gibs", g_SceneGibs, 6,
			                      "Recognized model-based gibs; independent of player filtering.");
			ImGui::TableNextColumn();
			SceneCategoryCheckbox("Buildings", g_SceneBuildings, 7,
			                      "Sentries, dispensers, teleporters and sappers.");
			ImGui::TableNextColumn();
			SceneCategoryCheckbox("Props / objectives", g_SceneProps, 8,
			                      "Doors, payloads, intelligence and props, including brush surface meshes.");
			ImGui::EndTable();
		}
		if ((g_ScenePlayerModels || g_SceneWeapons || g_SceneCosmetics) &&
		    ImGui::TreeNode("Player filter (optional)"))
		{
			ImGui::Checkbox("Only selected players", &g_ScenePlayerFilter);
			ImGui::TextWrapped("Applies to player bodies and held gear. Other categories are independent. "
			                   "Selection belongs to the current demo.");
			if (g_ScenePlayerFilter)
			{
				static ImGuiTextFilter search;
				search.Draw("Find player");
				std::map<int, ScenePlayerSelection> shown;
				for (int index = 1; index <= Mirv_Tf2_GetMaxPlayers(); ++index)
				{
					int user;
					ScenePlayerSelection player;
					if (SceneResolvePlayer(index, user, player) && search.PassFilter(player.Name.c_str()))
						shown.emplace(user, std::move(player));
				}
				if (ImGui::Button("Select shown"))
					for (const auto &entry : shown)
						g_ScenePlayers[entry.first] = entry.second;
				ImGui::SameLine();
				if (ImGui::Button("Clear selection"))
					g_ScenePlayers.clear();
				ImGui::SameLine();
				if (ImGui::Button("Invert shown"))
					for (const auto &entry : shown)
						if (g_ScenePlayers.count(entry.first))
							g_ScenePlayers.erase(entry.first);
						else
							g_ScenePlayers.emplace(entry);
				if (ImGui::BeginChild("Scene player list", ImVec2(0, 150), true))
				{
					for (const auto &entry : shown)
					{
						ImGui::PushID(entry.first);
						bool selected = g_ScenePlayers.count(entry.first) != 0;
						std::string label =
						    entry.second.Name + " (" + std::to_string(entry.second.Index) + ")";
						if (ImGui::Checkbox(label.c_str(), &selected))
							if (selected)
								g_ScenePlayers[entry.first] = entry.second;
							else
								g_ScenePlayers.erase(entry.first);
						ImGui::PopID();
					}
				}
				ImGui::EndChild();
				ImGui::Text("%zu selected", g_ScenePlayers.size());
				if (g_ScenePlayers.empty())
					ImGui::TextDisabled(
					    "No player bodies or held gear will be captured until you select players.");
			}
			ImGui::TreePop();
		}
		ImGui::Checkbox("Capture tick range", &g_SceneRangeEnabled);
		if (g_SceneRangeEnabled)
		{
			ImGui::InputInt("Capture start tick", &g_SceneRangeFirst);
			ImGui::SameLine();
			if (ImGui::Button("Start at playhead"))
				g_SceneRangeFirst = playhead;
			ImGui::InputInt("Capture end tick", &g_SceneRangeLast);
			ImGui::SameLine();
			if (ImGui::Button("End at playhead"))
				g_SceneRangeLast = playhead;
			const bool validRange = g_SceneRangeFirst >= 0 && g_SceneRangeLast >= g_SceneRangeFirst;

			if (!validRange)
				ImGui::TextDisabled("Start must be nonnegative; end must be at or after start.");
			ImGui::TextWrapped("Play from before the range. Capture starts at its first observed tick and "
			                   "stops automatically at/past its end. This does not seek or start the demo.");
		}
		const bool validRange =
		    !g_SceneRangeEnabled || (g_SceneRangeFirst >= 0 && g_SceneRangeLast >= g_SceneRangeFirst);
		ImGui::BeginDisabled(!g_SceneRecord && !validRange);
		if (ImGui::Button(g_SceneRecord ? "Stop recording" : "Start recording"))
			g_SceneRecord = !g_SceneRecord;
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Clear history"))
			SceneClearHistory();
		DrawOverlayHint("Play the actual demo forward to capture. Settings affect future frames.");
	}
	if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawOverlayHint("Scrub cached history with the playhead or Right Ctrl + arrows / Home / End.");
		if (g_SceneRenderSnapshot && ImGui::Checkbox("Suppress live studio models (experimental)", &g_SceneSuppressLiveStudio))
			++g_SceneDrawEpoch;
		if (ImGui::Checkbox("Smooth cached animation", &g_SceneSmoothAnimation))
			SceneCacheFrame(false);
		bool models = g_SceneDrawModels.load();
		if (ImGui::Checkbox("Draw cached models", &models))
		{
			g_SceneDrawModels.store(models);
			++g_SceneDrawEpoch;
			SceneModelStatus(models ? "Cached model drawing enabled." : "Cached model drawing disabled.");
		}

		int particleMode=g_SceneNativeParticleReplay?1:g_SceneNativeParticleMarkers?(g_SceneNativeParticleStyled?2:3):0;
		const char *particleModes[]={"Off","Native textured (experimental)","Styled diagnostic markers","Uniform diagnostic markers"};
		if(ImGui::Combo("Cached particles",&particleMode,particleModes,4))SceneParticlePreviewMode(particleMode);
		if(particleMode==1) {
		    DrawOverlayHint("Native particle preview requires a paused or frozen demo (demo_timescale 0) and immediate rendering.");
		    ImGui::Checkbox("Replay legacy dissolve particles (experimental)",&g_SceneLegacyReplay);
            ImGui::Checkbox("Queued legacy anchored-page overlay (experimental)",&g_SceneLegacyQueued);
            if(g_SceneLegacyReplay)DrawOverlayHint(g_SceneLegacyReplayResult.Status.c_str());
		    ImGui::Checkbox("Suppress live particles (experimental)",&g_SceneSuppressLiveParticles);
		    if(g_SceneSuppressLiveParticles) {
		        DrawOverlayHint("Temporarily disables live particle drawing during covered native preview frames. Does not stop simulation; some effects may be missing. Disable to compare.");
		        DrawOverlayHint(g_SceneParticleIsolationStatus.c_str());
		    }
		    if(ImGui::Button("Use immediate rendering (mat_queue_mode 0)"))Afx_ExecClientCmd("mat_queue_mode 0");
		    if(ImGui::IsItemHovered())ImGui::SetTooltip("Changes the engine's global rendering setting. Restore your previous mat_queue_mode after testing.");
		    if(!g_SceneParticleReplayResult.Status.empty())DrawOverlayHint(g_SceneParticleReplayResult.Status.c_str());
		    ImGui::Text("Native particle tick %d: %zu particles / %zu drawable collections; %zu rejected attempts",
		        g_SceneParticleReplayTick,g_SceneParticleReplayResult.Particles,g_SceneParticleReplayResult.Nodes,g_SceneParticleReplayResult.Rejected);
		}
        ImGui::Checkbox("Legacy dissolve diagnostic markers",&g_SceneLegacyMarkers);
        ImGui::Checkbox("Replay captured model color and alpha (experimental)",&g_SceneReplayModulation);
        ImGui::Checkbox("Capture native model lighting (TF2 x64 experimental)",&g_SceneLightingCapture);
        ImGui::Checkbox("Replay captured model lighting (experimental)",&g_SceneReplayLighting);
        if(g_SceneLightingCapture||g_SceneReplayLighting)DrawOverlayHint(g_SceneLightingStatus.c_str());
        ImGui::Checkbox("Hide live shadows (temporary)",&g_SceneHideShadows);
        ImGui::Checkbox("Hide live world decals (temporary)",&g_SceneHideDecals);
        ImGui::Checkbox("Historical projected blob shadows (experimental)",&g_SceneHistoricalShadows);
        ImGui::Checkbox("Historical world decal visibility (experimental)",&g_SceneHistoricalDecals);
        if(g_SceneHistoricalDecals){
            ImGui::Checkbox("Draw removed world decals from captured geometry (experimental)",&g_SceneRemovedDecals);
            ImGui::Checkbox("Replace surviving world decal opacity (experimental)",&g_SceneDecalOpacity);
            ImGui::Checkbox("Queued ordinary world decals (experimental)",&g_SceneQueuedDecals);
        }
        if(g_SceneHistoricalShadows||g_SceneHistoricalDecals)DrawOverlayHint(g_SceneHistoricalSurfaceStatus.c_str());
        if(g_SceneHideShadows||g_SceneHideDecals)DrawOverlayHint("Isolation only; historical shadows and decals are not reconstructed.");
		ImGui::Checkbox("Draw cached bounds", &g_SceneDrawing.Bounds);
		if (g_SceneDrawing.Bounds && ImGui::TreeNode("Bounding box filters"))
		{
			ImGui::PushID("Cached bounds filters");
			if (ImGui::Button("All categories"))
				g_SceneDrawing.BoundCategories.fill(true);
			ImGui::SameLine();
			if (ImGui::Button("No categories"))
				g_SceneDrawing.BoundCategories.fill(false);
			SceneCategoryFilters(g_SceneDrawing.BoundCategories, "Show bounds for this category.");
			ImGui::Checkbox("Only entities without cached meshes", &g_SceneDrawing.BoundsMissingOnly);
			ImGui::Checkbox("Only selected cached entities", &g_SceneDrawing.BoundsSelectedOnly);
			if (g_SceneDrawing.BoundsSelectedOnly)
			{
				static ImGuiTextFilter search;
				search.Draw("Find cached entity");
				if (ImGui::Button("Clear entity selection"))
					g_SceneDrawing.BoundEntities.clear();
				if (auto view = SceneCacheView())
				{
					if (ImGui::BeginChild("Bounds entity list", ImVec2(0, 180), true))
						for (const auto &entity : view->Entities)
						{
							if (!g_SceneDrawing.BoundCategories[entity.Category] ||
							    (g_SceneDrawing.BoundsMissingOnly && entity.ModelPose))
								continue;
							const std::string id = std::to_string(entity.Identity);
							std::string label = std::string(SceneCategoryNames[entity.Category]) +
							                    (entity.Category == 0 ? " | user " : " | entity ") +
							                    std::to_string(entity.Category == 0 ? entity.Identity
							                                                        : entity.Identity & 4095);
							if (entity.ModelPose)
								label += " | " + entity.ModelPose->ModelInfo->Name;
							if (!search.PassFilter(label.c_str()))
								continue;
							ImGui::PushID(id.c_str());
							bool selected = g_SceneDrawing.BoundEntities.count(entity.Identity) != 0;
							if (ImGui::Checkbox(label.c_str(), &selected))
								if (selected)
									g_SceneDrawing.BoundEntities.insert(entity.Identity);
								else
									g_SceneDrawing.BoundEntities.erase(entity.Identity);
							if (ImGui::IsItemHovered())
							{
								const auto corners = advancedfx::SceneCache::BoundsCorners(entity);
								ImGui::SetTooltip("Stable identity: %s\nBounds center: %.1f %.1f %.1f",
								                  id.c_str(), (corners[0][0] + corners[7][0]) * .5,
								                  (corners[0][1] + corners[7][1]) * .5,
								                  (corners[0][2] + corners[7][2]) * .5);
							}
							ImGui::PopID();
						}
					ImGui::EndChild();
				}
				else
					ImGui::TextDisabled("Enable cached preview to list entities at the cursor.");
				ImGui::Text("%zu selected", g_SceneDrawing.BoundEntities.size());
				ImGui::TextWrapped(
				    "Selections belong to this cache and clear with its history. Bounds do not restore "
					"enabled next session while this individual filter is in use.");
			}
			ImGui::PopID();
			ImGui::TreePop();
		}

		ImGui::Checkbox("Movement trails", &g_SceneDrawing.Trails);
		if (g_SceneDrawing.Trails)
		{
			double seconds = g_SceneDrawing.TrailSeconds;
			if (ImGui::InputDouble("Trail duration (seconds)", &seconds, 0, 0, "%.2f") &&
			    std::isfinite(seconds) && seconds >= .05 && seconds <= 30)
				g_SceneDrawing.TrailSeconds = seconds;
			if (ImGui::TreeNode("Trail categories"))
			{
				ImGui::PushID("Scene trail categories");
				if (ImGui::Button("All"))
					g_SceneDrawing.TrailCategories.fill(true);
				ImGui::SameLine();
				if (ImGui::Button("None"))
					g_SceneDrawing.TrailCategories.fill(false);
				SceneCategoryFilters(g_SceneDrawing.TrailCategories,
				                     "Draw retained movement for this category.");
				ImGui::PopID();
				ImGui::TreePop();
			}
			ImGui::TextWrapped("Trails show the retained movement before the cached cursor and fade with "
			                   "age. Missing samples, teleports and identity changes break the trail.");
		}
		ImGui::TextDisabled(g_SceneRenderSnapshot ? "Experimental: live studio and particle suppression are partial; brush and other effects stay live." : "Visual preview only; live models remain visible.");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Brush wireframes require readable BSP geometry. Bounds are independent of "
			                  "Show path.\nParticle snapshots are optional; audio and full simulation are not cached.");

		if (g_ScenePreview && !SceneCacheView())
			ImGui::TextDisabled("No recorded coverage at this playhead.");
	}
	if (ImGui::CollapsingHeader("Memory"))
	{
		int budget = int(g_SceneCache.Limit() / 1048576);
		if (ImGui::InputInt("Scene budget MiB", &budget))
		{
			budget = std::clamp(budget, 1, 1024);
			g_SceneCache.SetLimit(size_t(budget) * 1048576);
			SceneCacheFrame(false);
		}
		ImGui::TextWrapped(
		    "Oldest frames are evicted at the budget. More categories and longer captures need more memory.");
		int particleBudget=int(g_SceneNativeParticles.BudgetBytes()/1048576);
		if(ImGui::InputInt("Particle budget MiB",&particleBudget)) {
		    particleBudget=std::clamp(particleBudget,64,2048);
		    g_SceneNativeParticles.SetBudget(std::size_t(particleBudget)*1048576);
		}
		ImGui::Text("Particles: %.2f MiB / %zu frames; %zu evicted",g_SceneNativeParticles.Bytes/1048576.0,g_SceneNativeParticles.Frames.size(),g_SceneNativeParticles.Evicted);
		const double observedSpan = g_SceneCaptureLastTime - g_SceneCaptureFirstTime;
		if (g_SceneCaptureFrames > 1 && observedSpan > 0)
		{
			const double growth = g_SceneCache.AllocatedBytes() / (observedSpan + SceneTickInterval());
			if (growth > 0)
			{
				ImGui::Text("Observed growth: %.2f MiB/s | budget fits roughly %.1f seconds",
				            growth / 1048576.0, g_SceneCache.Limit() / growth);
				if (g_SceneRangeEnabled && g_SceneRangeFirst >= 0 && g_SceneRangeLast >= g_SceneRangeFirst)
					ImGui::Text("Range payload estimate: %.2f MiB",
					            (double(g_SceneRangeLast) - g_SceneRangeFirst + 1) * SceneTickInterval() *
					                growth / 1048576.0);
				DrawOverlayHint("Estimate from observed capture; scene activity changes memory needs.");
			}
		}
		else
			ImGui::TextDisabled("Record a short sample to estimate memory for longer captures.");
	}
	if (ImGui::CollapsingHeader("Diagnostics"))
	{
		ImGui::Text("Pose captures: %llu | Unavailable: %llu", g_ScenePoseCaptured, g_ScenePoseFailures);
		if (g_SceneRenderSnapshot) DrawOverlayHint(g_SceneAuditStatus.c_str());
		ImGui::Text("Capture ms: last %.3f | mean %.3f | peak %.3f (%llu frames)", g_SceneCaptureMs,
		            g_SceneCaptureFrames ? g_SceneCaptureTotalMs / g_SceneCaptureFrames : 0,
		            g_SceneCapturePeakMs, g_SceneCaptureFrames);
		if (auto view = SceneCacheView())
			ImGui::Text("Recorded tracking cameras at cursor: %zu", view->Cameras.size());
		ImGui::TextWrapped("Tracking cameras must exist before recording. Camera setting edits invalidate "
						   "their old samples; renaming cameras and editing clips do not.");
		ImGui::Text("Unique retained poses: %zu | Unchanged poses reused: %zu", g_SceneCache.PoseCount(),
		            g_SceneCache.ReusedPoses());
		unsigned pending = 0, peak = 0;
		Mirv_Tf2_SceneRenderStatus(pending, peak);
		ImGui::Text("Pending cached draws: %u (peak %u)", pending, peak);
		if (!SceneCanDrawModels())
			ImGui::TextDisabled("Cached model drawing suspended during focus/device recovery.");
		if (ImGui::TreeNode("Animation diagnostics"))
		{
			ImGui::Text("Cached tick %.3f", g_SceneCursor / SceneTickInterval());
			if (auto view = SceneCacheView())
			{
				for (const auto &entity : view->Entities)
					ImGui::TextWrapped("%s", SceneAnimationSummary(entity).c_str());
			}
			else
				ImGui::TextDisabled("Enable cached preview at a recorded interval.");
			ImGui::TreePop();
		}
		ImGui::TextWrapped("%s", g_ScenePoseStatus.c_str());
		if (g_ScenePreview)
			if (auto status = std::atomic_load(&g_SceneModelStatus))
				ImGui::TextWrapped("%s", status->c_str());
	}
	ImGui::TextDisabled("Console: mirv_cache help");
}

static int g_CinematicFileAction = 0;
static std::string g_CinematicFileStatus;
static std::string g_CinematicBakeSource;
static int g_CinematicBakeBegin = 0, g_CinematicBakeEnd = 100;
static double g_CinematicBakeStep = 1.0 / 66;
// Bake ranges follow recorded tick/client pairs; a paused or shifted client clock need not have a constant
// offset.
static bool CinematicClock(double value, bool clientToDemo, double &mapped, bool useCache = true)
{
	if (!std::isfinite(value))
		return false;
	const auto &frames = g_SceneCache.Frames();
	if (useCache && !frames.empty())
	{
		const advancedfx::SceneCache::Frame *nearest = &frames.front();
		auto x = [&](const auto &f) { return clientToDemo ? f.ClientTime : f.DemoTime; };
		auto y = [&](const auto &f) { return clientToDemo ? f.DemoTime : f.ClientTime; };
		for (size_t i = 0; i < frames.size(); ++i)
		{
			const auto &a = frames[i];
			if (std::abs(x(a) - value) < 1e-9)
			{
				mapped = y(a);
				return true;
			}
			if (std::abs(x(a) - value) < std::abs(x(*nearest) - value))
				nearest = &a;
			if (i + 1 < frames.size())
			{
				const auto &b = frames[i + 1];
				if (x(a) < value && value < x(b))
				{
					mapped = y(a) + (value - x(a)) / (x(b) - x(a)) * (y(b) - y(a));
					return std::isfinite(mapped);
				}
			}
		}
		mapped = y(*nearest) + value - x(*nearest);
		return std::isfinite(mapped);
	}
	double demo = 0;
	if (!GetCurrentDemoTime(demo))
		return false;
	const double now = g_MirvTime.GetTime();
	mapped = clientToDemo ? value - now + demo : value + now - demo;
	return std::isfinite(mapped);
}
static void DrawCinematicFiles()
{
	static ImGui::FileBrowser dialog(ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_EditPathString |
									 ImGuiFileBrowserFlags_EnterNewFilename |
									 ImGuiFileBrowserFlags_CreateNewDir);
	static int action = 0;
	static std::string source;
	static double begin = 0, end = 0, step = 0;
	if (g_CinematicFileAction)
	{
		action = g_CinematicFileAction;
		g_CinematicFileAction = 0;
		dialog.SetTitle(action == 1							  ? "Save cinematic project"
						: action == 2						  ? "Open cinematic project"
						: g_CinematicBakeSource == "sequence" ? "Bake camera sequence"
															  : "Bake selected camera");
		dialog.SetTypeFilters({action == 3 ? ".xml" : ".hlae"});
		dialog.SetInputName(
			action == 3 ? (g_CinematicBakeSource == "sequence" ? "baked-sequence.xml" : "baked-camera.xml")
						: "cinematic.hlae");
		if (action == 3)
		{
			double demo = 0;
			bool mapped = GetCurrentDemoTime(demo);
			double now = EditingClientTime(g_MirvTime.GetTime());
			if (SceneCachePreviewActive())
			{
				demo = g_SceneCursor;
				mapped = !g_SceneCache.Frames().empty();
			}
			const double interval = SceneTickInterval();
			if (!mapped || interval <= 0 || g_CinematicBakeBegin < 0 || g_CinematicBakeBegin >= g_CinematicBakeEnd)
			{
				g_DocumentStatus = "Choose a valid demo tick range before baking.";
				action = 0;
				return;
			}
			if (!CinematicClock(g_CinematicBakeBegin * interval, false, begin) ||
				!CinematicClock(g_CinematicBakeEnd * interval, false, end))
			{
				g_DocumentStatus = "Bake tick/client mapping unavailable.";
				action = 0;
				return;
			}
			step = g_CinematicBakeStep;
			source = g_CinematicBakeSource;
		}
		dialog.Open();
	}
	dialog.Display();
	if (dialog.HasSelected())
	{
		auto file = dialog.GetSelected().wstring();
		bool ok = action == 1	? MirvCam_SaveProject(file.c_str())
				  : action == 2 ? MirvCam_LoadProject(file.c_str())
								: MirvCam_Bake(file.c_str(), source.c_str(), begin, end, step);
		g_DocumentStatus = ok ? (action == 2 ? "Cinematic project loaded; playback and commands are disabled."
											 : "Cinematic file saved.")
							  : "Cinematic operation failed; check the file, range, source and cached "
								"coverage. Custom aim calculations cannot be saved.";
		if (action == 3)
			g_DocumentStatus = MirvCam_BakeStatus();
		g_CinematicFileStatus = g_DocumentStatus;
		dialog.ClearSelected();
	}
}
static void DrawCinematicFileButtons()
{
	if (ImGui::Button("Save cinematic project..."))
		g_CinematicFileAction = 1;
	ImGui::SameLine();
	if (ImGui::Button("Open cinematic project..."))
		g_CinematicFileAction = 2;
	if (!g_CinematicFileStatus.empty())
		ImGui::TextWrapped("%s", g_CinematicFileStatus.c_str());
}
static void DrawCinematicBakeControls(const std::string &source)
{
	const bool sequence = source == "sequence";
	if (!ImGui::CollapsingHeader(sequence ? "Export sequence as classic campath"
										  : "Export selected camera as classic campath"))
		return;
	// Each source keeps its own range, including when both export panels are open.
	struct BakeSettings
	{
		int Begin = 0, End = 100;
		double Step = 1.0 / 66;
		bool Initialized = false, CustomRange = false;
		double SourceBegin = 0, SourceEnd = 0;
	};
	static std::map<std::string, BakeSettings> settings;
	auto &values = settings[source];
	ImGui::PushID(source.c_str());
	const auto clips = MirvCam_Clips();
	double sourceBegin = 0, sourceEnd = 0;
	if (sequence && !clips.empty())
	{
		sourceBegin = (std::min)(clips.front().Time, clips.size() > 1
			? clips[1].TransitionStart() : clips.front().Time);
		sourceEnd = (std::max)(clips.back().Time, clips.back().TransitionStart() + clips.back().Blend);
		if (clips.back().Campath)
		{
			auto &paths = g_Hook_VClient_RenderView.m_CamPaths;
			for (size_t i = 0; i < paths.GetCount(); ++i)
				if (paths.GetId(i) == clips.back().Target)
					if (auto path = paths.GetPath(i))
						if (path->GetSize())
							sourceEnd = (std::max)(sourceEnd, path->GetOffset() + path->GetUpperBound());
		}
  for (const auto &m : MirvCam_SequenceModifiers())
  {sourceBegin = (std::min)(sourceBegin, m.Begin); sourceEnd = (std::max)(sourceEnd, m.End);}
	}
	auto range = [&]
	{
		double demo = 0;
		bool mapped = GetCurrentDemoTime(demo);
		double now = EditingClientTime(g_MirvTime.GetTime());
		if (SceneCachePreviewActive())
		{
			demo = g_SceneCursor;
			mapped = !g_SceneCache.Frames().empty();
		}
		const double interval = SceneTickInterval();
		if (!mapped || interval <= 0)
			return;
		double first = now, last = now + 1;
		if (sequence && !clips.empty())
		{
			first = sourceBegin;
			last = (std::max)(sourceEnd, first + interval);
		}
		else if (!g_SceneCache.Frames().empty())
		{
			first = g_SceneCache.Frames().front().ClientTime;
			last = g_SceneCache.Frames().back().ClientTime;
		}
		double firstDemo = 0, lastDemo = 0;
		if (!CinematicClock(first, true, firstDemo) || !CinematicClock(last, true, lastDemo))
			return;
		values.Begin = (std::max)(0, int(std::floor(firstDemo / interval)));
		values.End = (std::max)(values.Begin + 1, int(std::ceil(lastDemo / interval)));
		values.Step = interval;
		values.SourceBegin = sourceBegin;
		values.SourceEnd = sourceEnd;
	};
	if (!values.Initialized)
	{
		values.Initialized = true;
		range();
	}
	else if (sequence && !values.CustomRange &&
			 (values.SourceBegin != sourceBegin || values.SourceEnd != sourceEnd))
	{
		const double step = values.Step;
		range();
		values.Step = step;
	}
	if (ImGui::Button(sequence ? "Use sequence range" : "Use cached range"))
	{
		values.CustomRange = false;
		range();
	}
	ImGui::TextWrapped("%s", sequence
								 ? "Source: whole camera sequence, including transitions."
								 : "Source: selected camera only, without sequence clips or transitions.");
	values.CustomRange |= ImGui::InputInt("Bake start tick", &values.Begin);
	values.CustomRange |= ImGui::InputInt("Bake end tick", &values.End);
	ImGui::InputDouble("Bake step (seconds)", &values.Step, 0, 0, "%.6f");
	ImGui::TextWrapped("Tracking uses recorded camera samples when available; otherwise cached targeting is "
					   "unsmoothed. Hard cuts use one-microsecond transitions. Export does not include "
					   "commands or procedural bindings.");
	if (ImGui::Button(sequence ? "Bake sequence..." : "Bake selected camera..."))
	{
		g_CinematicBakeBegin = values.Begin;
		g_CinematicBakeEnd = values.End;
		g_CinematicBakeStep = values.Step;
		g_CinematicBakeSource = source;
		g_CinematicFileAction = 3;
	}
	ImGui::PopID();
}
static void DrawCommandFiles()
{
	auto &state = g_SequencerTimeline;
	static ImGui::FileBrowser loadDialog(ImGuiFileBrowserFlags_CloseOnEsc |
	                                     ImGuiFileBrowserFlags_EditPathString);
	static ImGui::FileBrowser saveDialog(
	    ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_EditPathString |
	    ImGuiFileBrowserFlags_EnterNewFilename | ImGuiFileBrowserFlags_CreateNewDir);
		if (ImGui::Button("Load command track"))
		{
			loadDialog.SetTitle("Load command track");
			loadDialog.SetTypeFilters({".xml"});
			loadDialog.Open();
		}
		ImGui::SameLine();
		if (ImGui::Button("Save command track"))
		{
			saveDialog.SetTitle("Save command track");
			saveDialog.SetTypeFilters({".xml"});
			if (!state.Index.File.empty())
			{
				saveDialog.SetDirectory(state.Index.File.parent_path());
				saveDialog.SetInputName(state.Index.File.stem().u8string() + ".commands.xml");
			}
			else if (!state.CommandsFile.empty())
				saveDialog.SetInputName(state.CommandsFile.filename().u8string());
			saveDialog.Open();
		}
		loadDialog.Display();
		saveDialog.Display();
		if (loadDialog.HasSelected())
		{
			auto file = loadDialog.GetSelected();
			bool ok = g_CommandSystem.LoadTickMarkers(file.wstring().c_str());
			state.Status = ok ? "Command track loaded; execution is disabled."
			                  : "Could not load command track; execution is disabled.";
			if (ok)
			{
				state.CommandsFile = file;
				state.SelectedCommand = 0;
				CancelCommandMarkerDrag();
			}
			loadDialog.ClearSelected();
		}
		if (saveDialog.HasSelected())
		{
			auto file = saveDialog.GetSelected();
			bool ok = g_CommandSystem.SaveTickMarkers(file.wstring().c_str());
			state.Status = ok ? "Command track saved." : "Could not save command track.";
			if (ok)
				state.CommandsFile = file;
			saveDialog.ClearSelected();
		}
}
static void DrawCameraClipsPanel();
static void DrawSequencerDetails(int playhead)
{
	auto &state = g_SequencerTimeline;
	static ImGui::FileBrowser indexDialog(ImGuiFileBrowserFlags_CloseOnEsc |
										  ImGuiFileBrowserFlags_EditPathString);
	if (!ImGui::BeginTabBar("Sequencer details"))
		return;
	g_CameraClipsEditing =
		ImGui::BeginTabItem("Camera clips", nullptr, g_OpenCameraClips ? ImGuiTabItemFlags_SetSelected : 0);
	if (g_CameraClipsEditing)
	{
		g_OpenCameraClips = false;
		DrawCameraClipsPanel();
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Scene Cache"))
	{
		DrawSceneCacheMenu(playhead);
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Commands", nullptr, state.FocusCommands ? ImGuiTabItemFlags_SetSelected : 0))
	{
		state.FocusCommands = false;
		bool enabled = g_CommandSystem.TickMarkersEnabled();
		ImGui::BeginDisabled(!g_VEngineClient || !g_VEngineClient->IsPlayingDemo() ||
							 !MirvCampath_CommandSeekObserverReady());
		if (ImGui::Checkbox("Enable command execution", &enabled))
			g_CommandSystem.EnableTickMarkers(enabled);
		ImGui::EndDisabled();
		if (!MirvCampath_CommandSeekObserverReady())
			ImGui::TextDisabled("Automatic commands require the TF2/TF2C native seek observer.");
		if (!g_CommandSystem.Enabled)
			ImGui::TextDisabled("The global mirv_cmd scheduler is disabled.");
		if (ImGui::Button("Add command at playhead"))
		{
			state.SelectedCommand = g_CommandSystem.AddTickMarker((std::max)(0, playhead), "Command", "");
			if (!state.SelectedCommand)
				state.Status = "Cannot add another command marker.";
		}

		const auto &markers = g_CommandSystem.GetTickMarkers();
		if (ImGui::BeginChild("Command marker list", ImVec2(0, 85), true))
		{
			ImGuiListClipper clipper;
			clipper.Begin(int(markers.size()));
			while (clipper.Step())
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
				{
					const auto &marker = markers[size_t(i)];
					ImGui::PushID(int(marker.Id));
					std::string label = std::to_string(marker.Tick) + ": " + marker.Name +
					                    (marker.Enabled ? "" : " [disabled]");
					if (ImGui::Selectable(label.c_str(), state.SelectedCommand == marker.Id))
						state.SelectedCommand = marker.Id;
					ImGui::PopID();
				}
		}
		ImGui::EndChild();
		auto selected = std::find_if(markers.begin(), markers.end(), [&](const CommandSystem::TickMarker &m) {
			return m.Id == state.SelectedCommand;
		});
		if (selected != markers.end())
		{
			if (state.DraftId != selected->Id ||
			    state.DraftRevision != g_CommandSystem.GetTickMarkerRevision())
			{
				state.Draft = *selected;
				state.DraftId = selected->Id;
				state.DraftRevision = g_CommandSystem.GetTickMarkerRevision();
				std::snprintf(state.CommandName, sizeof(state.CommandName), "%s", selected->Name.c_str());
				std::snprintf(state.CommandBody, sizeof(state.CommandBody), "%s", selected->Command.c_str());
			}
			ImGui::InputText("Command name", state.CommandName, sizeof(state.CommandName));
			ImGui::InputInt("Command tick", &state.Draft.Tick);
			ImGui::SameLine();
			if (ImGui::Button("Use playhead"))
				state.Draft.Tick = playhead;
			ImGui::Checkbox("Marker enabled", &state.Draft.Enabled);
			ImGui::SameLine();
			ImGui::Checkbox("Run on seek", &state.Draft.RunOnSeek);
			ImGui::InputTextMultiline("Command text", state.CommandBody, sizeof(state.CommandBody),
			                          ImVec2(-1, 90));
			if (ImGui::Button("Apply command edits"))
			{
				state.Draft.Name = state.CommandName;
				state.Draft.Command = state.CommandBody;
				state.Status = g_CommandSystem.SetTickMarker(state.Draft)
				                   ? "Command updated; no command executed."
				                   : "Invalid command tick or track size.";
			}
			ImGui::SameLine();
			if (ImGui::Button("Discard command edits"))
				state.DraftRevision = ~0ull;
			ImGui::SameLine();
			if (ImGui::Button("Duplicate command"))
			{
				auto copy = state.Draft;
				copy.Name = state.CommandName;
				copy.Command = state.CommandBody;
				copy.Id = g_CommandSystem.AddTickMarker(copy.Tick, copy.Name, copy.Command);
				if (copy.Id)
				{
					g_CommandSystem.SetTickMarker(copy);
					state.SelectedCommand = copy.Id;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Delete command"))
			{
				g_CommandSystem.RemoveTickMarker(state.SelectedCommand);
				state.SelectedCommand = 0;
			}
			ImGui::BeginDisabled(!g_VEngineClient || !g_VEngineClient->IsPlayingDemo());
			if (ImGui::Button("Run command now"))
				g_CommandSystem.RunTickMarker(state.SelectedCommand);
			ImGui::SameLine();
			if (ImGui::Button("Seek to command"))
				Afx_GotoDemoTick(state.Draft.Tick);
			ImGui::EndDisabled();
		}
		ImGui::TextWrapped("Playback executes crossed markers once, in tick order. Editing never runs a "
		                   "command. Run on seek reapplies enabled flagged markers up to the destination, in "
		                   "order; include initial/reset settings. Other crossed commands are skipped. "
		                   "Loading a track or starting another demo disables automatic execution.");
		if (!state.Status.empty())
			ImGui::TextWrapped("%s", state.Status.c_str());
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Events", nullptr, state.FocusEvents ? ImGuiTabItemFlags_SetSelected : 0))
	{
		state.FocusEvents = false;
		if (ImGui::Button("Index demo events"))
		{
			indexDialog.SetTitle("Index demo events");
			indexDialog.SetTypeFilters({".dem"});
			if (!g_OverlayPaths.demoDir.empty())
				indexDialog.SetDirectory(g_OverlayPaths.demoDir);
			indexDialog.Open();
		}
		if (state.Worker.valid())
		{
			ImGui::SameLine();
			if (ImGui::Button("Cancel indexing"))
			{
				state.Pending.clear();
				state.Cancel->store(true);
			}
			ImGui::ProgressBar(state.Progress->load());
		}
		indexDialog.Display();
		if (indexDialog.HasSelected())
		{
			StartTimelineIndex(indexDialog.GetSelected());
			indexDialog.ClearSelected();
		}
		if (!state.Index.File.empty())
			ImGui::TextWrapped("Demo: %s", state.Index.File.u8string().c_str());
		else
			ImGui::TextDisabled("No demo indexed. Open a demo or choose Index demo events.");
		if (!state.Index.Error.empty())
			ImGui::TextWrapped("Index failed: %s", state.Index.Error.c_str());
		ImGui::Checkbox("Automatically index opened demos", &state.AutoIndex);
		ImGui::SetNextItemWidth(140);
		ImGui::InputInt("Event lead-in (ticks)", &state.EventLeadTicks);
		state.EventLeadTicks = (std::max)(0, state.EventLeadTicks);
		if (!state.Status.empty())
			ImGui::TextWrapped("%s", state.Status.c_str());
		const char *labels[] = {"Deaths / kills", "Rounds", "Objectives", "Players"};

		for (int i = 0; i < 4; ++i)
		{
			if (i)
				ImGui::SameLine();
			state.FilterDirty |= ImGui::Checkbox(labels[i], &state.Categories[i]);
		}
		state.FilterDirty |= ImGui::Checkbox("Recorder deaths only", &state.RecorderOnly);
		state.FilterDirty |=
		    ImGui::InputText("Find event / player / weapon", state.Search, sizeof(state.Search));
		FilterTimelineEvents();
		ImGui::Text("%zu matching events", state.Filtered.size());
		int reference = state.SelectedTick >= 0 ? state.SelectedTick : playhead;
		if (ImGui::Button("Previous event"))
			for (auto i = state.Filtered.rbegin(); i != state.Filtered.rend(); ++i)
				if (state.Index.Events[*i].Tick < reference)
				{
					state.SelectedTick = state.Index.Events[*i].Tick;
					break;
				}
		ImGui::SameLine();
		if (ImGui::Button("Next event"))
			for (auto i : state.Filtered)
				if (state.Index.Events[i].Tick > reference)
				{
					state.SelectedTick = state.Index.Events[i].Tick;
					break;
				}
		ImGui::SameLine();
		ImGui::BeginDisabled(state.SelectedTick < 0 || !TimelineMatchesDemo());
		if (ImGui::Button("Seek to event"))
			Afx_GotoDemoTick(EventSeekTick(state.SelectedTick));
		ImGui::EndDisabled();
		if (!state.Index.File.empty() && !TimelineMatchesDemo())
			DrawOverlayHint("Seeking requires a playing demo with the indexed map and tick count.");
		if (state.SelectedTick >= 0)
		{
			ImGui::Text("All events at tick %d (%.3f s)", state.SelectedTick,
			            state.SelectedTick * state.Index.Interval);
			auto first = std::lower_bound(
			    state.Index.Events.begin(), state.Index.Events.end(), state.SelectedTick,
			    [](const advancedfx::DemoTimeline::Event &e, int tick) { return e.Tick < tick; });
			for (auto i = first; i != state.Index.Events.end() && i->Tick == state.SelectedTick; ++i)
			{
				ImGui::PushID(int(i - state.Index.Events.begin()));
				ImGui::TextWrapped("%s%s", i->RecorderDeath ? "[Recorder died] " : "", i->Summary.c_str());
				if (ImGui::TreeNode(i->Name.c_str()))
				{
					for (const auto &field : i->Fields)
						ImGui::TextWrapped("%s: %s", field.first.c_str(), field.second.c_str());
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}
		ImGui::EndTabItem();
	}
	DrawPlayerRepairEditor(playhead);
	ImGui::EndTabBar();
}

static std::atomic<bool> g_PreviewHudDrawn{false};
void ResetPreviewHudFrame() { g_PreviewHudDrawn.store(false); }
void SetPreviewHud(bool value) {g_PreviewHudEnabled=value; if(!value)g_PreviewHudDrawn=false; if(ImGui::GetCurrentContext())ImGui::MarkIniSettingsDirty();}
bool GetPreviewHud() { return g_PreviewHudEnabled; }
bool PreviewHudVisible() { return g_PreviewHudDrawn.load() && !Recording::IsActive(); }
static void DrawPreviewHud()
{
	g_PreviewHudDrawn = false;
	if (!g_PreviewHudEnabled || Recording::IsActive() ||
		(!MirvCampathPreview_IsActive() && !MirvCam_RigPreviewActive()))
		return;
	std::string text = "PREVIEW | ";
	if (MirvCam_RigPreviewActive())
		text += MirvCam_PreviewLabel();
	else
	{
		auto &path = g_Hook_VClient_RenderView.m_CamPath;
		int left = -1, right = -1, index = 0;
		double time = MirvCampathPreview_GetTime();
		for (auto it = path.GetBegin(); it != path.GetEnd(); ++it, ++index)
		{
			if (std::abs(it.GetTime() - time) < 1e-6)
			{
				left = right = index;
				break;
			}
			if (it.GetTime() > time)
			{
				right = index;
				break;
			}
			left = index;
		}
		std::string keys = left < 0 ? "no key" : "#" + std::to_string(left);
		if (right >= 0 && right != left)
			keys += " -> #" + std::to_string(right);
		auto &paths = g_Hook_VClient_RenderView.m_CamPaths;
		std::string name =
			paths.GetActive() < paths.GetCount() ? paths.GetName(paths.GetActive()).substr(0, 24) : "Path";
		for (auto &c : name)
			if (c == '\n' || c == '\r')
				c = ' ';
		char label[160];
		snprintf(label, sizeof(label), "Keys | %s | %s | %.3fs", name.c_str(), keys.c_str(), time);
		text += label;
	}
	ImGui::SetNextWindowPos(ImVec2(8, ImGui::GetIO().DisplaySize.y - 8), ImGuiCond_Always, ImVec2(0, 1));
	ImGui::SetNextWindowBgAlpha(.65f);
	ImGui::Begin("##camera_preview_hud", nullptr,
				 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
					 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
					 ImGuiWindowFlags_NoDocking);
	ImGui::TextUnformatted(text.c_str());
	ImGui::End();
	g_PreviewHudDrawn = true;
}
void SetCameraGuides(bool enabled)
{
	g_ShowCameraGuides = enabled;
	g_CameraGuidesPublished.store(enabled);
	if (ImGui::GetCurrentContext())
		ImGui::MarkIniSettingsDirty();
}
void SetCameraFrustums(bool enabled)
{
	g_CameraFrustums = enabled;
	g_CameraFrustumsPublished.store(enabled);
	if (ImGui::GetCurrentContext())
		ImGui::MarkIniSettingsDirty();
}
bool CameraGuidesEnabled() { return g_CameraGuidesPublished.load(); }
bool CameraFrustumsEnabled() { return g_CameraFrustumsPublished.load(); }
std::uint64_t TransitionGuideClip() { return g_TransitionGuidePublished.load(); }
bool TransitionGuidesEnabled() { return CameraGuidesEnabled() && g_TransitionGuidesPublished.load(); }
static std::string CameraClipSourceName(bool campath, std::uint64_t target)
{
	if (campath)
	{
		auto &paths = g_Hook_VClient_RenderView.m_CamPaths;
		for (size_t i = 0; i < paths.GetCount(); ++i)
			if (paths.GetId(i) == target)
				return paths.GetName(i);
	}
	else
		for (const auto &camera : MirvCam_Cameras(false))
			if (camera.Id == target)
				return camera.Name;
	return std::string("Missing ") + (campath ? "campath #" : "camera #") + std::to_string(target);
}
static int AdjustmentTick(double seconds, double interval)
{
 if (!std::isfinite(seconds) || !std::isfinite(interval) || interval <= 0) return 0;
 return int(std::llround(std::clamp(seconds / interval, 0.0, double(INT_MAX - 1))));
}
// This row is intentionally separate from source and adjustment tracks.
// A point marker retains a usable hit area at every zoom level.
static void DrawWorkRangeTrack(int total)
{
	if (!ImGui::BeginNeoTimelineEx("Work range", nullptr, ImGuiNeoTimelineFlags_None)) return;
	static int edge = 0, startIn = -1, startOut = -1, pending = -1, mouseStart = 0;
	int mouse = 0;
	const bool haveMouse = ImGui::GetNeoSequencerMouseFrame(&mouse, ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	const bool editable = !WorkRangeBusy() && !g_LockKeyTimes;
	if (edge)
	{
		if (!editable || startIn != g_WorkIn || startOut != g_WorkOut || ImGui::IsKeyPressed(ImGuiKey_Escape))
			edge = 0;
		else if (ImGui::IsMouseReleased(0))
		{
			if (edge == 1) g_WorkIn = pending; else g_WorkOut = pending;
			edge = 0;
		}
		else if (!ImGui::IsMouseDown(0)) edge = 0;
		else if (haveMouse)
		{
			const auto base = edge == 1 ? startIn : startOut;
			const auto lo = edge == 2 && startIn >= 0 ? startIn + 1 : 0;
			const auto hi = edge == 1 && startOut >= 0 ? startOut - 1 : total;
			if (lo <= hi)
				pending = int(std::clamp(std::int64_t(base) + mouse - mouseStart,
					std::int64_t(lo), std::int64_t(hi)));
			ImGui::NeoCancelSelection();
		}
	}
	const int in = edge == 1 ? pending : g_WorkIn, out = edge == 2 ? pending : g_WorkOut;
	if (in >= 0 && out > in)
		ImGui::NeoTimelineSpan(in, out, IM_COL32(80,180,165,100), "Selected range", nullptr, true, false, -1, false, false);
	bool inHovered = false, outHovered = false;
	if (in >= 0)
		ImGui::NeoTimelineSpan(in, in, IM_COL32(100,235,170,255), "In (drag to edit)", &inHovered, false, false, -1, false, editable);
	if (out >= 0)
		ImGui::NeoTimelineSpan(out, out, IM_COL32(255,185,85,255), "Out (drag to edit)", &outHovered, false, false, -1, false, editable);
	if (!edge && editable && haveMouse && ImGui::IsMouseClicked(0) && (inHovered || outHovered))
	{
		edge = inHovered && (!outHovered || std::abs(mouse-in) <= std::abs(mouse-out)) ? 1 : 2;
		startIn = g_WorkIn; startOut = g_WorkOut; mouseStart = mouse;
		pending = edge == 1 ? startIn : startOut;
		ImGui::NeoCancelSelection();
	}
	ImGui::EndNeoTimeLine();
}

static void DrawWorkRangeControls(int playhead, int total)
{
	ImGui::PushID("sequencer_work_range");
	ImGui::BeginDisabled(WorkRangeBusy() || g_LockKeyTimes);
	if (ImGui::Button("Set in"))
	{
		g_WorkIn = std::clamp(playhead, 0, total);
		if (g_WorkOut <= g_WorkIn) g_WorkOut = -1;
	}
	ImGui::SameLine();
	if (ImGui::Button("Set out"))
	{
		g_WorkOut = std::clamp(playhead, 0, total);
		if (g_WorkIn >= g_WorkOut) g_WorkIn = -1;
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear range")) { g_WorkIn = g_WorkOut = -1; g_WorkStatus.clear(); }
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Range actions")) ImGui::OpenPopup("range_actions");
	if (ImGui::BeginPopup("range_actions"))
	{
		ImGui::BeginDisabled(WorkRangeBusy() || g_LockKeyTimes);
		ImGui::InputInt("In tick", &g_WorkIn);
		ImGui::InputInt("Out tick", &g_WorkOut);
		if (!WorkRangeBusy() && !g_LockKeyTimes)
		{
			g_WorkIn = std::clamp(g_WorkIn, -1, total);
			g_WorkOut = std::clamp(g_WorkOut, -1, total);
		}
		ImGui::EndDisabled();
		ImGui::TextWrapped("Set In/Out at the playhead or drag the work-range markers. Video records from In to before Out.");
		ImGui::BeginDisabled(!WorkRangeValid() || WorkRangeBusy() || Recording::IsActive());
		if (ImGui::Button("Seek actual demo to In")) Afx_GotoDemoTick(g_WorkIn);
		ImGui::BeginDisabled(g_ScenePreview);
		if (ImGui::Button("Play to Out")) StartWorkTransport(false);
		if (ImGui::Button("Record In to Out")) StartWorkTransport(true);
		ImGui::EndDisabled();
		DrawOverlayHint("Actual demo: seek to In or earlier, then play/record. Playback resumes at normal speed and pauses at Out.");
		ImGui::BeginDisabled(g_SceneRecord);
		if (ImGui::Button("Use range for cache capture"))
		{
			g_SceneRangeFirst = g_WorkIn; g_SceneRangeLast = g_WorkOut; g_SceneRangeEnabled = true;
			g_WorkStatus = "Cache capture range set (includes Out); arm capture in Scene Cache.";
		}
		ImGui::EndDisabled();
		ImGui::BeginDisabled(!g_ScenePreview);
		if (ImGui::Button("Play cached loop"))
		{
			if (WorkCacheBounds(g_WorkLoopFirst, g_WorkLoopLast) && g_VEngineClient && g_VEngineClient->IsPlayingDemo())
			{
				g_SceneScrubRate = 0;
				g_WorkLoopRevision = g_SceneCache.Revision();
				g_WorkLoop = true;
				g_SceneScrubLast = std::chrono::steady_clock::now();
				ScenePreviewTime(g_WorkLoopFirst);
				g_WorkStatus = "Looping cached range. Manual scrubbing pauses the loop.";
			}
			else g_WorkStatus = "Cached loop requires continuous recorded coverage from In through Out.";
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		DrawOverlayHint("Cached playback does not seek or record the actual demo.");
		ImGui::EndPopup();
	}
	if (WorkRangeBusy())
	{
		ImGui::SameLine();
		if (ImGui::Button("Stop range"))
		{
			if (g_WorkLoop) SceneCacheStopScrub();
			else StopWorkTransport(true, "Range stopped.");
		}
	}
	if (WorkRangeValid()) ImGui::TextDisabled("In %d | Out %d | %.3f s", g_WorkIn, g_WorkOut,
		(double(g_WorkOut) - g_WorkIn) * SceneTickInterval());
	else ImGui::TextDisabled("Set In and Out (Out must follow In).");
	if (!g_WorkStatus.empty()) ImGui::TextWrapped("%s", g_WorkStatus.c_str());
	ImGui::PopID();
}

static int FreeModifierTrack(const advancedfx::SequenceModifierClip &clip,
 const std::vector<advancedfx::SequenceModifierClip> &clips)
{
 auto candidate = clip;
 for (int track = 0; track < advancedfx::SequenceModifierClip::MaxTracks; ++track)
 {
  candidate.Track = track;
  if (std::none_of(clips.begin(), clips.end(), [&](const auto &other){return other.Id != candidate.Id && candidate.Overlaps(other);})) return track;
 }
 return -1;
}
static void SelectSequenceModifier(const advancedfx::SequenceModifierClip &clip)
{
 g_SelectedSequenceModifier = clip.Id;
 // Keep the camera source selected: adjustment clips have independent properties.
 g_SelectedModifierDefinition = clip.Definition;
 g_FocusModifierLibrary = true;
 g_OpenCameraClips = true;
}
// Draw zero-duration fade handles too. Only Ctrl makes marker hit areas
// interactive, so ordinary range drags and Alt-duplication are unchanged.
static void DrawClipModifierFades(int begin, int end, int in, int out,
 bool *inHovered = nullptr, bool *outHovered = nullptr, bool interactive = false)
{
 if (in > begin)
  ImGui::NeoTimelineSpan(begin, in, IM_COL32(230,190,255,100), "Fade in", nullptr, false, false, -1, false, false);
 if (out < end)
  ImGui::NeoTimelineSpan(out, end, IM_COL32(230,190,255,100), "Fade out", nullptr, false, false, -1, false, false);
 ImGui::NeoTimelineSpan(in, in, IM_COL32(240,210,255,255), "Fade in end", inHovered, false, false, -1, false, interactive);
 ImGui::NeoTimelineSpan(out, out, IM_COL32(240,210,255,255), "Fade out start", outHovered, false, false, -1, false, interactive);
}
static int PickClipModifierEdge(int mouse, int begin, int end, int in, int out,
 int threshold, bool fades, bool inHovered, bool outHovered)
{
 if (fades && (inHovered || outHovered))
  return inHovered && (!outHovered || std::abs(mouse-in) <= std::abs(mouse-out)) ? 2 : 3;
 const int edges[] = {begin, end, in, out};
 const int first = fades ? 2 : 0;
 int result = -1, distance = threshold + 1;
 for (int i = first; i < first + 2; ++i)
  if (std::abs(mouse - edges[i]) < distance)
  {distance = std::abs(mouse - edges[i]); result = i;}
 return result;
}
// Snap in demo time, not client time: cached demo coverage may have a different offset.
static double SnapClipDemoTime(double dest, int visibleStart, int visibleEnd, bool camera, std::uint64_t id)
{
 if (ImGui::GetIO().KeyShift) return dest;
 const auto &grid = advancedfx::TimelineTimebase();
 if (grid.Mode == 2 && grid.Snap) return grid.SnapTime(dest);
 const double interval = SceneTickInterval();
 const double threshold = (std::max)(1.0, (double(visibleEnd) - visibleStart) / 180.0) * interval;
 double best = threshold, result = dest;
 auto consider = [&](double candidate) {
  const double distance = std::abs(candidate - dest);
  if (std::isfinite(candidate) && distance < best) { best = distance; result = candidate; }
 };
 auto considerClient = [&](double client) {
  double demo = 0;
  if (CinematicClock(client, true, demo, SceneCachePreviewActive())) consider(demo);
 };
 for (const auto &marker : g_CommandSystem.GetTickMarkers()) consider(marker.Tick * interval);
 if (!camera) for (const auto &clip : MirvCam_Clips()) considerClient(clip.Time);
 for (const auto &clip : MirvCam_SequenceModifiers()) if (camera || clip.Id != id) {
  considerClient(clip.Begin); considerClient(clip.End);
 }
 return result;
}
static void DrawSequenceModifierTracks(const std::function<int(double)> &tick, int visibleStart, int visibleEnd, bool &contextOpened)
{
 auto clips = MirvCam_SequenceModifiers();
 const auto definitions = MirvCam_Modifiers();
 static advancedfx::SequenceModifierClip original, pending;
 static int mouseStart = 0, edge = -1;
 static bool dragging = false, duplicating = false;
 int mouseTick = 0;
 const bool haveMouse = ImGui::GetNeoSequencerMouseFrame(&mouseTick, ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
 if (dragging)
 {
  auto found = std::find_if(clips.begin(), clips.end(), [&](const auto &c){return c.Id == original.Id;});
  if (found == clips.end() || !(*found == original) || g_SelectedSequenceModifier != original.Id || g_LockKeyTimes ||
      ImGui::IsKeyPressed(ImGuiKey_Escape) || (!ImGui::IsMouseDown(0) && !ImGui::IsMouseReleased(0))) dragging = false;
  else if (haveMouse && ImGui::IsMouseDown(0))
  {
   const double interval = SceneTickInterval();
   const int delta = mouseTick - mouseStart;
   auto shifted = [&](double client, double &value, bool snap)
   {
    double demo = 0;
    if (!CinematicClock(client, true, demo, SceneCachePreviewActive())) return false;
    double dest = (std::max)(0.0, demo + delta * interval);
    if (delta == 0) { value = client; return true; }
    if (snap) dest = SnapClipDemoTime(dest, visibleStart, visibleEnd, false, original.Id);
    return CinematicClock(dest, false, value, SceneCachePreviewActive());
   };
   pending = original;
   double moved = 0;
   if (edge == 0 && shifted(original.Begin, moved, true))
    pending.Begin = (std::min)(moved, original.End - (std::min)(interval, original.End - original.Begin));
   else if (edge == 1 && shifted(original.End, moved, true))
    pending.End = (std::max)(original.Begin + (std::min)(interval, original.End - original.Begin), moved);
   else if (edge == 2 && shifted(original.Begin + original.FadeIn, moved, advancedfx::TimelineTimebase().Mode == 2 && advancedfx::TimelineTimebase().Snap))
    pending.FadeIn = std::clamp(moved - original.Begin, 0.0, original.End - original.Begin);
   else if (edge == 3 && shifted(original.End - original.FadeOut, moved, advancedfx::TimelineTimebase().Mode == 2 && advancedfx::TimelineTimebase().Snap))
    pending.FadeOut = std::clamp(original.End - moved, 0.0, original.End - original.Begin);
   else if (edge == -1 && shifted(original.Begin, moved, true))
   {pending.Begin = moved; pending.End = moved + original.End - original.Begin;}
   pending.FadeIn = (std::min)(pending.FadeIn, pending.End - pending.Begin);
   pending.FadeOut = (std::min)(pending.FadeOut, pending.End - pending.Begin);
   if (duplicating && !(pending == original)) {
    auto ghost = pending; ghost.Id = ~std::uint64_t(0);
    // A duplicate can use its original track if it fits; otherwise choose a free track.
    if (std::any_of(clips.begin(), clips.end(), [&](const auto &c){return ghost.Overlaps(c);})) {
     const int track = FreeModifierTrack(ghost, clips);
     if (track >= 0) ghost.Track = track;
    }
    pending.Track = ghost.Track;
    clips.push_back(ghost);
   } else if (!duplicating) *found = pending;
   ImGui::NeoCancelSelection();
  }
  else if (ImGui::IsMouseReleased(0))
  {
   if (!(pending == original)) {
    if (duplicating) pending.Id = ~std::uint64_t(0);
    if (MirvCam_EditSequenceModifier(pending, nullptr)) SelectSequenceModifier(pending);
    else g_DocumentStatus = "Modifier edit rejected: check range, clip limit and free tracks.";
   }
   dragging = false;
  }
 }
 for (int track = advancedfx::SequenceModifierClip::MaxTracks - 1; track >= 0; --track)
 {
  if (std::none_of(clips.begin(), clips.end(), [&](const auto &c){return c.Track == track;})) continue;
  const std::string row = "Modifier track " + std::to_string(track + 1);
  if (!ImGui::BeginNeoTimelineEx(row.c_str(), nullptr, ImGuiNeoTimelineFlags_None)) continue;
  for (const auto &clip : clips)
  {
   if (clip.Track != track) continue;
   const int begin = tick(clip.Begin), end = (std::max)(begin + 1, tick(clip.End));
   if (begin > visibleEnd || end < visibleStart) continue;
   const int in = tick(clip.Begin + clip.FadeIn), out = tick(clip.End - clip.FadeOut);
   const auto definition = std::find_if(definitions.begin(), definitions.end(), [&](const auto &m){return m.Id == clip.Definition;});
   const std::string label = definition == definitions.end() ? "Missing modifier" : definition->Name;
   bool hovered = false;
   const bool ghost = clip.Id == ~std::uint64_t(0);
   const bool selected = !ghost && clip.Id == g_SelectedSequenceModifier;
   const bool choose = ImGui::NeoTimelineSpan(begin, end, clip.Enabled ? IM_COL32(170, 115, 210, 200) : IM_COL32(95, 95, 95, 150),
    label.c_str(), &hovered, true, selected || ghost, -1, false, !ghost);
   bool inHovered = false, outHovered = false;
   const bool editFades = !ghost && !g_LockKeyTimes && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt;
   DrawClipModifierFades(begin, end, in, out, &inHovered, &outHovered, editFades);
   if (!ghost) {
    ImGui::PushID(("adjustment_context_" + std::to_string(clip.Id)).c_str());
    if (!contextOpened && hovered && ImGui::IsMouseClicked(1)) { contextOpened = true; SelectSequenceModifier(clip); ImGui::NeoCancelSelection(); ImGui::OpenPopup("Clip options"); }
    if (ImGui::BeginPopup("Clip options")) {
     if (ImGui::MenuItem("Edit start")) g_ClipInspectorFocus = ClipInspectorFocus::AdjustmentStart;
     if (ImGui::MenuItem("Edit duration / end")) g_ClipInspectorFocus = ClipInspectorFocus::AdjustmentEnd;
     if (ImGui::MenuItem(clip.Enabled ? "Disable effect" : "Enable effect")) { auto edit = clip; edit.Enabled = !edit.Enabled; MirvCam_EditSequenceModifier(edit, nullptr); }
     if (ImGui::MenuItem("Delete adjustment") && MirvCam_RemoveSequenceModifier(clip.Id)) g_SelectedSequenceModifier = ~std::uint64_t(0);
     ImGui::EndPopup();
    }
    ImGui::PopID();
   }
   if (!ghost && !dragging && (choose || ((hovered || inHovered || outHovered) && ImGui::IsMouseClicked(0))) && haveMouse)
   {
    SelectSequenceModifier(clip);
    if (!g_LockKeyTimes && (!ImGui::GetIO().KeyAlt || selected))
    {
     original = pending = clip; dragging = true; mouseStart = mouseTick; edge = -1;
     duplicating = selected && ImGui::GetIO().KeyAlt;
     const int threshold = (std::max)(1, (visibleEnd - visibleStart) / 150);
     if (!duplicating)
      edge = PickClipModifierEdge(mouseTick, begin, end, in, out, threshold, editFades, inHovered, outHovered);
     // Ctrl without a fade handle must not accidentally move the adjustment.
     if (editFades && edge < 0) dragging = false;
     ImGui::NeoCancelSelection();
    }
   }
  }
  ImGui::EndNeoTimeLine();
 }
 g_AdjustmentDuplicateDrag = dragging && duplicating;
}
static void DrawCameraClipTrack(const std::function<int(double)> &tick, int visibleStart, int visibleEnd, bool &contextOpened)
{
	const bool wasOpen = g_SequencerSourcesOpen;
	const bool childrenOpen = ImGui::BeginNeoGroup("Camera sources", &g_SequencerSourcesOpen);
	if (wasOpen != g_SequencerSourcesOpen)
		ImGui::MarkIniSettingsDirty();
	auto clips = MirvCam_Clips();
	static std::uint64_t moving = ~std::uint64_t(0);
	static advancedfx::CameraClip originalMove, pendingMove;
	static int moveMouse = 0;
 static bool duplicateMove = false;
	// Envelope editing is independent of dragging the underlying source block.
	static std::uint64_t movingEnvelope = ~std::uint64_t(0);
	static advancedfx::CameraClip originalEnvelope, pendingEnvelope;
	static int envelopeMouse = 0, envelopeEdge = 0;
	int mouseTick = 0;
	const bool haveMouseTick =
		ImGui::GetNeoSequencerMouseFrame(&mouseTick, ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	if (moving != ~std::uint64_t(0))
	{
		auto found = std::find_if(clips.begin(), clips.end(), [&](const auto &c) { return c.Id == moving; });
		if (found == clips.end() || g_SelectedCameraClip != moving || !(*found == originalMove) || g_LockKeyTimes ||
			ImGui::IsKeyPressed(ImGuiKey_Escape) || (!ImGui::IsMouseDown(0) && !ImGui::IsMouseReleased(0)))
			moving = ~std::uint64_t(0);
		else if (haveMouseTick && ImGui::IsMouseDown(0))
		{
			pendingMove = originalMove;
			double originalDemo = 0;
			if (mouseTick != moveMouse && CinematicClock(originalMove.Time, true, originalDemo, SceneCachePreviewActive())) {
    const double dest = (std::max)(0.0, originalDemo + (mouseTick - moveMouse) * SceneTickInterval());
    CinematicClock(SnapClipDemoTime(dest, visibleStart, visibleEnd, true, originalMove.Id), false, pendingMove.Time, SceneCachePreviewActive());
   }
   if (duplicateMove && !(pendingMove == originalMove)) {
    auto ghost = pendingMove; ghost.Id = ~std::uint64_t(0); clips.push_back(ghost);
   } else if (!duplicateMove) *found = pendingMove;
   std::stable_sort(clips.begin(), clips.end(), [](const auto &a, const auto &b){return a.Time < b.Time;});
			ImGui::NeoCancelSelection();
		}
		else if (ImGui::IsMouseReleased(0))
		{
   if (!(pendingMove == originalMove)) {
    if (duplicateMove) pendingMove.Id = ~std::uint64_t(0);
    if (MirvCam_EditClip(pendingMove)) g_SelectedCameraClip = pendingMove.Id;
    else g_DocumentStatus = "Clip edit rejected: check neighboring clips, transition order and clip limit.";
   }
			moving = ~std::uint64_t(0);
		}
	}
	if (movingEnvelope != ~std::uint64_t(0))
	{
		auto found = std::find_if(clips.begin(), clips.end(),
			[&](const auto &c) { return c.Id == movingEnvelope; });
		if (found == clips.end() || !(*found == originalEnvelope) || g_LockKeyTimes || g_SelectedCameraClip != movingEnvelope ||
			ImGui::IsKeyPressed(ImGuiKey_Escape) ||
			(!ImGui::IsMouseDown(0) && !ImGui::IsMouseReleased(0)))
			movingEnvelope = ~std::uint64_t(0);
		else if (haveMouseTick && ImGui::IsMouseDown(0))
		{
			double originalDemo = 0, shifted = 0;
			const double edgeTime = originalEnvelope.TransitionStart() + (envelopeEdge == 1 ? originalEnvelope.Blend : 0.);
            if (CinematicClock(edgeTime, true, originalDemo, SceneCachePreviewActive()) &&
                CinematicClock((!ImGui::GetIO().KeyShift && mouseTick != envelopeMouse) ? advancedfx::TimelineTimebase().SnapTime(originalDemo + (mouseTick - envelopeMouse) * SceneTickInterval()) : originalDemo + (mouseTick - envelopeMouse) * SceneTickInterval(), false,
                    shifted, SceneCachePreviewActive()))
			{
				pendingEnvelope = originalEnvelope;
				if (envelopeEdge == 1)
					pendingEnvelope.Blend = (std::max)(0.0,
						shifted - originalEnvelope.TransitionStart());
				else
					pendingEnvelope.TransitionOffset =
						originalEnvelope.TransitionOffset + shifted - originalEnvelope.TransitionStart();
				*found = pendingEnvelope;
			}
			ImGui::NeoCancelSelection();
		}
		else if (ImGui::IsMouseReleased(0))
		{
			if (!(pendingEnvelope == originalEnvelope) && !MirvCam_EditClip(pendingEnvelope))
				g_DocumentStatus = "Transition edit rejected: envelope starts must remain in clip order.";
			movingEnvelope = ~std::uint64_t(0);
		}
	}
	for (size_t i = 0; i < clips.size(); ++i)
	{
		const auto &clip = clips[i];
		const int begin = tick(clip.Time);
		const int end = i + 1 < clips.size() ? tick(clips[i + 1].Time) : (std::max)(begin + 1, visibleEnd);
		if (begin > visibleEnd || end < visibleStart)
			continue;
		std::string label = CameraClipSourceName(clip.Campath, clip.Target);
		if (i && clip.Blend > 0) { char blend[64]; snprintf(blend, sizeof(blend),
			" | Transition %.2f s", clip.Blend); label += blend; }
  const bool ghost = clip.Id == ~std::uint64_t(0);
  const bool selected = !ghost && clip.Id == g_SelectedCameraClip;
  bool hovered = false;
		const bool choose = ImGui::NeoTimelineSpan(
			begin, end, IM_COL32(90, 65, 125, 230), label.c_str(), &hovered, true,
			selected || ghost, -1, true, !ghost);
  if (!ghost) {
   ImGui::PushID(("camera_context_" + std::to_string(clip.Id)).c_str());
   if (!contextOpened && hovered && ImGui::IsMouseClicked(1)) {
    contextOpened = true;
    g_SelectedCameraClip = clip.Id; g_SelectedSequenceModifier = ~std::uint64_t(0); g_OpenCameraClips = true;
    ImGui::NeoCancelSelection(); ImGui::OpenPopup("Clip options");
   }
   if (ImGui::BeginPopup("Clip options")) {
    if (ImGui::MenuItem("Edit start")) g_ClipInspectorFocus = ClipInspectorFocus::CameraStart;
    if (ImGui::MenuItem("Edit transition duration")) g_ClipInspectorFocus = ClipInspectorFocus::CameraTransition;
    if (ImGui::MenuItem("Delete camera clip") && MirvCam_RemoveClip(clip.Id)) g_SelectedCameraClip = ~std::uint64_t(0);
    ImGui::EndPopup();
   }
   ImGui::PopID();
  }
		if (choose)
		{
			g_SelectedCameraClip = clip.Id;
   g_SelectedSequenceModifier = ~std::uint64_t(0);
			g_OpenCameraClips = true;
			if (haveMouseTick && !g_LockKeyTimes && (!ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyAlt) && (!ImGui::GetIO().KeyAlt || selected))
			{
				moving = clip.Id;
				originalMove = pendingMove = clip;
    duplicateMove = selected && ImGui::GetIO().KeyAlt;
				moveMouse = mouseTick;
				ImGui::NeoCancelSelection();
			}
		}
	}
	// Render all transition envelopes on the SAME camera-source row. The
	// sequencer's groupHeader span mode supplies the parent row's exact Y,
	// eliminating the former one-row-per-transition clutter.
	for (size_t i = 1; i < clips.size(); ++i)
	{
		const auto &clip = clips[i];
		const int begin = tick(clip.TransitionStart());
		const int end = (std::max)(begin, tick(clip.TransitionStart() + clip.Blend));
		if (end < visibleStart || begin > visibleEnd)
			continue;
		const std::string caption = CameraClipSourceName(clip.Campath, clip.Target) +
			(begin == end ? " transition | Ctrl-drag marker to extend" :
			 " transition | Ctrl-drag to move; Ctrl-drag right edge to resize");
		// Default drags go to the camera clip. Only Ctrl+drag turns the
		// overlaid transition into an interactive target. Keeping the span
		// visible but inert avoids the lower clip's mouse events being stolen.
		const bool editEnvelope = !g_LockKeyTimes && clip.Id != ~std::uint64_t(0) && ((ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt &&
			movingEnvelope == ~std::uint64_t(0)) || movingEnvelope == clip.Id);
		const bool chose = ImGui::NeoTimelineSpan(begin, end, IM_COL32(55, 168, 236, 115),
			caption.c_str(), nullptr, false, clip.Id == g_SelectedCameraClip, -1, true, editEnvelope);
		if (chose && haveMouseTick)
		{
			moving = ~std::uint64_t(0); // The envelope takes precedence over the source block.
			movingEnvelope = clip.Id;
			originalEnvelope = pendingEnvelope = clip;
			envelopeMouse = mouseTick;
			envelopeEdge = begin == end || std::abs(mouseTick - end) <=
				(std::max)(2, (visibleEnd - visibleStart) / 180) ? 1 : 0;
			g_SelectedCameraClip = clip.Id;
   g_SelectedSequenceModifier = ~std::uint64_t(0);
			g_OpenCameraClips = true;
			ImGui::NeoCancelSelection();
		}
	}
	if (!childrenOpen)
		return;
	static advancedfx::CameraClip dragClip, dragOriginal;
	static size_t dragIndex = 0;
	static int dragEdge = -1, dragMouse = 0;
	static std::uint64_t dragging = ~std::uint64_t(0);
	auto selected =
		std::find_if(clips.begin(), clips.end(), [](const auto &c) { return c.Id == g_SelectedCameraClip; });
	// An interrupted drag must not overwrite a later edit, undo, deletion or selection change.
	if (dragging != ~std::uint64_t(0) &&
		(selected == clips.end() || !(*selected == dragOriginal) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
		 (!ImGui::IsMouseDown(0) && !ImGui::IsMouseReleased(0))))
		dragging = ~std::uint64_t(0);
	if (selected != clips.end())
	{
		auto definitions = MirvCam_Modifiers();
		for (size_t i = 0; i < selected->Modifiers.size(); ++i)
		{
			auto source = dragging == selected->Id ? dragClip : *selected;
			if (i >= source.Modifiers.size())
				break;
			auto &link = source.Modifiers[i];
			auto definition = std::find_if(definitions.begin(), definitions.end(),
										   [&](const auto &m) { return m.Id == link.Id; });
			std::string label =
				(definition == definitions.end() ? std::string("Missing modifier") : definition->Name) +
				"##clip_modifier_" + std::to_string(i);
			if (!ImGui::BeginNeoTimelineEx(label.c_str(), nullptr, ImGuiNeoTimelineFlags_None))
				continue;
			int begin = tick(source.Time + link.Begin), end = tick(source.Time + link.End);
			int in = tick(source.Time + link.Begin + link.FadeIn),
				out = tick(source.Time + link.End - link.FadeOut);
			bool hovered = false;
			ImGui::NeoTimelineSpan(
				begin, end, link.Enabled ? IM_COL32(170, 115, 210, 200) : IM_COL32(95, 95, 95, 150),
				definition == definitions.end() ? "Missing modifier" : definition->Name.c_str(), &hovered,
				true);
			bool inHovered = false, outHovered = false;
			const bool editFades = !g_LockKeyTimes && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt;
			DrawClipModifierFades(begin, end, in, out, &inHovered, &outHovered, editFades);
			ImGui::FrameIndexType mouse;
			const bool haveMouse = ImGui::GetNeoSequencerMouseFrame(&mouse, ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
			if ((hovered || inHovered || outHovered) && haveMouse && !g_LockKeyTimes &&
				dragging == ~std::uint64_t(0) && ImGui::IsMouseClicked(0))
			{
				dragClip = dragOriginal = *selected;
				dragging = selected->Id;
				dragIndex = i;
				dragMouse = mouse;
				const int threshold = (std::max)(1, (visibleEnd - visibleStart) / 150);
				dragEdge = PickClipModifierEdge(mouse, begin, end, in, out, threshold, editFades, inHovered, outHovered);
				// Preserve direct dragging of existing, non-coincident legacy fade handles.
				if (!editFades)
				{
					const int fadeEdge = PickClipModifierEdge(mouse, begin, end, in, out, threshold, true, false, false);
					const int edges[] = {begin, end, in, out};
					if (fadeEdge >= 0 && (dragEdge < 0 || std::abs(mouse - edges[fadeEdge]) < std::abs(mouse - edges[dragEdge])))
						dragEdge = fadeEdge;
				}
				if (editFades && dragEdge < 0) dragging = ~std::uint64_t(0);
				ImGui::NeoCancelSelection();
			}
			if (dragging == selected->Id && dragIndex == i && haveMouse && ImGui::IsMouseDown(0))
			{
				auto &m = dragClip.Modifiers[i];
				const auto &original = dragOriginal.Modifiers[i];
				double delta = (mouse - dragMouse) * SceneTickInterval();
                if (delta != 0 && !ImGui::GetIO().KeyShift) {
                    const double relative = dragEdge == 1 ? original.End : dragEdge == 2 ? original.Begin + original.FadeIn : dragEdge == 3 ? original.End - original.FadeOut : original.Begin;
                    double demoTime, client;
                    const double base = dragOriginal.Time + relative;
                    if (CinematicClock(base,true,demoTime,SceneCachePreviewActive()) && CinematicClock(advancedfx::TimelineTimebase().SnapTime(demoTime+delta),false,client,SceneCachePreviewActive())) delta=client-base;
                }
				m = original;
				if (dragEdge == 0)
					m.Begin = std::clamp(original.Begin + delta, 0.0,
										 original.End -
											 (std::min)(SceneTickInterval(), original.End - original.Begin));
				else if (dragEdge == 1)
					m.End = (std::max)(original.Begin + SceneTickInterval(), original.End + delta);
				else if (dragEdge == 2)
					m.FadeIn = std::clamp(original.FadeIn + delta, 0.0, m.End - m.Begin);
				else if (dragEdge == 3)
					m.FadeOut = std::clamp(original.FadeOut - delta, 0.0, m.End - m.Begin);
				else
				{
					m.Begin = (std::max)(0.0, original.Begin + delta);
					m.End = m.Begin + original.End - original.Begin;
				}
				m.FadeIn = (std::min)(m.FadeIn, m.End - m.Begin);
				m.FadeOut = (std::min)(m.FadeOut, m.End - m.Begin);
				ImGui::NeoCancelSelection();
			}
			ImGui::EndNeoTimeLine();
		}
		if (dragging != ~std::uint64_t(0) && !ImGui::IsMouseDown(0))
		{
			if (dragging == selected->Id && !(dragClip == dragOriginal) && !MirvCam_EditClip(dragClip))
				g_DocumentStatus = "Modifier timing edit rejected; previous values retained.";
			dragging = ~std::uint64_t(0);
		}
	}
	ImGui::EndNeoGroup();

}

static void DrawTransitionTrajectoryGraph(std::array<double, 3> &handleOut,
										  std::array<double, 3> &handleIn)
{
	// An uncomplicated projected handle editor. The origin/end and reference
	// controls are fixed; handle movement edits actual world-space offset axes.
	static int plane = 0;
	static float halfRange = 64;
	static std::uint64_t activeClip = ~std::uint64_t(0);
	static int activeHandle = -1;
	const char *planes[] = {"XY (top)", "XZ (side)", "YZ (side)"};
	ImGui::Combo("Graph plane", &plane, planes, 3);
	ImGui::SliderFloat("Graph scale (units)", &halfRange, 8, 2048, "%.0f");
	ImGui::TextDisabled("Drag either control point. Switch planes to edit the third axis.");
	const int axes[3][2] = {{0, 1}, {0, 2}, {1, 2}};
	const int horizontal = axes[plane][0], vertical = axes[plane][1];
	const float width = (std::max)(240.0f, ImGui::GetContentRegionAvail().x);
	const ImVec2 size(width, 180);
	ImGui::InvisibleButton("##transition_bezier_graph", size, ImGuiButtonFlags_MouseButtonLeft);
	const ImVec2 origin = ImGui::GetItemRectMin(), end = ImGui::GetItemRectMax();
	const ImVec2 a(origin.x + 24, end.y - 24), b(end.x - 24, origin.y + 24);
	const float pixelsPerUnit = (std::min)(size.x, size.y) * 0.42f / halfRange;
	auto baseline = [&](double t) {
		return ImVec2(float(a.x + (b.x - a.x) * t), float(a.y + (b.y - a.y) * t));
	};
	auto move = [&](const std::array<double, 3> &handle, double t) {
		auto p = baseline(t);
		p.x += float(handle[horizontal]) * pixelsPerUnit;
		p.y -= float(handle[vertical]) * pixelsPerUnit;
		return p;
	};
	ImVec2 p1 = move(handleOut, 1.0 / 3.0), p2 = move(handleIn, 2.0 / 3.0);
	const ImVec2 cursor = ImGui::GetIO().MousePos;
	auto distanceSquared = [&](const ImVec2 &p) {
		const float dx = p.x - cursor.x, dy = p.y - cursor.y;
		return dx * dx + dy * dy;
	};
	if (activeClip != g_SelectedCameraClip || !ImGui::IsMouseDown(0))
	{
		activeHandle = -1;
		activeClip = g_SelectedCameraClip;
	}
	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
	{
		const float d1 = distanceSquared(p1), d2 = distanceSquared(p2);
		if ((std::min)(d1, d2) < 16 * 16)
			activeHandle = d1 <= d2 ? 0 : 1;
	}
	if (activeHandle >= 0 && ImGui::IsMouseDown(0) && pixelsPerUnit > 0)
	{
		auto &handle = activeHandle == 0 ? handleOut : handleIn;
		const auto delta = ImGui::GetIO().MouseDelta;
		handle[horizontal] = std::clamp(handle[horizontal] + double(delta.x / pixelsPerUnit), -1e6, 1e6);
		handle[vertical] = std::clamp(handle[vertical] - double(delta.y / pixelsPerUnit), -1e6, 1e6);
		p1 = move(handleOut, 1.0 / 3.0);
		p2 = move(handleIn, 2.0 / 3.0);
	}
	ImDrawList *draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(origin, end, IM_COL32(23, 28, 38, 245), 5);
	draw->PushClipRect(origin, end, true);
	for (int n = 1; n < 4; ++n)
	{
		const float x = origin.x + size.x * n / 4, y = origin.y + size.y * n / 4;
		draw->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y), IM_COL32(100, 115, 135, 60));
		draw->AddLine(ImVec2(origin.x, y), ImVec2(end.x, y), IM_COL32(100, 115, 135, 60));
	}
	draw->AddLine(a, b, IM_COL32(155, 155, 160, 100), 1.3f);
	draw->AddLine(a, p1, IM_COL32(190, 165, 255, 160));
	draw->AddLine(p2, b, IM_COL32(190, 165, 255, 160));
	ImVec2 last = a;
	for (int i = 1; i <= 64; ++i)
	{
		const double t = i / 64.0, u = 1 - t;
		const ImVec2 point(
			float(u*u*u*a.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*b.x),
			float(u*u*u*a.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*b.y));
		draw->AddLine(last, point, IM_COL32(130, 198, 255, 245), 2.5f);
		last = point;
	}
	draw->AddCircleFilled(a, 4, IM_COL32(80, 215, 170, 255));
	draw->AddCircleFilled(b, 4, IM_COL32(80, 215, 170, 255));
	draw->AddCircleFilled(p1, 7, IM_COL32(210, 155, 255, 255));
	draw->AddCircleFilled(p2, 7, IM_COL32(255, 189, 120, 255));
	draw->AddCircle(p1, 10, IM_COL32(210, 155, 255, 190));
	draw->AddCircle(p2, 10, IM_COL32(255, 189, 120, 190));
	draw->PopClipRect();
	ImGui::TextDisabled("Projected offset handles: %s. Dashed in-world preview shows the actual path.", planes[plane]);
}

static void DrawCameraModifierLibrary();
static void DrawSequenceModifierPanel(bool mapping, double demo, double interval)
{
 // Reveal on selection, but allow the user to collapse the section again.
 static std::uint64_t lastRevealed = ~std::uint64_t(0);
 if (g_SelectedSequenceModifier != lastRevealed && g_SelectedSequenceModifier != ~std::uint64_t(0))
  ImGui::SetNextItemOpen(true, ImGuiCond_Always);
 lastRevealed = g_SelectedSequenceModifier;
 if (!ImGui::CollapsingHeader("Sequence modifier clips")) return;
 ImGui::PushID("sequence_modifier_panel");
 ImGui::TextWrapped("Adjustment clips affect the evaluated sequence, including transitions. Higher tracks apply last. Drag blocks or edges. Alt-drag a selected adjustment to duplicate; Shift bypasses snapping; Ctrl-drag fade markers (including zero duration).");
 const auto definitions = MirvCam_Modifiers();
 auto clips = MirvCam_SequenceModifiers();
 auto found = std::find_if(clips.begin(), clips.end(), [](const auto &c){return c.Id == g_SelectedSequenceModifier;});
 if (found == clips.end()) g_SelectedSequenceModifier = ~std::uint64_t(0);
 const auto definition = std::find_if(definitions.begin(), definitions.end(), [](const auto &m){return m.Id == g_SelectedModifierDefinition;});
 if (ImGui::BeginCombo("Adjustment effect", definition == definitions.end() ? "Choose an effect" : definition->Name.c_str()))
 {
  for (const auto &m : definitions)
  {
   ImGui::PushID(std::to_string(m.Id).c_str());
   if (ImGui::Selectable(m.Name.c_str(), m.Id == g_SelectedModifierDefinition))
   {g_SelectedModifierDefinition = m.Id; g_FocusModifierLibrary = true;}
   ImGui::PopID();
  }
  ImGui::EndCombo();
 }
 auto addAtPlayhead = [&](advancedfx::SequenceModifierClip clip, double duration)
 {
  clip.Id = ~std::uint64_t(0);
  if (!CinematicClock(ImGui::GetIO().KeyShift ? demo : advancedfx::TimelineTimebase().SnapTime(demo), false, clip.Begin, SceneCachePreviewActive())) return false;
  clip.End = clip.Begin + duration;
  clip.Track = FreeModifierTrack(clip, clips);
  if (clip.Track < 0 || !MirvCam_EditSequenceModifier(clip, nullptr)) return false;
  SelectSequenceModifier(clip);
  return true;
 };
 ImGui::BeginDisabled(!mapping || definition == definitions.end());
 if (ImGui::Button("Add modifier clip at playhead"))
 {
  advancedfx::SequenceModifierClip clip;
  clip.Definition = g_SelectedModifierDefinition;
  if (definition != definitions.end())
  {
   const double duration = definition->Settings.End - definition->Settings.Begin;
   clip.FadeIn = (std::min)(1.0, definition->Settings.FadeIn / duration);
   clip.FadeOut = (std::min)(1.0, definition->Settings.FadeOut / duration);
  }
  g_DocumentStatus = addAtPlayhead(clip, 1.0) ? "Modifier clip added." : "Could not add adjustment: check clock, effect or free tracks.";
 }
 ImGui::EndDisabled();
 if (definitions.empty()) ImGui::TextDisabled("Create an effect using Camera modifiers below.");
 for (const auto &clip : clips)
 {
  const auto m = std::find_if(definitions.begin(), definitions.end(), [&](const auto &d){return d.Id == clip.Definition;});
  const std::string label = "Track " + std::to_string(clip.Track + 1) + ": " + (m == definitions.end() ? "Missing modifier" : m->Name) + "##adjustment_" + std::to_string(clip.Id);
  if (ImGui::Selectable(label.c_str(), clip.Id == g_SelectedSequenceModifier)) SelectSequenceModifier(clip);
 }
 found = std::find_if(clips.begin(), clips.end(), [](const auto &c){return c.Id == g_SelectedSequenceModifier;});
 if (found != clips.end())
 {
  auto clip = *found;
  static std::uint64_t gesture = 0, lastId = ~std::uint64_t(0);
  static advancedfx::SequenceModifierClip baseline;
  static int beginTick = 0, endTick = 0, trackNumber = 1;
  static double fadeIn = 0, fadeOut = 0;
  if (lastId != clip.Id || !(baseline == clip))
  {
   lastId = clip.Id; baseline = clip; gesture = 0;
   double start = 0, end = 0;
   if (mapping && CinematicClock(clip.Begin, true, start, SceneCachePreviewActive()) && CinematicClock(clip.End, true, end, SceneCachePreviewActive()))
   {beginTick = AdjustmentTick(start, interval); endTick = AdjustmentTick(end, interval);}
   trackNumber = clip.Track + 1; fadeIn = clip.FadeIn; fadeOut = clip.FadeOut;
  }
  if (!ImGui::IsMouseDown(0)) gesture = 0;
  ImGui::Separator();
  ImGui::TextDisabled("Selected adjustment properties (live)");
  bool changed = ImGui::Checkbox("Adjustment enabled", &clip.Enabled);
  float influence = float(clip.Influence);
  if (ImGui::SliderFloat("Adjustment influence", &influence, 0, 1, "%.2f")) {clip.Influence = influence; changed = true;}
  ImGui::BeginDisabled(!mapping || g_LockKeyTimes);
  FocusClipField(ClipInspectorFocus::AdjustmentStart);
  ImGui::InputInt("Adjustment start tick", &beginTick);
  if (ImGui::IsItemDeactivatedAfterEdit())
  {double next; if (beginTick >= 0 && CinematicClock(beginTick * interval, false, next, SceneCachePreviewActive())) {clip.Begin = next; changed = true;}}
  FocusClipField(ClipInspectorFocus::AdjustmentEnd);
  ImGui::InputInt("Adjustment end tick", &endTick);
  if (ImGui::IsItemDeactivatedAfterEdit())
  {double next; if (endTick >= 0 && CinematicClock(endTick * interval, false, next, SceneCachePreviewActive())) {clip.End = next; changed = true;}}
  if(advancedfx::TimelineTimebase().Mode==2) {
   double beats=(clip.End-clip.Begin)/advancedfx::TimelineTimebase().Beat();
   if(ImGui::InputDouble("Adjustment duration (beats)",&beats,0,0,"%.4f",ImGuiInputTextFlags_EnterReturnsTrue)) {clip.End=clip.Begin+beats*advancedfx::TimelineTimebase().Beat();changed=true;}
   double bars=(clip.End-clip.Begin)/advancedfx::TimelineTimebase().Bar();
   if(ImGui::InputDouble("Adjustment duration (bars)",&bars,0,0,"%.4f",ImGuiInputTextFlags_EnterReturnsTrue)) {clip.End=clip.Begin+bars*advancedfx::TimelineTimebase().Bar();changed=true;}
  }
  ImGui::EndDisabled();
  ImGui::InputInt("Adjustment track (1-8)", &trackNumber);
  if (ImGui::IsItemDeactivatedAfterEdit()) {clip.Track = trackNumber >= 1 && trackNumber <= advancedfx::SequenceModifierClip::MaxTracks ? trackNumber - 1 : -1; changed = true;}
  ImGui::InputDouble("Adjustment fade in (s)", &fadeIn, 0, 0, "%.3f");
  if (ImGui::IsItemDeactivatedAfterEdit()) {clip.FadeIn = fadeIn; changed = true;}
  ImGui::InputDouble("Adjustment fade out (s)", &fadeOut, 0, 0, "%.3f");
  if (ImGui::IsItemDeactivatedAfterEdit()) {clip.FadeOut = fadeOut; changed = true;}
  if (changed)
  {
   if (clip.End > clip.Begin)
   {clip.FadeIn = (std::min)(clip.FadeIn, clip.End - clip.Begin); clip.FadeOut = (std::min)(clip.FadeOut, clip.End - clip.Begin);}
   if (MirvCam_EditSequenceModifier(clip, &gesture)) {baseline = clip; g_DocumentStatus = "Adjustment updated.";}
   else {lastId = ~std::uint64_t(0); g_DocumentStatus = "Adjustment rejected: check timing, fades, or track overlap.";}
  }
  ImGui::BeginDisabled(!mapping);
  if (ImGui::Button("Duplicate adjustment at playhead"))
   g_DocumentStatus = addAtPlayhead(*found, found->End - found->Begin) ? "Adjustment duplicated." : "No free track at the playhead.";
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Remove adjustment") && MirvCam_RemoveSequenceModifier(found->Id)) g_SelectedSequenceModifier = ~std::uint64_t(0);
  if (ImGui::Button("Make effect unique"))
  {
   advancedfx::SequenceModifierClip unique;
   if (MirvCam_MakeSequenceModifierUnique(found->Id, unique)) {SelectSequenceModifier(unique); g_DocumentStatus = "Adjustment now has its own effect definition.";}
   else g_DocumentStatus = "Could not duplicate the effect (definition limit).";
  }
  if (ImGui::Button("Edit adjustment effect")) {g_SelectedModifierDefinition = found->Definition; g_FocusModifierLibrary = true;}
  ImGui::SameLine();
  if (ImGui::Button("Use chosen effect") && definition != definitions.end())
  {clip.Definition = definition->Id; if (MirvCam_EditSequenceModifier(clip, nullptr)) g_FocusModifierLibrary = true;}
  if (ImGui::Button("Deselect adjustment")) g_SelectedSequenceModifier = ~std::uint64_t(0);
 }
 ImGui::TextWrapped("%s", g_DocumentStatus.c_str());
 ImGui::PopID();
}
static void DrawCameraClipsPanel()
{
	ImGui::TextWrapped(
		"Enable the sequence to play these clips. Preview navigation is independent. You can use free camera "
		"to edit shots while playback stays enabled. Cached preview supplies the scene and clock.");
	ImGui::TextWrapped("Drag to move clips. Alt-drag duplicates; Shift bypasses snapping. Ctrl-drag transition edges or fade markers, including zero-duration markers. Right-click for clip options.");
	ImGui::BeginDisabled(!g_ShowCameraGuides);
	ImGui::Checkbox("Show transition guides in 3D", &g_ShowTransitionGuide);
	ImGui::EndDisabled();
	if (!g_ShowCameraGuides) DrawOverlayHint("Enable Show camera guides in Cameras > Display to draw transitions.");
	DrawOverlayHint("Amber: transition. Cyan: selected transition. Labels mark the sampled start and end.");
	DrawOverlayHint("Editor only; hidden during recording. Moving/aimed cameras need Scene Cache history for accurate guides.");
	if (SceneCachePreviewActive())
		ImGui::TextWrapped("Recorded tracking preserves observed aiming. Without camera samples, aiming is "
						   "unsmoothed. Re-record after editing a camera. Targets must be "
						   "included in the cache.");
	static std::string clipStatus;
	bool enabled = MirvCam_SequenceEnabled();
	if (ImGui::Checkbox("Enable camera sequence", &enabled) && !MirvCam_EnableSequence(enabled))
		g_DocumentStatus = "Add a camera clip first, and use the demo session it belongs to.";
	if (enabled)
	{
		auto input = g_Hook_VClient_RenderView.m_MirvInput;
		ImGui::TextDisabled(MirvCampathPreview_IsActive()
								? "Sequence enabled; preview temporarily overrides the view."
							: input && input->GetCameraControlMode()
								? "Sequence enabled; free camera temporarily overrides the view."
								: "Sequence enabled; following the demo/cache clock.");
	}
	double demo = 0;
	bool mapping = GetCurrentDemoTime(demo);
	if (SceneCachePreviewActive())
	{
		demo = g_SceneCursor;
		mapping = !g_SceneCache.Frames().empty();
	}
	const double interval = SceneTickInterval();
	mapping = mapping && std::isfinite(interval) && interval > 0;
 if (MirvCam_SequencePreviewActive())
 {
  double mapped = 0;
  if (CinematicClock(MirvCam_SequencePreviewTime(), true, mapped, SceneCachePreviewActive())) demo = mapped;
  else mapping = false;
 }
 else if (MirvCampathPreview_IsActive())
 {
  double mapped = 0;
  if (CinematicClock(MirvCampathPreview_GetTime() + g_Hook_VClient_RenderView.m_CamPath.GetOffset(), true, mapped, SceneCachePreviewActive())) demo = mapped;
  else mapping = false;
 }
 // Camera and adjustment inspectors remain available together.
	auto clips = MirvCam_Clips();
	static std::uint64_t loaded = ~std::uint64_t(0);
	static int tick = 0;
	static double blend = 0, transitionOffset = 0;
	static int blendCurve = 0, rotationCurve = 0, fovCurve = 0;
	static bool freezeOutgoing = true, curvedPath = false;
	static std::array<double, 3> handleOut{{0, 0, 0}}, handleIn{{0, 0, 0}};
	static bool campath = true;
	static std::uint64_t target =
		g_Hook_VClient_RenderView.m_CamPaths.GetId(g_Hook_VClient_RenderView.m_CamPaths.GetActive());
	static advancedfx::CameraClip loadedValue;
 static std::uint64_t gesture = 0;
 static int loadedTick = 0;
 static double musicalDraftDemo = -1.;
 bool textEditing = false;
 if (!ImGui::IsMouseDown(0)) gesture = 0;
	auto found =
		std::find_if(clips.begin(), clips.end(), [](const auto &c) { return c.Id == g_SelectedCameraClip; });
	if (found != clips.end() && mapping &&
		(loaded != g_SelectedCameraClip || found->Time != loadedValue.Time ||
		 found->Blend != loadedValue.Blend || found->BlendCurve != loadedValue.BlendCurve ||
		 found->TransitionOffset != loadedValue.TransitionOffset ||
		 found->RotationCurve != loadedValue.RotationCurve || found->FovCurve != loadedValue.FovCurve ||
		 found->FreezeOutgoing != loadedValue.FreezeOutgoing || found->CurvedPath != loadedValue.CurvedPath ||
		 found->HandleOut != loadedValue.HandleOut || found->HandleIn != loadedValue.HandleIn ||
		 found->Target != loadedValue.Target || found->Campath != loadedValue.Campath))
	{
		loadedValue = *found;
		loaded = g_SelectedCameraClip;
        musicalDraftDemo = -1.;
  gesture = 0;
		double clipDemo = 0;
		CinematicClock(found->Time, true, clipDemo, SceneCachePreviewActive());
		tick = loadedTick = (std::max)(0, int(llround(clipDemo / interval)));
		blend = found->Blend;
		transitionOffset = found->TransitionOffset;
		blendCurve = found->BlendCurve;
		rotationCurve = found->RotationCurve;
		fovCurve = found->FovCurve;
		freezeOutgoing = found->FreezeOutgoing;
		curvedPath = found->CurvedPath;
		handleOut = found->HandleOut;
		handleIn = found->HandleIn;
		campath = found->Campath;
		target = found->Target;
	}
	if(found==clips.end())g_SelectedCameraClip=~std::uint64_t(0);
	const bool editing=found!=clips.end();
	if(editing && ImGui::Button("New clip"))
	{
		g_SelectedCameraClip=loaded=~std::uint64_t(0);
        musicalDraftDemo = -1.;
		blend = transitionOffset = 0;
		blendCurve = rotationCurve = fovCurve = 0;
		freezeOutgoing = true;
		curvedPath = false;
		handleOut = handleIn = {{0, 0, 0}};
		clipStatus="Choose a start tick and source, then add the new camera clip.";
	}
	if (ImGui::Button("New camera clip at playhead") && mapping)
	{
        // The panel already resolved the exact active sequence/cached/campath clock.
        const auto &grid=advancedfx::TimelineTimebase();
        const bool musical=grid.Mode==2 && grid.Snap && !ImGui::GetIO().KeyShift;
        const double playheadDemo=musical ? grid.SnapTime(demo) : demo;
        if (std::isfinite(playheadDemo) && playheadDemo>=0 && playheadDemo/interval<=INT_MAX)
        {
            musicalDraftDemo=musical ? playheadDemo : -1.;
            tick=AdjustmentTick(playheadDemo,interval);
			g_SelectedCameraClip = loaded = ~std::uint64_t(0);
			blend = transitionOffset = 0;
			blendCurve = rotationCurve = fovCurve = 0;
			freezeOutgoing = true;
			curvedPath = false;
			handleOut = handleIn = {{0, 0, 0}};
			clipStatus = "Start tick set to playhead; choose a source and press Add camera clip.";
		}
	}
	FocusClipField(ClipInspectorFocus::CameraStart);
	if(ImGui::InputInt("Start tick", &tick)) musicalDraftDemo=-1.;
    textEditing |= ImGui::IsItemActive();
    if(musicalDraftDemo>=0.) ImGui::TextDisabled("Exact snapped start: %.6f demo seconds",musicalDraftDemo);
	tick = (std::max)(0, tick);
	FocusClipField(ClipInspectorFocus::CameraTransition);
	ImGui::InputDouble("Transition duration (s)", &blend, 0, 0, "%.3f"); textEditing |= ImGui::IsItemActive();
    if(advancedfx::TimelineTimebase().Mode==2) {
        double beats=blend/advancedfx::TimelineTimebase().Beat();
        if(ImGui::InputDouble("Transition duration (beats)",&beats,0,0,"%.4f"))blend=beats*advancedfx::TimelineTimebase().Beat();
        textEditing |= ImGui::IsItemActive();
        double bars=blend/advancedfx::TimelineTimebase().Bar();
        if(ImGui::InputDouble("Transition duration (bars)",&bars,0,0,"%.4f"))blend=bars*advancedfx::TimelineTimebase().Bar();
        textEditing |= ImGui::IsItemActive();
    }
	ImGui::InputDouble("Transition offset from edit point (s)", &transitionOffset, 0, 0, "%.3f"); textEditing |= ImGui::IsItemActive();
	if (ImGui::Button("Before edit")) transitionOffset = -blend;
	ImGui::SameLine();
	if (ImGui::Button("Center on edit")) transitionOffset = -blend * 0.5;
	ImGui::SameLine();
	if (ImGui::Button("After edit")) transitionOffset = 0;
	if (std::isfinite(blend) && std::isfinite(transitionOffset))
		ImGui::TextDisabled("Envelope: %.3f - %.3f s (demo time)",
			(tick * interval) + transitionOffset, (tick * interval) + transitionOffset + blend);
	const char *blendCurves[] = {"Smooth", "Linear", "Smoother"};
	ImGui::Combo("Position easing", &blendCurve, blendCurves, 3);
	ImGui::Combo("Rotation easing", &rotationCurve, blendCurves, 3);
	ImGui::Combo("FOV easing", &fovCurve, blendCurves, 3);
	ImGui::Checkbox("Freeze outgoing camera at transition start", &freezeOutgoing);
	ImGui::Checkbox("Curved transition trajectory (cubic Bezier)", &curvedPath);
	if (curvedPath)
	{
		DrawTransitionTrajectoryGraph(handleOut, handleIn);
		if (ImGui::Button("Reset trajectory handles"))
			handleOut = handleIn = {{0, 0, 0}};
		if (ImGui::TreeNode("Precise XYZ handles (optional)"))
		{
			const char *axis[] = {"X", "Y", "Z"};
			for (int i = 0; i < 3; ++i)
			{
				ImGui::PushID(i);
				ImGui::InputDouble((std::string("Outgoing ") + axis[i]).c_str(), &handleOut[i], 0, 0, "%.2f"); textEditing |= ImGui::IsItemActive();
				ImGui::InputDouble((std::string("Incoming ") + axis[i]).c_str(), &handleIn[i], 0, 0, "%.2f"); textEditing |= ImGui::IsItemActive();
				ImGui::PopID();
			}
			ImGui::TreePop();
		}
	}
	auto cameras = MirvCam_Cameras();
	auto &paths = g_Hook_VClient_RenderView.m_CamPaths;
	std::string selectedName = CameraClipSourceName(campath, target);
	if (ImGui::BeginCombo("Camera clip source", selectedName.c_str()))
	{
		for (const auto &c : cameras)
		{
			const auto name = c.Name;
			ImGui::PushID(("camera_source_" + std::to_string(c.Id)).c_str());
			if (ImGui::Selectable(name.c_str(), !campath && target == c.Id))
			{
				campath = false;
				target = c.Id;
			}
			ImGui::PopID();
		}
		for (size_t i = 0; i < paths.GetCount(); ++i)
		{
			const auto name = paths.GetName(i);
			ImGui::PushID(("path_source_" + std::to_string(paths.GetId(i))).c_str());
			if (ImGui::Selectable(name.c_str(), campath && target == paths.GetId(i)))
			{
				campath = true;
				target = paths.GetId(i);
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	ImGui::BeginDisabled(!mapping);
 const bool adding = g_SelectedCameraClip == ~std::uint64_t(0);
 const bool addRequested = adding && ImGui::Button("Add camera clip");
 if (!adding) ImGui::TextDisabled("Camera clip properties update automatically.");
 if (addRequested || (!adding && !textEditing && mapping))
	{
		advancedfx::CameraClip clip = found != clips.end() ? *found : advancedfx::CameraClip{};
		clip.Id = g_SelectedCameraClip;
  bool timeMapped = true;
  if (adding || tick != loadedTick)
   timeMapped = CinematicClock(adding && musicalDraftDemo>=0. ? musicalDraftDemo : tick * interval, false, clip.Time, SceneCachePreviewActive());
		clip.Blend = blend;
		clip.TransitionOffset = transitionOffset;
		clip.BlendCurve = blendCurve;
		clip.RotationCurve = rotationCurve;
		clip.FovCurve = fovCurve;
		clip.FreezeOutgoing = freezeOutgoing;
		clip.CurvedPath = curvedPath;
		clip.HandleOut = handleOut;
		clip.HandleIn = handleIn;
		clip.Campath = campath;
		clip.Target = target;
  if (adding || !(clip == loadedValue))
  {
		clipStatus.clear();
  if (!timeMapped) clipStatus = "Clip time cannot be mapped to the demo clock.";
		for (const auto &other : clips)
			if (other.Id != clip.Id)
			{
				if (std::abs(other.Time - clip.Time) < 1e-9)
					clipStatus = "Another clip starts at this tick. Select that clip to edit it, or choose a "
								 "different tick.";
				else if ((other.Time < clip.Time && other.TransitionStart() >= clip.TransitionStart() - 1e-9) ||
						 (other.Time > clip.Time && other.TransitionStart() <= clip.TransitionStart() + 1e-9))
					clipStatus = "Transition starts must follow the same order as camera edit points. "
								 "Move the envelope offset or edit point.";
			}
		if (clipStatus.empty() && MirvCam_EditClipLive(clip, &gesture))
		{
			g_SelectedCameraClip = clip.Id;
   if (found != clips.end()) *found = clip;
			loaded = clip.Id;
   loadedValue = clip;
   loadedTick = tick;
   musicalDraftDemo = -1.;
			clipStatus = g_DocumentStatus = "Camera clip updated.";
		}
		else if (clipStatus.empty())
			clipStatus = "Clip rejected. Check its source, blend duration (0..3600 seconds), 256-clip limit "
						 "and demo session.";
  }
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(g_SelectedCameraClip==~std::uint64_t(0));
	if (ImGui::Button("Remove camera clip") && MirvCam_RemoveClip(g_SelectedCameraClip))
		g_SelectedCameraClip = loaded = ~std::uint64_t(0);
	ImGui::EndDisabled();
	if (ImGui::Button("Clear camera clips"))
	{
		MirvCam_ClearClips();
		g_SelectedCameraClip = loaded = ~std::uint64_t(0);
	}
	if (found != clips.end() && ImGui::CollapsingHeader("Clip modifiers"))
	{
		auto clip = *found;
		auto definitions = MirvCam_Modifiers();
		if (definitions.empty())
			ImGui::TextDisabled("Create shared effects in Camera modifiers below.");
		if (ImGui::BeginCombo("Link modifier", "Choose shared modifier"))
		{
			for (const auto &m : definitions)
			{
				ImGui::PushID(std::to_string(m.Id).c_str());
				if (ImGui::Selectable(m.Name.c_str()))
				{
					advancedfx::CameraClipModifier link;
					link.Id = m.Id;
					auto next = std::next(found);
					link.End = next != clips.end() ? next->Time - clip.Time : (std::max)(1.0, clip.Blend);
					if (m.Settings.End > m.Settings.Begin)
					{
						link.FadeIn =
							std::clamp(m.Settings.FadeIn / (m.Settings.End - m.Settings.Begin) * link.End,
									   0.0, link.End);
						link.FadeOut =
							std::clamp(m.Settings.FadeOut / (m.Settings.End - m.Settings.Begin) * link.End,
									   0.0, link.End);
					}
					auto existing = std::find_if(clip.Modifiers.begin(), clip.Modifiers.end(),
												 [&](const auto &l) { return l.Id == m.Id; });
					if (existing == clip.Modifiers.end())
						clip.Modifiers.push_back(link);
					else
					{
						existing->Enabled = true;
						if (existing->Influence == 0)
							existing->Influence = 1;
					}
					if (!MirvCam_EditClip(clip))
						g_DocumentStatus = "Could not link modifier (64-instance limit).";
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		for (size_t i = 0; i < clip.Modifiers.size(); ++i)
		{
			ImGui::PushID(int(i));
			auto &link = clip.Modifiers[i];
			auto definition = std::find_if(definitions.begin(), definitions.end(),
										   [&](const auto &m) { return m.Id == link.Id; });
			ImGui::TextUnformatted(definition == definitions.end() ? "Missing modifier"
																   : definition->Name.c_str());
			bool changed = ImGui::Checkbox("Enabled", &link.Enabled);
			float influence = float(link.Influence);
			if (ImGui::SliderFloat("Influence", &influence, 0, 1, "%.2f"))
			{
				link.Influence = influence;
				changed = true;
			}
			double clipDemo = 0;
			CinematicClock(clip.Time, true, clipDemo, SceneCachePreviewActive());
			int beginTick = int(std::llround((clipDemo + link.Begin) / interval));
			int endTick = int(std::llround((clipDemo + link.End) / interval));
			int fadeIn = int(std::llround(link.FadeIn / interval)),
				fadeOut = int(std::llround(link.FadeOut / interval));
			ImGui::BeginDisabled(!mapping);
			if (ImGui::InputInt("Modifier start tick", &beginTick, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				link.Begin = beginTick * interval - clipDemo;
				changed = true;
			}
			if (ImGui::InputInt("Modifier end tick", &endTick, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				link.End = endTick * interval - clipDemo;
				changed = true;
			}
			if (ImGui::InputInt("Fade in (ticks)", &fadeIn, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				link.FadeIn = fadeIn * interval;
				changed = true;
			}
			if (ImGui::InputInt("Fade out (ticks)", &fadeOut, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				link.FadeOut = fadeOut * interval;
				changed = true;
			}
			ImGui::EndDisabled();
			if (ImGui::Button("Up") && i)
			{
				std::swap(clip.Modifiers[i], clip.Modifiers[i - 1]);
				changed = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Down") && i + 1 < clip.Modifiers.size())
			{
				std::swap(clip.Modifiers[i], clip.Modifiers[i + 1]);
				changed = true;
			}
			ImGui::SameLine();
   if (ImGui::Button("Convert to adjustment clip"))
   {
    advancedfx::SequenceModifierClip next;
    next.Begin = clip.Time + link.Begin; next.End = clip.Time + link.End;
    const int track = FreeModifierTrack(next, MirvCam_SequenceModifiers());
    if (track >= 0 && MirvCam_ConvertClipModifier(clip.Id, i, track, next))
    {
     SelectSequenceModifier(next);
     clipStatus = "Converted: adjustment now affects the whole composite, including transitions.";
     ImGui::PopID();
     break;
    }
    clipStatus = "Conversion rejected: no free track or invalid timing.";
   }
   ImGui::SameLine();
			if (ImGui::Button("Unlink"))
			{
				clip.Modifiers.erase(clip.Modifiers.begin() + i);
				changed = true;
			}
			if (changed && !MirvCam_EditClip(clip))
				g_DocumentStatus = "Invalid modifier range or fades; previous values retained.";
			ImGui::PopID();
			if (changed)
				break;
		}
		ImGui::TextDisabled(
			"Legacy ranges follow their parent clip. Ctrl-drag fade markers, even at zero duration. Convert to affect the entire sequence; this may change transition behavior.");
	}
	ImGui::TextWrapped(
		"Camera edit points and transition envelopes are independent. Envelopes may overlap: a later "
		"transition blends from the prior composite, and Freeze outgoing captures it at the boundary. "
		"Envelope starts must retain edit-point order. The first clip provides pre-roll and has no incoming "
		"transition. Save the cinematic project to preserve timing, handles and easing.");
	if (!clipStatus.empty())
		ImGui::TextWrapped("%s", clipStatus.c_str());
	if (!mapping)
		ImGui::TextDisabled("Demo tick mapping is unavailable.");
 DrawSequenceModifierPanel(mapping, demo, interval);
 DrawCameraModifierLibrary();
}
static void DrawSharedCollisionOptions(MirvCameraCollisionSettings *value=nullptr,
 const std::function<bool(const MirvCameraCollisionSettings&)> &commit={})
{
 auto collision=value?*value:MirvCameraCollisionGet();bool dirty=false;
 dirty|=ImGui::Checkbox("Ignore local player",&collision.IgnoreLocal);
 dirty|=ImGui::Checkbox("Collide with other players",&collision.Players);
 dirty|=ImGui::Checkbox("Slide along obstacles",&collision.Slide);
 dirty|=ImGui::InputDouble("Camera clearance (units)",&collision.Radius,0,0,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue);
 dirty|=ImGui::InputDouble("Collision return half-life (s)",&collision.ReturnHalfLife,0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue);
 dirty|=ImGui::InputDouble("Maximum return speed (units/s)",&collision.ReturnSpeed,0,0,"%.1f",ImGuiInputTextFlags_EnterReturnsTrue);
 if(dirty){if(commit)commit(collision);else MirvCameraCollisionSet(collision);}
 if(value)*value=collision;
}
static std::string CameraModifierLabel(const MirvCameraModifier &modifier,
									   const std::vector<MirvCameraModifier> &definitions)
{
	return std::count_if(definitions.begin(), definitions.end(),
						 [&](const auto &m) { return m.Name == modifier.Name; }) > 1
			   ? modifier.Name + " [#" + std::to_string(modifier.Id) + "]"
			   : modifier.Name;
}
static const char *CameraModifierDefaultName(CamPath::ModifierType type)
{
	return type == CamPath::ModifierType::Remap			? "Remap"
		   : type == CamPath::ModifierType::Limits		? "Limits"
		   : type == CamPath::ModifierType::Oscillation ? "Oscillation"
														: "Shake";
}
static bool CameraModifierHasDefaultName(const std::string &name, CamPath::ModifierType type)
{
	const std::string base = CameraModifierDefaultName(type);
	if (name == base)
		return true;
	if (name.compare(0, base.size() + 1, base + " ") != 0)
		return false;
	const std::string suffix = name.substr(base.size() + 1);
	return !suffix.empty() && suffix[0] != '0' &&
		   std::all_of(suffix.begin(), suffix.end(), [](char c) { return c >= '0' && c <= '9'; });
}
static void DrawCameraModifierLibrary()
{
	if (g_FocusModifierLibrary) {ImGui::SetNextItemOpen(true, ImGuiCond_Always); g_FocusModifierLibrary = false;}
	if (!ImGui::CollapsingHeader("Camera modifiers"))
		return;
	ImGui::PushID("camera_modifier_library");
	auto modifiers = MirvCam_Modifiers();
	auto &selected = g_SelectedModifierDefinition;
 static std::uint64_t gesture = 0;
 if (!ImGui::IsMouseDown(0)) gesture = 0;
	static MirvCameraModifier draft, baseline;
	static std::uint64_t draftId = ~std::uint64_t(0);
	static char name[129]{};
	static int cameraPreset = 0;
	const char *cameraPresets[] = {"Handheld",
								   "Impact shake",
								   "Oscillation",
								   "Subtle handheld",
								   "Heavy handheld",
								   "Gentle drift",
								   "FOV punch",
								   "Recoil",
								   "Dolly",
								   "Slide",
								   "Roll / bank",
								   "FOV soft bounds",
								   "Pitch soft bounds",
								   "Half roll",
								   "Widen FOV"};
	const char *cameraPresetIds[] = {"handheld",	   "impact",	   nullptr,		 "subtle-handheld",
									 "heavy-handheld", "camera-drift", "zoom-punch", "recoil",
									 "dolly",		   "slide",		   "bank",		 "fov-safe",
									 "pitch-safe",	   "reduce-roll",  "widen-fov"};
	ImGui::SetNextItemWidth(180);
	ImGui::Combo("Camera modifier preset", &cameraPreset, cameraPresets, IM_ARRAYSIZE(cameraPresets));
	ImGui::SameLine();
	if (ImGui::Button("Create modifier from preset"))
	{
		MirvCameraModifier preset;
		preset.Name = cameraPresets[cameraPreset];
		bool ok = true;
		if (cameraPresetIds[cameraPreset])
			ok = CamPath::MakeModifierPreset(cameraPresetIds[cameraPreset], 0, 1, preset.Settings);
		else
		{
			preset.Settings.Type = CamPath::ModifierType::Oscillation;
			preset.Settings.Begin = 0;
			preset.Settings.End = 1;
			preset.Settings.Amplitude[4] = preset.Settings.Amplitude[5] = 1;
		}
		if (ok && MirvCam_SetModifier(preset))
		{
			selected = preset.Id;
			draftId = ~std::uint64_t(0);
			modifiers = MirvCam_Modifiers();
		}
	}
	if (ImGui::Button("New modifier"))
	{
		MirvCameraModifier m;
		m.Name = "Shake";
		m.Settings.Begin = 0;
		m.Settings.End = 1000000;
		m.Settings.FadeIn = m.Settings.FadeOut = 0;
		m.Settings.Frequency = 4;
		m.Settings.Amplitude[4] = m.Settings.Amplitude[5] = 1;
		if (MirvCam_SetModifier(m))
		{
			selected = m.Id;
			draftId = ~std::uint64_t(0);
			modifiers = MirvCam_Modifiers();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Duplicate modifier") && selected)
	{
		auto found =
			std::find_if(modifiers.begin(), modifiers.end(), [&](const auto &m) { return m.Id == selected; });
		if (found != modifiers.end())
		{
			auto copy = *found;
			copy.Id = 0;
			copy.Name += " copy";
			if (MirvCam_SetModifier(copy))
			{
				selected = copy.Id;
				draftId = ~std::uint64_t(0);
				modifiers = MirvCam_Modifiers();
			}
		}
	}
	for (const auto &m : modifiers)
	{
		ImGui::PushID(std::to_string(m.Id).c_str());
		if (ImGui::Selectable(CameraModifierLabel(m, modifiers).c_str(), selected == m.Id))
		{
			selected = m.Id;
			draftId = ~std::uint64_t(0);
		}
		ImGui::PopID();
	}
	auto found =
		std::find_if(modifiers.begin(), modifiers.end(), [&](const auto &m) { return m.Id == selected; });
	if (found != modifiers.end())
	{
		if (draftId != selected || baseline.Name != found->Name || !(baseline.Settings == found->Settings))
		{
			baseline = *found;
			draft = *found;
			std::snprintf(name, sizeof(name), "%s", draft.Name.c_str());
			draftId = selected;
   gesture = 0;
		}
		ImGui::InputText("Modifier name", name, sizeof(name));
  bool textEditing = ImGui::IsItemActive();
  if (ImGui::IsItemDeactivatedAfterEdit()) draft.Name = name;
		int type = draft.Settings.Type == CamPath::ModifierType::Remap		   ? 3
				   : draft.Settings.Type == CamPath::ModifierType::Limits	   ? 2
				   : draft.Settings.Type == CamPath::ModifierType::Oscillation ? 1
																			   : 0;
		const char *types[] = {"Noise / shake", "Oscillation", "Value limits", "Value remap"};
		const auto oldType = draft.Settings.Type;
		if (ImGui::Combo("Modifier type", &type, types, 4))
		{
			draft.Settings.Type = type == 3	  ? CamPath::ModifierType::Remap
								  : type == 2 ? CamPath::ModifierType::Limits
								  : type	  ? CamPath::ModifierType::Oscillation
											  : CamPath::ModifierType::Noise;
			if (type >= 2)
			{
				draft.Settings.CameraSpace = false;
				if (draft.Settings.Frequency <= 0)
					draft.Settings.Frequency = 1;
				std::fill(draft.Settings.Amplitude, draft.Settings.Amplitude + 7, 0.0);
				draft.Settings.Amplitude[3] = 1;
			}
			if (CameraModifierHasDefaultName(draft.Name, oldType))
			{
				auto renamed = *found;
				const std::string base = CameraModifierDefaultName(draft.Settings.Type);
				renamed.Name = base;
				unsigned suffix = 2;
				while (std::any_of(modifiers.begin(), modifiers.end(), [&](const auto &m)
								   { return m.Id != renamed.Id && m.Name == renamed.Name; }))
					renamed.Name = base + " " + std::to_string(suffix++);
    draft.Name = renamed.Name;
    std::snprintf(name, sizeof(name), "%s", renamed.Name.c_str());
			}
		}
		auto &m = draft.Settings;
		ImGui::Checkbox("Modifier enabled", &m.Enabled);
		if (m.Type == CamPath::ModifierType::Limits)
		{
			ImGui::InputDouble("Lower bound", &m.Lower); textEditing |= ImGui::IsItemActive();
			ImGui::InputDouble("Upper bound", &m.Upper); textEditing |= ImGui::IsItemActive();
			ImGui::InputDouble("Softness", &m.Softness); textEditing |= ImGui::IsItemActive();
		}
		if (m.Type == CamPath::ModifierType::Remap)
		{
			ImGui::InputDouble("Scale", &m.Scale); textEditing |= ImGui::IsItemActive();
			ImGui::InputDouble("Offset", &m.Offset); textEditing |= ImGui::IsItemActive();
			ImGui::InputDouble("Pivot", &m.Pivot); textEditing |= ImGui::IsItemActive();
		}
		ImGui::BeginDisabled(m.Type == CamPath::ModifierType::Limits ||
							 m.Type == CamPath::ModifierType::Remap);
		ImGui::Checkbox("Camera space", &m.CameraSpace);
		ImGui::EndDisabled();
		ImGui::InputDouble("Strength", &m.Strength); textEditing |= ImGui::IsItemActive();
		ImGui::InputDouble("Frequency (Hz)", &m.Frequency); textEditing |= ImGui::IsItemActive();
		ImGui::InputDouble("Phase (cycles)", &m.Phase); textEditing |= ImGui::IsItemActive();
		ImGui::InputScalar("Octaves", ImGuiDataType_U32, &m.Octaves); textEditing |= ImGui::IsItemActive();
		ImGui::InputDouble("Roughness", &m.Roughness); textEditing |= ImGui::IsItemActive();
		DrawModifierEnvelope(m, selected);
		ImGui::TextWrapped("Gesture presets use zero frequency and phase 0.25; edit their strength envelope "
						   "to shape movement. Camera-space X/Y/Z means forward/right/up. Clip timing and "
						   "fades are edited in the sequencer.");
		ImGui::InputScalar("Seed", ImGuiDataType_U32, &m.Seed); textEditing |= ImGui::IsItemActive();
		ImGui::InputScalarN("Position amplitude (X/Y/Z)", ImGuiDataType_Double, m.Amplitude, 3); textEditing |= ImGui::IsItemActive();
		ImGui::InputDouble("FOV amplitude", &m.Amplitude[3]); textEditing |= ImGui::IsItemActive();
		ImGui::InputScalarN("Angle amplitude (pitch/yaw/roll)", ImGuiDataType_Double, m.Amplitude + 4, 3); textEditing |= ImGui::IsItemActive();
  if (!textEditing && (draft.Name != baseline.Name || !(draft.Settings == baseline.Settings)))
  {
   if (MirvCam_SetModifierLive(draft, &gesture))
   {baseline = draft; g_DocumentStatus = "Shared effect updated live.";}
   else g_DocumentStatus = "Use a unique name and valid modifier values; previous settings retained.";
  }
  ImGui::TextDisabled("Changes take effect automatically when valid.");
		if (ImGui::Button("Delete modifier"))
		{
			if (MirvCam_RemoveModifier(selected))
			{
				selected = 0;
				draftId = ~std::uint64_t(0);
				g_DocumentStatus =
					"Modifier deleted and unlinked from all clips and legacy cameras. Undo restores it.";
			}
			else
				g_DocumentStatus = "Modifier was not deleted; its entry no longer exists.";
		}
	}
 ImGui::TextWrapped("Shared effect definitions: editing one affects every linked adjustment and legacy link. Timing, fades and mix belong to each independent adjustment clip.");
	ImGui::TextWrapped("%s", g_DocumentStatus.c_str());
	ImGui::PopID();
}
static void DrawCamerasTab()
{
	g_CamerasTabActive = ImGui::BeginTabItem("Cameras", nullptr, g_FocusCameras ? ImGuiTabItemFlags_SetSelected : 0);
	if (!g_CamerasTabActive)
		return;
	g_FocusCameras = false;
	if (ImGui::Button("Add camera") && MirvCam_SelectedCameraAction("add"))
	{
		MirvCam_SetRigNavigation(true);
		g_DocumentStatus = "Camera added. The free camera is ready.";
	}

	auto cameras = MirvCam_Cameras(true);
	static std::vector<std::uint64_t> selectedIds;
	static std::uint64_t observed = ~std::uint64_t(0), rangeAnchor = ~std::uint64_t(0);
	std::uint64_t selected = ~std::uint64_t(0);
	for (const auto &camera : cameras)
		if (camera.Selected)
			selected = camera.Id;
	selectedIds.erase(std::remove_if(selectedIds.begin(), selectedIds.end(),
									 [&](auto id)
									 {
										 return std::none_of(cameras.begin(), cameras.end(),
															 [&](const auto &c) { return c.Id == id; });
									 }),
					  selectedIds.end());
	if (selected != observed)
	{
		if (std::find(selectedIds.begin(), selectedIds.end(), selected) == selectedIds.end())
		{
			selectedIds.clear();
			if (selected != ~std::uint64_t(0))
				selectedIds.push_back(selected);
		}
		observed = selected;
	}
	if (cameras.empty())
		DrawOverlayHint("No cameras yet. Add one here, or use mirv_cam attach in live playback.");
	if (ImGui::BeginChild("##camera_list", ImVec2(0, 120), true))
	{
		for (const auto &camera : cameras)
		{
			ImGui::PushID(std::to_string(camera.Id).c_str());
			const auto label = std::to_string(camera.Index) + ": " + camera.Name +
							   (camera.Attached ? " [attached]" : " [stationary]") +
							   (camera.Viewed ? " [viewing]" : "") +
							   (!camera.Available ? " [unavailable]" : "");
			const bool picked =
				std::find(selectedIds.begin(), selectedIds.end(), camera.Id) != selectedIds.end();
			if (ImGui::Selectable(label.c_str(), picked))
			{
				if (ImGui::GetIO().KeyShift && rangeAnchor != ~std::uint64_t(0))
				{
					if (!ImGui::GetIO().KeyCtrl)
						selectedIds.clear();
					auto anchor = std::find_if(cameras.begin(), cameras.end(),
											   [&](const auto &c) { return c.Id == rangeAnchor; });
					const size_t start = anchor == cameras.end() ? camera.Index : anchor->Index;
					for (const auto &c : cameras)
						if (c.Index >= (std::min)(start, camera.Index) &&
							c.Index <= (std::max)(start, camera.Index) &&
							std::find(selectedIds.begin(), selectedIds.end(), c.Id) == selectedIds.end())
							selectedIds.push_back(c.Id);
				}
				else if (ImGui::GetIO().KeyCtrl)
				{
					if (picked)
						selectedIds.erase(std::remove(selectedIds.begin(), selectedIds.end(), camera.Id),
										  selectedIds.end());
					else
						selectedIds.push_back(camera.Id);
					rangeAnchor = camera.Id;
				}
				else
				{
					const bool deselect = picked && selectedIds.size() == 1;
					selectedIds.clear();
					if (!deselect)
						selectedIds.push_back(camera.Id);
					rangeAnchor = camera.Id;
				}
				if (selectedIds.empty())
				{
					MirvCam_ClearCameraSelection();
					selected = ~std::uint64_t(0);
				}
				else
				{
					selected =
						std::find(selectedIds.begin(), selectedIds.end(), camera.Id) != selectedIds.end()
							? camera.Id
							: selectedIds.back();
					MirvCam_SelectCamera(selected);
				}
				observed = selected;
			}
			ImGui::PopID();
		}
	}
	ImGui::EndChild();
	if (ImGui::Button("Select all cameras"))
	{
		selectedIds.clear();
		for (const auto &c : cameras)
			selectedIds.push_back(c.Id);
		if (!selectedIds.empty())
		{
			selected = selectedIds.front();
			observed = selected;
			MirvCam_SelectCamera(selected);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Deselect cameras"))
	{
		selectedIds.clear();
		selected = observed = ~std::uint64_t(0);
		MirvCam_ClearCameraSelection();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%zu selected", selectedIds.size());
	auto editSelected = [&](const std::function<bool(std::uint64_t)> &edit)
	{ return MirvCam_EditCameras(selectedIds, edit); };
	if (!selectedIds.empty())
	{
		if (ImGui::Button(selectedIds.size() > 1 ? "Remove selected cameras" : "Remove camera"))
		{
			g_DocumentStatus = editSelected([](auto id) { return MirvCam_CameraAction("remove", id); })
								   ? "Cameras removed. Undo restores them."
								   : "Could not remove cameras.";
			selectedIds.clear();
			selected = observed = ~std::uint64_t(0);
			MirvCam_ClearCameraSelection();
		}
	}
	if (MirvCam_RigPreviewActive())
	{
		if (ImGui::Button("Stop preview"))
			MirvCam_EndRigPreview();
	}
	cameras = MirvCam_Cameras(true);
	auto found =
		std::find_if(cameras.begin(), cameras.end(), [&](const auto &camera) { return camera.Id == selected; });
	if (found != cameras.end())
	{
		ImGui::BeginDisabled(selectedIds.size() != 1);
		static std::uint64_t nameId = ~std::uint64_t(0);
		static std::string previousName;
		static char name[129]{};
		if (nameId != selected || previousName != found->Name)
		{
			nameId = selected;
			previousName = found->Name;
			snprintf(name, sizeof(name), "%s", found->Name.c_str());
		}
		bool rename =
			ImGui::InputText("Camera name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		rename = ImGui::Button("Rename camera") || rename;
		if (rename)
			g_DocumentStatus = MirvCam_RenameCamera(selected, name)
								   ? "Camera renamed."
								   : "Use a unique, nonnumeric camera name (1..128 bytes).";

		ImGui::EndDisabled();
		if (selectedIds.size() > 1)
			ImGui::TextWrapped(
				"Changes apply to all selected cameras. Unchanged fields keep each camera's values. "
				"Recapture applies the full current view to every selected camera.");
		if (ImGui::Button(MirvCam_SequenceEnabled() ? "Preview sequence" : "Preview camera"))
		{
			if (MirvCam_SequenceEnabled())
				MirvCampathPreview_SetActive(true);
			else
				MirvCam_CameraAction("preview", selected);
		}
		ImGui::SameLine();
		if (ImGui::Button("Edit camera"))
		{
			if (MirvCam_CameraAction("edit", selected))
			{
				MirvCam_SetRigNavigation(true);
				g_DocumentStatus = "Reframe with the free camera, then Recapture camera.";
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Recapture camera"))
			g_DocumentStatus = editSelected([](auto id) { return MirvCam_CameraAction("capture", id); })
								   ? "Camera recaptured."
								   : "Camera could not be recaptured; its target is unavailable.";

		if (!found->Modifiers.empty() && ImGui::CollapsingHeader("Legacy camera modifier stack"))
		{
			auto definitions = MirvCam_Modifiers();
			if (selectedIds.size() == 1)
			{
				for (size_t i = 0; i < found->Modifiers.size(); ++i)
				{
					auto links = found->Modifiers;
					auto &link = links[i];
					ImGui::PushID(int(i));
					auto definition = std::find_if(definitions.begin(), definitions.end(),
												   [&](const auto &m) { return m.Id == link.Id; });
					ImGui::TextUnformatted(definition == definitions.end() ? "Missing modifier"
																		   : definition->Name.c_str());
					bool changed = ImGui::Checkbox("Enabled", &link.Enabled);
					ImGui::SameLine();
					float influence = float(link.Influence);
					if (ImGui::SliderFloat("Influence", &influence, 0, 1, "%.2f"))
					{
						link.Influence = influence;
						changed = true;
					}
					if (ImGui::Button("Up") && i)
					{
						std::swap(links[i], links[i - 1]);
						changed = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("Down") && i + 1 < links.size())
					{
						std::swap(links[i], links[i + 1]);
						changed = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("Unlink"))
					{
						links.erase(links.begin() + i);
						changed = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("Make independent") && definition != definitions.end())
					{
						auto copy = *definition;
						copy.Id = 0;
						copy.Name += " copy";
						MirvCam_EditCameras({selected},
											[&](auto id)
											{
												if (!MirvCam_SetModifier(copy))
													return false;
												links[i].Id = copy.Id;
												return MirvCam_SetModifierLinks(id, links);
											});
					}
					if (changed)
						MirvCam_SetModifierLinks(selected, links);
					ImGui::PopID();
				}
			}
			DrawOverlayHint("Legacy links are retained for project compatibility. New effects belong to "
								"clips. These links affect camera preview and "
								"sequence playback; free-camera editing shows the base pose.");
		}
		if (ImGui::CollapsingHeader("Camera pose"))
		{
			static std::uint64_t poseId = ~std::uint64_t(0);
			static double position[3]{}, angles[3]{}, fov = 90;
			static double basePosition[3]{}, baseAngles[3]{}, baseFov = 90;
			static bool poseDirty = false;
			auto readPose = [&]()
			{
				poseId = selected;
				for (int axis = 0; axis < 3; ++axis)
					position[axis] = found->BasePose.Position[axis];
				const auto rotation = found->BasePose.Rotation.ToQREulerAngles().ToQEulerAngles();
				angles[0] = rotation.Pitch;
				angles[1] = rotation.Yaw;
				angles[2] = rotation.Roll;
				fov = found->BasePose.Fov;
				std::copy(position, position + 3, basePosition);
				std::copy(angles, angles + 3, baseAngles);
				baseFov = fov;
				poseDirty = false;
			};
			if (poseId != selected || (!poseDirty && !ImGui::IsAnyItemActive()))
				readPose();
			poseDirty |= ImGui::InputScalarN("Position (X/Y/Z)", ImGuiDataType_Double, position, 3, nullptr,
											 nullptr, "%.3f");
			poseDirty |= ImGui::InputScalarN("Angles (pitch/yaw/roll)", ImGuiDataType_Double, angles, 3,
											 nullptr, nullptr, "%.3f");
			poseDirty |= ImGui::InputDouble("Camera FOV", &fov, 0, 0, "%.3f");
			if (ImGui::Button("Apply camera pose"))
			{
				advancedfx::CameraPose pose;
				for (int axis = 0; axis < 3; ++axis)
					pose.Position[axis] = position[axis];
				pose.Rotation = Quaternion::FromQREulerAngles(
					QREulerAngles::FromQEulerAngles(QEulerAngles(angles[0], angles[1], angles[2])));
				pose.Fov = fov;
				const bool valid = pose.Valid() && std::all_of(std::begin(angles), std::end(angles),
															   [](double a) { return std::isfinite(a); });
				const bool applied =
					valid &&
					editSelected(
						[&](auto id)
						{
							if (selectedIds.size() == 1)
								return MirvCam_EditCamera(id, pose);
							auto camera = std::find_if(cameras.begin(), cameras.end(),
													   [&](const auto &c) { return c.Id == id; });
							if (camera == cameras.end())
								return false;
							auto updated = camera->BasePose;
							const auto euler = updated.Rotation.ToQREulerAngles().ToQEulerAngles();
							double updatedAngles[3] = {euler.Pitch, euler.Yaw, euler.Roll};
							bool changed = false, rotationChanged = false;
							for (int axis = 0; axis < 3; ++axis)
							{
								if (position[axis] != basePosition[axis])
								{
									updated.Position[axis] = position[axis];
									changed = true;
								}
								if (angles[axis] != baseAngles[axis])
								{
									updatedAngles[axis] = angles[axis];
									changed = rotationChanged = true;
								}
							}
							if (fov != baseFov)
							{
								updated.Fov = fov;
								changed = true;
							}
							if (rotationChanged)
								updated.Rotation =
									Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(
										QEulerAngles(updatedAngles[0], updatedAngles[1], updatedAngles[2])));
							return !changed || MirvCam_EditCamera(id, updated);
						});
				g_DocumentStatus = applied ? "Camera pose updated."
								   : valid ? "Camera target unavailable; pose retained."
										   : "Use finite pose values and a FOV between 1 and 179 degrees.";
				if (applied)
					poseDirty = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset camera fields"))
				readPose();
			ImGui::TextDisabled(
				"World position and angles. Attached cameras keep their target and attachment mode.");
		}
		if (ImGui::CollapsingHeader("Attachment"))
		{
			static std::uint64_t attachmentId = ~std::uint64_t(0);
			static int entity = -1, mode = 0;
			static char bone[128]{};
			static int sourceEntity = -2, sourceMode = -1, sourceBone = -2;
			static std::string sourceBoneName;
			if (attachmentId != selected || sourceEntity != found->EntityAddress ||
				sourceMode != found->AttachMode || sourceBone != found->BoneIndex ||
				sourceBoneName != found->BoneName)
			{
				attachmentId = selected;
				sourceEntity = found->EntityAddress;
				sourceMode = found->AttachMode;
				sourceBone = found->BoneIndex;
				sourceBoneName = found->BoneName;
				entity = found->EntityAddress;
				mode = found->AttachMode;
				std::snprintf(bone, sizeof(bone), "%s",
							  found->BoneName.empty() ? std::to_string(found->BoneIndex).c_str()
													  : found->BoneName.c_str());
			}
			ImGui::InputInt("Attachment entity", &entity);
			ImGui::InputText("Attachment bone", bone, sizeof(bone));
			const char *modes[] = {"Bone", "Entity", "Aligned", "World"};
			ImGui::Combo("Attachment mode", &mode, modes, 4);
			ImGui::BeginDisabled(SceneCachePreviewActive());
			if (ImGui::Button("Apply attachment"))
				g_DocumentStatus =
					editSelected(
						[&](auto id)
						{
							auto camera = std::find_if(cameras.begin(), cameras.end(),
													   [&](const auto &c) { return c.Id == id; });
							if (camera == cameras.end())
								return false;
							const auto initialBone =
								found->BoneName.empty() ? std::to_string(found->BoneIndex) : found->BoneName;
							const auto cameraBone = camera->BoneName.empty()
														? std::to_string(camera->BoneIndex)
														: camera->BoneName;
							return MirvCam_EditAttachment(
								id, entity != found->EntityAddress ? entity : camera->EntityAddress,
								bone != initialBone ? bone : cameraBone.c_str(),
								mode != found->AttachMode ? mode : camera->AttachMode);
						})
						? "Camera attachment updated; relative framing preserved."
						: "Attachment unavailable. Check the entity and bone in live playback.";
			ImGui::EndDisabled();
			ImGui::TextWrapped("Bone accepts -1, an index, or a name. Apply changes this camera without "
							   "creating another. Return to live playback to change targets.");
		}
		if (ImGui::CollapsingHeader("Motion"))
		{
			static std::uint64_t motionId = ~std::uint64_t(0);
			static advancedfx::CameraMotion motion;
			static bool motionDirty = false;
			if (motionId != selected || (!motionDirty && !ImGui::IsAnyItemActive()))
			{
				motionId = selected;
				motion = found->Motion;
				motionDirty = false;
			}
			ImGui::BeginDisabled(!found->Attached);
			static int followPreset = 0;
			const char *followPresets[] = {"Rigid attachment", "Stable head mount", "Soft follow"};
			const char *followPresetIds[] = {"rigid", "stable-head", "soft-follow"};
			ImGui::SetNextItemWidth(180);
			ImGui::Combo("Motion preset", &followPreset, followPresets, 3);
			ImGui::SameLine();
			if (ImGui::Button("Apply motion preset"))
			{
				advancedfx::CameraMotion preset;
				advancedfx::CameraMotion::MakePreset(followPresetIds[followPreset], preset);
				motionDirty = !editSelected([&](auto id) { return MirvCam_EditMotion(id, preset); });
				motion = preset;
			}
			ImGui::BeginDisabled(found->AttachMode == 3); // World heading remains fixed.
			motionDirty |= ImGui::Checkbox("Preserve subject framing", &motion.PreserveFraming);
			ImGui::EndDisabled();
			motionDirty |= ImGui::InputDouble("Follow half-life (s)", &motion.FollowHalfLife, 0, 0, "%.3f");
   if(ImGui::IsItemHovered())ImGui::SetTooltip("Smooths entity root translation. Bone and orbit half-lives control the other motion channels; zero keeps that channel rigid.");
			motionDirty |=
				ImGui::InputDouble("Follow bone half-life (s)", &motion.BoneHalfLife, 0, 0, "%.3f");
			motionDirty |=
				ImGui::InputDouble("Follow turn-rate limit (deg/s)", &motion.TurnRate, 0, 0, "%.1f");
			motionDirty |=
				ImGui::InputDouble("Follow orbit half-life (s)", &motion.RotationHalfLife, 0, 0, "%.3f");
			motionDirty |= ImGui::InputDouble("Maximum follow lag", &motion.MaxDistance, 0, 0, "%.1f");
			motionDirty |=
				ImGui::InputDouble("Teleport reset distance", &motion.TeleportDistance, 0, 0, "%.1f");
			motionDirty |= ImGui::InputDouble("Orbit speed (deg/s)", &motion.OrbitSpeed, 0, 0, "%.3f");
			ImGui::Indent();
			if (ImGui::CollapsingHeader("Bone influence and framing limits"))
			{
				motionDirty |=
					ImGui::InputDouble("Framing margin (0..0.45)", &motion.FramingMargin, 0, 0, "%.3f");
				motionDirty |= ImGui::InputDouble("Bone position influence (0..1)",
												  &motion.BonePositionInfluence, 0, 0, "%.3f");
				motionDirty |= ImGui::InputDouble("Bone rotation influence (0..1)",
												  &motion.BoneRotationInfluence, 0, 0, "%.3f");
				motionDirty |= ImGui::InputDouble("Bone displacement soft limit (units)",
												  &motion.BonePositionLimit, 0, 0, "%.2f");
				motionDirty |= ImGui::InputDouble("Bone rotation soft limit (degrees)",
												  &motion.BoneRotationLimit, 0, 0, "%.2f");
			}
			ImGui::Unindent();
			if (ImGui::Button("Apply camera motion"))
			{
				const bool applied = editSelected(
					[&](auto id)
					{
						auto camera = std::find_if(cameras.begin(), cameras.end(),
												   [&](const auto &c) { return c.Id == id; });
						if (camera == cameras.end())
							return false;
						auto updated = camera->Motion;
						if (motion.PreserveFraming != found->Motion.PreserveFraming)
							updated.PreserveFraming = motion.PreserveFraming;
						for (auto field :
							 {&advancedfx::CameraMotion::FollowHalfLife,
							  &advancedfx::CameraMotion::BoneHalfLife,
							  &advancedfx::CameraMotion::RotationHalfLife,
							  &advancedfx::CameraMotion::FramingMargin,
							  &advancedfx::CameraMotion::BonePositionInfluence,
							  &advancedfx::CameraMotion::BoneRotationInfluence,
							  &advancedfx::CameraMotion::BonePositionLimit,
							  &advancedfx::CameraMotion::BoneRotationLimit,
							  &advancedfx::CameraMotion::TurnRate, &advancedfx::CameraMotion::MaxDistance,
							  &advancedfx::CameraMotion::TeleportDistance,
							  &advancedfx::CameraMotion::OrbitSpeed})
							if (motion.*field != found->Motion.*field)
								updated.*field = motion.*field;
						return MirvCam_EditMotion(id, updated);
					});
				g_DocumentStatus = applied ? "Camera motion updated."
										   : "Use half-lives 0..10 s, influence 0..1, margin 0..0.45, "
											 "rotation limit 0..180 degrees and nonnegative distances.";
				if (applied)
					motionDirty = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset camera motion"))
			{
				motion = {};
				motionDirty = !editSelected([&](auto id) { return MirvCam_EditMotion(id, motion); });
				g_DocumentStatus = motionDirty ? "Could not reset camera motion." : "Camera motion reset.";
			}
			ImGui::EndDisabled();
			if (!found->Attached)
				ImGui::TextDisabled("Attach this camera to an entity or bone to use follow and orbit.");
			ImGui::TextWrapped(
				"Root follow adds travel lag. Bone follow and influence reduce animation movement.");
			ImGui::TextDisabled("Attachment framing, limits and orbit (?)");
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34);
				ImGui::TextUnformatted(
					"Framing preserves the attachment anchor's captured screen location. The margin limits "
					"positional lag when the anchor starts inside that safe region. A camera needs an offset "
					"to show its own attachment point. Collision and occlusion can make framing "
					"impossible.\n\nBone influence and soft limits use the captured attachment pose as their "
					"reference; recapture sets a new reference. Zero limits disable the bound. Explicit "
					"aiming owns the final heading. World mode retains its fixed heading and ignores angular "
					"bone controls.\n\nOrbit speed circles the physical pivot on the world-up axis. Capture "
					"an offset first; orbit smoothing and turn limits apply with root follow.");
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}
		if (ImGui::CollapsingHeader("Aiming"))
		{
			auto aimCommand = [&](const char *option, const std::string &value)
			{
				editSelected(
					[&](auto id)
					{
						const std::string command =
							"mirv_aim camera @" + std::to_string(id) + " " + option + " " + value;
						Afx_ExecClientCmd(command.c_str());
						return true;
					});
			};
			auto triple = [](const std::array<double, 3> &values)
			{
				char text[192];
				std::snprintf(text, sizeof(text), "%.17g %.17g %.17g", values[0], values[1], values[2]);
				return std::string(text);
			};
			if (found->AimCustomSource)
				ImGui::TextWrapped("Using a mirv_calcs source. Choosing an entity or world point replaces it. Custom "
								   "aim sources currently require live playback.");
			bool active = found->AimActive, snap = found->AimSnap;
			if (ImGui::Checkbox("Aim at target", &active))
				aimCommand("active", active ? "1" : "0");
			ImGui::SameLine();
			if (ImGui::Checkbox("Snap to target", &snap))
				aimCommand("snapTo", snap ? "1" : "0");
			int entity = found->AimEntity;
			if (ImGui::InputInt("Aim entity", &entity, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
				aimCommand("entityIndex", std::to_string(entity));
			auto point = found->AimPoint, offset = found->AimOffset;
			if (ImGui::InputScalarN("Aim world point", ImGuiDataType_Double, point.data(), 3, nullptr, nullptr, "%.3f",
									ImGuiInputTextFlags_EnterReturnsTrue))
				aimCommand("point abs", triple(point));
			if (ImGui::InputScalarN("Aim target offset", ImGuiDataType_Double, offset.data(), 3, nullptr, nullptr,
									"%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
				aimCommand("offset", triple(offset));
			if (!snap)
			{
				double velocity = found->AimVelocity, acceleration = found->AimAcceleration;
				if (ImGui::InputDouble("Aim velocity limit", &velocity, 0, 0, "%.3f",
									   ImGuiInputTextFlags_EnterReturnsTrue))
					aimCommand("velLimit", std::to_string(velocity));
				if (ImGui::InputDouble("Aim acceleration limit", &acceleration, 0, 0, "%.3f",
									   ImGuiInputTextFlags_EnterReturnsTrue))
					aimCommand("accelLimit", std::to_string(acceleration));
				if (SceneCachePreviewActive())
					ImGui::TextDisabled("Cached aiming currently requires Snap to target.");
			}
			ImGui::TextWrapped("Press Enter to save target fields. A world point replaces entity tracking. Active "
							   "aiming overrides the saved angles, keeping the camera's position.");
		}
	}
	if (ImGui::CollapsingHeader("Transitions"))
	{
		static double transitionSeconds = 1;
		static std::uint64_t destination = ~std::uint64_t(0);
		ImGui::InputDouble("Transition duration (s)", &transitionSeconds, 0, 0, "%.3f");
		if (!std::isfinite(transitionSeconds))
			transitionSeconds = 0;
		transitionSeconds = std::clamp(transitionSeconds, 0.0, 3600.0);
		auto target =
			std::find_if(cameras.begin(), cameras.end(), [&](const auto &c) { return c.Id == destination; });
		const bool campath = destination == ~std::uint64_t(0);
		const std::string targetName = campath					 ? "Active campath"
									   : target != cameras.end() ? target->Name
																 : "Choose a destination";
		if (ImGui::BeginCombo("Transition destination", targetName.c_str()))
		{
			if (ImGui::Selectable("Active campath", campath))
				destination = ~std::uint64_t(0);
			for (const auto &c : cameras)
			{
				const auto label = std::to_string(c.Index) + ": " + c.Name;
				if (ImGui::Selectable(label.c_str(), c.Id == destination))
					destination = c.Id;
			}
			ImGui::EndCombo();
		}
		const bool destinationIsCampath = destination == ~std::uint64_t(0);
		ImGui::BeginDisabled(
			!destinationIsCampath &&
			std::none_of(cameras.begin(), cameras.end(), [&](const auto &c) { return c.Id == destination; }));
		if (ImGui::Button("Transition"))
		{
			char command[160];
			const auto reference =
				destinationIsCampath ? std::string("campath") : "@" + std::to_string(destination);
			std::snprintf(command, sizeof(command), "mirv_cam transition %s %.9g", reference.c_str(),
						  transitionSeconds);
			Afx_ExecClientCmd(command);
		}
		ImGui::EndDisabled();
	}

 if(ImGui::CollapsingHeader("Attached camera collision")){
  bool overrideCollision=found!=cameras.end() && found->CollisionOverride.has_value();
  if(found!=cameras.end() && ImGui::Checkbox("Use per-camera collision override",&overrideCollision)){
   const auto defaults=MirvCameraCollisionGet();
   editSelected([&](auto id){return MirvCam_EditCollision(id,overrideCollision?std::optional<MirvCameraCollisionSettings>(defaults):std::nullopt);});
  }
  auto collision=overrideCollision && found!=cameras.end()?found->CollisionOverride.value_or(MirvCameraCollisionGet()):MirvCameraCollisionGet();
  const auto initialCollision=collision;
  auto commit=[&](const MirvCameraCollisionSettings &value){
   const bool ok=overrideCollision?editSelected([&](auto id){
    auto target=std::find_if(cameras.begin(),cameras.end(),[&](const auto &c){return c.Id==id;});
    if(target==cameras.end())return false;
    auto next=target->CollisionOverride.value_or(MirvCameraCollisionGet());
    auto patch=[&](auto field){if(initialCollision.*field!=value.*field)next.*field=value.*field;};
    patch(&MirvCameraCollisionSettings::Attached);patch(&MirvCameraCollisionSettings::Players);patch(&MirvCameraCollisionSettings::Slide);patch(&MirvCameraCollisionSettings::IgnoreLocal);patch(&MirvCameraCollisionSettings::Debug);patch(&MirvCameraCollisionSettings::Radius);patch(&MirvCameraCollisionSettings::ReturnHalfLife);patch(&MirvCameraCollisionSettings::ReturnSpeed);patch(&MirvCameraCollisionSettings::AttachedSoftDistance);patch(&MirvCameraCollisionSettings::SubjectClearance);patch(&MirvCameraCollisionSettings::NearPlane);patch(&MirvCameraCollisionSettings::FollowRecoveryDelay);
    return MirvCam_EditCollision(id,next);
   }):MirvCameraCollisionSet(value);
   if(!ok)g_DocumentStatus="Invalid collision settings.";return ok;
  };
  ImGui::TextDisabled(overrideCollision?"Editing selected camera overrides.":"Editing global collision defaults.");
  DrawSharedCollisionOptions(&collision,commit);
  bool dirty=false;
  dirty|=ImGui::InputDouble("Minimum subject distance (units)",&collision.SubjectClearance,0,0,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue);
  dirty|=ImGui::InputDouble("Attached near-plane depth (0 disables)",&collision.NearPlane,0,0,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue);
  dirty|=ImGui::Checkbox("Draw collision diagnostics",&collision.Debug);
  dirty|=ImGui::Checkbox("Enable attached collision",&collision.Attached);
  dirty|=ImGui::InputDouble("Attached collision soft zone (units)",&collision.AttachedSoftDistance,0,0,"%.1f",ImGuiInputTextFlags_EnterReturnsTrue);
  dirty|=ImGui::InputDouble("Blocked recovery cut delay (s)",&collision.FollowRecoveryDelay,0,0,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue);
  if(dirty)commit(collision);
  if(found!=cameras.end() && found->CollisionDebug)ImGui::TextWrapped("Collision: %s%s",found->CollisionBlocked?"no subject-sized gap":"clear camera hull",found->CollisionAnchorEmbedded?"; anchor hull overlaps geometry":"");
  ImGui::TextWrapped("Cameras inherit global settings unless overridden. Overrides belong to selected cameras; freecam/campath remain global. Cached preview never traces the live world.");
 }
	if (ImGui::CollapsingHeader("Display"))
	{
		ImGui::Checkbox("Show camera guides", &g_ShowCameraGuides);
		ImGui::SameLine();
		ImGui::Checkbox("Camera frustums", &g_CameraFrustums);
		ImGui::TextDisabled(
			"Selected: gold. Other cameras: purple. Unavailable: gray. Hidden during HLAE recording.");
	}
	ImGui::TextDisabled(
		"Ctrl-click selects multiple cameras; Shift-click selects a range. During preview, selection "
		"switches the view. Tab switches Cameras/Keys.");
	ImGui::TextWrapped("%s", g_DocumentStatus.c_str());
	ImGui::EndTabItem();
}
// The profile editor reads the *same* live registry as mirv_streams. Draft
// changes are applied explicitly, never on each ImGui frame or during a take.
static void DrawRecordingLog()
{
 if (!ImGui::CollapsingHeader("Recording log / timings", ImGuiTreeNodeFlags_DefaultOpen)) return;
 if (ImGui::SmallButton("Clear log")) advancedfx::RecordingLog::Get().Clear();
 static uint64_t revision=0;
 static std::vector<advancedfx::RecordingLog::Entry> entries;
 const bool changed=advancedfx::RecordingLog::Get().Read(revision,entries);
 ImGui::SameLine();
 ImGui::BeginDisabled(entries.empty());
 if (ImGui::SmallButton("Copy log")) {
  const auto text=advancedfx::RecordingLog::ClipboardText(entries);
  ImGui::SetClipboardText(text.c_str());
 }
 ImGui::EndDisabled();
 if (ImGui::BeginChild("##ffmpeg_log",ImVec2(0,ImGui::GetTextLineHeightWithSpacing()*6),true,
                       ImGuiWindowFlags_HorizontalScrollbar)) {
  const bool atBottom=ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-1;
  if (entries.empty()) ImGui::TextDisabled("No recording messages.");
  std::string source;
  for (const auto &entry:entries) {
   if (source!=entry.Source) {source=entry.Source;ImGui::TextDisabled("%s",source.c_str());}
   ImGui::TextUnformatted(entry.Text.data(),entry.Text.data()+entry.Text.size());
  }
  if (changed && atBottom) ImGui::SetScrollHereY(1.0f);
 }
 ImGui::EndChild();
}

static void DrawRecordingProfileEditor(const std::string &selected)
{
	advancedfx::RecordingProfileInfo current;
	if (!Recording::GetProfileInfo(selected.c_str(), current))
	{
		ImGui::TextDisabled("Profile not found in the recording settings registry.");
		return;
	}
	static std::string editingName, status;
	static advancedfx::RecordingProfileInfo draft;
	static char copyName[64] = {};
	static char ffmpegOptions[4096] = {}, ffmpegNoAlphaOptions[4096] = {};
	if (editingName != current.Name)
	{
		editingName = current.Name;
		draft = current;
		copyName[0] = 0;
		std::snprintf(ffmpegOptions, sizeof(ffmpegOptions), "%s", draft.FfmpegOptions.c_str());
  std::snprintf(ffmpegNoAlphaOptions,sizeof(ffmpegNoAlphaOptions),"%s",draft.FfmpegOptionsNoAlpha.c_str());
		status.clear();
	}
	if (!ImGui::CollapsingHeader("Recording profile details / editor", ImGuiTreeNodeFlags_DefaultOpen))
		return;

	const bool recording = Recording::IsActive();
	const bool editable = !current.Protected && !recording;
	const bool supported = current.Type == advancedfx::RecordingProfileInfo::Kind::Sampler || current.Type == advancedfx::RecordingProfileInfo::Kind::Ffmpeg || current.Type == advancedfx::RecordingProfileInfo::Kind::FfmpegEx;
	ImGui::Text("Profile: %s", current.Name.c_str());
	ImGui::SameLine();
	ImGui::TextDisabled(current.Protected ? "(built-in, read-only)" : "(custom)");
	if (recording)
		ImGui::TextDisabled("Stop the current recording before changing any profile.");
	if (current.Type == advancedfx::RecordingProfileInfo::Kind::Sampler)
	{
		ImGui::TextUnformatted("Sampler: captures at the input FPS and blends to its own output FPS.");
		if (!editable) ImGui::BeginDisabled();
		ImGui::SetNextItemWidth(120);
		ImGui::InputFloat("Sampler output FPS", &draft.OutputFps, 0, 0, "%.3f");
		ImGui::Combo("Sampling method", &draft.Method, "Rectangle\0Trapezoid\0");
		ImGui::SetNextItemWidth(120);
		ImGui::InputDouble("Exposure (shutter fraction)", &draft.Exposure, 0, 0, "%.3f");
		ImGui::SliderFloat("Cross-frame strength", &draft.Strength, 0.0f, 1.0f, "%.3f");
		if (ImGui::BeginCombo("Sampler output settings", draft.OutputSettings.c_str()))
		{
			for (const auto &name : Recording::GetProfiles())
			{
				if (name == current.Name) continue;
				if (ImGui::Selectable(name.c_str(), name == draft.OutputSettings))
					draft.OutputSettings = name;
			}
			ImGui::EndCombo();
		}
		if (!editable) ImGui::EndDisabled();
		const double capture = std::strtod(g_RecordFps, nullptr);
		if (std::isfinite(capture) && capture > 0 && std::isfinite(draft.OutputFps) && draft.OutputFps > 0)
		{
			ImGui::Text("Capture: %.3g FPS  ->  Output: %.3g FPS  (%.3g:1)",
				capture, double(draft.OutputFps), capture / draft.OutputFps);
			if (capture <= draft.OutputFps)
				ImGui::TextDisabled("Capture FPS should exceed output FPS for temporal supersampling.");
		}
		ImGui::TextDisabled("Input FPS is set above; this sampler's output FPS is independent.");
	}
	else if (current.Type == advancedfx::RecordingProfileInfo::Kind::Ffmpeg ||
			 current.Type == advancedfx::RecordingProfileInfo::Kind::FfmpegEx)
	{
		ImGui::TextUnformatted(current.Type == advancedfx::RecordingProfileInfo::Kind::FfmpegEx ?
			"Extended FFmpeg output options" : "FFmpeg output options");
		if (!editable) ImGui::BeginDisabled();
		ImGui::InputTextMultiline("##record_ffmpeg_options", ffmpegOptions, sizeof(ffmpegOptions),
			ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4));
  if(!draft.FfmpegOptionsNoAlpha.empty()){
   ImGui::TextUnformatted("Color-only options (inputs without alpha)");
   ImGui::InputTextMultiline("##record_ffmpeg_noalpha",ffmpegNoAlphaOptions,sizeof(ffmpegNoAlphaOptions),ImVec2(-1,ImGui::GetTextLineHeight()*3));
   draft.FfmpegOptionsNoAlpha=ffmpegNoAlphaOptions;
  }
		if (!editable) ImGui::EndDisabled();
		draft.FfmpegOptions = ffmpegOptions;
		DrawOverlayHint("Use HLAE's {QUOTE} / {AFX_STREAM_PATH} variables; applies to future takes.");
	}
	else
		DrawOverlayHint("This profile type is not editable here. Use mirv_streams settings edit.");

	if (supported && !current.Protected)
	{
		if (!editable) ImGui::BeginDisabled();
		if (ImGui::Button("Apply profile changes"))
		{
			status = Recording::ApplyProfileEdits(draft)
				? "Profile updated in the live HLAE settings registry."
				: "Profile edit rejected: check values, downstream profile and dependency cycles.";
		}
		ImGui::SameLine();
		if (ImGui::Button("Revert unsaved edits"))
		{
			draft = current;
			std::snprintf(ffmpegOptions, sizeof(ffmpegOptions), "%s", current.FfmpegOptions.c_str());
   std::snprintf(ffmpegNoAlphaOptions,sizeof(ffmpegNoAlphaOptions),"%s",current.FfmpegOptionsNoAlpha.c_str());
			status = "Restored the latest profile values.";
		}
		if (!editable) ImGui::EndDisabled();
	}

	if (supported)
	{
		ImGui::Separator();
		DrawOverlayHint("Built-in profiles cannot be modified. Duplicate one to make an editable copy.");
		ImGui::SetNextItemWidth(220);
		ImGui::InputText("New profile name", copyName, sizeof(copyName));
		if (recording) ImGui::BeginDisabled();
		if (ImGui::Button("Duplicate as editable profile"))
		{
			if (Recording::DuplicateProfile(current.Name.c_str(), copyName) &&
				Recording::SetProfile(copyName))
			{
				editingName.clear(); // Read new profile from registry next frame.
				status = "Editable profile created; settings are session-only until recreated on startup.";
			}
			else
				status = "Could not duplicate. Use a unique name (letters, digits, _ or -; not beginning with afx).";
		}
		if (recording) ImGui::EndDisabled();
	}
	if (!current.Protected)
	{
		ImGui::SameLine();
		if (recording) ImGui::BeginDisabled();
		if (ImGui::Button("Delete custom profile"))
		{
			// The active screen profile holds a registry reference; release it
			// before trying to delete. Other dependent presets may still block removal.
			if (Recording::SetProfile("afxDefault") && Recording::RemoveProfile(current.Name.c_str()))
			{
				editingName.clear();
				status = "Custom profile removed.";
			}
			else
			{
				// Removal can fail while another sampler still references this profile.
				// Keep a failed delete from changing the next recording's encoder.
				Recording::SetProfile(current.Name.c_str());
				status = "Profile still referenced by another setting, or removal failed.";
			}
		}
		if (recording) ImGui::EndDisabled();
	}
	if (!status.empty())
		ImGui::TextWrapped("%s", status.c_str());
	DrawOverlayHint("Custom profiles exist only in this session; recreate them in autoexec.cfg to persist.");
}

void Draw()
{
	CheckOverlayPreferences();
	g_CameraGuidesPublished.store(g_ShowCameraGuides);
	g_TransitionGuidePublished.store(g_SelectedCameraClip);
	g_TransitionGuidesPublished.store(g_ShowTransitionGuide);
	g_CameraFrustumsPublished.store(g_CameraFrustums);
	DrawPreviewHud();
	PollTimelineIndex();
	AutoIndexRequestedDemo();
	if (!g_Open)
		return;
	DrawCinematicFiles();
	static bool subscribed = false;
	if (!subscribed)
	{
		g_Hook_VClient_RenderView.m_CamPath.OnChangedAdd(PathChanged, nullptr);
		subscribed = true;
	}
	HistoryObserve();
	auto &document = g_Hook_VClient_RenderView.m_CamPaths;
	if (g_GestureStart &&
		(g_GestureOwner != document.GetId(document.GetActive()) ||
		 g_GestureRevision != g_Hook_VClient_RenderView.m_CamPath.GetRevision() || IsMouseLook()))
		CancelEditGesture();
	if (ImGui::IsMouseClicked(0) && !ImGui::GetIO().WantTextInput)
	{
		g_GestureStart =
			std::make_unique<CamPathCollection::State>(g_Hook_VClient_RenderView.m_CamPaths.CaptureState());
		g_GestureOwner = document.GetId(document.GetActive());
		g_GestureRevision = g_Hook_VClient_RenderView.m_CamPath.GetRevision();
		g_CancelGesture = false;
	}
	if (g_GestureStart && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && ImGui::IsMouseDown(0))
	{
		g_Hook_VClient_RenderView.m_CamPaths.RestoreState(*g_GestureStart);
		g_CurveKeyDrag = false;
		g_CancelGesture = true;
		ImGui::ClearActiveID();
		g_DocumentStatus = "Drag cancelled.";
	}
	if (ImGui::IsKeyPressed(ImGuiKey_RightCtrl, false))
	{
		g_GraphRightCtrlHeld = true;
		g_GraphRightCtrlChord = false;
	}
	if (g_GraphRightCtrlHeld)
		for (int key = ImGuiKey_Tab; key < ImGuiKey_GamepadStart; ++key)
			if (key != ImGuiKey_RightCtrl && ImGui::IsKeyPressed((ImGuiKey)key, false))
				g_GraphRightCtrlChord = true;
	if (g_GraphRightCtrlHeld &&
		(ImGui::GetIO().MouseWheel != 0 || ImGui::GetIO().MouseWheelH != 0 || ImGui::IsMouseClicked(0) ||
		 ImGui::IsMouseClicked(1) || ImGui::IsMouseClicked(2)))
		g_GraphRightCtrlChord = true;
	if (g_GraphRightCtrlHeld && ImGui::IsKeyReleased(ImGuiKey_RightCtrl))
	{
		g_GraphRightCtrlHeld = false;
		if (!g_GraphRightCtrlChord && !ImGui::GetIO().WantTextInput &&
			(MirvCam_SequencePreviewActive() || !MirvCam_RigNavigation()) && !g_CamerasTabActive)
			MirvCampathPreview_Seek();
	}
	if (MirvCam_SequencePreviewActive() &&
		(ImGui::GetIO().WantTextInput || g_CancelGesture ||
		 ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)))
	{
		MirvCam_PreviewKey(VK_LEFT, false, false);
		MirvCam_PreviewKey(VK_RIGHT, false, false);
	}
	if (!ImGui::GetIO().WantTextInput && !g_CancelGesture &&
		!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
	{
		if (MirvCampathPreview_IsActive() && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt &&
			ImGui::IsKeyPressed(ImGuiKey_Tab, false)) MirvCam_TogglePreviewNavigation();
		if (MirvCam_SequencePreviewActive())
		{
			const bool navigate =
				!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt;
			MirvCam_PreviewKey(VK_LEFT, navigate && ImGui::IsKeyDown(ImGuiKey_LeftArrow), false);
			MirvCam_PreviewKey(VK_RIGHT, navigate && ImGui::IsKeyDown(ImGuiKey_RightArrow), false);
		}
		if(MirvCam_RigPreviewActive() && !ImGui::GetIO().KeyCtrl)
		{
			const ImGuiKey keys[]={ImGuiKey_UpArrow,ImGuiKey_DownArrow,ImGuiKey_Home,ImGuiKey_End,ImGuiKey_Escape};
			const unsigned int codes[]={VK_UP,VK_DOWN,VK_HOME,VK_END,VK_ESCAPE};
			for(int i=0;i<5;++i)if(ImGui::IsKeyPressed(keys[i],false))MirvCam_PreviewKey(codes[i],true,false);
		}

		if (g_GraphRightCtrlHeld && !IsMouseLook() && !g_Hook_VClient_RenderView.IsSourceUiActive())
		{
			const ImGuiKey arrows[] = {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow,
			                           ImGuiKey_DownArrow, ImGuiKey_Home,       ImGuiKey_End};
			const unsigned int keys[] = {VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_HOME, VK_END};
			for (size_t i = 0; i < 6; ++i)
			{
				if (ImGui::IsKeyPressed(arrows[i], false))
					SceneCacheNavigationKey(keys[i], true);
				if (i < 4 && ImGui::IsKeyDown(arrows[i]) && !ImGui::IsKeyDown(arrows[i ^ 1]))
				{
					const double held = ImGui::GetKeyData(arrows[i])->DownDuration - .3;
					if (held > 0)
						SceneCacheScrubKey(keys[i], (std::min)(double(ImGui::GetIO().DeltaTime), held));
				}
			}
		}
		if (!g_CamerasTabActive && !MirvCam_RigNavigation() && ImGui::IsKeyPressed(ImGuiKey_E, false))
		{
			if (ImGui::GetIO().KeyCtrl)
			{
				if (!ImGui::GetIO().KeyShift)
					PreviewInsert();
			}
			else
				PreviewCapture(ImGui::GetIO().KeyShift);
		}
		if (!g_CamerasTabActive && !MirvCam_RigNavigation() && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			CancelEditGesture();
			g_Hook_VClient_RenderView.m_CamPath.DeleteSelected();
		}
		if (!g_CamerasTabActive && !MirvCam_RigNavigation() && ImGui::IsKeyPressed(ImGuiKey_Y, false))
			PreviewHold(ImGui::GetIO().KeyShift);

		if (!g_CamerasTabActive && !MirvCam_RigNavigation() && ImGui::GetIO().KeyCtrl &&
			ImGui::IsKeyPressed(ImGuiKey_A, false))
		{
			if (ImGui::GetIO().KeyShift)
				PreviewAnchor();
			else
				PreviewSelectAll();
		}
	}
	if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Z, false))
		HistoryUndo(ImGui::GetIO().KeyShift);

	if (g_Docking)
		ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
	// Diagnostic watermark
	ImGui::GetForegroundDrawList()->AddText(
		ImVec2(8, 8), IM_COL32(255, 255, 255, 255),
		"HLAE Camera Overlay | F8 closes | RMB outside panels moves camera", nullptr);

	if (!s_campathDialogsInit)
	{
		s_campathOpenDialog.SetTitle("Open Campath");
		s_campathOpenDialog.SetTypeFilters({".xml"});
		s_campathSaveDialog.SetTitle("Save Campath");
		s_campathSaveDialog.SetTypeFilters({".xml"});
		s_campathDialogsInit = true;
	}

	ImGuizmo::BeginFrame();
	ImGui::SetNextWindowPos(ImVec2(20, 30), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(540, 200), ImVec2(900, ImGui::GetIO().DisplaySize.y - 50));
	ImGui::Begin("HLAE Camera", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	if (ImGui::Button("Undo"))
		HistoryUndo(false);
	ImGui::SameLine();
	if (ImGui::Button("Redo"))
		HistoryUndo(true);
	ImGui::SameLine();
	ImGui::TextDisabled("Session edit #%llu", static_cast<unsigned long long>(CinematicSessionEditIndex()));
	if (ImGui::BeginTabBar("##hlae_tabs"))
	{
		// Campath tab
		if (ImGui::BeginTabItem("Campath"))
		{
			DrawDocumentControls();
			ImGui::Separator();
			bool showPath = g_CampathDrawer.Draw_get();
			if (ImGui::Checkbox("Show path", &showPath))
				g_CampathDrawer.Draw_set(showPath);
			bool preview = MirvCampathPreview_IsActive();
			if (ImGui::Checkbox("Preview", &preview))
				MirvCampathPreview_SetActive(preview);
			ImGui::TextDisabled(MirvCam_SequenceEnabled() ? "Preview: sequence"
								: MirvCam_RigNavigation() && MirvCam_CameraCount()
									? "Preview: cameras (Tab: keys, when available)"
								: g_Hook_VClient_RenderView.m_CamPath.GetSize()
									? "Preview: keys (Tab: cameras, when available)"
									: "Preview: cameras");
			if(ImGui::Button("Manage cameras"))g_FocusCameras=true;

			if (ImGui::CollapsingHeader("Path display"))
			{
				float labelHeight = g_CampathDrawer.GetDrawKeyframeIndex();
				if (ImGui::SliderFloat("Key label size", &labelHeight, 0, 100, "%.0f",
									   ImGuiSliderFlags_AlwaysClamp))
					g_CampathDrawer.SetDrawKeyframeIndex(labelHeight);
				bool names = g_CampathDrawer.GetDrawNames(), holds = g_CampathDrawer.GetDrawHolds(),
					 durations = g_CampathDrawer.GetDrawDurations();
				if (ImGui::Checkbox("Path names", &names))
				{
					g_CampathDrawer.SetDrawNames(names);
					ImGui::MarkIniSettingsDirty();
				}
				ImGui::SameLine();
				if (ImGui::Checkbox("Hold labels", &holds))
				{
					g_CampathDrawer.SetDrawHolds(holds);
					ImGui::MarkIniSettingsDirty();
				}
				ImGui::SameLine();
				if (ImGui::Checkbox("Segment durations", &durations))
				{
					g_CampathDrawer.SetDrawDurations(durations);
					ImGui::MarkIniSettingsDirty();
				}
			}

			// Info
			size_t cpCount = g_Hook_VClient_RenderView.m_CamPath.GetSize();
			if (cpCount > 0)
			{
				double seconds = g_Hook_VClient_RenderView.m_CamPath.GetDuration();
				int ticks = 0;
				double demoNow = 0.0;
				if (GetCurrentDemoTime(demoNow) && seconds > 0.0)
				{
					int curTick = 0, endTick = 0;
					double curTime = S1_ClientTime();
					double startClient = g_Hook_VClient_RenderView.m_CamPath.GetOffset() +
										 g_Hook_VClient_RenderView.m_CamPath.GetBegin().GetTime();
					double endClient = startClient + seconds;
					if (GetDemoTickFromClientTime(curTime, startClient, curTick) &&
						GetDemoTickFromClientTime(curTime, endClient, endTick))
						ticks = endTick - curTick;
				}
				ImGui::Separator();
				ImGui::Text("Campath: points=%u, length=%d ticks (%.2f s)", (unsigned)cpCount, ticks,
							seconds);
			}

			// Basic controls
			bool campathEnabled = g_Hook_VClient_RenderView.m_CamPath.Enabled_get();
			if (ImGui::Checkbox("Campath enabled", &campathEnabled))
			{
				g_Hook_VClient_RenderView.m_CamPath.Enabled_set(campathEnabled);
			}
			ImGui::SameLine();
			if (ImGui::Button("Add point"))
				PreviewCapture(false);
			ImGui::SameLine();
			if (ImGui::Button("Clear"))
			{
				g_Hook_VClient_RenderView.m_CamPath.SelectNone();
				g_Hook_VClient_RenderView.m_CamPath.Clear();
			}
			ImGui::SameLine();
			if (ImGui::Button("Goto Start"))
			{
				if (g_Hook_VClient_RenderView.m_CamPath.GetSize() > 0)
				{
					double firstT = g_Hook_VClient_RenderView.m_CamPath.GetBegin().GetTime();
					double targetClientTime = g_Hook_VClient_RenderView.m_CamPath.GetOffset() + firstT;
					int targetTick = 0;
					if (GetDemoTickFromClientTime(S1_ClientTime(), targetClientTime, targetTick))
						Afx_GotoDemoTick(targetTick);
					else
						advancedfx::Warning("Overlay: Failed to compute demo tick for campath start.\n");
				}
			}

			if (ImGui::Button("Replace selected"))
				PreviewCapture(true);
			ImGui::SameLine();
			if (ImGui::Button("Select all keys"))
				PreviewSelectAll();
			ImGui::SameLine();
			if (ImGui::Button("Anchor selection here"))
				PreviewAnchor();
			DrawSeekSettings();
			if (ImGui::CollapsingHeader("Camera collision"))
			{
				auto collision = MirvCameraCollisionGet();
				if (ImGui::Checkbox("Free camera collision", &collision.Free))
					Afx_ExecClientCmd(collision.Free ? "mirv_cam collision free 1" : "mirv_cam collision free 0");
				if (ImGui::Checkbox("Campath collision", &collision.Campath))
					Afx_ExecClientCmd(collision.Campath ? "mirv_cam collision campath 1" : "mirv_cam collision campath 0");
				DrawSharedCollisionOptions();
				DrawOverlayHint("Campaths return gradually after blocking. Speed 0 removes the cap; half-life 0 removes damping.");
				if (ImGui::Button("Reset collision history"))
					Afx_ExecClientCmd("mirv_cam collision reset");
				ImGui::TextWrapped("TF2/TF2C only; all collision switches default off. Uses solid map/prop geometry and optional player hulls, not visible meshes or particles. The local player can be included independently. Campath correction changes framing without editing the path. Start in clear space; deeply embedded cameras may need collision disabled to escape. Clearance is a cube around the camera, not a guarantee against near-plane clipping.");
			}

			if (ImGui::CollapsingHeader("Demo fixes"))
			{
				bool muteRecorder = g_DemoMute;
				if (ImGui::Checkbox("Mute recorder sounds", &muteRecorder))
				{
					RememberDemoFixPreference(DemoFixPreference::RecorderMute, muteRecorder);
					MirvFix_SetRecorderSoundMute(muteRecorder);
				}
				if (g_DemoMute && !MirvFix_GetRecorderSoundMute())
					ImGui::TextDisabled("Saved; waiting for compatible POV playback.");
				ImGui::TextWrapped(
				    "TF2/TF2C POV only. Blocks new recorder/owned-weapon sounds before playback and mutes "
					"surviving sounds. The preference remains enabled for subsequent demos.");
				bool deathcam = g_DemoDeathcam;
				if (ImGui::Checkbox("Suppress POV deathcam", &deathcam))
				{
					RememberDemoFixPreference(DemoFixPreference::Deathcam, deathcam);
					MirvFix_SetPovDeathcam(deathcam);
				}
				if (g_DemoDeathcam && !MirvFix_GetPovDeathcam())
					ImGui::TextDisabled("Saved; waiting for the TF2/TF2C client hooks.");
				ImGui::TextWrapped("Prevents POV death/freeze camera effects from freezing the HLAE view or "
				                   "selecting the freeze-cam sound mixer. Enable before death.");
				if (MirvFix_StaticPropFadeSupported())
				{
					bool enabled = g_NoStaticPropFade;
					if (ImGui::Checkbox("Disable static prop fading", &enabled))
					{
						RememberDemoFixPreference(DemoFixPreference::NoStaticPropFade, enabled);
						MirvFix_SetNoStaticPropFade(enabled);
					}
					if (g_NoStaticPropFade && !MirvFix_GetNoStaticPropFade())
						ImGui::TextDisabled("Waiting for the static prop renderer.");
				}
				bool active = g_DemoSmooth;
				if (ImGui::Checkbox("Smooth player movement", &active))
				{
					RememberDemoFixPreference(DemoFixPreference::PlayerInterpolation,
					                          active ? g_DemoInterpolation : -1);
					MirvFix_SetPlayerInterpolation(active ? g_DemoInterpolation : -1);
				}
				double interpolation = g_DemoInterpolation;
				if (ImGui::InputDouble("Player interpolation (seconds)", &interpolation, 0, 0, "%.3f"))
				{
					if (std::isfinite(interpolation) && interpolation >= .001 && interpolation <= 1)
					{
						g_DemoInterpolation = interpolation;
						g_DemoFixAttempt = {};
						ImGui::MarkIniSettingsDirty();
						if (g_DemoSmooth)
							MirvFix_SetPlayerInterpolation(interpolation);
					}
					else
						g_DocumentStatus =
						    "Use 0.001..1 seconds; existing interpolation preference retained.";
				}
				if (g_DemoSmooth && MirvFix_GetPlayerInterpolation() < 0)
					ImGui::TextDisabled("Saved; waiting for compatible demo playback.");
				if (ImGui::Button("Interpolation status") && g_VEngineClient)
					g_VEngineClient->ClientCmd_Unrestricted("mirv_fix playerInterpolation status");
				ImGui::TextWrapped("Adds presentation delay using TF2 interpolation; cannot reconstruct "
				                   "missing snapshots. Native settings restore when playback ends; the saved "
				                   "preference applies to the next demo.");
			}

			auto &path = g_Hook_VClient_RenderView.m_CamPath;
			DrawInterpolationSettings(path);
			ImGui::Separator();

			// Sequencer / Camera Control / Gizmo toggles
			ImGui::Separator();
			ImGui::Checkbox("Show Sequencer", &g_ShowSequencer);
			ImGui::SameLine();
			if (ImGui::Checkbox("Show graph", &g_ShowCurveEditor) && g_ShowCurveEditor)
				g_FocusGraph = true;
			ImGui::SameLine();
			ImGui::Checkbox("Camera Control", &g_ShowCameraControl);
			ImGui::SameLine();
			ImGui::Checkbox("Show Gizmo", &g_ShowGizmo);
			ImGui::SameLine();
			ImGui::Checkbox("Preview indicator", &g_PreviewHudEnabled);

			ImGui::EndTabItem();
		}

		DrawCamerasTab();
		if (ImGui::BeginTabItem("Project"))
		{
			DrawCinematicFileButtons();
			ImGui::TextWrapped("Cinematic projects include paths, cameras, clips and command markers. "
							   "Loading leaves playback and command execution disabled.");
			if (ImGui::CollapsingHeader("Campath files"))
			{
				auto &path = g_Hook_VClient_RenderView.m_CamPath;
				if (ImGui::Button("Open path"))
				{
					g_OpenAll = false;
					s_campathOpenDialog.SetTitle("Open path");
					s_campathOpenDialog.Open();
				}
				ImGui::SameLine();
				if (ImGui::Button("Open collection"))
				{
					g_OpenAll = true;
					s_campathOpenDialog.SetTitle("Open collection");
					s_campathOpenDialog.Open();
				}
				if (ImGui::Button("Choose path folder..."))
				{
					s_campathFolderDialog.SetTitle("Choose path save folder");
					s_campathFolderDialog.SetDirectory(s_campathSaveDialog.GetDirectory());
					s_campathFolderDialog.Open();
				}
				ImGui::TextWrapped("Path save folder: %s",
								   s_campathSaveDialog.GetDirectory().u8string().c_str());
				ImGui::TextDisabled("Save as chooses a new file; Save updates the current file.");
				s_campathFolderDialog.Display();
				if (s_campathFolderDialog.HasSelected())
				{
					auto folder = s_campathFolderDialog.GetSelected();
					if (s_campathSaveDialog.SetDirectory(folder))
					{
						s_campathOpenDialog.SetDirectory(folder);
						g_OverlayPaths.campathDir = folder.string();
						ImGui::MarkIniSettingsDirty();
					}
					s_campathFolderDialog.ClearSelected();
				}
				if (ImGui::Button("Save path"))
				{
					const auto &file = g_Hook_VClient_RenderView.m_CamPaths.GetFileName();
					if (file.empty())
					{
						g_SaveAll = false;
						g_ExportClassic = false;
						s_campathSaveDialog.Open();
					}
					else
						SaveDocument(file.c_str(), false);
				}
				ImGui::SameLine();
				if (ImGui::Button("Save path as"))
				{
					g_SaveAll = false;
					g_ExportClassic = false;
					s_campathSaveDialog.SetTitle("Save path as");
					s_campathSaveDialog.Open();
				}
				ImGui::SameLine();
				if (ImGui::Button("Save all"))
				{
					const auto &file = g_Hook_VClient_RenderView.m_CamPaths.GetCollectionFileName();
					if (file.empty())
					{
						g_SaveAll = true;
						g_ExportClassic = false;
						s_campathSaveDialog.SetTitle("Save collection");
						s_campathSaveDialog.Open();
					}
					else
						SaveDocument(file.c_str(), true);
				}
				ImGui::SameLine();
				if (ImGui::Button("Save all as"))
				{
					g_SaveAll = true;
					g_ExportClassic = false;
					s_campathSaveDialog.SetTitle("Save collection as");
					s_campathSaveDialog.Open();
				}
				if (ImGui::Button("Export classic XML"))
				{
					g_ExportClassic = true;
					s_campathSaveDialog.SetInputName("campath-classic.xml");
					s_campathSaveDialog.SetTitle("Export classic sampled path");
					s_campathSaveDialog.Open();
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(100);
				ImGui::InputDouble("Sample FPS", &g_ExportFps, 0, 0, "%.0f");
				g_ExportFps = std::isfinite(g_ExportFps) ? std::clamp(g_ExportFps, 1.0, 1000.0) : 120;
				s_campathOpenDialog.Display();
				s_campathSaveDialog.Display();
				if (s_campathOpenDialog.HasSelected())
				{
					LoadDocument(s_campathOpenDialog.GetSelected().wstring().c_str(), g_OpenAll);
					g_OverlayPaths.campathDir = s_campathOpenDialog.GetSelected().parent_path().string();
					s_campathSaveDialog.SetDirectory(s_campathOpenDialog.GetSelected().parent_path());
					ImGui::MarkIniSettingsDirty();
					s_campathOpenDialog.ClearSelected();
				}
				if (s_campathSaveDialog.HasSelected())
				{
					auto file = s_campathSaveDialog.GetSelected();
					if (!file.has_extension())
						file += ".xml";
					if (g_ExportClassic)
						g_DocumentStatus =
							MirvCam_Bake(file.wstring().c_str(), "campath",
										 path.GetLowerBound() + path.GetOffset(),
										 path.GetUpperBound() + path.GetOffset(), 1 / g_ExportFps)
								? "Exported sampled classic XML. Editing data and savepoint retained."
								: "Classic export failed (unplayable path, excessive sample count or "
								  "unwritable "
								  "file).";
					else
						SaveDocument(file.wstring().c_str(), g_SaveAll);
					g_OverlayPaths.campathDir = file.parent_path().string();
					s_campathOpenDialog.SetDirectory(file.parent_path());
					ImGui::MarkIniSettingsDirty();
					s_campathSaveDialog.ClearSelected();
				}
			}
			if (ImGui::CollapsingHeader("Command track files"))
				DrawCommandFiles();
			DrawCinematicBakeControls("sequence");
			for (const auto &camera : MirvCam_Cameras())
				if (camera.Selected)
				{
					DrawCinematicBakeControls(std::to_string(camera.Id));
					break;
				}
			ImGui::EndTabItem();
		}

		// Recording tab (Source 1 specific)
		if (ImGui::BeginTabItem("Recording"))
		{
			ImGui::TextUnformatted(Recording::IsActive() ? "Recording is active" : "Recording is stopped");
   DrawRecordingLog();
			// Shared: record path chooser
			static char s_recName[512] = {0};
			static bool recInit = false;
			static ImGui::FileBrowser dirDialog(
				ImGuiFileBrowserFlags_SelectDirectory | ImGuiFileBrowserFlags_CloseOnEsc |
				ImGuiFileBrowserFlags_EditPathString | ImGuiFileBrowserFlags_CreateNewDir);
			static bool dialogInit = false;
			if (!dialogInit)
			{
				dirDialog.SetTitle("Select record folder");
				dialogInit = true;
			}
			if (!recInit)
			{
				const char *rn = AfxStreams_GetRecordNameUtf8();
				if (rn)
					strncpy_s(s_recName, rn, _TRUNCATE);
				recInit = true;
			}

			ImGuiStyle &st = ImGui::GetStyle();
			ImGui::TextUnformatted("Record path");
			ImGui::SameLine(0.0f, st.ItemInnerSpacing.x);
			float avail = ImGui::GetContentRegionAvail().x;
			float btnBrowseW = ImGui::CalcTextSize("Browse...").x + st.FramePadding.x * 2.0f;
			float btnSetW = ImGui::CalcTextSize("Set").x + st.FramePadding.x * 2.0f;
			float gap1 = st.ItemInnerSpacing.x * 2.0f + 8.0f;
			float gap2 = st.ItemInnerSpacing.x;
			float boxW = avail - (btnBrowseW + btnSetW + gap1 + gap2);
			if (boxW < 50.0f)
				boxW = 50.0f;
			ImGui::SetNextItemWidth(boxW);
			ImGui::InputText("##recname", s_recName, sizeof(s_recName));
			ImGui::SameLine(0.0f, gap1);
			if (ImGui::Button("Browse...##recname"))
			{
				if (!g_OverlayPaths.recordBrowseDir.empty())
					dirDialog.SetDirectory(g_OverlayPaths.recordBrowseDir);
				dirDialog.Open();
			}
			ImGui::SameLine(0.0f, gap2);
			if (ImGui::Button("Set##recname"))
			{
				AfxStreams_SetRecordNameUtf8(s_recName);
			}
			dirDialog.Display();
			if (dirDialog.HasSelected())
			{
				const std::string selected = dirDialog.GetSelected().string();
				strncpy_s(s_recName, selected.c_str(), _TRUNCATE);
				AfxStreams_SetRecordNameUtf8(s_recName);
				g_OverlayPaths.recordBrowseDir = s_recName;
				ImGui::MarkIniSettingsDirty();
				dirDialog.ClearSelected();
			}

			if (ImGui::Button("Open recordings folder"))
			{
				std::wstring wPath;
				if (s_recName[0])
				{
					int wlen = MultiByteToWideChar(CP_UTF8, 0, s_recName, -1, nullptr, 0);
					if (wlen > 0)
					{
						wPath.resize((size_t)wlen);
						MultiByteToWideChar(CP_UTF8, 0, s_recName, -1, &wPath[0], wlen);
						wPath.resize((size_t)wlen - 1);
					}
				}
				if (!wPath.empty())
					ShellExecuteW(nullptr, L"open", wPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			}

			ImGui::Separator();
			ImGui::SetNextItemWidth(110);
			if (ImGui::InputText("Capture FPS (input)", g_RecordFps, sizeof(g_RecordFps),
				ImGuiInputTextFlags_CharsDecimal))
				ImGui::MarkIniSettingsDirty();
			const auto keyLabel = g_RecordCapturingHotkey
				? std::string("Press a key... (Esc cancel, Backspace clear)")
				: RecordHotkeyLabel();
			if (ImGui::Button(keyLabel.c_str()))
				g_RecordCapturingHotkey = true;
			ImGui::SameLine();
			ImGui::TextDisabled("Record hotkey");
			if (g_RecordHotkey && ImGui::SmallButton("Clear bind"))
			{
				g_RecordHotkey = g_RecordHotkeyMods = 0;
				g_RecordCapturingHotkey = false;
				ImGui::MarkIniSettingsDirty();
			}
			if (ImGui::CollapsingHeader("Recording behavior", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;
				changed |= ImGui::Checkbox("Auto-resume demo on start", &g_RecordAutoResume);
				changed |= ImGui::Checkbox("Auto-pause demo on stop", &g_RecordAutoPause);
				changed |= ImGui::Checkbox("Close menu on start", &g_RecordAutoClose);
				changed |= ImGui::Checkbox("Stop recording when opening menu", &g_RecordAutoStopOnOpen);
				if (changed)
					ImGui::MarkIniSettingsDirty();
				DrawOverlayHint("Recording uses the screen stream and encoder settings already configured below.");
			}

			if (ImGui::CollapsingHeader("Recording resolution", ImGuiTreeNodeFlags_DefaultOpen))
			{
				const auto state = RecordingResolution::GetState();
				if (state.RenderWidth > 0 && state.RenderHeight > 0)
				{
					ImGui::Text("Native render: %d x %d", state.RenderWidth, state.RenderHeight);
					ImGui::Text("Capture surface: %d x %d", state.CaptureWidth, state.CaptureHeight);
					ImGui::Text("Window preview: %d x %d", state.PreviewWidth, state.PreviewHeight);
					ImGui::TextDisabled("GPU texture limit: %d x %d", state.MaxWidth, state.MaxHeight);
				}
				else ImGui::TextDisabled("Waiting for Direct3D surface dimensions.");
				static int preset = 3, width = 3840, height = 2160;
				static bool fit = true;
				const char *names[] = {"Custom", "Full HD (1920 x 1080)", "QHD (2560 x 1440)",
					"4K UHD (3840 x 2160)", "4K DCI (4096 x 2160)", "5K (5120 x 2880)", "8K (7680 x 4320)"};
				const int sizes[][2] = {{0,0},{1920,1080},{2560,1440},{3840,2160},{4096,2160},{5120,2880},{7680,4320}};
				const bool editable = state.Supported && state.Windowed && !Recording::IsActive() && !state.Pending;
				ImGui::BeginDisabled(!editable);
				if (ImGui::Combo("Resolution preset", &preset, names, IM_ARRAYSIZE(names)) && preset)
				{ width = sizes[preset][0]; height = sizes[preset][1]; }
				ImGui::SetNextItemWidth(120);
				if (ImGui::InputInt("Width##captureResolution", &width, 0)) preset = 0;
				ImGui::SetNextItemWidth(120);
				if (ImGui::InputInt("Height##captureResolution", &height, 0)) preset = 0;
				ImGui::Checkbox("Fit preview to monitor", &fit);
				const bool valid = width >= 640 && height >= 480 && width <= 16384 && height <= 16384 &&
					width <= state.MaxWidth && height <= state.MaxHeight;
				ImGui::BeginDisabled(!valid);
				if (ImGui::Button("Apply resolution"))
				{
					char command[100];
					std::snprintf(command,sizeof(command),"mirv_resolution apply %d %d %d",width,height,int(fit));
					Afx_ExecClientCmd(command);
				}
				ImGui::EndDisabled();
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(!state.Managed || Recording::IsActive());
				if (ImGui::Button("Restore original resolution")) Afx_ExecClientCmd("mirv_resolution restore");
				ImGui::EndDisabled();
				ImGui::BeginDisabled(!state.Managed || !state.Windowed || Recording::IsActive());
				if (ImGui::Button("Refit preview")) Afx_ExecClientCmd("mirv_resolution refit");
				ImGui::EndDisabled();
				if (state.Fit) ImGui::TextDisabled("Overlay: %s", state.NativePreview ? "native preview pixels" : "scaled backbuffer (fallback)");
				if (!state.Supported || !state.Windowed)
					ImGui::TextDisabled("TF2 windowed mode required.");
				if (!valid && state.MaxWidth > 0)
					ImGui::TextDisabled("Requested size is outside the supported dimension or GPU limit.");
				ImGui::TextWrapped("%s", state.Message.c_str());
				DrawOverlayHint("Preview scaling affects presentation only, not the recorded render target.");
			}

			// Common HUD toggles
			ImGui::Separator();
			static bool s_hideHud = false;
			if (ImGui::Checkbox("Hide HUD", &s_hideHud))
				Afx_ExecClientCmd(s_hideHud ? "cl_drawhud 0" : "cl_drawhud 1");

			{
				// Other Source 1 games workflow (screen recording)
				ImGui::Separator();
				if (ImGui::SmallButton("Apply pre-load tweaks"))
				{
					Afx_ExecClientCmd("engine_no_focus_sleep 0; snd_mute_losefocus 0");
				}

				// Separate TF2 passes share the existing capture clock and profile registry.
    if (ImGui::CollapsingHeader("Separate recording passes")) {
     const bool editable = Recording::SeparatePassSupported() && !Recording::IsActive();
     if (!editable) ImGui::BeginDisabled();
     for (unsigned i = 0; i < unsigned(Tf2RenderPass::Count); ++i) {
      bool enabled = Recording::PassEnabled(i);
      if (ImGui::Checkbox(i==0?"Base / world":Tf2RenderPassName(Tf2RenderPass(i)), &enabled)) Recording::SetPassEnabled(i, enabled);
     }
     bool reference=Recording::FullReference();
     if(ImGui::Checkbox("Record full scene reference",&reference))Recording::SetFullReference(reference);
     if(ImGui::SmallButton("Reset separate outputs"))Recording::ResetSeparateOutputs();
     bool alpha = Recording::PassAlpha();
     if (ImGui::Checkbox("Transparent isolated passes", &alpha)) Recording::SetPassAlpha(alpha);
     if (alpha) ImGui::TextWrapped("Alpha replays isolated draws into a second target; the world renders once. Choose an alpha-capable profile below, such as afxFfmpegProresAlpha; other codecs may discard transparency.");
     if (alpha) ImGui::BeginDisabled();
     const unsigned background = Recording::PassBackground() & 0xffffffu;
     const char *backgroundNames[] = {"Green", "Blue", "Black"};
     const unsigned backgroundColors[] = {0x00ff00u, 0x0000ffu, 0x000000u};
     const char *preview = "Custom RGB";
     for (int i = 0; i < 3; ++i) if (background == backgroundColors[i]) preview = backgroundNames[i];
     if (ImGui::BeginCombo("Isolated pass background", preview)) {
      for (int i = 0; i < 3; ++i) if (ImGui::Selectable(backgroundNames[i], background == backgroundColors[i])) Recording::SetPassBackground(backgroundColors[i]);
      ImGui::EndCombo();
     }
     if (alpha) ImGui::EndDisabled();
     if (!editable) ImGui::EndDisabled();
     ImGui::TextWrapped("Base/world contains everything not separated into Players, Viewmodel or HUD. Depth is additional data. The optional full-scene reference preserves the original shot. Outputs share the profile below; isolates use alpha or the chosen chroma background. Refraction and interleaved effects still have compositing limits.");
     if (!Recording::SeparatePassSupported()) ImGui::TextDisabled("TF2 render-pass hooks are unavailable.");
    }
    // Record screen settings
				bool screenEnabled = Recording::ScreenEnabled();
				if (!Recording::SeparatePassSupported() && ImGui::Checkbox("Record screen enabled", &screenEnabled))
				{
					Afx_ExecClientCmd(screenEnabled ? "mirv_streams record screen enabled 1"
													: "mirv_streams record screen enabled 0");
				}
				const auto currentProfile = Recording::CurrentProfile();
				if (ImGui::BeginCombo("Record screen profile", currentProfile.c_str()))
				{
					for (const auto &name : Recording::GetProfiles())
						if (ImGui::Selectable(name.c_str(), name == currentProfile))
							Recording::SetProfile(name.c_str());
					ImGui::EndCombo();
				}
				ImGui::TextWrapped("Includes built-in and custom mirv_streams settings. The menu always reads the active profile.");
				DrawRecordingProfileEditor(Recording::CurrentProfile());
                bool gpuCapture=Recording::GpuCaptureEnabled();
                ImGui::BeginDisabled(Recording::IsActive() || Recording::SeparateConfigured());
                if(ImGui::Checkbox("GPU capture (experimental)",&gpuCapture)) Recording::SetGpuCaptureEnabled(gpuCapture);
                ImGui::EndDisabled();
                ImGui::TextWrapped("Screen color only; unsupported devices/profiles use CPU. See recording log.");

				ImGui::Separator();
				ImGui::BeginDisabled(Recording::IsActive() || !RecordingResolution::CanStart());
				if (ImGui::Button("Start Recording"))
					Recording::Toggle();
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Button("Stop") && Recording::IsActive())
					Recording::Toggle();
			}

			ImGui::EndTabItem();
		}

		DrawObserving();
		// Settings
		if (ImGui::BeginTabItem("Settings"))
		{
			if (ImGui::Checkbox("Periodic recovery autosave", &g_Autosave))
				ImGui::MarkIniSettingsDirty();
			ImGui::TextDisabled(
				"Unsaved work is also backed up on closing the overlay or loading another file.");
			if (ImGui::InputDouble("Autosave interval (seconds)", &g_AutosaveSeconds))
				ImGui::MarkIniSettingsDirty();
			g_AutosaveSeconds =
				std::isfinite(g_AutosaveSeconds) ? std::clamp(g_AutosaveSeconds, 5.0, 3600.0) : 30;
			if (ImGui::Button("Refresh recovery backups"))
				ScanRecovery();
			ImGuiIO &io = ImGui::GetIO();
			if (ImGui::Checkbox("Docking workspace", &g_Docking))
				ImGui::MarkIniSettingsDirty();
			ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
			// UI scale (DPI) controls
			float uiScaleTmp = g_UiScale;
			ImGui::SetNextItemWidth(ImGui::CalcTextSize("000.00x").x +
									ImGui::GetStyle().FramePadding.x * 6.0f);
			if (ImGui::SliderFloat("UI scale", &uiScaleTmp, 0.50f, 2.00f, "%.2fx",
								   ImGuiSliderFlags_AlwaysClamp))
				g_UiScale = uiScaleTmp;
			ImGui::SameLine();
			if (ImGui::SmallButton("90%"))
				g_UiScale = 0.90f;
			ImGui::SameLine();
			if (ImGui::SmallButton("100%"))
				g_UiScale = 1.00f;
			ImGui::SameLine();
			if (ImGui::SmallButton("125%"))
				g_UiScale = 1.25f;
			ImGui::SameLine();
			if (ImGui::SmallButton("150%"))
				g_UiScale = 1.50f;
			ImGui::SameLine();
			if (ImGui::SmallButton("200%"))
				g_UiScale = 2.00f;
			static float savedScale = g_UiScale;
			if (savedScale != g_UiScale)
			{
				savedScale = g_UiScale;
				ImGui::MarkIniSettingsDirty();
			}
			float scale = g_UiScale;

			// apply font-specific correction
			if (g_FontChoice == 2)
			{ // Sans
				scale *= 1.2f;
			}
			else if (g_FontChoice == 3)
			{ // Silly
				scale *= 1.2f;
			}
			float snapped = floorf(scale * 16.0f + 0.5f) / 16.0f;
			io.FontGlobalScale = snapped;

			if (ImGui::Button("Open demo"))
				OpenDemoPicker();

			// UI Font picker
			ImGui::SameLine();
			{
				const char *items[] = {"Default (ImGui)", "Monospace", "Sans Serif", "Silly"};
				int prevChoice = g_FontChoice;

				ImGui::SetNextItemWidth(ImGui::CalcTextSize("Default (ImGui)   ").x +
										ImGui::GetStyle().FramePadding.x * 8.0f);
				if (ImGui::Combo("UI Font", &g_FontChoice, items, (int)(sizeof(items) / sizeof(items[0]))))
				{
					ImFont *chosen = g_FontDefault;

					if (g_FontChoice == 1)
						chosen = g_FontMono ? g_FontMono : g_FontDefault; // Monospace
					else if (g_FontChoice == 2)
						chosen = g_FontSans ? g_FontSans : g_FontDefault; // Sans Serif
					else if (g_FontChoice == 3)
						chosen = g_FontSilly ? g_FontSilly : g_FontDefault; // Silly

					ImGui::GetIO().FontDefault = chosen ? chosen : g_FontDefault;
					ImGui::MarkIniSettingsDirty();
				}

				// If the user picked a font that wasn't found, inform them (non-intrusively).
				if ((g_FontChoice == 1 && !g_FontMono) || (g_FontChoice == 2 && !g_FontSans) ||
					(g_FontChoice == 3 && !g_FontSilly))
				{
					ImGui::SameLine();
					ImGui::TextDisabled("(font not found, using Default)");
				}
			}
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
	ImGui::End();

	// Sequencer window (ImGui Neo Sequencer) – Source1 port of DX11 implementation
	if (g_ShowSequencer)
	{
		std::lock_guard<std::recursive_mutex> sceneLock(g_SceneMutex);
		ImGui::SetNextWindowSize(ImVec2(900, 520), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(20, (std::max)(380.0f, ImGui::GetIO().DisplaySize.y - 560)),
								ImGuiCond_FirstUseEver);
		if (ImGui::Begin("HLAE Sequencer", &g_ShowSequencer, ImGuiWindowFlags_NoCollapse))
		{
			auto &s_seqFrame = g_SequencerFrame;
			static ImGui::FrameIndexType s_seqStart = 0;
			static ImGui::FrameIndexType s_seqEnd = 0;
			static bool s_seqInit = false;
			static ImGui::FrameIndexType visibleStart = 0, visibleEnd = 0;

			bool cached = SceneCachePreviewActive();
			if (ImGui::Checkbox("Cached preview", &cached))
			{
				std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
				++g_SceneDrawEpoch;
				g_ScenePreview = cached;
				SceneCacheStopScrub();
				g_SequencerNeedsRefresh = true;
				if (cached && !g_SceneCache.Frames().empty())
					s_seqFrame = int(std::floor(g_SceneCache.Frames().back().DemoTime/SceneTickInterval()));
				ScenePreviewTick(s_seqFrame);
			}
			ImGui::SameLine();
			bool sequencePreview = MirvCam_SequenceEnabled();
			if (ImGui::Checkbox("Enable sequence", &sequencePreview) &&
				!MirvCam_EnableSequence(sequencePreview))
				g_DocumentStatus = "Add clips in the Camera clips tab for this demo first.";
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Use the camera clips to choose the view, including during cached "
								  "scrubbing. This replaces individual camera/path preview.");
			ImGui::SameLine();
			bool previewing = MirvCampathPreview_IsActive();
			if (ImGui::Checkbox("Preview", &previewing))
				MirvCampathPreview_SetActive(previewing);
			ImGui::SameLine();
			if (ImGui::Button("Seek actual demo here")) Afx_GotoDemoTick(s_seqFrame);
			int curDemoTick = 0;
			const bool haveDemoTick = GetCurrentDemoTick(curDemoTick);
			int demoTotalTicks = 0;
			if (g_VEngineClient)
				if (auto *demo = g_VEngineClient->GetDemoInfoEx())
					demoTotalTicks = demo->GetDemoPlaybackTotalTicks();
			ImGui::FrameIndexType prevFrame = s_seqFrame;
			bool fractionalPointerHandled=false;

			// Initial content bounds (max campath tick)
			if (!s_seqInit)
			{
				int contentMaxTick = 0;
				WrpGlobals *gl = g_Hook_VClient_RenderView.GetGlobals();
				double ipt = gl ? gl->interval_per_tick_get() : (1.0 / 64.0);
				double curTime = g_MirvTime.GetTime();
				double demoNow = 0.0;
				bool haveDemoNow = GetCurrentDemoTime(demoNow);
					if (SceneCachePreviewActive())
					{
						curTime = EditingClientTime(curTime);
						demoNow = g_SceneCursor;
						haveDemoNow = !g_SceneCache.Frames().empty();
					}

				auto tick_from_client = [&](double clientTime) -> int {
					int t = 0;
						if (SceneCachePreviewActive() && haveDemoNow)
						{
							double mapped = 0;
							if (CinematicClock(clientTime, true, mapped))
								return (std::max)(0, int(llround(mapped / ipt)));
						}
					if (!GetDemoTickFromClientTime(curTime, clientTime, t))
						t = (int)llround(clientTime / ipt);
					return t < 0 ? 0 : t;
				};
				for (const auto &clip : MirvCam_Clips())
					contentMaxTick = (std::max)(contentMaxTick, tick_from_client(clip.Time + clip.Blend));
    for (const auto &m : MirvCam_SequenceModifiers())
     contentMaxTick = (std::max)(contentMaxTick, tick_from_client(m.End));
				for (CamPathIterator it = g_Hook_VClient_RenderView.m_CamPath.GetBegin();
					 it != g_Hook_VClient_RenderView.m_CamPath.GetEnd(); ++it)
				{
					double tRel = it.GetTime();
					double clientTarget = g_Hook_VClient_RenderView.m_CamPath.GetOffset() + tRel;
					int tick = 0;
					if (haveDemoNow)
					{
						double demoTarget = clientTarget - (curTime - demoNow);
						tick = (int)llround(demoTarget / ipt);
					}
					else
					{
						tick = tick_from_client(clientTarget);
					}
					if (tick > contentMaxTick)
						contentMaxTick = tick;
				}
				s_seqStart = 0;
				s_seqEnd = (ImGui::FrameIndexType)(contentMaxTick > 0 ? contentMaxTick : 1);
				s_seqInit = true;
			}

			// The view can zoom/pan independently of the immutable demo bounds.
			static int s_lastDemoTotal = -1;
			static bool s_fitTimeline = false;
			if (demoTotalTicks != s_lastDemoTotal)
			{
				s_fitTimeline = true;
				s_lastDemoTotal = demoTotalTicks;
			}
			s_seqStart = 0;
			s_seqEnd = demoTotalTicks > 0 ? demoTotalTicks
										 : (std::max)(s_seqEnd, (std::max)(1, curDemoTick));

			DrawWorkRangeControls(s_seqFrame, s_seqEnd);
			const auto clipsForView = MirvCam_Clips();
   const auto modifiersForView = MirvCam_SequenceModifiers();
			if (ImGui::Button("Tracks"))
				ImGui::OpenPopup("sequencer_tracks");
			if (ImGui::BeginPopup("sequencer_tracks"))
			{
				const char *names[] = {"Paths",	   "Playback / cuts", "Camera sources", "Scene Cache",
									   "Commands", "Demo events",	  "Player repair"};
				const char *modes[] = {"Auto (hide empty)", "Hidden", "Shown"};
				for (int i = 0; i < 7; ++i)
				{
					int mode = g_SequencerTracks[i] + 1;
					if (ImGui::Combo(names[i], &mode, modes, 3))
					{
						g_SequencerTracks[i] = mode - 1;
						ImGui::MarkIniSettingsDirty();
					}
				}
				if (ImGui::Button("Show all"))
				{
					for (int &track : g_SequencerTracks)
						track = 1;
					g_SequencerPathMode = 2;
					ImGui::MarkIniSettingsDirty();
				}
				ImGui::SameLine();
				if (ImGui::Button("Reset tracks"))
				{
					for (int &track : g_SequencerTracks)
						track = -1;
					g_SequencerTracks[5] = 0;
					g_SequencerPathMode = 0;
					ImGui::MarkIniSettingsDirty();
				}
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(150);
			const char *pathModes[] = {"Active path", "Selected paths", "All paths"};
			if (ImGui::Combo("##sequencer_path_mode", &g_SequencerPathMode, pathModes, 3))
				ImGui::MarkIniSettingsDirty();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Selected paths includes the active path and paths with selected keys.");
			static int frameRequest = 0;
			ImGui::SameLine();
			if (ImGui::Button("Frame selection"))
				frameRequest = 1;
			ImGui::SameLine();
			if (ImGui::Button("Frame authored content"))
				frameRequest = 2;
			auto &pathsForView = g_Hook_VClient_RenderView.m_CamPaths;
			bool haveAuthoredPaths = false;
			int pathRows = 0;
			for (size_t i = 0; i < pathsForView.GetCount(); ++i)
			{
				haveAuthoredPaths |= pathsForView.GetPath(i)->GetSize() != 0;
				if (SequencerPathVisible(i))
					++pathRows;
			}
			if (frameRequest)
			{
				int first = INT_MAX, last = 0;
				auto include = [&](double client)
				{
					int tick;
					double demoNowForFrame;
					if (CinematicClock(client, true, demoNowForFrame, false))
						tick = int(std::llround(demoNowForFrame / SceneTickInterval()));
					else if (!GetDemoTickFromClientTime(g_MirvTime.GetTime(), client, tick))
						tick = int(std::llround(client / SceneTickInterval()));
					first = (std::min)(first, tick);
					last = (std::max)(last, tick);
				};
				for (size_t i = 0; i < pathsForView.GetCount(); ++i)
					for (auto key = pathsForView.GetPath(i)->GetBegin();
						 key != pathsForView.GetPath(i)->GetEnd(); ++key)
						if (frameRequest == 2 || key.GetValue().Selected)
						{
							include(key.GetTime() + pathsForView.GetPath(i)->GetOffset());
							include(key.GetTime() + key.GetValue().HoldDuration +
									pathsForView.GetPath(i)->GetOffset());
						}
				for (const auto &clip : clipsForView)
					if (frameRequest == 2 || clip.Id == g_SelectedCameraClip)
					{
						include(clip.Time);
						include(clip.Time + clip.Blend);
						for (const auto &m : clip.Modifiers)
						{
							include(clip.Time + m.Begin);
							include(clip.Time + m.End);
						}
					}
    for (const auto &m : modifiersForView)
     if (frameRequest == 2 || m.Id == g_SelectedSequenceModifier) {include(m.Begin); include(m.End);}
				if (first != INT_MAX)
				{
					const int padding = (std::max)(10, (last - first) / 20);
					ImGui::SetNextNeoSequencerView((std::max)(0, first - padding),
												   (std::min)(INT_MAX - padding, last) + padding);
					s_fitTimeline = false;
				}
				frameRequest = 0;
			}
            auto &timebase = advancedfx::TimelineTimebase();
            if (ImGui::CollapsingHeader("Timeline timebase")) {
                auto draft = timebase;
                const char *modes[] = {"Demo ticks", "Seconds", "Bars & beats"};
                ImGui::Combo("Timeline ruler", &draft.Mode, modes, 3);
                if (draft.Mode == 2) {
                    ImGui::InputDouble("Tempo (BPM)", &draft.BPM, 0, 0, "%.3f");
                    ImGui::InputInt("Beats per bar", &draft.Numerator);
                    const int den[] = {1,2,4,8,16}; const char *denNames[] = {"1","2","4","8","16"};
                    int denIndex=0; for(int i=0;i<5;++i) if(den[i]==draft.Denominator) denIndex=i;
                    if(ImGui::Combo("Beat note denominator",&denIndex,denNames,5))draft.Denominator=den[denIndex];
                    const int divisions[]={1,2,4,8,16,32}; const char *names[]={"Whole","Half","Quarter","Eighth","Sixteenth","Thirty-second"};
                    int division=0;for(int i=0;i<6;++i)if(divisions[i]==draft.Division)division=i;
                    if(ImGui::Combo("Snap subdivision",&division,names,6))draft.Division=divisions[division];
                    bool triplet=draft.Tuplet==3;if(ImGui::Checkbox("Triplets",&triplet))draft.Tuplet=triplet?3:1;
                    ImGui::Checkbox("Musical snapping (Shift bypasses)",&draft.Snap);
                    ImGui::InputDouble("Downbeat demo time (s)",&draft.Origin,0,0,"%.6f");
                    if(ImGui::Button("Set bar 1 at playhead")) {
                        double demoTime = double(s_seqFrame)*SceneTickInterval();
                        if(SceneCachePreviewActive()) demoTime=g_SceneCursor;
                        else if(MirvCam_SequencePreviewActive()) CinematicClock(MirvCam_SequencePreviewTime(),true,demoTime,false);
                        else if(MirvCampathPreview_IsActive()) CinematicClock(MirvCampathPreview_GetTime()+g_Hook_VClient_RenderView.m_CamPath.GetOffset(),true,demoTime,false);
                        draft.Origin=(std::max)(0.,demoTime);draft.OffsetMs=0;
                    }
                    ImGui::InputDouble("Downbeat offset (ms)",&draft.OffsetMs,0,0,"%.3f");
                    DrawOverlayHint("BPM counts quarter notes. Grid density adapts to zoom; snapping does not.");
                }
                if(draft.Valid())timebase=draft;
                else ImGui::TextDisabled("Invalid tempo, meter or offset; last valid settings retained.");
            }
			// Layout sizes similar to DX11
			ImGuiStyle &st = ImGui::GetStyle();
			float availY = ImGui::GetContentRegionAvail().y;
			float btnRowH = ImGui::GetFrameHeight();
			ImGuiNeoSequencerStyle &neo = ImGui::GetNeoSequencerStyle();
			neo.TrackSeparators = neo.GroupBorders = neo.TreeGuides = true;
            neo.Ruler=timebase; neo.RulerTickInterval=SceneTickInterval();
			neo.DepthItemSpacing = 18;
			float topBarH = neo.TopBarHeight > 0.0f
								? neo.TopBarHeight
								: (ImGui::CalcTextSize("100").y + st.FramePadding.y * 2.0f);
			float zoomH = ImGui::GetFontSize() * neo.ZoomHeightScale + st.FramePadding.y * 2.0f;
			float rowLabelH =
				ImGui::CalcTextSize("Campath").y + st.FramePadding.y * 2.0f + neo.ItemSpacing.y * 2.0f;
			int modifierRows = 0;
			for (const auto &clip : clipsForView)
				if (g_SequencerSourcesOpen && clip.Id == g_SelectedCameraClip &&
					SequencerTrackVisible(2, true))
					modifierRows = int(clip.Modifiers.size());
   int adjustmentRows = 0;
   if (SequencerTrackVisible(2, !clipsForView.empty() || !modifiersForView.empty()))
    for (int track = 0; track < advancedfx::SequenceModifierClip::MaxTracks; ++track)
     if (std::any_of(modifiersForView.begin(), modifiersForView.end(), [&](const auto &m){return m.Track == track;})) ++adjustmentRows;
   if (g_AdjustmentDuplicateDrag && adjustmentRows > 0 && adjustmentRows < advancedfx::SequenceModifierClip::MaxTracks) ++adjustmentRows;
			const float rows =
				float(1 + adjustmentRows + modifierRows + (pathRows > 1 ? 1 + (g_SequencerPathsOpen ? pathRows : 0) : pathRows) +
					  int(SequencerTrackVisible(1, haveAuthoredPaths)) +
					  int(!clipsForView.empty() && SequencerTrackVisible(2, true)) +
					  int(SequencerTrackVisible(3, !g_SceneCache.Frames().empty())) +
					  int(SequencerTrackVisible(4, !g_CommandSystem.GetTickMarkers().empty())) +
					  int(SequencerTrackVisible(5, !g_SequencerTimeline.Index.Events.empty())) +
					  int(SequencerTrackVisible(6, SequencerRepairPopulated())));
			float desiredSeqH = topBarH + neo.TopBarSpacing + zoomH + rowLabelH * (std::min)(rows, 6.0f);
			float minSeqH = ImMax(60.0f, topBarH + neo.TopBarSpacing + zoomH + rowLabelH);
			float maxSeqHAvail = ImMax(minSeqH, ImMax(55.0f, availY - btnRowH));
			float seqH = ImClamp(desiredSeqH, minSeqH, maxSeqHAvail);

			ImGui::BeginChild("seq_child", ImVec2(0.0f, seqH), false);
			{
				const ImVec2 seqSize(ImVec2(ImGui::GetContentRegionAvail().x,
											topBarH + neo.TopBarSpacing + zoomH + rowLabelH * rows));
				const ImGuiNeoSequencerFlags seqFlags =
					ImGuiNeoSequencerFlags_AlwaysShowHeader | ImGuiNeoSequencerFlags_EnableSelection |
					(g_LockKeyTimes ? 0 : ImGuiNeoSequencerFlags_Selection_EnableDragging);
				if (s_fitTimeline)
				{
					ImGui::SetNextNeoSequencerFit();
					s_fitTimeline = false;
				}
				if (ImGui::BeginNeoSequencer("##demo_seq", &s_seqFrame, &s_seqStart, &s_seqEnd, seqSize,
											 seqFlags))
				{
					bool keyframeContextOpened = false;
					ImGui::GetNeoSequencerView(&visibleStart, &visibleEnd);
					if (MirvCam_SequencePreviewActive() && !SceneCachePreviewActive())
					{
						double tick, client;
						if (ImGui::GetNeoSequencerPlayheadDrag(&tick) &&
							CinematicClock(tick * SceneTickInterval(), false, client, false))
						{
							MirvCam_SetSequencePreviewTime(client);
							fractionalPointerHandled = true;
						}
						double demo;
						if (CinematicClock(MirvCam_SequencePreviewTime(), true, demo, false))
							ImGui::SetNeoSequencerPlayhead(demo / SceneTickInterval(), s_seqFrame);
					}
					if (SceneCachePreviewActive())
					{
						double tick;
						if (ImGui::GetNeoSequencerPlayheadDrag(&tick))
						{
							SceneCacheStopScrub();
							ScenePreviewTime(tick * SceneTickInterval());
							fractionalPointerHandled = true;
						}
						ImGui::SetNeoSequencerPlayhead(g_SceneCursor / SceneTickInterval(), s_seqFrame);
					}
					if (g_CancelGesture)
						ImGui::NeoCancelSelection();
					DrawWorkRangeTrack(s_seqEnd);
					auto &document = g_Hook_VClient_RenderView.m_CamPaths;
					size_t choosePath = document.GetCount();
					// Cache (ticks <-> times <-> values)
					struct KfCache
					{
						bool valid = false;
						std::vector<int32_t> ticks, originalTicks;
						std::vector<double> times;
						std::vector<CamPathValue> values;
					};
					static KfCache s_cache;
					static bool s_wasDragging = false;

					WrpGlobals *gl = g_Hook_VClient_RenderView.GetGlobals();
					double ipt = gl ? gl->interval_per_tick_get() : (1.0 / 64.0);
					double curTime = g_MirvTime.GetTime();
					double demoNow = 0.0;
					bool haveDemoNow = GetCurrentDemoTime(demoNow);
					if (SceneCachePreviewActive())
					{
						curTime = EditingClientTime(curTime);
						demoNow = g_SceneCursor;
						haveDemoNow = !g_SceneCache.Frames().empty();
					}

					auto tick_from_client = [&](double clientTime) -> int {
						int t = 0;
						if (SceneCachePreviewActive() && haveDemoNow)
						{
							double mapped = 0;
							if (CinematicClock(clientTime, true, mapped))
								return (std::max)(0, int(llround(mapped / ipt)));
						}
						if (!GetDemoTickFromClientTime(curTime, clientTime, t))
							t = (int)llround(clientTime / ipt);
						return t < 0 ? 0 : t;
					};

					auto rebuild_cache = [&]() {
						s_cache.valid = true;
						s_cache.ticks.clear();
						s_cache.times.clear();
						s_cache.values.clear();
						s_cache.ticks.reserve(g_Hook_VClient_RenderView.m_CamPath.GetSize());
						s_cache.times.reserve(g_Hook_VClient_RenderView.m_CamPath.GetSize());
						s_cache.values.reserve(g_Hook_VClient_RenderView.m_CamPath.GetSize());
						for (CamPathIterator it = g_Hook_VClient_RenderView.m_CamPath.GetBegin();
							 it != g_Hook_VClient_RenderView.m_CamPath.GetEnd(); ++it)
						{
							double tRel = it.GetTime();
							s_cache.times.push_back(tRel);
							s_cache.values.push_back(it.GetValue());
							double clientTarget = g_Hook_VClient_RenderView.m_CamPath.GetOffset() + tRel;
							int tick = haveDemoNow ? (int)llround((clientTarget - (curTime - demoNow)) / ipt)
												   : tick_from_client(clientTarget);
							if (tick < 0)
								tick = 0;
							s_cache.ticks.push_back(tick);
						}
						s_cache.originalTicks = s_cache.ticks;
					};

					bool syncSelection =
						g_SequencerNeedsRefresh || !s_cache.valid ||
						s_cache.ticks.size() != g_Hook_VClient_RenderView.m_CamPath.GetSize();
					if (s_cache.ticks.size() != g_Hook_VClient_RenderView.m_CamPath.GetSize())
						ImGui::NeoCancelSelection();
					syncSelection =
						syncSelection && !ImGui::NeoIsSelecting() && !ImGui::NeoIsDraggingSelection();
					if (syncSelection)
						ImGui::NeoClearSelection();
					if (g_SequencerNeedsRefresh)
					{
						rebuild_cache();
						g_SequencerNeedsRefresh = false;
						s_cache.valid = true;
						g_CurveCache.valid = false;
						s_wasDragging = ImGui::NeoIsDraggingSelection();
					}
					if (!ImGui::NeoIsDraggingSelection())
					{
						if (!s_cache.valid ||
							s_cache.ticks.size() != g_Hook_VClient_RenderView.m_CamPath.GetSize())
							rebuild_cache();
					}

					const bool pathsWereOpen = g_SequencerPathsOpen;
					const bool pathsOpen =
						pathRows <= 1 || ImGui::BeginNeoGroup("Paths", &g_SequencerPathsOpen);
					if (pathsWereOpen != g_SequencerPathsOpen)
						ImGui::MarkIniSettingsDirty();
					bool open = true;
					ImGui::PushID((int)document.GetId(document.GetActive()));
					std::string editingLabel = document.GetName(document.GetActive()) + " (editing)";
					if (pathsOpen && SequencerPathVisible(document.GetActive()) &&
						ImGui::BeginNeoTimelineEx(editingLabel.c_str(), &open, ImGuiNeoTimelineFlags_None))
					{
						for (size_t i = 0; i < s_cache.values.size(); ++i)
							if (s_cache.values[i].HoldDuration > 0)
							{
								int release =
									tick_from_client(s_cache.times[i] + s_cache.values[i].HoldDuration +
													 g_Hook_VClient_RenderView.m_CamPath.GetOffset());
								ImGui::NeoTimelineSpan(s_cache.ticks[i], release, IM_COL32(220, 150, 50, 90),
													   "Hold");
								ImGui::NeoTimelineSpan(release, release, IM_COL32(255, 190, 70, 255),
													   "Hold release");
							}
						int rightClickedIndex = -1;
						static CampathCtx s_ctxMenuPending;
						static unsigned long long contextRevision = 0;
						static std::vector<int> s_lastUiSelection;
						std::vector<int> curUiSelection;
						curUiSelection.reserve(s_cache.ticks.size());

						for (int i = 0; i < (int)s_cache.ticks.size(); ++i)
						{
							if (syncSelection)
								ImGui::NeoSetKeyframeSelected(&s_cache.ticks[i], s_cache.values[i].Selected);
							ImGui::NeoKeyframe(&s_cache.ticks[i]);
							if (ImGui::IsNeoKeyframeSelected())
								curUiSelection.push_back(i);
							if (ImGui::IsNeoKeyframeRightClicked())
								rightClickedIndex = i;
						}

						// Sync UI selection -> engine
						if (!ImGui::NeoIsSelecting() && !ImGui::NeoIsDraggingSelection())
						{
							auto normalized = [](std::vector<int> &v) {
								std::sort(v.begin(), v.end());
								v.erase(std::unique(v.begin(), v.end()), v.end());
							};
							normalized(curUiSelection);
							std::vector<int> last = s_lastUiSelection;
							normalized(last);
							if (last != curUiSelection)
							{
								if (!syncSelection)
									SetKeySelection(curUiSelection);
								s_lastUiSelection = curUiSelection;
							}
							g_CurveSelection = curUiSelection;
						}

						// Context menu on keyframe
						if (rightClickedIndex >= 0)
						{
							// The marker owns this click; the later background seek
							// menu must not replace its popup at the same ImGui level.
							keyframeContextOpened = true;
							contextRevision = g_Hook_VClient_RenderView.m_CamPath.GetGeometryRevision();
							s_ctxMenuPending.active = true;
							s_ctxMenuPending.time = s_cache.times[(size_t)rightClickedIndex];
							s_ctxMenuPending.value = s_cache.values[(size_t)rightClickedIndex];
							ImGui::OpenPopup("campath_kf_ctx");
							ImGui::SetNextWindowPos(ImGui::GetMousePos());
							ImGui::SetNextWindowSizeConstraints(ImVec2(120, 0), ImVec2(300, FLT_MAX));
						}
						if (ImGui::BeginPopup("campath_kf_ctx"))
						{
							if (contextRevision != g_Hook_VClient_RenderView.m_CamPath.GetGeometryRevision())
							{
								s_ctxMenuPending.active = false;
								ImGui::CloseCurrentPopup();
							}
							ImGui::BeginDisabled(!s_ctxMenuPending.active);
							const int contextTick = tick_from_client(s_ctxMenuPending.time +
							    g_Hook_VClient_RenderView.m_CamPath.GetOffset());
							ImGui::BeginDisabled(!g_VEngineClient || !g_VEngineClient->IsPlayingDemo());
							if (ImGui::MenuItem("Go to"))
								Afx_GotoDemoTick(contextTick);
							if (ImGui::MenuItem("Go to with lead-in"))
								Afx_GotoDemoTick(EventSeekTick(contextTick));
							ImGui::EndDisabled();
							ImGui::Separator();
							bool doRemove = false, doGet = false, doSet = false;
							if (ImGui::MenuItem("Remove"))
								doRemove = true;
							if (ImGui::MenuItem("Get"))
								doGet = true;
							if (ImGui::MenuItem("Set"))
								doSet = true;
							if (ImGui::MenuItem("Edit"))
							{
								if (s_ctxMenuPending.active)
								{
									g_LastCampathCtx = s_ctxMenuPending;
									g_ShowGizmo = true;
								}
								ImGui::CloseCurrentPopup();
							}
							ImGui::EndDisabled();
							ImGui::EndPopup();

							if (s_ctxMenuPending.active)
							{
								bool prevDraw = g_CampathDrawer.Draw_get();
								if (doRemove)
								{
									g_CampathDrawer.Draw_set(false);
									g_Hook_VClient_RenderView.m_CamPath.Remove(s_ctxMenuPending.time);
									g_CampathDrawer.Draw_set(prevDraw);
									g_SequencerNeedsRefresh = true;
								}
								if (doGet)
								{
									MirvCampathPreview_SetActive(false);
									if (MirvInput *pMirv = g_Hook_VClient_RenderView.m_MirvInput)
									{
										pMirv->SetCameraControlMode(true);
										const CamPathValue &v = s_ctxMenuPending.value;
										pMirv->SetTx((float)v.X);
										pMirv->SetTy((float)v.Y);
										pMirv->SetTz((float)v.Z);
										using namespace Afx::Math;
										QEulerAngles angles = v.GetEulerAngles();
										pMirv->SetRx((float)angles.Pitch);
										pMirv->SetRy((float)angles.Yaw);
										pMirv->SetRz((float)angles.Roll);
										pMirv->SetFov((float)v.Fov);
									}
									s_ctxMenuPending.active = false;
								}
								if (doSet)
								{
									double cx, cy, cz, rX, rY, rZ;
									float cfov;
									Afx_GetLastCameraData(cx, cy, cz, rX, rY, rZ, cfov);
									CamPathValue newVal = s_ctxMenuPending.value;
									newVal.X = cx;
									newVal.Y = cy;
									newVal.Z = cz;
									newVal.Fov = cfov;
									newVal.SetEulerAngles(QEulerAngles(rX, rY, rZ));
									g_CampathDrawer.Draw_set(false);
									if (!g_Hook_VClient_RenderView.m_CamPath.ReplacePose(
											s_ctxMenuPending.time, newVal,
											[&]()
											{
												unsigned int mask = 0;
												for (int ch = 0; ch < 7; ++ch)
													if (!g_CurveLocked[ch])
														mask |= 1u << ch;
												return mask;
											}()))
										g_DocumentStatus =
											"Cannot change a pose that shares an adjacent hold release.";
									g_CampathDrawer.Draw_set(prevDraw);
									s_ctxMenuPending.active = false;
									g_SequencerNeedsRefresh = true;
								}
							}
						}

						// Retime commit on drag end
						bool draggingNow = ImGui::NeoIsDraggingSelection();
						if (!g_CancelGesture && s_wasDragging && !draggingNow)
						{
							// Preserve selection indices
							std::vector<int> prevSelectedIdx = g_CurveSelection;
							int prevGizmoIdx = -1;
							if (g_ShowGizmo && g_LastCampathCtx.active)
							{
								int idx = 0;
								const double eps = 1e-9;
								for (CamPathIterator it = g_Hook_VClient_RenderView.m_CamPath.GetBegin();
									 it != g_Hook_VClient_RenderView.m_CamPath.GetEnd(); ++it, ++idx)
								{
									if (fabs(it.GetTime() - g_LastCampathCtx.time) < eps)
									{
										prevGizmoIdx = idx;
										break;
									}
								}
							}
                            // Snap the first moved key, keeping the entire selection's spacing intact.
                            double musicalCorrection = 0;
                            if (!ImGui::GetIO().KeyShift && advancedfx::TimelineTimebase().Mode == 2 && advancedfx::TimelineTimebase().Snap) {
                                for (size_t i=0;i<s_cache.ticks.size();++i) if(s_cache.ticks[i]!=s_cache.originalTicks[i]) {
                                    const double desired = s_cache.times[i] + (double(s_cache.ticks[i])-s_cache.originalTicks[i])*ipt + g_Hook_VClient_RenderView.m_CamPath.GetOffset();
                                    double demoTime, client;
                                    if(CinematicClock(desired,true,demoTime,SceneCachePreviewActive()) && CinematicClock(advancedfx::TimelineTimebase().SnapTime(demoTime),false,client,SceneCachePreviewActive())) musicalCorrection=client-desired;
                                    break;
                                }
                            }
                            auto retimed = [&](size_t i) {return s_cache.times[i]+(double(s_cache.ticks[i])-s_cache.originalTicks[i])*ipt+(s_cache.ticks[i]!=s_cache.originalTicks[i]?musicalCorrection:0.);};
							// Convert ticks -> new times
							struct TmpKey
							{
								double t;
								int origIndex;
							};
							std::vector<TmpKey> newKeys;
							newKeys.reserve(s_cache.ticks.size());
							for (size_t i = 0; i < s_cache.ticks.size(); ++i)
							{
								const double newRelTime =
									retimed(i);
								newKeys.push_back({newRelTime, (int)i});
							}
							// Validate all arrivals before replacing the native map.
							std::sort(newKeys.begin(), newKeys.end(),
									  [](const TmpKey &a, const TmpKey &b) { return a.t < b.t; });

							std::vector<double> arrivals;
							for (size_t i = 0; i < s_cache.ticks.size(); ++i)
								arrivals.push_back(retimed(i));
							if (!g_Hook_VClient_RenderView.m_CamPath.Retime(arrivals))
							{
								g_DocumentStatus = "Keys cannot share an arrival time or cross a hold release.";
								g_SequencerNeedsRefresh = true;
								newKeys.clear();
							}

							if (!prevSelectedIdx.empty() && !newKeys.empty())
							{
								std::vector<int> newSelected;
								newSelected.reserve(prevSelectedIdx.size());
								for (int oldIdx : prevSelectedIdx)
								{
									for (size_t ni = 0; ni < newKeys.size(); ++ni)
										if (newKeys[ni].origIndex == oldIdx)
										{
											newSelected.push_back((int)ni);
											break;
										}
								}
								std::sort(newSelected.begin(), newSelected.end());
								newSelected.erase(std::unique(newSelected.begin(), newSelected.end()),
												  newSelected.end());
								SetKeySelection(newSelected);
								g_CurveSelection = newSelected;
							}

							for (size_t i = 0; i < newKeys.size(); ++i)
								s_cache.times[i] = newKeys[i].t;
							s_cache.valid = false;
							g_CurveCache.valid = false;
							if (prevGizmoIdx >= 0)
							{
								int newIdx = -1;
								for (size_t ni = 0; ni < newKeys.size(); ++ni)
									if (newKeys[ni].origIndex == prevGizmoIdx)
									{
										newIdx = (int)ni;
										break;
									}
								if (newIdx >= 0)
								{
									int idx = 0;
									for (CamPathIterator it = g_Hook_VClient_RenderView.m_CamPath.GetBegin();
										 it != g_Hook_VClient_RenderView.m_CamPath.GetEnd(); ++it, ++idx)
										if (idx == newIdx)
										{
											g_LastCampathCtx.active = true;
											g_LastCampathCtx.time = it.GetTime();
											g_LastCampathCtx.value = it.GetValue();
											break;
										}
								}
								else
								{
									g_LastCampathCtx.active = false;
								}
							}
						}
						s_wasDragging = draggingNow;
						ImGui::EndNeoTimeLine();
					}

					ImGui::PopID();
					for (size_t index = 0; index < document.GetCount(); ++index)
						if (pathsOpen && index != document.GetActive() && SequencerPathVisible(index))
						{
							ImGui::PushID((int)document.GetId(index));
							std::string name =
								document.GetName(index) + (document.IsMuted(index)	? " [disabled]"
														   : document.IsSolo(index) ? " [marked]"
																					: "");
							if (ImGui::BeginNeoTimelineEx(name.c_str(), nullptr, ImGuiNeoTimelineFlags_None))
							{
								if (ImGui::IsItemClicked())
									choosePath = index;
								auto *shot = document.GetPath(index);
								if (shot->GetSize())
								{
									int begin = tick_from_client(shot->GetLowerBound() + shot->GetOffset()),
										end = tick_from_client(shot->GetUpperBound() + shot->GetOffset());
									if (ImGui::NeoTimelineSpan(begin, end,
															   document.IsMuted(index)
																   ? IM_COL32(100, 100, 100, 140)
																   : IM_COL32(50, 140, 190, 150),
															   name.c_str()))
										choosePath = index;
									for (auto key = shot->GetBegin(); key != shot->GetEnd(); ++key)
									{
										int tick = tick_from_client(key.GetTime() + shot->GetOffset());
										ImGui::NeoTimelineSpan(tick, tick, IM_COL32(220, 220, 220, 255),
															   "Keyframe");
										if (key.GetValue().HoldDuration > 0)
										{
											int release =
												tick_from_client(key.GetTime() + key.GetValue().HoldDuration +
																 shot->GetOffset());
											ImGui::NeoTimelineSpan(tick, release, IM_COL32(220, 150, 50, 150),
																   "Hold");
											ImGui::NeoTimelineSpan(release, release,
																   IM_COL32(255, 190, 70, 255),
																   "Hold release");
										}
									}
								}
								ImGui::EndNeoTimeLine();
							}
							ImGui::PopID();
						}
					if (pathRows > 1 && pathsOpen)
						ImGui::EndNeoGroup();
					if (SequencerTrackVisible(1, haveAuthoredPaths) &&
						ImGui::BeginNeoTimelineEx("Playback / cuts", nullptr, ImGuiNeoTimelineFlags_None))
					{
						const auto state = document.CaptureState();
						const double demoBegin = haveDemoNow ? curTime - demoNow : 0;
						std::vector<double> boundaries{demoBegin, demoBegin + (double)s_seqEnd * ipt};
						for (const auto &entry : state.Paths)
							if (!entry.Data->Keys.empty())
							{
								boundaries.push_back(entry.Data->Keys.begin()->first + entry.Data->Offset);
								boundaries.push_back(entry.Data->Keys.rbegin()->first +
													 entry.Data->Keys.rbegin()->second.HoldDuration +
													 entry.Data->Offset);
							}
						std::sort(boundaries.begin(), boundaries.end());
						boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
						size_t previous = document.GetCount();
						for (size_t i = 1; i < boundaries.size(); ++i)
						{
							double begin = (std::max)(demoBegin, boundaries[i - 1]),
								   end = (std::min)(demoBegin + (double)s_seqEnd * ipt, boundaries[i]);
							if (end <= begin)
								continue;
							const auto winner = document.GetPlaybackIndex((begin + end) * 0.5, false);
							if (winner < document.GetCount())
							{
								const auto &shot = *state.Paths[winner].Data;
								const double first = shot.Keys.begin()->first + shot.Offset,
											 last = shot.Keys.rbegin()->first +
													shot.Keys.rbegin()->second.HoldDuration + shot.Offset;
								const bool endpointHold = end <= first || begin >= last;
								const std::string caption =
									document.GetName(winner) + (endpointHold ? " (endpoint hold)" : "");
								if (ImGui::NeoTimelineSpan(tick_from_client(begin), tick_from_client(end),
														   endpointHold ? IM_COL32(120, 130, 145, 80)
														   : winner % 2 ? IM_COL32(100, 170, 110, 200)
																		: IM_COL32(80, 130, 210, 200),
														   caption.c_str()))
									choosePath = winner;
							}
							if (winner != previous)
								ImGui::NeoTimelineSpan(tick_from_client(begin), tick_from_client(begin),
													   IM_COL32(255, 230, 110, 255),
													   "Playback cut / boundary");
							previous = winner;
						}
						ImGui::EndNeoTimeLine();
					}
     if (SequencerTrackVisible(2, !clipsForView.empty() || !modifiersForView.empty()))
     {
      DrawSequenceModifierTracks(tick_from_client, visibleStart, visibleEnd, keyframeContextOpened);
      if (!clipsForView.empty()) DrawCameraClipTrack(tick_from_client, visibleStart, visibleEnd, keyframeContextOpened);
     }
					if (SequencerTrackVisible(3, !g_SceneCache.Frames().empty()))
						DrawSceneCoverage();
					DrawSequencerExtraTracks(visibleStart, visibleEnd);
					if (SequencerTrackVisible(6, SequencerRepairPopulated()))
						DrawPlayerRepairTrack(visibleStart, visibleEnd);
					DrawSequencerSeekContext(keyframeContextOpened);
					if (choosePath < document.GetCount())
						ImGui::NeoCancelSelection();
					ImGui::EndNeoSequencer();
					if (choosePath < document.GetCount())
					{
						const double client =
							MirvCampathPreview_GetTime() + g_Hook_VClient_RenderView.m_CamPath.GetOffset();
						document.Select(choosePath);
						g_CurveKeyDrag = false;
						if (MirvCampathPreview_IsActive())
							MirvCampathPreview_SetTime(client -
													   g_Hook_VClient_RenderView.m_CamPath.GetOffset());
					}
				}
			}
			ImGui::EndChild();

			if (ImGui::SmallButton("Fit demo"))
				s_fitTimeline = true;
			ImGui::SameLine();
			ImGui::TextDisabled("View %d - %d | Wheel: zoom | MMB / Shift+wheel: pan", visibleStart, visibleEnd);
			DrawOverlayHint("Retiming keeps Auto handles automatic and rescales Free handles. Classic scalar curves convert when using editable Euler.");
			// Controls row
			if (ImGui::SmallButton("Play"))
			{
				Afx_ExecClientCmd("demo_timescale 1;demo_resume");
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Freeze"))
			{
				Afx_ExecClientCmd("demo_timescale 0;demo_resume");
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("0.2x"))
			{
				Afx_ExecClientCmd("demo_timescale 0.2;demo_resume");
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("0.5x"))
			{
				Afx_ExecClientCmd("demo_timescale 0.5;demo_resume");
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("1x"))
			{
				Afx_ExecClientCmd("demo_timescale 1;demo_resume");
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("2x"))
			{
				Afx_ExecClientCmd("demo_timescale 2;demo_resume");
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("5x"))
			{
				Afx_ExecClientCmd("demo_timescale 5;demo_resume");
			}
			ImGui::SameLine();
			ImGui::Text("Start: %d  End: %d  Current: %d", (int)s_seqStart, (int)s_seqEnd, (int)s_seqFrame);
            if(timebase.Mode==2) {
                double demoTime=double(s_seqFrame)*SceneTickInterval();
                if(SceneCachePreviewActive())demoTime=g_SceneCursor;
                else if(MirvCam_SequencePreviewActive())CinematicClock(MirvCam_SequencePreviewTime(),true,demoTime,false);
                else if(MirvCampathPreview_IsActive())CinematicClock(MirvCampathPreview_GetTime()+g_Hook_VClient_RenderView.m_CamPath.GetOffset(),true,demoTime,false);
                char position[48];timebase.Position(demoTime,position,sizeof(position));
                ImGui::Text("Music: %s | %+.3f s relative to bar 1",position,demoTime-timebase.Downbeat());
            }
			if (!SceneCachePreviewActive() && ImGui::SmallButton("Seek to preview"))
				MirvCampathPreview_Seek();
			ImGui::SameLine();
			if (ImGui::SmallButton("Insert keyframe at preview"))
				PreviewInsert();

			ImGui::SameLine();
			if (ImGui::SmallButton(g_ShowCurveEditor ? "Hide Curve Editor" : "Show Curve Editor"))
			{
				g_ShowCurveEditor = !g_ShowCurveEditor;
				if (g_ShowCurveEditor)
					g_FocusGraph = true;
			}
			// Standalone Curve Editor (outside Neo timeline)
			// A view onto the native preview cursor; the controller owns the camera.
			if (MirvCam_SequencePreviewActive() || g_Hook_VClient_RenderView.m_CamPath.GetSize())
			{
				auto &path = g_Hook_VClient_RenderView.m_CamPath;
				ImGui::Separator();
				const bool sequence = MirvCam_SequencePreviewActive();
				double begin = 0, end = 0;
				if (sequence)
					MirvCam_SequencePreviewRange(begin, end);
				else
				{
					begin = path.GetLowerBound();
					end = path.GetUpperBound();
				}
				const double previewTime =
					sequence ? MirvCam_SequencePreviewTime() : MirvCampathPreview_GetTime();
				float position = end > begin ? (float)((previewTime - begin) / (end - begin)) : 0;
				position = std::clamp(position, 0.0f, 1.0f);
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
				ImGui::BeginDisabled((!sequence && !path.CanEval()) || end <= begin);
				if (!SceneCachePreviewActive() && ImGui::SliderFloat("##previewcampath", &position, 0, 1, "%.3f"))
				{
					if (sequence)
						MirvCam_SetSequencePreviewTime(begin + position * (end - begin));
					else
						MirvCampathPreview_SetTime(begin + position * (end - begin));
				}
				ImGui::EndDisabled();
				ImGui::TextDisabled("%s preview: %.3f s | Demo time stays independent",
									sequence ? "Sequence" : "Camera", previewTime);
				if (sequence && !SceneCachePreviewActive())
					ImGui::TextDisabled(
						"Entity targets use their current poses. Cached preview supplies recorded motion.");
			}

			if (ImGui::Button("Open demo..."))
				OpenDemoPicker();
			ImGui::SameLine();
			ImGui::SetNextItemWidth(140);
			bool go = ImGui::InputInt("Go to tick", &g_SequencerTimeline.GoToTick, 1, 100,
			                          ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			go |= ImGui::Button("Go");
			if (go)
			{
				if (SceneCachePreviewActive()) s_seqFrame=(std::max)(0,g_SequencerTimeline.GoToTick);
				else if (MirvCam_SequencePreviewActive())
				{
					double client;
					if (CinematicClock((std::max)(0, g_SequencerTimeline.GoToTick) * SceneTickInterval(),
									   false, client, false))
						MirvCam_SetSequencePreviewTime(client);
				}
				else Afx_GotoDemoTick((std::max)(0,g_SequencerTimeline.GoToTick));
			}
			DrawSequencerDetails(int(s_seqFrame));

			// Cached mode routes pointer changes only to the historical preview clock.
			static bool s_draggingPointer = false;
			static ImGui::FrameIndexType s_dragStartFrame = 0;
			const bool pointerMoved = (s_seqFrame != prevFrame);
			const bool lmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
			const bool lmbReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
			const bool seqHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
			if (!s_draggingPointer)
			{
				if (pointerMoved && lmbDown && seqHovered)
				{
					s_draggingPointer = true;
					s_dragStartFrame = prevFrame;
				}
			}
			if (s_draggingPointer)
			{
				if (lmbReleased || !lmbDown)
				{
					if (s_seqFrame != s_dragStartFrame && !SceneCachePreviewActive() &&
						!MirvCam_SequencePreviewActive())
						Afx_GotoDemoTick((int)s_seqFrame);
					s_draggingPointer = false;
				}
			}
			else if (haveDemoTick && !SceneCachePreviewActive() && !MirvCam_SequencePreviewActive())
			{
				s_seqFrame = std::clamp(curDemoTick, int(s_seqStart), int(s_seqEnd));
			}

			if (MirvCam_SequencePreviewActive() && !SceneCachePreviewActive() && pointerMoved &&
				!fractionalPointerHandled)
			{
				double client;
				if (CinematicClock(s_seqFrame * SceneTickInterval(), false, client, false))
					MirvCam_SetSequencePreviewTime(client);
			}
			if (SceneCachePreviewActive())
			{
				if (pointerMoved && !fractionalPointerHandled)
				{
					SceneCacheStopScrub();
					ScenePreviewTick(s_seqFrame);
				}
				ImGui::TextDisabled("Cached tick %.3f | Actual demo tick %d",
				                    g_SceneCursor / SceneTickInterval(), curDemoTick);
				if (!SceneCacheView())
					ImGui::TextDisabled("No cached coverage at this tick.");
			}

			ImGui::End();
		}
	}
	// Camera Control Window
	if (g_ShowCameraControl)
	{
		if (MirvInput *pMirv = g_Hook_VClient_RenderView.m_MirvInput)
		{
			// Mirv Camera: horizontally resizable; adjust height to content each frame
			ImGui::SetNextWindowPos(ImVec2((std::max)(600.0f, ImGui::GetIO().DisplaySize.x - 370), 30),
									ImGuiCond_FirstUseEver);
			ImGui::Begin("Mirv Camera");
			bool camEnabled = pMirv->GetCameraControlMode() && !MirvCampathPreview_IsActive();
			ImGui::Text("Mirv Camera (C): %s", camEnabled ? "enabled" : "disabled");
			ImGui::SameLine();
			if (ImGui::Button(camEnabled ? "Disable" : "Enable"))
			{
				if (!camEnabled)
					MirvCam_BeginFreeCamera();
				else
					pMirv->SetCameraControlMode(false);
			}
			// FOV slider (reserve right space so label is always visible)
			{
				ImGuiStyle &st3 = ImGui::GetStyle();
				float avail = ImGui::GetContentRegionAvail().x;
				const char *lbl = "FOV";
				float lblW = ImGui::CalcTextSize(lbl).x;
				float rightGap = st3.ItemInnerSpacing.x * 3.0f + st3.FramePadding.x * 2.0f + 20.0f;
				float width = avail - (lblW + rightGap);
				if (width < 100.0f)
					width = avail * 0.6f; // fallback
				ImGui::SetNextItemWidth(width);
			}
			// FOV slider (synced with RMB+wheel in passthrough)
			if (!g_uiFovInit)
			{
				g_uiFov = GetLastCameraFov();
				g_uiFovDefault = g_uiFov;
				g_uiFovInit = true;
			}
			// While using RMB+F FOV-modifier, reflect live FOV into the slider value
			if (IsMouseLook())
			{
				g_uiFov = GetLastCameraFov();
				g_uiFovInit = true;
			}
			{
				float tmp = g_uiFov;
				bool changed = ImGui::SliderFloat("FOV (F)", &tmp, 1.0f, 179.0f, "%.1f deg");
				bool reset = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
				if (reset)
				{
					g_uiFov = g_uiFovDefault;
					ImGui::ClearActiveID();
					pMirv->SetFov(g_uiFov);
				}
				else if (changed)
				{
					g_uiFov = tmp;
					pMirv->SetFov(g_uiFov);
				}
			}

			// Roll slider (reserve right space so label is always visible)
			{
				ImGuiStyle &st3 = ImGui::GetStyle();
				float avail = ImGui::GetContentRegionAvail().x;
				const char *lbl = "Roll";
				float lblW = ImGui::CalcTextSize(lbl).x;
				float rightGap = st3.ItemInnerSpacing.x * 3.0f + st3.FramePadding.x * 2.0f + 20.0f;
				float width = avail - (lblW + rightGap);
				if (width < 100.0f)
					width = avail * 0.6f; // fallback
				ImGui::SetNextItemWidth(width);
			}
			if (!g_uiRollInit)
			{
				g_uiRoll = GetLastCameraRoll();
				g_uiRollDefault = g_uiRoll;
				g_uiRollInit = true;
			}
			// While using RMB+R roll-modifier, reflect live roll into the slider value
			if (IsMouseLook())
			{
				g_uiRoll = GetLastCameraRoll();
				g_uiRollInit = true;
			}
			{
				float tmp = g_uiRoll;
				bool changed = ImGui::SliderFloat("Roll (R)", &tmp, -180.0f, 180.0f, "%.1f deg");
				bool reset = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
				if (reset)
				{
					g_uiRoll = g_uiRollDefault;
					ImGui::ClearActiveID();
					pMirv->SetRz(g_uiRoll);
				}
				else if (changed)
				{
					g_uiRoll = tmp;
					pMirv->SetRz(g_uiRoll);
				}
			}

			// Keyboard sensitivity (mirv_input cfg ksens) (reserve right space so label is always visible)
			{
				ImGuiStyle &st3 = ImGui::GetStyle();
				float avail = ImGui::GetContentRegionAvail().x;
				const char *lbl = "ksenss";
				float lblW = ImGui::CalcTextSize(lbl).x * g_UiScale;
				float rightGap = st3.ItemInnerSpacing.x * 3.0f + st3.FramePadding.x * 2.0f + 20.0f;
				float width = avail - (lblW + rightGap);
				if (width < 100.0f)
					width = avail * 0.6f; // fallback
				ImGui::SetNextItemWidth(width);
			}
			if (!g_uiKsensInit)
			{
				g_uiKsens = (float)pMirv->GetKeyboardSensitivty();
				g_uiKsensDefault = g_uiKsens;
				g_uiKsensInit = true;
			}
			{
				float tmp = g_uiKsens;
				bool changed = ImGui::SliderFloat("ksens (Scroll)", &tmp, 0.01f, 10.0f, "%.2f");
				bool reset = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
				if (reset)
				{
					g_uiKsens = g_uiKsensDefault;
					ImGui::ClearActiveID();
					pMirv->SetKeyboardSensitivity(g_uiKsens);
				}
				else if (changed)
				{
					g_uiKsens = tmp;
					pMirv->SetKeyboardSensitivity(g_uiKsens);
				}
			}
			// Auto-height: shrink/grow to fit current content while preserving user-set width
			{
				ImVec2 cur = ImGui::GetWindowSize();
				float remain = ImGui::GetContentRegionAvail().y;
				float desired = cur.y - remain;
				float min_h = ImGui::GetFrameHeightWithSpacing() * 4.0f; // camera panel needs a bit more
				if (desired < min_h)
					desired = min_h;
				ImGui::SetWindowSize(ImVec2(cur.x, desired));
			}
			ImGui::End();
		}
	}
	// Gizmo overlay + camera control mini panel
	{

		// Hotkeys: G = translate, R = rotate, C = toggle camera control
		if (ImGui::GetActiveID() == 0)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_G, false))
			{
				g_ShowGizmo = true;
				g_GizmoOp = ImGuizmo::TRANSLATE;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_R, false))
			{
				g_ShowGizmo = true;
				g_GizmoOp = ImGuizmo::ROTATE;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_C, false))
			{
				if (MirvInput *pMirv = g_Hook_VClient_RenderView.m_MirvInput)
				{
					bool camEnabled = pMirv->GetCameraControlMode() && !MirvCampathPreview_IsActive();
					if (!camEnabled)
						MirvCam_BeginFreeCamera();
					else
						pMirv->SetCameraControlMode(false);
				}
			}
		}
		if (g_ShowGizmo)
		{
			ImGui::SetNextWindowPos(ImVec2((std::max)(700.0f, ImGui::GetIO().DisplaySize.x - 260), 280),
									ImGuiCond_FirstUseEver);
			ImGui::Begin("Gizmo", &g_ShowGizmo, ImGuiWindowFlags_AlwaysAutoResize);
			if (ImGui::RadioButton("Pos (G)", g_GizmoOp == ImGuizmo::TRANSLATE))
				g_GizmoOp = ImGuizmo::TRANSLATE;
			ImGui::SameLine();
			if (ImGui::RadioButton("Rot (R)", g_GizmoOp == ImGuizmo::ROTATE))
				g_GizmoOp = ImGuizmo::ROTATE;
			if (ImGui::RadioButton("Local", g_GizmoMode == ImGuizmo::LOCAL))
				g_GizmoMode = ImGuizmo::LOCAL;
			ImGui::SameLine();
			if (ImGui::RadioButton("World", g_GizmoMode == ImGuizmo::WORLD))
				g_GizmoMode = ImGuizmo::WORLD;
			ImGui::End();
		}

		if (g_ShowGizmo && g_LastCampathCtx.active)
		{
			ImVec2 ds = ImGui::GetIO().DisplaySize;
			ImGuizmo::Enable(!IsMouseLook());
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
			ImGuizmo::SetRect(0.0f, 0.0f, ds.x, ds.y);

			// Build view from current camera
			double cx, cy, cz, rX, rY, rZ;
			float cfovDeg;
			Afx_GetLastCameraData(cx, cy, cz, rX, rY, rZ, cfovDeg);
			auto DegToRad = [](double d) { return (float)(d * 3.14159265358979323846 / 180.0); };
			auto SrcToDx = [](float x, float y, float z, float out[3]) {
				out[0] = -y;
				out[1] = z;
				out[2] = x;
			};
			auto BuildModel = [&](float px, float py, float pz, float pitchDeg, float yawDeg, float rollDeg,
								  float out[16]) {
				double fwdS[3], rightS[3], upS[3];
				Afx::Math::MakeVectors(rollDeg, pitchDeg, yawDeg, fwdS, rightS, upS);
				float fwd[3], right[3], up[3], pos[3];
				SrcToDx((float)rightS[0], (float)rightS[1], (float)rightS[2], right);
				SrcToDx((float)upS[0], (float)upS[1], (float)upS[2], up);
				SrcToDx((float)fwdS[0], (float)fwdS[1], (float)fwdS[2], fwd);
				SrcToDx(px, py, pz, pos);
				out[0] = right[0];
				out[1] = right[1];
				out[2] = right[2];
				out[3] = 0.0f;
				out[4] = up[0];
				out[5] = up[1];
				out[6] = up[2];
				out[7] = 0.0f;
				out[8] = fwd[0];
				out[9] = fwd[1];
				out[10] = fwd[2];
				out[11] = 0.0f;
				out[12] = pos[0];
				out[13] = pos[1];
				out[14] = pos[2];
				out[15] = 1.0f;
			};
			auto BuildView = [&](float eyeX, float eyeY, float eyeZ, float pitchDeg, float yawDeg,
								 float rollDeg, float out[16]) {
				float M[16];
				BuildModel(eyeX, eyeY, eyeZ, pitchDeg, yawDeg, rollDeg, M);
				const float r00 = M[0], r01 = M[1], r02 = M[2];
				const float r10 = M[4], r11 = M[5], r12 = M[6];
				const float r20 = M[8], r21 = M[9], r22 = M[10];
				const float tx = M[12], ty = M[13], tz = M[14];
				// inverse rigid: R^T and -T * R^T (row-major)
				out[0] = r00;
				out[1] = r10;
				out[2] = r20;
				out[3] = 0.0f;
				out[4] = r01;
				out[5] = r11;
				out[6] = r21;
				out[7] = 0.0f;
				out[8] = r02;
				out[9] = r12;
				out[10] = r22;
				out[11] = 0.0f;
				out[12] = -(tx * r00 + ty * r01 + tz * r02);
				out[13] = -(tx * r10 + ty * r11 + tz * r12);
				out[14] = -(tx * r20 + ty * r21 + tz * r22);
				out[15] = 1.0f;
			};
			auto PerspectiveGlRh = [](float fovyDeg, float aspect, float znear, float zfar, float m16[16]) {
				const float f = 1.0f / tanf((float)(fovyDeg * 3.14159265358979323846 / 180.0 * 0.5));
				m16[0] = f / aspect;
				m16[1] = 0;
				m16[2] = 0;
				m16[3] = 0;
				m16[4] = 0;
				m16[5] = f;
				m16[6] = 0;
				m16[7] = 0;
				m16[8] = 0;
				m16[9] = 0;
				m16[10] = -(zfar + znear) / (zfar - znear);
				m16[11] = -1.0f;
				m16[12] = 0;
				m16[13] = 0;
				m16[14] = -(2.0f * zfar * znear) / (zfar - znear);
				m16[15] = 0;
			};

			float view[16];
			BuildView((float)cx, (float)cy, (float)cz, (float)rX, (float)rY, (float)rZ, view);
			// Z-flip
			view[2] *= -1.0f;
			view[6] *= -1.0f;
			view[10] *= -1.0f;
			view[14] *= -1.0f;
			float aspect =
				g_Hook_VClient_RenderView.LastHeight > 0
					? (float)g_Hook_VClient_RenderView.LastWidth / (float)g_Hook_VClient_RenderView.LastHeight
					: (ds.y > 0 ? ds.x / ds.y : 16.0f / 9.0f);
			// Convert game FOV to a vertical FOV based on 4:3
			float fovyDeg = cfovDeg;
			{
				const float baseAspect = 4.0f / 3.0f;
				const float fovRad = (float)(cfovDeg * 3.14159265358979323846 / 180.0);
				const float half = 0.5f * fovRad;
				const float halfVert = atanf(tanf(half) / baseAspect);
				fovyDeg = (float)(2.0f * (halfVert) * 180.0 / 3.14159265358979323846);
			}
			float proj[16];
			PerspectiveGlRh(fovyDeg, aspect, 0.1f, 100000.0f, proj);

			// Model from selected key
			float model[16];
			{
				const CamPathValue &v = g_LastCampathCtx.value;
				using namespace Afx::Math;
				QEulerAngles ea = v.GetEulerAngles();
				BuildModel((float)v.X, (float)v.Y, (float)v.Z, (float)ea.Pitch, (float)ea.Yaw, (float)ea.Roll,
						   model);
			}

			// Manipulate
			bool snap = ImGui::GetIO().KeyCtrl;
			float snapTranslate[3] = {1, 1, 1};
			float snapRotate = 5.0f;
			if (g_GizmoDrag.HasModel && fabs(g_GizmoDrag.KeyTime - g_LastCampathCtx.time) < 1e-9)
				for (int i = 0; i < 16; ++i)
					model[i] = g_GizmoDrag.Model[i];
			else
				g_GizmoDrag.HasModel = false;
			ImGuizmo::Enable(!g_CancelGesture && !IsMouseLook() &&
							 (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || ImGuizmo::IsUsing()));
			bool changed = ImGuizmo::Manipulate(
				view, proj, g_GizmoOp, g_GizmoMode, model, nullptr,
				snap ? (g_GizmoOp == ImGuizmo::ROTATE ? &snapRotate : snapTranslate) : nullptr);
			(void)changed; // suppress warn
			bool usingNow = ImGuizmo::IsUsing();
			if (usingNow && !g_GizmoDrag.WasUsing)
				EnsureEditGesture();
			if (usingNow)
			{
				for (int i = 0; i < 16; ++i)
					g_GizmoDrag.Model[i] = model[i];
				g_GizmoDrag.HasModel = true;
				g_GizmoDrag.KeyTime = g_LastCampathCtx.time;
				if (changed)
					g_GizmoDrag.Changed = true;
			}
			if (g_CancelGesture)
			{
				g_GizmoDrag.Changed = false;
				g_GizmoDrag.HasModel = false;
				g_GizmoDrag.WasUsing = false;
			}
			if (!g_CancelGesture && g_GizmoDrag.WasUsing && !usingNow &&
				(g_GizmoDrag.Changed || g_GizmoDrag.HasModel))
			{
				float t[3], r_dummy[3], s_dummy[3];
				const float *finalModel = g_GizmoDrag.HasModel ? g_GizmoDrag.Model : model;
				ImGuizmo::DecomposeMatrixToComponents(finalModel, t, r_dummy, s_dummy);
				// Back to Source axes
				double sx = (double)t[2];
				double sy = (double)(-t[0]);
				double sz = (double)t[1];
				// Extract basis
				float Rx = finalModel[0], Ry = finalModel[1], Rz = finalModel[2];
				float Ux = finalModel[4], Uy = finalModel[5], Uz = finalModel[6];
				float Fx = finalModel[8], Fy = finalModel[9], Fz = finalModel[10];
				auto im_to_src = [](float ix, float iy, float iz, double out[3]) {
					out[0] = (double)iz;
					out[1] = (double)(-ix);
					out[2] = (double)iy;
				};
				double R_s[3], U_s[3], F_s[3];
				im_to_src(Rx, Ry, Rz, R_s);
				im_to_src(Ux, Uy, Uz, U_s);
				im_to_src(Fx, Fy, Fz, F_s);
				auto norm3 = [](double v[3]) {
					double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
					if (l > 1e-12)
					{
						v[0] /= l;
						v[1] /= l;
						v[2] /= l;
					}
				};
				norm3(R_s);
				norm3(U_s);
				norm3(F_s);
				double sp = -F_s[2];
				sp = sp < -1.0 ? -1.0 : (sp > 1.0 ? 1.0 : sp);
				double pitch = asin(sp) * (180.0 / 3.14159265358979323846);
				double cp = cos(pitch * (3.14159265358979323846 / 180.0));
				double yaw = 0.0;
				if (fabs(cp) > 1e-6)
					yaw = atan2(F_s[1], F_s[0]) * (180.0 / 3.14159265358979323846);
				double fwd0[3], right0[3], up0[3];
				{
					double fwdQ[3], rightQ[3], upQ[3];
					Afx::Math::MakeVectors(0.0, pitch, yaw, fwdQ, rightQ, upQ);
					fwd0[0] = fwdQ[0];
					fwd0[1] = fwdQ[1];
					fwd0[2] = fwdQ[2];
					right0[0] = rightQ[0];
					right0[1] = rightQ[1];
					right0[2] = rightQ[2];
					up0[0] = upQ[0];
					up0[1] = upQ[1];
					up0[2] = upQ[2];
				}
				double dot_ur = U_s[0] * right0[0] + U_s[1] * right0[1] + U_s[2] * right0[2];
				double dot_uu = U_s[0] * up0[0] + U_s[1] * up0[1] + U_s[2] * up0[2];
				double roll = atan2(dot_ur, dot_uu) * (180.0 / 3.14159265358979323846);
				CamPathValue newVal = g_LastCampathCtx.value;
				newVal.X = sx;
				newVal.Y = sy;
				newVal.Z = sz;
				if (g_GizmoOp == ImGuizmo::ROTATE)
					newVal.SetOrientation(Quaternion::FromQREulerAngles(
						QREulerAngles::FromQEulerAngles(QEulerAngles(pitch, yaw, roll))));
				if (!g_Hook_VClient_RenderView.m_CamPath.ReplacePose(
						g_LastCampathCtx.time, newVal,
						[&]()
						{
							unsigned int mask = g_GizmoOp == ImGuizmo::ROTATE ? 0x70 : 7;
							for (int ch = 0; ch < 7; ++ch)
								if (g_CurveLocked[ch])
									mask &= ~(1u << ch);
							return mask;
						}()))
					g_DocumentStatus = "Gizmo edit conflicts with a neighbouring hold; edit rejected.";

				g_GizmoDrag.Changed = false;
				g_GizmoDrag.HasModel = false;
			}
			g_GizmoDrag.WasUsing = usingNow;
		}
	}

	DrawDemoPicker();
	DrawCurveEditor();
	if (g_GestureStart)
	{
		g_GestureOwner = document.GetId(document.GetActive());
		g_GestureRevision = g_Hook_VClient_RenderView.m_CamPath.GetRevision();
	}
	if (!ImGui::IsMouseDown(0))
	{
		g_GestureStart.reset();
		g_TangentDrag = {};
		g_CancelGesture = false;
	}
	HistoryObserve();
	if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(0))
		FinishEdit();
}

} // namespace CameraOverlay
} // namespace AfxHookSource
