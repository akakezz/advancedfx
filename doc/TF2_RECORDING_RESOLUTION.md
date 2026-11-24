# Native recording resolution and fitted preview (TF2)

The Recording tab can request arbitrary engine-native windowed video modes
within the supported size range and current GPU texture limits. The feature
is opt-in and session-local. It does not rescale captured video.

## Workflow

Start TF2 in windowed mode, load a demo, and stop recording. Under
Recording > Recording resolution choose a preset or enter width and height.
Leave Fit preview to monitor enabled and click Apply resolution. Wait for
the status to confirm the resolution. The native render and capture surface
must both report the requested dimensions. The preview is fitted inside the
work area of the monitor that contained the original game window; window
borders and aspect ratio are accounted for. Start recording only when ready.
Restore original resolution restores the initial mode and attempts to restore
the initial window rectangle. Resolution changes are disabled during capture.

Console equivalents:

    mirv_resolution apply 3840 2160 1
    mirv_resolution status
    mirv_resolution restore

The final argument is 1 for fitted preview or 0 for a native-size window.
If the resolution is rejected or the preview fails to fit, stop recording,
restore the original resolution, or retry without fit. Use the console if
the enlarged window extends beyond the monitor. Fullscreen is not supported.
The GPU maximum is not a guarantee of available memory or encoder support.
Choose an FFmpeg profile compatible with the output dimensions; the
capture dimensions may exceed hardware encoder limits.

## Implementation

Inspired by the video-mode registration technique documented and demonstrated
by jooonior/SourceRes (https://github.com/jooonior/SourceRes). The
implementation here is independent and uses the public Source SDK 2013
IGameUIFuncs005 GetVideoModes and vmode_s layout. The actual array capacity
is not exposed by the interface; current TF2 returned non-mode data after
the reported list. No entries outside the reported count are accessed.
Instead, an existing non-current, non-original video mode (never index
zero) is temporarily replaced in place. The entry's original dimensions,
bits and refresh rate are saved. The engine's count and other entries
are unchanged. The list is re-queried to verify the substitution. Restore
the original resolution before selecting a different custom size.
After the original resolution is confirmed, the saved entry is restored
only if the pointer, count and custom value still match. If the list was
rebuilt or changed, restart TF2 rather than overwriting unknown memory.
No SourceRes plugin or source file is bundled.

The engine applies mat_setvideomode in windowed mode. HLAE waits for a
successful Direct3D present and exact engine/backbuffer/capture dimensions
before enabling recording. Fit mode changes only the physical client
window, suppressing the synchronous HLAE-owned WM_SIZE notification.
Present/PresentEx use the full backbuffer and client area, relying on
Direct3D's windowed presentation scaling. Neither the capture node nor
the recording encoder receives the preview-sized surface. The existing
overlay renderer maps the logical preview viewport/scissors to the actual
backbuffer, so UI geometry remains usable. A second successful present
must confirm the requested native dimensions after fitting.

If the engine shrinks its native mode after fitting, recording stays
blocked instead of silently accepting the smaller capture. Mode changes
through this feature are disallowed during recording, and a detected
unexpected dimension change requests a recording stop. GPU reset, device
loss, window recreation, DPI handling, and actual pass output dimensions
must be tested on Windows. Portable tests cover mode insertion, bounds,
aspect fitting and exact surface matching; headless overlay tests compile
the controls. This is the initial integration, not a verified Windows
capture result.

## Ultrawide fitting and native preview UI

The preview fit validates the actual outer window against the original monitor's work area and checks the client aspect ratio to within integer pixel rounding. A bounded retry handles subsequent engine-driven window resizing; use `mirv_resolution refit` (or Refit preview in the Recording tab) to retry without changing the engine mode. If the exact render/capture mode was not accepted, refit is refused.

When the overlay is visible, the fitted preview attempts to use a preview-sized additional D3D9 swap chain. The native game backbuffer is copied/downsampled to it, resolving MSAA if necessary. ImGui renders onto that preview surface in preview pixel coordinates, then the preview is presented. The original render target, depth, viewport and scissor are restored. The capture pipeline continues using the native game surfaces. Unsupported drivers/flags or a failed copy use the original scaled presentation path. Resources are released on reset, shutdown or when fit mode ends. The UI/status distinguishes preview-resolution compositing from the fallback. Live Windows support and output correctness still require testing.

## Native Source UI cursor mapping

While a fitted preview is active and the game is foreground, Source's visible
cursor/menu state is sampled on the engine thread. Existing module-local
cursor hooks translate absolute physical preview coordinates into native
render coordinates and inverse-map cursor warps. Absolute window messages
are translated only after HLAE processes them; relative/raw input is untouched.
Extended render-space clip rectangles are mapped while already-physical ones
are left alone. HLAE's overlay retains physical coordinates. Disable mapping
with `mirv_resolution cursor 0` or enable with `mirv_resolution cursor 1`.
Status reports the setting and cached Source UI state. No Windows VGUI
behavior is verified by the portable coordinate tests.

When a fitted Source UI owns the pointer, freecam mouse/button processing
is suspended independently of the optional freecam UI-cursor preference.
This avoids consuming native button messages or rewriting mouse-move button
masks before Source receives them. HLAE's overlay retains first priority.

Source's queued absolute mouse events can originate before the hooked
window procedure. When the existing InputSystemVersion001 event hooks are
available, keep WM_MOUSEMOVE physical and transform the copied event queue
(MOUSE_X, MOUSE_Y and MOUSE_XY), rather than scaling the same event twice.
Preserve all button events, counts and ordering, and the existing overlay
capture filter. Polling/warps remain mapped by the cursor hooks. The status
command reports whether the queue hooks installed. Live click behavior is
not yet confirmed.
