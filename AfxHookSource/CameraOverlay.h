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
void Draw();
void SceneCacheFrame(bool capture = true);
void SceneCacheDrawModels();
void SceneCacheConsole(advancedfx::ICommandArgs *args);
bool SceneCacheNavigationKey(unsigned int key, bool down);
void SceneCacheStopScrub();
void SceneCacheFollowCameraTime(double clientTime);
bool SceneCachePreviewActive();
std::shared_ptr<const advancedfx::SceneCache::Frame> SceneCacheView();
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
