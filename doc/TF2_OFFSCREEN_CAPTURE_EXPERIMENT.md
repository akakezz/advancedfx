# Experimental TF2 independent high-resolution base capture

This is a narrow test, separate from mirv_resolution and its windowed video
mode registration. It does not issue mat_setvideomode, resize the window,
alter cursor coordinates or upscale the captured native screen image.

## Test workflow

Start TF2 at the normal resolution with no managed custom video mode active.
Use a disposable demo segment. Stop any existing recording. Use immediate
material rendering (mat_queue_mode 0); the actual material call queue must
be null. Configure base/world as the sole separate output, with reference off:

    mat_queue_mode 0
    mat_postprocess_enable 0
    mirv_streams record pass reset
    mirv_streams record pass base 1
    mirv_streams record pass offscreen 3840 2160

The example assumes 1920x1080 game resolution. Other larger sizes at the
same aspect ratio are allowed up to 8192 per dimension and GPU caps. Do
not use Apply resolution from the Recording tab for this experiment.
Choose the usual profile/FPS/path, resume the demo, then start recording
as usual. Stop after a short sample. Check:

    mirv_streams record pass stats
    mirv_streams record pass offscreen

Inspect the base output dimensions and actual visual detail. A successful
GPU readback is reported only after observing successful geometry draw
calls with the high-resolution viewport and color/depth pair; this is
a necessary check, not proof of correct scene contents. The recording
path skips rejected captures rather than falling back to a low-res surface.
To disable the experiment (recording stopped):

    mirv_streams record pass offscreen off

Restore your original mat_queue_mode and mat_postprocess_enable values.
The setting is session-local and deliberately not a permanent Recording
tab control until Windows behavior is established.

## Implementation and limitations

The normal frame is rendered first. The existing TF2 pass adapter copies
GetPlayerView into aligned storage and validates the public SDK 2013 view
prefix and projection. It changes only the copied viewport, aspect and
bloom flag. Allocate a separate non-MSAA color/depth pair and link it to
the existing tracked render target and depth surface for the additional
world render. Restore the original bindings, viewport and scissor without
copying high-res pixels back to the game window. The capture node is
created through the existing base encoder path, and reads the high-res
surface at SceneEnd before later screen-space processing. Release resources
on capture teardown or device loss.

The test does not support HUD, viewmodel, isolated players, depth output,
full-scene reference, nonmatching aspect, orthographic/VR/subrect views,
managed custom window mode, or queued rendering. It does not resize
Source's frame-buffer-dependent intermediate textures. Refraction, water,
HDR, sky effects, effects that read screen textures, and engine view-state
side effects are not guaranteed correct. Capture at SceneEnd avoids many
later effects, but RenderView itself still runs to completion. No promise
of Windows compatibility or absence of demo/render regressions is made.
Portable tests exercise the copied view layout and validation, not the
native renderer. Disable the option if the output is wrong.
