#pragma once
#include <string>
namespace RecordingResolution
{
struct State
{
    int RenderWidth = 0, RenderHeight = 0, CaptureWidth = 0, CaptureHeight = 0;
    int PreviewWidth = 0, PreviewHeight = 0, MaxWidth = 0, MaxHeight = 0;
    bool Supported = false, Windowed = false, Pending = false, Managed = false, Fit = false, Ready = false;
    bool NativePreview = false;
    std::string Message;
};
State GetState();
bool CanStart();
void Frame();
void Publish(void *window, bool windowed, int backWidth, int backHeight,
             int captureWidth, int captureHeight, int maxWidth, int maxHeight);
void Invalidate();
void PublishPreviewPath(bool native);
bool FitPresentation(void *window);
bool SuppressSizeMessage(void *window);
bool MapCursor(int &x, int &y, bool toRender, bool screen, void *window = nullptr);
bool MapCursorClip(int &left, int &top, int &right, int &bottom);
}
