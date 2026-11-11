// Adapted from Paper (papesgit/advancedfx), shared/overlay/OverlayDx9.cpp.
// MIT license; see shared/overlay/PORTING.md for provenance and scope.
#include "CameraOverlay.h"
#include "CampathDrawer.h"
#include "Gui.h"
#include "MirvCalcs.h"
#include "MirvCam.h"
#include "MirvTime.h"
#include "RenderView.h"
#include "WrpVEngineClient.h"
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
#include <shared/overlay/third_party/imgui_filebrowser/imfilebrowser.h>
#include <shared/overlay/third_party/imgui_neo_sequencer/imgui_neo_sequencer.h>
#include <shared/overlay/third_party/imguizmo/ImGuizmo.h>
#include <shellapi.h>
#include <string>
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
static std::atomic_bool g_Open{false}, g_MouseLook{false};
static std::recursive_mutex g_SceneMutex;
static advancedfx::SceneCache g_SceneCache;
static bool g_SceneRecord = false, g_ScenePreview = false;
static double g_SceneCursor = 0;
static ImGui::FrameIndexType g_SequencerFrame = 0;
static int g_SceneLastTick = -1;
static std::shared_ptr<const advancedfx::SceneCache::Frame> g_SceneView;
std::shared_ptr<const advancedfx::SceneCache::Frame> SceneCacheView()
{
	return std::atomic_load(&g_SceneView);
}
bool SceneCachePreviewActive() { std::lock_guard<std::recursive_mutex> lock(g_SceneMutex); return g_ScenePreview; }
double EditingClientTime(double liveTime)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	if (!g_ScenePreview) return liveTime;
	auto view = SceneCacheView();
	return view ? view->ClientTime : liveTime;
}
void SceneCacheFrame(bool capture)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	int tick;
	double time;
	if (!g_VEngineClient || !g_VEngineClient->IsPlayingDemo())
	{
		g_ScenePreview = false;
		std::atomic_store(&g_SceneView, std::shared_ptr<const advancedfx::SceneCache::Frame>{});
		return;
	}
	if (capture && g_SceneRecord && !g_VEngineClient->IsSkippingDemo() && GetCurrentDemoTick(tick) &&
		GetCurrentDemoTime(time) && tick != g_SceneLastTick)
	{
		advancedfx::SceneCache::Frame frame;
		frame.DemoTime = time;
		frame.ClientTime = g_MirvTime.GetTime();
		for (int index = 1; index <= Mirv_Tf2_GetMaxPlayers(); ++index)
		{
			char name[128]; int user = 0;
			advancedfx::SceneCache::Entity entity;
			if (!Mirv_Tf2_GetPlayerInfo(index, name, sizeof(name), &user) || user <= 0 ||
				!Mirv_Tf2_GetSceneEntity(index, entity.Origin, entity.Angles, entity.Mins, entity.Maxs)) continue;
			entity.Identity = static_cast<unsigned>(user);
			frame.Entities.push_back(entity);
		}
		g_SceneCache.Append(std::move(frame));
		g_SceneLastTick = tick;
	}
	advancedfx::SceneCache::Frame frame;
	bool valid = false;
	if (g_ScenePreview)
	{
		valid = g_SceneCache.Sample(g_SceneCursor, frame);
	}
	std::atomic_store(&g_SceneView, valid ? std::make_shared<const advancedfx::SceneCache::Frame>(std::move(frame)) :
		std::shared_ptr<const advancedfx::SceneCache::Frame>{});
}
static double SceneTickInterval()
{
	auto gl = g_Hook_VClient_RenderView.GetGlobals();
	return gl && gl->interval_per_tick_get() > 0 ? gl->interval_per_tick_get() : 1.0/66.0;
}
static void ScenePreviewTick(int tick)
{
	std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
	const double interval = SceneTickInterval();
	g_SequencerFrame = tick;
	g_SceneCursor = tick * interval;
	const auto &frames = g_SceneCache.Frames();
	if (!frames.empty() && tick >= int(std::floor(frames.front().DemoTime/interval)) &&
		tick <= int(std::floor(frames.back().DemoTime/interval)))
		g_SceneCursor = std::clamp(g_SceneCursor, frames.front().DemoTime, frames.back().DemoTime);
	SceneCacheFrame(false);
	auto view = SceneCacheView();
	auto &path = g_Hook_VClient_RenderView.m_CamPath;
	if (view && MirvCampathPreview_IsActive() && path.CanEval())
		MirvCampathPreview_SetTime(view->ClientTime - path.GetOffset());
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

bool IsOpen()
{
	return g_Open;
}
void SetOpen(bool value)
{
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
	g_Hook_VClient_RenderView.m_CamPaths.Undo(redo);
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
static ImGuizmo::OPERATION g_GizmoOp = ImGuizmo::TRANSLATE;
static ImGuizmo::MODE g_GizmoMode = ImGuizmo::LOCAL;

// Sequencer/curve editor shared state (parity with DX11)
static bool g_SequencerNeedsRefresh = false;
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
	out_buf->appendf("UiScale=%.3f\nFont=%d\nDocking=%d\n\n", g_UiScale, g_FontChoice, g_Docking ? 1 : 0);
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
	ImGui::TextDisabled("Latest start wins overlaps. Duplicates start disabled. Marked paths exclude "
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
	ImGui::TextDisabled("Graph: right-click a channel checkbox to focus; drag keys; Shift locks an axis; Alt "
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
static bool DrawModifierEnvelope(CamPath::NoiseModifier &modifier)
{
	if (!ImGui::TreeNode("Strength envelope"))
		return false;
	static int dragged = -1, ownerModifier = -1;
	static unsigned long long owner = 0;
	const auto pathId =
		g_Hook_VClient_RenderView.m_CamPaths.GetId(g_Hook_VClient_RenderView.m_CamPaths.GetActive());
	if (!g_GestureStart || owner != pathId || ownerModifier != g_ModifierSelection || g_CancelGesture)
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
				EnsureEditGesture();
				dragged = (int)i;
				owner = pathId;
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
	if (dragged >= 0 && ImGui::IsMouseDragging(0) && !g_CancelGesture && (size_t)dragged < keys.size())
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
	if (preset >= 9)
		ImGui::TextWrapped("%s", controlDescriptions[preset - 9]);
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
	ImGui::TextDisabled("Presets add editable settings to the stack; existing modifiers are retained.");
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
	ImGui::TextDisabled("Authored / baked tangents survive profile changes unless reset to Auto.");
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

static void DrawObserving()
{
	if (!ImGui::BeginTabItem("Observing"))
		return;
	ImGui::TextUnformatted("Click a player to spectate in STV. Select a bone below for attachments.");
	if (ImGui::Button("Detach"))
		Afx_ExecClientCmd("mirv_cam attach clear");
	ImGui::SameLine();
	if (ImGui::Button("Capture relative framing"))
		Afx_ExecClientCmd("mirv_cam attach capture; mirv_input end");
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
	ImGui::TextDisabled("Aim the camera first; trace selects the entity at the center of the view.");
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
				if (g_VEngineClient && g_VEngineClient->IsPlayingDemo() && !g_VEngineClient->IsHLTV())
					traceStatus = "Native spectating requires an STV demo. This player remains selected for bone attachments.";
				else if (user > 0)
				{
					MirvCampathPreview_SetActive(false);
					g_Hook_VClient_RenderView.m_CamPath.Enabled_set(false);
					char cmd[192];
					sprintf_s(cmd, "mirv_campath preview exit; mirv_cam attach clear; mirv_input end; spec_autodirector 0; spec_mode 4; spec_player \"#%d\"", user);
					Afx_ExecClientCmd(cmd);
					traceStatus = "Requested spectating " + std::string(name) + ".";
				}
				else
					traceStatus = "Player has no current user ID; select them again after the demo has loaded.";
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	if (selectedEntity > 0)
	{
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
		ImGui::TextDisabled("World keeps the camera heading and offset direction. Mode applies on attachment.");
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
		ImGui::TextDisabled("Wheel: zoom time | Ctrl+wheel: zoom values | MMB: pan | Shift+handle: selected "
							"handles | Esc: cancel drag");
		ImGui::TextDisabled("Time axis: demo tick / path seconds. Circles: Auto. Squares: Manual / baked.");

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
		g_SceneCache.Clear(); g_SceneLastTick = -1; g_ScenePreview = false;
		std::atomic_store(&g_SceneView, std::shared_ptr<const advancedfx::SceneCache::Frame>{});
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
	if (ImGui::BeginNeoTimelineEx("Commands", nullptr, ImGuiNeoTimelineFlags_None))
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
	if (ImGui::BeginNeoTimelineEx("Demo events", nullptr, ImGuiNeoTimelineFlags_None))
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
static void DrawSequencerSeekContext()
{
	auto &state = g_SequencerTimeline;
	ImGui::FrameIndexType tick;
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::GetNeoSequencerMouseFrame(&tick))
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
		ImGui::TextDisabled("Amber spans are blocked or partly blocked; hover for anchor details.");
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
static void DrawSequencerDetails(int playhead)
{
	auto &state = g_SequencerTimeline;
	static ImGui::FileBrowser indexDialog(ImGuiFileBrowserFlags_CloseOnEsc |
	                                      ImGuiFileBrowserFlags_EditPathString);
	static ImGui::FileBrowser loadDialog(ImGuiFileBrowserFlags_CloseOnEsc |
	                                     ImGuiFileBrowserFlags_EditPathString);
	static ImGui::FileBrowser saveDialog(
	    ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_EditPathString |
	    ImGuiFileBrowserFlags_EnterNewFilename | ImGuiFileBrowserFlags_CreateNewDir);
	if (!ImGui::BeginTabBar("Sequencer details"))
		return;
	if (ImGui::BeginTabItem("Scene Cache"))
	{
		std::lock_guard<std::recursive_mutex> lock(g_SceneMutex);
		ImGui::Checkbox("Record scene history", &g_SceneRecord);
		ImGui::TextWrapped("Enable Cached preview above the timeline, then scrub its playhead. Actual demo playback stays independent. Player model bounds are proxies; live models remain visible. Dormancy and respawn boundaries are not yet classified.");
		if (ImGui::Button("Clear scene history")) { g_SceneCache.Clear(); g_SceneLastTick=-1; SceneCacheFrame(false); }
		static int budget = 64;
		if (ImGui::InputInt("Scene budget MiB", &budget)) { budget=std::clamp(budget,1,1024);g_SceneCache.SetLimit(size_t(budget)*1024*1024);SceneCacheFrame(false); }
		ImGui::Text("%zu frames; %.2f MiB payload",g_SceneCache.Count(),g_SceneCache.Bytes()/1048576.0);
		if (g_ScenePreview && !SceneCacheView()) ImGui::TextWrapped("No cached coverage at this playhead. Capture is unavailable here.");
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
		ImGui::SameLine();
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
			ImGui::TextDisabled("Seeking requires a playing demo with the indexed map and tick count.");
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

void Draw()
{
	PollTimelineIndex();
	AutoIndexRequestedDemo();
	if (!g_Open)
		return;
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
		if (!g_GraphRightCtrlChord && !ImGui::GetIO().WantTextInput)
			MirvCampathPreview_Seek();
	}
	if (!ImGui::GetIO().WantTextInput && !g_CancelGesture &&
		!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
	{
		if (ImGui::IsKeyPressed(ImGuiKey_E, false))
			PreviewCapture(ImGui::GetIO().KeyShift);
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			CancelEditGesture();
			g_Hook_VClient_RenderView.m_CamPath.DeleteSelected();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
			PreviewHold(ImGui::GetIO().KeyShift);

		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
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
		"TF2 camera overlay | F8 closes | RMB outside panels moves camera", nullptr);

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
	ImGui::Begin("TF2 Camera", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	if (ImGui::Button("Undo"))
		HistoryUndo(false);
	ImGui::SameLine();
	if (ImGui::Button("Redo"))
		HistoryUndo(true);
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
			if (ImGui::Checkbox("Preview path", &preview))
				MirvCampathPreview_SetActive(preview);

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
				if (ImGui::Checkbox("Attached camera collision", &collision.Attached))
					Afx_ExecClientCmd(collision.Attached ? "mirv_cam collision attached 1" : "mirv_cam collision attached 0");
				if (ImGui::Checkbox("Ignore local player", &collision.IgnoreLocal))
					Afx_ExecClientCmd(collision.IgnoreLocal ? "mirv_cam collision ignoreLocal 1" : "mirv_cam collision ignoreLocal 0");
				if (ImGui::Checkbox("Collide with other players", &collision.Players))
					Afx_ExecClientCmd(collision.Players ? "mirv_cam collision players 1" : "mirv_cam collision players 0");
				if (ImGui::Checkbox("Slide along obstacles", &collision.Slide))
					Afx_ExecClientCmd(collision.Slide ? "mirv_cam collision response slide" : "mirv_cam collision response stop");
				if (ImGui::InputDouble("Camera clearance (units)", &collision.Radius, 0, 0, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
				{
					char command[128];
					std::snprintf(command, sizeof(command), "mirv_cam collision radius %.9g", collision.Radius);
					Afx_ExecClientCmd(command);
				}
				if (ImGui::InputDouble("Collision return half-life (s)", &collision.ReturnHalfLife, 0, 0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
				{
					char command[128];std::snprintf(command,sizeof(command),"mirv_cam collision returnHalfLife %.9g",collision.ReturnHalfLife);Afx_ExecClientCmd(command);
				}
				if (ImGui::InputDouble("Maximum return speed (units/s)", &collision.ReturnSpeed, 0, 0, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue))
				{
					char command[128];std::snprintf(command,sizeof(command),"mirv_cam collision returnSpeed %.9g",collision.ReturnSpeed);Afx_ExecClientCmd(command);
				}
				if (ImGui::InputDouble("Attached collision soft zone (units)", &collision.AttachedSoftDistance, 0, 0, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue))
				{
					char command[128];
					std::snprintf(command, sizeof(command), "mirv_cam collision attachedSoftDistance %.9g", collision.AttachedSoftDistance);
					Afx_ExecClientCmd(command);
				}
				ImGui::TextDisabled("Campaths return gradually after blocking. Speed 0 removes the cap; half-life 0 removes damping.");
				if (ImGui::Button("Reset collision history"))
					Afx_ExecClientCmd("mirv_cam collision reset");
				ImGui::TextWrapped("TF2/TF2C only; all collision switches default off. Uses solid map/prop geometry and optional player hulls, not visible meshes or particles. The local player can be included independently. Campath correction changes framing without editing the path. Start in clear space; deeply embedded cameras may need collision disabled to escape. Clearance is a cube around the camera, not a guarantee against near-plane clipping.");
			}
			if (ImGui::CollapsingHeader("Attached camera follow"))
			{
				auto follow = MirvCameraCollisionGet();
				if (ImGui::InputDouble("Follow root half-life (s)", &follow.FollowHalfLife, 0, 0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
				{
					char command[128];std::snprintf(command,sizeof(command),"mirv_cam attach follow %.9g",follow.FollowHalfLife);Afx_ExecClientCmd(command);
				}
				if (ImGui::InputDouble("Orbit half-life (s)", &follow.OrbitHalfLife, 0, 0,
				                      "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
				{
					char command[128];
					std::snprintf(command, sizeof(command), "mirv_cam attach follow orbitHalfLife %.9g",
					              follow.OrbitHalfLife);
					Afx_ExecClientCmd(command);
				}
				if (ImGui::InputDouble("Maximum orbit turn rate (deg/s)", &follow.OrbitSpeed, 0, 0,
				                      "%.1f", ImGuiInputTextFlags_EnterReturnsTrue))
				{
					char command[128];
					std::snprintf(command, sizeof(command), "mirv_cam attach follow orbitSpeed %.9g",
					              follow.OrbitSpeed);
					Afx_ExecClientCmd(command);
				}
				if (ImGui::InputDouble("Blocked recovery cut delay (s)", &follow.FollowRecoveryDelay, 0, 0,
				                      "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
				{
					char command[128];
					std::snprintf(command, sizeof(command), "mirv_cam attach follow recoveryDelay %.9g",
					              follow.FollowRecoveryDelay);
					Afx_ExecClientCmd(command);
				}
				if (ImGui::InputDouble("Bone position half-life (s)", &follow.BoneHalfLife, 0, 0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
				{
					char command[128];
					std::snprintf(command, sizeof(command), "mirv_cam attach follow boneHalfLife %.9g", follow.BoneHalfLife);
					Afx_ExecClientCmd(command);
				}
				if (ImGui::InputDouble("Subject teleport reset (units)", &follow.FollowTeleportDistance, 0, 0, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue))
				{
					char command[128];
					std::snprintf(command, sizeof(command), "mirv_cam attach follow teleportDistance %.9g", follow.FollowTeleportDistance);
					Afx_ExecClientCmd(command);
				}
				if (ImGui::InputDouble("Maximum entity lag (units)", &follow.FollowMaxDistance, 0, 0, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue))
				{
					char command[128];std::snprintf(command,sizeof(command),"mirv_cam attach follow maxDistance %.9g",follow.FollowMaxDistance);Afx_ExecClientCmd(command);
				}
				if (ImGui::Button("Reset attached follow"))
					Afx_ExecClientCmd("mirv_cam attach follow reset");
				ImGui::TextWrapped("Follow coordinates a filtered focus, orbit and framing. Root half-life 0 restores rigid attachment; otherwise orbit half-life smooths sharp turns and composition changes. Orbit speed 0 removes the turn cap. Bone damping separately smooths animation translation. World mode retains its heading; mirv_aim retains control of its target. Teleport reset responds to large subject jumps. With follow enabled, blocked recovery cuts to a verified clear subject-side pose after the configured demo time (default 1 s); 0 disables cuts. Physical overlaps still require immediate correction.");
				ImGui::TextDisabled("Blocked recovery requires attached camera collision.");
			}

			if (ImGui::CollapsingHeader("Demo fixes"))
			{
				bool muteRecorder = MirvFix_GetRecorderSoundMute();
				if (ImGui::Checkbox("Mute recorder sounds", &muteRecorder) &&
					!MirvFix_SetRecorderSoundMute(muteRecorder))
					g_DocumentStatus = MirvFix_RecorderSoundStatus();
				ImGui::TextWrapped("TF2/TF2C POV demos only. Mutes active sounds from the recorder and owned "
					"entities, including weapons. New recorder sounds are blocked before playback. "
					"Disabling restores surviving sounds; ending playback disables the mute.");
				bool deathcam = MirvFix_GetPovDeathcam();
				if (ImGui::Checkbox("Suppress POV deathcam", &deathcam) && g_VEngineClient)
					g_VEngineClient->ClientCmd_Unrestricted(deathcam ? "mirv_fix povDeathcam 1" : "mirv_fix povDeathcam 0");
				ImGui::TextWrapped("Enable before the recorder dies. Keeps death/freeze camera effects from "
					"freezing HLAE's view or selecting the freeze-cam sound mixer. Applies only to TF2/TF2C POV demos.");
				static double interpolation = .1;
				bool active = MirvFix_GetPlayerInterpolation() >= 0;
				if (active)
					interpolation = MirvFix_GetPlayerInterpolation();
				ImGui::BeginDisabled(!active && !MirvFix_PlayerInterpolationAvailable());
				if (ImGui::Checkbox("Smooth player movement", &active) &&
					!MirvFix_SetPlayerInterpolation(active ? interpolation : -1))
					g_DocumentStatus =
						"Player interpolation is unavailable or the engine rejected its settings.";
				if (ImGui::InputDouble("Player interpolation (seconds)", &interpolation, 0, 0, "%.3f") &&
					active && !MirvFix_SetPlayerInterpolation(interpolation))
					g_DocumentStatus = "Use 0.001..1 seconds; existing interpolation settings retained.";
				ImGui::EndDisabled();
				if (ImGui::Button("Interpolation status") && g_VEngineClient)
					g_VEngineClient->ClientCmd_Unrestricted("mirv_fix playerInterpolation status");
				ImGui::TextWrapped("Uses TF2's existing movement interpolation. Adds presentation delay; it "
								   "cannot add missing snapshots or fix limb-animation glitches. Restores "
								   "settings when the demo ends. Default suggestion: 0.1 seconds.");
			}

			auto &path = g_Hook_VClient_RenderView.m_CamPath;
			DrawInterpolationSettings(path);
			ImGui::Separator();
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
			ImGui::TextWrapped("Path save folder: %s", s_campathSaveDialog.GetDirectory().u8string().c_str());
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
						path.ExportClassic(file.wstring().c_str(), 1 / g_ExportFps)
							? "Exported sampled classic XML. Editing data and savepoint retained."
							: "Classic export failed (unplayable path, excessive sample count or unwritable "
							  "file).";
				else
					SaveDocument(file.wstring().c_str(), g_SaveAll);
				g_OverlayPaths.campathDir = file.parent_path().string();
				s_campathOpenDialog.SetDirectory(file.parent_path());
				ImGui::MarkIniSettingsDirty();
				s_campathSaveDialog.ClearSelected();
			}

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

			ImGui::EndTabItem();
		}

		// Recording tab (Source 1 specific)
		if (ImGui::BeginTabItem("Recording"))
		{
			ImGui::TextUnformatted(Recording::IsActive() ? "Recording is active" : "Recording is stopped");
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
			// FPS input (interpreted differently depending on mode)
			static char s_fpsText[64] = {0};
			static bool fpsInit = false;
			if (!fpsInit)
			{
				strncpy_s(s_fpsText, "60", _TRUNCATE);
				fpsInit = true;
			}
			ImGui::SetNextItemWidth(ImGui::CalcTextSize("00000").x + st.FramePadding.x * 3.0f);
			ImGui::InputText("Record FPS", s_fpsText, sizeof(s_fpsText));

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

				// Record screen settings
				bool screenEnabled = Recording::ScreenEnabled();
				if (ImGui::Checkbox("Record screen enabled", &screenEnabled))
				{
					Afx_ExecClientCmd(screenEnabled ? "mirv_streams record screen enabled 1"
													: "mirv_streams record screen enabled 0");
				}
				ImGui::TextUnformatted("Record screen profile");
				ImGui::SameLine();
				static const char *s_screenProfile = "Select profile...";
				if (ImGui::BeginCombo("##screen_profiles", s_screenProfile))
				{
					if (ImGui::Selectable("TGA Sequence"))
					{
						s_screenProfile = "TGA Sequence";
						Afx_ExecClientCmd(R"(mirv_streams record screen settings afxClassic)");
					}
					if (ImGui::Selectable("FFmpeg YUV420p"))
					{
						s_screenProfile = "FFMPEG YUV420p";
						Afx_ExecClientCmd(R"(mirv_streams record screen settings afxFfmpegYuv420p)");
					}
					if (ImGui::Selectable("FFmpeg Lossless Best"))
					{
						s_screenProfile = "FFMPEG Lossless Best";
						Afx_ExecClientCmd(R"(mirv_streams record screen settings afxFfmpegLosslessBest)");
					}
					if (ImGui::Selectable("ProRes 4444"))
					{
						s_screenProfile = "ProRes 4444";
						Afx_ExecClientCmd(
							R"(mirv_streams settings add ffmpeg afxPaperProRes4444 "-c:v prores_ks -profile:v 4 -pix_fmt yuva444p10le {QUOTE}{AFX_STREAM_PATH}\video.mov{QUOTE}")");
						Afx_ExecClientCmd("mirv_streams record screen settings afxPaperProRes4444");
					}
					if (ImGui::Selectable("ProRes 422 HQ"))
					{
						s_screenProfile = "ProRes 422 HQ";
						Afx_ExecClientCmd(
							R"(mirv_streams settings add ffmpeg afxPaperProResHQ "-c:v prores_ks -profile:v 3 -pix_fmt yuv422p10le {QUOTE}{AFX_STREAM_PATH}\video.mov{QUOTE}")");
						Afx_ExecClientCmd("mirv_streams record screen settings afxPaperProResHQ");
					}
					if (ImGui::Selectable("ProRes 422"))
					{
						s_screenProfile = "ProRes 422";
						Afx_ExecClientCmd(
							R"(mirv_streams settings add ffmpeg afxPaperProRes422 "-c:v prores_ks -profile:v 2 -pix_fmt yuv422p10le {QUOTE}{AFX_STREAM_PATH}\video.mov{QUOTE}")");
						Afx_ExecClientCmd("mirv_streams record screen settings afxPaperProRes422");
					}
					if (ImGui::Selectable("x264 Lossless"))
					{
						s_screenProfile = "x264 Lossless";
						Afx_ExecClientCmd(
							R"(mirv_streams settings add ffmpeg afxPaperX264Lossless "-c:v libx264 -preset ultrafast -qp 0 -pix_fmt yuv444p {QUOTE}{AFX_STREAM_PATH}\video.mp4{QUOTE}")");
						Afx_ExecClientCmd("mirv_streams record screen settings afxPaperX264Lossless");
					}
					if (ImGui::Selectable("x264 HQ"))
					{
						s_screenProfile = "x264 HQ";
						Afx_ExecClientCmd(
							R"(mirv_streams settings add ffmpeg afxPaperX264HQ "-c:v libx264 -preset slow -crf 16 -pix_fmt yuv420p {QUOTE}{AFX_STREAM_PATH}\video.mp4{QUOTE}")");
						Afx_ExecClientCmd("mirv_streams record screen settings afxPaperX264HQ");
					}
					if (ImGui::Selectable("x265 Lossless"))
					{
						s_screenProfile = "x265 Lossless";
						Afx_ExecClientCmd(
							R"(mirv_streams settings add ffmpeg afxPaperX265Lossless "-c:v libx265 -preset ultrafast -x265-params lossless=1 -pix_fmt yuv444p {QUOTE}{AFX_STREAM_PATH}\video.mp4{QUOTE}")");
						Afx_ExecClientCmd("mirv_streams record screen settings afxPaperX265Lossless");
					}
					if (ImGui::Selectable("x265 HQ"))
					{
						s_screenProfile = "x265 HQ";
						Afx_ExecClientCmd(
							R"(mirv_streams settings add ffmpeg afxPaperX265HQ "-c:v libx265 -preset medium -crf 18 -pix_fmt yuv420p {QUOTE}{AFX_STREAM_PATH}\video.mp4{QUOTE}")");
						Afx_ExecClientCmd("mirv_streams record screen settings afxPaperX265HQ");
					}
					ImGui::EndCombo();
				}

				ImGui::Separator();
				if (ImGui::Button("Start Recording"))
				{
					// host_framerate X; mirv_streams record fps X; mirv_streams record screen enabled 1;
					// mirv_streams record start
					float fps = (float)atof(s_fpsText);
					if (!std::isfinite(fps) || fps <= 0.0f || fps > 100000.0f)
						fps = 60.0f;
					char cmd[512];
					sprintf_s(cmd,
							  "host_framerate %.0f; mirv_streams record fps %.0f; mirv_streams record screen "
							  "enabled 1; mirv_streams record start",
							  fps, fps);
					AfxHookSource::Gui::SetOverlayOpen(false);
					Afx_ExecClientCmd(cmd);
				}
				ImGui::SameLine();
				if (ImGui::Button("Stop"))
				{
					Afx_ExecClientCmd("mirv_streams record end; host_framerate 0");
				}
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
				g_ScenePreview = cached;
				g_SequencerNeedsRefresh = true;
				if (cached && !g_SceneCache.Frames().empty())
					s_seqFrame = int(std::floor(g_SceneCache.Frames().back().DemoTime/SceneTickInterval()));
				ScenePreviewTick(s_seqFrame);
			}
			ImGui::SameLine();
			if (ImGui::Button("Seek actual demo here")) Afx_GotoDemoTick(s_seqFrame);
			int curDemoTick = 0;
			const bool haveDemoTick = GetCurrentDemoTick(curDemoTick);
			int demoTotalTicks = 0;
			if (g_VEngineClient)
				if (auto *demo = g_VEngineClient->GetDemoInfoEx())
					demoTotalTicks = demo->GetDemoPlaybackTotalTicks();
			ImGui::FrameIndexType prevFrame = s_seqFrame;

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
						if (auto view = SceneCacheView())
						{ curTime = view->ClientTime; demoNow = view->DemoTime; haveDemoNow = true; }

				auto tick_from_client = [&](double clientTime) -> int {
					int t = 0;
						if (SceneCachePreviewActive() && haveDemoNow)
							return (std::max)(0,int(llround((clientTime-curTime+demoNow)/ipt)));
					if (!GetDemoTickFromClientTime(curTime, clientTime, t))
						t = (int)llround(clientTime / ipt);
					return t < 0 ? 0 : t;
				};
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

			// Layout sizes similar to DX11
			ImGuiStyle &st = ImGui::GetStyle();
			float availY = ImGui::GetContentRegionAvail().y;
			float btnRowH = ImGui::GetFrameHeight();
			const ImGuiNeoSequencerStyle &neo = ImGui::GetNeoSequencerStyle();
			float topBarH = neo.TopBarHeight > 0.0f
								? neo.TopBarHeight
								: (ImGui::CalcTextSize("100").y + st.FramePadding.y * 2.0f);
			float zoomH = ImGui::GetFontSize() * neo.ZoomHeightScale + st.FramePadding.y * 2.0f;
			float rowLabelH =
				ImGui::CalcTextSize("Campath").y + st.FramePadding.y * 2.0f + neo.ItemSpacing.y * 2.0f;
			const float rows = (float)(g_Hook_VClient_RenderView.m_CamPaths.GetCount() + 5);
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
					ImGui::GetNeoSequencerView(&visibleStart, &visibleEnd);
					if (g_CancelGesture)
						ImGui::NeoCancelSelection();
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
						if (auto view = SceneCacheView())
						{ curTime = view->ClientTime; demoNow = view->DemoTime; haveDemoNow = true; }

					auto tick_from_client = [&](double clientTime) -> int {
						int t = 0;
						if (SceneCachePreviewActive() && haveDemoNow)
							return (std::max)(0,int(llround((clientTime-curTime+demoNow)/ipt)));
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

					bool open = true;
					ImGui::PushID((int)document.GetId(document.GetActive()));
					std::string editingLabel = document.GetName(document.GetActive()) + " (editing)";
					if (ImGui::BeginNeoTimelineEx(editingLabel.c_str(), &open, ImGuiNeoTimelineFlags_None))
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
									g_LastCampathCtx = s_ctxMenuPending;
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
									s_cache.times[i] +
									(double(s_cache.ticks[i]) - s_cache.originalTicks[i]) * ipt;
								newKeys.push_back({newRelTime, (int)i});
							}
							// Validate all arrivals before replacing the native map.
							std::sort(newKeys.begin(), newKeys.end(),
									  [](const TmpKey &a, const TmpKey &b) { return a.t < b.t; });

							std::vector<double> arrivals;
							for (size_t i = 0; i < s_cache.ticks.size(); ++i)
								arrivals.push_back(s_cache.times[i] + (double(s_cache.ticks[i]) - s_cache.originalTicks[i]) * ipt);
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
						if (index != document.GetActive())
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
					if (ImGui::BeginNeoTimelineEx("Playback / cuts", nullptr, ImGuiNeoTimelineFlags_None))
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
					DrawSceneCoverage();
					DrawSequencerExtraTracks(visibleStart, visibleEnd);
					DrawPlayerRepairTrack(visibleStart, visibleEnd);
					DrawSequencerSeekContext();
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
			ImGui::TextDisabled("Retiming preserves editable curve shape: Auto tangents become Free; Classic scalar curves convert when using editable Euler.");
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
			if (!SceneCachePreviewActive() && ImGui::SmallButton("Seek to preview"))
				MirvCampathPreview_Seek();
			ImGui::SameLine();
			if (ImGui::SmallButton("Insert keyframe at preview"))
			{
				auto cachedView = SceneCacheView();
				const bool cachedMode = SceneCachePreviewActive();
				const double insertTime = cachedMode && cachedView
					? cachedView->ClientTime - g_Hook_VClient_RenderView.m_CamPath.GetOffset()
					: MirvCampathPreview_GetTime();
				if ((cachedMode && !cachedView) || !g_Hook_VClient_RenderView.m_CamPath.Insert(insertTime))
					g_DocumentStatus = "Insert requires an unoccupied interior time and Custom or Linear "
									   "channels. The path was retained.";
			}

			ImGui::SameLine();
			if (ImGui::SmallButton(g_ShowCurveEditor ? "Hide Curve Editor" : "Show Curve Editor"))
			{
				g_ShowCurveEditor = !g_ShowCurveEditor;
				if (g_ShowCurveEditor)
					g_FocusGraph = true;
			}
			// Standalone Curve Editor (outside Neo timeline)
			// A view onto the native preview cursor; the controller owns the camera.
			if (g_Hook_VClient_RenderView.m_CamPath.GetSize())
			{
				auto &path = g_Hook_VClient_RenderView.m_CamPath;
				ImGui::Separator();
				bool active = MirvCampathPreview_IsActive();
				if (ImGui::Checkbox("Preview path", &active))
					MirvCampathPreview_SetActive(active);
				const double begin = path.GetLowerBound(), end = path.GetUpperBound();
				float position =
					end > begin ? (float)((MirvCampathPreview_GetTime() - begin) / (end - begin)) : 0;
				position = std::clamp(position, 0.0f, 1.0f);
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
				ImGui::BeginDisabled(!path.CanEval() || end <= begin);
				if (!SceneCachePreviewActive() && ImGui::SliderFloat("##previewcampath", &position, 0, 1, "%.3f"))
					MirvCampathPreview_SetTime(begin + position * (end - begin));
				ImGui::EndDisabled();
				ImGui::TextDisabled("Camera preview: %.3f s | Demo time stays independent",
									MirvCampathPreview_GetTime());
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
					if (s_seqFrame != s_dragStartFrame && !SceneCachePreviewActive())
						Afx_GotoDemoTick((int)s_seqFrame);
					s_draggingPointer = false;
				}
			}
			else if (haveDemoTick && !SceneCachePreviewActive())
			{
				s_seqFrame = std::clamp(curDemoTick, int(s_seqStart), int(s_seqEnd));
			}

			if (SceneCachePreviewActive())
			{
				ScenePreviewTick(s_seqFrame);
				ImGui::TextDisabled("Cached tick %d | Actual demo tick %d",int(s_seqFrame),curDemoTick);
				if (!SceneCacheView()) ImGui::TextDisabled("No cached coverage at this tick.");
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
			bool camEnabled = pMirv->GetCameraControlMode();
			ImGui::Text("Mirv Camera (C): %s", camEnabled ? "enabled" : "disabled");
			ImGui::SameLine();
			if (ImGui::Button(camEnabled ? "Disable" : "Enable"))
			{
				pMirv->SetCameraControlMode(!camEnabled);
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
					bool camEnabled = pMirv->GetCameraControlMode();
					pMirv->SetCameraControlMode(!camEnabled);
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
