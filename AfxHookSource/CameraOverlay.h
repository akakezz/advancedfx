#pragma once
#include <shared/SceneCache.h>
#include <shared/SceneEffects.h>
#include <shared/NativeParticleSnapshot.h>
#include <shared/LegacyParticleSnapshot.h>
#include <memory>
#include <map>
#include <functional>
#include <string>
#include <vector>
#include <shared/RecordingProfileInfo.h>
namespace advancedfx { class ICommandArgs; }
// Native engine-thread position snapshot, exact TF2 x64 build only.
advancedfx::NativeParticles::Frame Mirv_Tf2_CaptureNativeParticles(int tick, bool appearance=false);
std::string Mirv_Tf2_InspectLegacyParticles(advancedfx::LegacyParticles::Frame *snapshot=nullptr);
std::string Mirv_Tf2_ProbeDetachedParticles(const advancedfx::NativeParticles::Frame &frame, std::size_t collection);
advancedfx::NativeParticles::ReplayResult Mirv_Tf2_RenderNativeParticles(const advancedfx::NativeParticles::Frame &frame);
// Engine-thread preflight and registered TF2 particle visibility cvar access.
bool Mirv_Tf2_ParticleIsolationReady();
bool Mirv_Tf2_LiveParticleVisibility(float &value, bool write);
bool Mirv_Tf2_SceneDrawVisibility(const char *name,float &value,bool write);
advancedfx::NativeParticles::ReplayResult Mirv_Tf2_RenderLegacyParticles(const advancedfx::LegacyParticles::Frame &frame);
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
	NoStaticPropFade,
	PlayerInterpolation
};
void RememberDemoFixPreference(DemoFixPreference setting, double value);
void PreferencesFrame(); // Engine FRAME_START, including when the overlay is closed.
void Draw();
void SceneCacheFrame(bool capture = true); // Capture at TF2 FRAME_RENDER_END; false only updates preview.
void SceneCacheDrawModels();
void SceneCacheDrawParticles();
bool SceneCacheSuppressLiveParticles();
void SceneCachePublishParticleIsolation(const char *status);
unsigned SceneCacheLiveSurfaceIsolation(); // bit 1 shadows, bit 2 decals
void SceneCachePublishSurfaceIsolation(unsigned index,const char *status);
// Synchronous native particle tool message capture; payload is copied immediately.
// Return a message kind once per capture to request a lightweight client stack audit.
int SceneCacheParticleMessage(const char *name, const void *state);
void SceneCacheParticleCallsites(int kind, std::vector<std::uintptr_t> rvas);
// Experimental replacement of registered live studio-model submissions only.
bool SceneCacheReplayActive();
bool SceneCacheSuppressLiveStudio();
// Same-engine-frame observation only; no live pointer is kept in history.
bool SceneCacheAuditActive();
void SceneCachePublishModelAudit(int frame, std::map<const void *, int> submissions);
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
// Thread-safe editor visibility and selected clip for in-world transition guides.
bool TransitionGuidesEnabled();
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
