#pragma once
#include <shared/SceneCache.h>
#include <memory>
namespace advancedfx { class ICommandArgs; }
namespace AfxHookSource
{
namespace CameraOverlay
{
bool IsOpen();
void SetOpen(bool value);
bool IsMouseLook();
void SetMouseLook(bool value);
void Initialize();
enum class DemoFixPreference
{
	RecorderMute,
	Deathcam,
	PlayerInterpolation
};
void RememberDemoFixPreference(DemoFixPreference setting, double value);
void PreferencesFrame(); // Engine FRAME_START, including when the overlay is closed.
void Draw();
void SceneCacheFrame(bool capture = true);
void SceneCacheDrawModels();
std::shared_ptr<const std::vector<advancedfx::SceneCache::Line>> SceneCacheLines();
void SceneCacheFocus(bool focused);
void SceneCacheDevicePresent(bool success);
void SceneCacheConsole(advancedfx::ICommandArgs *args);
bool SceneCacheNavigationKey(unsigned int key, bool down);
void SceneCacheStopScrub();
bool SceneCacheScrubKey(unsigned int key, double seconds);
void SceneCacheFollowCameraTime(double clientTime);
bool SceneCachePreviewActive();
std::shared_ptr<const advancedfx::SceneCache::Frame> SceneCacheView();
void ResetProjectRecovery();
bool SampleCameraScene(double clientTime, advancedfx::SceneCache::Frame &frame);
double EditingClientTime(double liveTime);
// Called on the engine thread for playdemo/timedemo; indexing starts outside the command callback.
void NotifyDemoPlaybackFile(const char *file);
// Shared by the overlay and native mirv_campath / preview commands.
void BeginEdit();
void FinishEdit();
void UpdateHistory();
void HistoryUndo(bool redo);
bool SaveDocument(const wchar_t *fileName, bool all);
bool LoadDocument(const wchar_t *fileName, bool all);
void FlushRecovery();
bool PreviewCapture(bool replace);
bool PreviewHudVisible();
void ResetPreviewHudFrame();
void SetPreviewHud(bool value);
bool GetPreviewHud();
void SetCameraGuides(bool enabled);
void SetCameraFrustums(bool enabled);
bool CameraGuidesEnabled();
bool CameraFrustumsEnabled();
bool PreviewInsert();
bool PreviewHold(bool clear);
void PreviewSelectAll();
bool PreviewAnchor();
namespace Recording
{
const char *GetName();
void SetName(const char *value);
bool ScreenEnabled();
bool IsActive();
} // namespace Recording

} // namespace CameraOverlay
} // namespace AfxHookSource
