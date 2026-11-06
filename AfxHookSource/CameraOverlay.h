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
void FinishEdit();
namespace Recording
{
const char *GetName();
void SetName(const char *value);
bool ScreenEnabled();
bool IsActive();
} // namespace Recording

} // namespace CameraOverlay
} // namespace AfxHookSource
