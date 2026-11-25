#pragma once
namespace advancedfx { struct GpuCaptureSettings; }
class ICaptureOutput;
// Owned and used by the drawing thread. Failure after activation invalidates
// the take rather than silently switching sampler timebases mid-recording.
class GpuScreenCapture {
public:
    virtual ~GpuScreenCapture() = default;
    virtual void Capture() = 0;
    virtual void Finish() = 0;
};
GpuScreenCapture *CreateGpuScreenCapture(const advancedfx::GpuCaptureSettings &, ICaptureOutput *);
