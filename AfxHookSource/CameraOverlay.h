#pragma once
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
