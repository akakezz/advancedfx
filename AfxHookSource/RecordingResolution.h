#pragma once
#include <string>
namespace RecordingResolution
{
struct State
{
    int RenderWidth = 0, RenderHeight = 0, CaptureWidth = 0, CaptureHeight = 0;
    int PreviewWidth = 0, PreviewHeight = 0, MaxWidth = 0, MaxHeight = 0;
    bool Supported = false, Windowed = false, Pending = false, Managed = false, Fit = false, Ready = false;
    std::string Message;
};
State GetState();
bool CanStart();
void Frame();
void Publish(void *window, bool windowed, int backWidth, int backHeight,
             int captureWidth, int captureHeight, int maxWidth, int maxHeight);
void Invalidate();
bool FitPresentation(void *window);
bool SuppressSizeMessage(void *window);
}
