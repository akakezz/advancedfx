# TF2 camera overlay — Paper port

This feature is based on Paper's overlay and the original HLAE base commit
`3906a61e` (upstream HLAE 2.192.10, `8abc2719`). It uses ordinary `mirv_campath` keys and the classic path renderer.
There is no separate cinematic system or per-segment travel-blending layer.

## Build and open

Update `feature/cinematic-campath`, initialize the existing Git submodules as
usual, and follow [BUILDING.md](../BUILDING.md). CMake downloads the pinned ImGui
release automatically; no patch or manual copy from Paper's repository is needed.

In TF2, close the console and press **F8**, or run:

```text
mirv_overlay
mirv_overlay 1
mirv_overlay 0
```

F8 toggles the whole workspace. Source menus, the console and Shift+F2 demo UI
retain priority. Enable the free camera in Camera Control, then hold **right mouse
outside panels** to move it with ordinary free-camera controls. Releasing right
mouse restores the pointer. Entering or leaving UI ownership clears pending camera
movement. Panels can be moved and docked inside the game window.

## Capture and playback

1. Move the camera and advance the demo to the desired time.
2. Click **Add point** in Campath. This captures position, angles, FOV and time.
3. Repeat. A new overlay-created path uses **Custom**, which works with two keys.
4. Enable **Campath enabled** and release the free camera to let native playback
   follow demo time. Use `mirv_input end` when necessary.

Custom curves use tangents across the complete path, rather than restarting an
easing function at every key. Ordinary keys therefore do not force a stop.
Positions, timestamps, Flat tangents and deliberate direction changes still affect
speed. It does not promise constant speed between arbitrarily timed positions.

The interpolation selector also exposes native Linear and Classic cubic. Classic
cubic keeps its four-key requirement. Loading an existing file retains that file's
interpolation. Classic console capture with `mirv_campath add` remains available.

## Sequencer and curve editor

Enable **Show Sequencer**. The timeline spans the entire demo, including future
ticks, and displays its current visible range. Scroll the wheel to zoom around the
pointer. Middle-drag or Shift+wheel to move through the timeline, or drag its
overview bar. **Fit demo** restores the full range. Navigation does not retime keys.
The sequencer supports selecting and dragging keys and exposes a right-click menu:

- **Remove** deletes the key.
- **Get** moves the free camera to the key's pose.
- **Set** captures the current pose at its timestamp, preserving tangent metadata.
- **Edit** selects the key for the viewport gizmo.

Use the pause/speed buttons or scrub the timeline to navigate the demo. The preview
slider evaluates the path without seeking; Follow Preview keeps the resulting
camera pose. **Add keyframe at preview** inserts an evaluated camera pose.

Enable **Show Curve Editor** to display X/Y/Z, FOV, pitch/yaw/roll individually or
in combination; X is visible initially. Normalization makes channels with different
units readable. Wheel zooms time around the pointer; Ctrl+wheel zooms values.
Middle-drag pans both axes. **Fit graph** resets the view. The sequencer window
can be resized vertically; zoom and pan no longer require sliders.
Right-click a key for Auto, Flat, Linear or Free tangent modes, applied to the
incoming side, outgoing side or both.

- Drag a curve handle to edit the shared slope through that key.
- Hold **Shift** while dragging to change its weight/influence.
- Hold **Alt** to edit a side independently for an intentional corner.
- **Undo / Redo** or **Ctrl+Z / Ctrl+Shift+Z** restore data edits while the overlay
  owns input, and in native `mirv_campath preview` mode with the overlay closed.
  Console edits enter the same history, starting with the first edit. A drag is
  one edit. History retains 64 entries. Console equivalents are
  `mirv_campath undo|redo` and `mirv_campath preview undo|redo`.

Weighted curves solve their time coordinate during playback, so the graph and the
camera use the same curve. Euler channels unwrap adjacent angles to avoid ordinary
360-degree discontinuities. Holds at the path's boundaries use native
`mirv_campath hold`; the previous experimental per-key hold system is not included.

Console equivalents remain under the native command tree, for example:

```text
mirv_campath edit interp position custom
mirv_campath edit interp rotation custom
mirv_campath edit interp fov custom
mirv_campath select #1 #1
mirv_campath edit tangentmode x both auto
mirv_campath edit tangent x both 20
mirv_campath edit tangentweight x both 1
```

Tangent commands accept `x|y|z|fov|pitch|yaw|roll` and `in|out|both`, applying to the
selection or to all keys when none is selected.

## Gizmos and observing

Choose **Edit** on a timeline key, then enable **Show Gizmo**. **G** selects
translation and **R** rotation when a text field is not active. Local/world modes
and Ctrl snapping are available. **C** toggles free-camera control. Camera Control
also provides FOV, roll and keyboard speed sliders.

The **Observing** tab lists TF2 players with a text filter. **View** spectates a
player; **Attach** uses the existing TF2 attachment system. Click a player name
or **Bones** to select that player, then choose a named model bone from the
**Bone** dropdown (or **Entity origin**). **Attach selected** applies that target,
bone and forward/left/up offsets. Fly to a composition and use **Capture relative
framing** to follow the target from that composition. **Detach** clears the attachment.

**Trace entity** selects the entity under the camera crosshair, including non-player
entities. Aim the camera before opening the menu. Walls and static map geometry
block the trace. The panel reports hits, misses and unavailable engine hooks.
This uses TF2's `EngineTraceClient003`; the original console command
`mirv_traceentity` is CS:GO-specific in this HLAE base.

## Recording, files and settings

The Recording tab uses TF2 screen recording through `mirv_streams`. Choose a
record directory, FPS and screen profile. TGA, HLAE's built-in FFmpeg profiles,
ProRes and x264/x265 profiles are available. Start Recording closes the overlay;
reopen it with F8 to stop. Recording uses the chosen screen profile directly.
FFmpeg profiles require HLAE's normal FFmpeg installation.

Open/save dialogs use native `.xml` camera paths. Save appends `.xml` when no
extension is provided. Custom channel tangents and weights round-trip in the same
XML. Failed loads preserve the current path. Earlier experimental multi-path
collections and segment-easing extensions are not part of this fresh implementation;
keep those source files separately when reviewing this version.

Settings include a demo browser, UI scale, fonts and docking. Availability of the
extra fonts depends on Windows. UI preferences, panel layout and last-used
folders are stored in HLAE's existing `AfxHookSource_imgui.ini`. Key label size is
adjustable in the Campath tab using the classic world-space index renderer.

[Port inventory and source attribution](../shared/overlay/PORTING.md)

## Validation

```text
cmake -S tests/cinematic -B build/camera-tests
cmake --build build/camera-tests --config Debug
ctest --test-dir build/camera-tests -C Debug --output-on-failure
```

On Windows select `-A x64` or `-A Win32` with separate build directories. On Linux,
the headless GUI integration test also runs the actual editor and bundled widgets:

```text
python tests/cinematic/test_overlay.py --imgui-dir <path-to-imgui-v1.89.9-docking>
```

These checks support review; the complete MSVC build, in-game input handoffs,
gizmo axes/projection, device resets and recorded output still require testing in TF2.
