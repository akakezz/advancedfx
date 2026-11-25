#pragma once
#include <shared/SceneCache.h>
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <shared/RecordingProfileInfo.h>
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
// Called once at game-mode detection, before any capture or UI actions.
void SetOfSceneDefaults(bool integrated);
enum class DemoFixPreference
{
	RecorderMute,
	Deathcam,
	NoStaticPropFade,
	PlayerInterpolation
};
void RememberDemoFixPreference(DemoFixPreference setting, double value);
// Saved OF engine-override preference. An enabled setting is applied only when
// a compatible OF demo is playing, never while disconnected or in multiplayer.
void RememberOfEngineFixPreference(bool enabled, double seconds, bool allEntityIntervals);
void PreferencesFrame(); // Engine FRAME_START, including when the overlay is closed.
void Draw();
void ProcessExternalActions(); // After ImGui::Render, never from inside the widget callback.
void SceneCacheFrame(bool capture = true); // Capture at TF2 FRAME_RENDER_END; false only updates preview.
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
// Borrow frames only during the callback; no entity/bone arrays are copied.
void VisitCameraSceneFrames(
	const std::function<void(const std::deque<advancedfx::SceneCache::Frame> &, std::uint64_t)> &visitor);
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
// Thread-safe selected clip for optional in-world transition trajectory guides.
std::uint64_t TransitionGuideClip();
bool PreviewInsert();
bool PreviewHold(bool clear);
void PreviewSelectAll();
bool PreviewAnchor();
namespace Recording
{
bool PassAlpha();
bool SetPassAlpha(bool value);
unsigned PassBackground();
bool SetPassBackground(unsigned color);
bool SeparatePassSupported();
bool SeparateConfigured();
bool FullReference();
bool SetFullReference(bool enabled);
bool ResetSeparateOutputs();
bool PassEnabled(unsigned pass);
bool SetPassEnabled(unsigned pass, bool enabled);
const char *GetName();
void SetName(const char *value);
bool ScreenEnabled();
bool GpuCaptureEnabled();
bool SetGpuCaptureEnabled(bool enabled);
bool IsActive();
bool Toggle();
bool HandleHotkey(unsigned int key);
bool CaptureHotkey(unsigned int key);
std::vector<std::string> GetProfiles();
std::string CurrentProfile();
bool SetProfile(const char *name);
bool GetProfileInfo(const char *name, advancedfx::RecordingProfileInfo &out);
bool DuplicateProfile(const char *source, const char *name);
bool ApplyProfileEdits(const advancedfx::RecordingProfileInfo &info);
bool RemoveProfile(const char *name);
} // namespace Recording

} // namespace CameraOverlay
} // namespace AfxHookSource
