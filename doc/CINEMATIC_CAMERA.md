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
3. Repeat. A new overlay-created path uses **Spline**, which works with two keys.
4. Enable **Campath enabled** and release the free camera to let native playback
   follow demo time. Use `mirv_input end` when necessary.

Choose **Interpolation** in the Campath tab or curve editor:

| Option | Behavior |
| --- | --- |
| Spline | Original whole-path spline with a smooth start and stop |
| Catmull-Rom | Shared tangents based on neighboring keys and their time spacing; travels through keys and uses nonzero endpoint slopes |
| Shape-preserving (Monotone) | PCHIP interpolation limits overshoot in each automatic channel; peaks, reversals and plateaus can flatten a channel |
| Classic cubic | Original HLAE interpolation with its four-key requirement |
| Linear | Straight interpolation with two keys; corners can change velocity abruptly |

Spline, Catmull-Rom and Monotone share one automatic derivative at each key
between its incoming and outgoing intervals. They are whole-path settings for
position, angles and FOV. **Tension** adjusts Auto tangents across the entire path:
0% preserves their full slopes; 100% flattens them and deliberately stops at keys.
Intermediate values reduce the slopes. Existing Free, Flat and Linear tangent
edits are retained. Return those tangents to Auto to apply the chosen profile.

For flowing travel, start with Catmull-Rom at 0% tension. Authored positions and
timestamps still determine speed; these profiles do not promise constant speed
between arbitrarily timed positions. Classic console capture with
`mirv_campath add` remains available. Existing files keep their interpolation.
Profile and tension settings save in the same XML and participate in undo/redo;
older XML files use the original defaults.

## Sequencer and curve editor

Enable **Show Sequencer**. The timeline spans the entire demo, including future
ticks, and displays its current visible range. Scroll the wheel to zoom around the
pointer. Middle-drag or Shift+wheel to move through the timeline, or drag its
overview bar. **Fit demo** restores the full range. Navigation does not retime keys.
Named paths appear on separate tracks. The editing track has draggable keys;
other tracks show reference keys and holds. Click another path's name or range to
make it the editing path. **Playback / cuts** shows which eligible shot wins each
time interval, including overlaps and returns to an earlier shot. It displays the
planned sequence while global playback is disabled. Yellow boundaries identify
switches; the Campath tab also names the current playback winner.

Wheel over the time area zooms; wheel over track names scrolls a tall collection.
Long names are clipped in the name column and shown fully in tooltips. The
sequencer supports selecting and dragging keys and exposes a right-click menu:

- **Remove** deletes the key.
- **Get** moves the free camera to the key's pose.
- **Set** captures the current pose at its timestamp, preserving tangent metadata.
- **Edit** selects the key for the viewport gizmo.

Use the pause/speed buttons or scrub the timeline to navigate the demo. The preview
slider evaluates the path without seeking. **Preview path** in either panel and
`mirv_campath preview` operate the same cursor and camera. Opening or closing the
overlay retains that cursor; right-mouse flying explicitly exits preview.
Free-camera movement and smoothing do not alter the evaluated pose. **Seek to
preview** seeks the demo to that cursor.

**Insert keyframe at preview** subdivides an existing Custom/Linear path without
changing its curve, including weighted handles and Euler winding. It requires an
unoccupied time inside the path. To preserve all existing intervals, automatic
tangents are frozen to their effective Free tangents; use **Reset selected tangents
to Auto** when you want them recalculated. Classic quaternion/cubic interpolation
cannot be split exactly by this editor and insertion is rejected there. **Add
point** remains normal pose-and-demo-time capture.

Enable **Show graph**, or **Show Curve Editor** in the sequencer, to open the
dockable **Campath Graph** window. It displays X/Y/Z, FOV, pitch/yaw/roll individually or
in combination; X is visible initially. Normalization makes channels with different
units readable. Wheel zooms time around the pointer; Ctrl+wheel zooms values.
Middle-drag pans both axes. **Fit graph** resets the view. The sequencer window
can be resized vertically; zoom and pan no longer require sliders. Graph time
labels show demo ticks / path seconds; value labels and cursor tooltips show the
focused channel (or first visible channel). Tooltips also report evaluated values
for every visible channel. Holds have release markers, including final holds and
single-key stationary shots.
Click a graph key to select it, Ctrl-click to extend/toggle selection, or drag a
box on empty canvas. Right-click a channel checkbox to focus editing when
normalized channels overlap. Drag keys to change time and that channel's value;
Shift locks the dominant axis, and Alt snaps time displacement to demo ticks.
Selection is shared by native commands, graph editing and timeline highlights.
Selected keys move together. **Escape** cancels an unfinished mouse drag,
including handles, timeline retiming and the viewport gizmo. Colliding times and overlapping holds are rejected
without deleting keys. The **Selected key inspector** provides exact path time,
demo tick, position, angles, FOV and hold duration. Numeric changes apply as an
offset to all selected keys (hold duration applies the same duration).

Right-click a key for Auto, Flat, Linear or Free tangent modes, applied to the
incoming side, outgoing side or both. Clicking a selected key preserves the
selection for batch tangent changes. **Reset selected tangents to Auto** resets
visible/focused channels, including their handle weights. **Reset whole path to
Auto** recalculates all channels without changing selection. Circles identify Auto
tangents; squares identify authored/baked tangents, and the interpolation controls
report their counts. Insert and retime preserve shape by baking automatic tangents;
changing profile or tension then affects only tangents still set to Auto.

Handles and tangent menus are available only for Custom channels. Linear and
Classic curves are drawn from their actual evaluator and expose no inactive
handles. Distance-timed channels are also drawn from playback; disable distance
timing to edit their underlying handles directly.

- Drag a curve handle to edit the shared slope through that key.
- Hold **Shift** while dragging to change its weight/influence.
- Hold **Alt** to edit a side independently for an intentional corner.
- **Undo / Redo** or **Ctrl+Z / Ctrl+Shift+Z** restore data edits while the overlay
  owns input, and in native `mirv_campath preview` mode with the overlay closed.
  Console edits enter the same history, starting with the first edit. A drag is
  one edit. History retains up to 64 entries within a 64 MiB budget. Preview,
  selection, panel navigation and playback enable changes do not consume history. Console equivalents are
  `mirv_campath undo|redo` and `mirv_campath preview undo|redo`.

The graph reads effective tangents directly from the path evaluator, including
automatic and tension-modified tangents. Weighted curves solve their time
coordinate during playback, so the graph and camera use the same curve. Euler channels unwrap adjacent angles to avoid ordinary
360-degree discontinuities. Authored Euler values retain winding after insertion.

**Travel speed / distance timing** plots the evaluated positional speed. **Retime
by distance** preserves the spatial curve, distributes travel duration by arc
length, and retains hold durations and stationary intervals. Custom/Linear
channels are required; the total duration must leave time for travel. This enables
**Distance timing**, which maps each travel interval by distance and smoothly
ramps into and out of explicit holds. Ordinary keys share the overall travel
speed after retiming; geometric corners can still change direction abruptly.
Changing positions, tangents or holds afterward can require another retime.
New paths apply distance timing to **position only**. Angle and FOV curves keep
their own time coordinate within the retimed key intervals. **Apply distance timing
to angles / FOV too** restores the shared pose clock when desired. Earlier files
without a mode retain their original whole-pose clock; new files store the choice.
Retime still changes the arrival timestamps of existing keys for every channel.
Turning Distance timing off restores ordinary interpolation in the new time layout.

Console equivalents remain under the native command tree, for example:

```text
mirv_campath edit interp auto catmullrom
mirv_campath edit interp tension 0.2
mirv_campath edit interp position custom
mirv_campath edit interp rotation custom
mirv_campath edit interp fov custom
mirv_campath select #1 #1
mirv_campath edit tangentmode x both auto
mirv_campath edit tangent x both 20
mirv_campath edit tangentweight x both 1
```

`edit interp auto spline|catmullrom|monotone` chooses the profile and enables
Custom interpolation on all channels with one command. `edit interp tension`
accepts 0..1. The existing per-channel interpolation commands remain available.

Tangent commands accept `x|y|z|fov|pitch|yaw|roll` and `in|out|both`, applying to the
selection or to all keys when none is selected.

## Named paths and holds

The Campath tab selects one named path for editing, with New, Duplicate, Rename
and Remove controls. Native keyframe indices remain zero based within that path.
**Campath enabled** is the shared playback switch. Playback automatically chooses
the evaluable path with the latest start at the current demo time; ties go to the
later path in the list. Selecting another path changes the editing target, not
playback priority. All paths use ordinary native keys and the classic drawer.
Each path has **Mute playback**, **Solo playback** and **Visible in world**.
Duplicates start muted. Muting excludes a path; if any paths are soloed, only soloed
paths are eligible. Mute wins over solo, so muting the only soloed path does not
silently enable other takes. Visibility controls world drawing independently.
These flags save with both single-path files and collections and participate in
undo/redo.

Names, hold releases and interval durations are drawn with the classic text
projection and depth test, using the existing GUI font atlas for full text.
**Key label size** also scales these labels; zero hides text. Indices and extended
labels share an overlap layout and stack upward when projected areas collide.
**Path names**, **Hold labels** and **Segment durations** can be hidden separately.
World projection and depth testing remain in the classic drawer; no second
world renderer is used.

In the inspector, set **Hold (seconds)**, **Capture hold release** at the current
demo time, or **Clear hold**. A held key keeps its complete pose from arrival until
release, then travels to the next key by its arrival time. Release cannot pass the
next arrival; sharing it is allowed only for an identical pose. Smooth custom
interpolation arrives and departs with zero velocity. Linear remains linear.
Adding a hold converts Classic cubic/quaternion channels to Custom, which supports
endpoint derivatives. This is separate from classic `mirv_campath hold`, which
continues to hold a whole path outside its bounds. A single key with a hold is a
playable stationary shot.

```text
mirv_campath path list
mirv_campath path new "Second shot"
mirv_campath path duplicate "Alternate shot"
mirv_campath path use 0
mirv_campath path rename "Opening"
mirv_campath path mute 0
mirv_campath path solo 1
mirv_campath path visible 0
mirv_campath path remove "Alternate shot"
mirv_campath insert
mirv_campath insert 12.5
mirv_campath keyHold 1
mirv_campath keyHold 1 14.0
mirv_campath keyHold 1 clear
mirv_campath retime 10
mirv_campath distanceTiming 1 position
mirv_campath distanceTiming 1 pose
mirv_campath distanceTiming 0
mirv_campath save "opening.xml"
mirv_campath save "sequence.xml" all
mirv_campath load "sequence.xml" all
```

`insert` without a time uses the active preview cursor. `keyHold` without a release
captures the current demo time; an explicit release uses path seconds. Path
selection/removal accepts a zero-based path index or a quoted name. Undo restores
path creation, removal, renaming and camera edits across the document.

## Preview shortcuts and seeking

The shortcuts below work in native preview with the overlay closed, and in the
open editor when a text field is not being edited:

| Key | Action |
| --- | --- |
| E | Capture pose and current demo time |
| Shift+E | Replace the one selected key's pose, keeping its time, hold and tangents |
| Y | Capture the selected keys' hold release at current demo time |
| Shift+Y | Clear selected holds |
| Ctrl+A | Select all keys |
| Ctrl+Shift+A | Anchor the selection from its first selected key to the current camera, using HLAE's existing anchor command |
| Right Ctrl alone | Seek to the preview cursor on release |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Up / Down | Native preview: next / previous key |
| Home / End | Native preview: first / last key |
| Left / Right | Native preview: scrub, or step keys if interpolation is unavailable |

Using Right Ctrl as part of another chord does not also seek. Shift remains
available for Source's Shift+F2 demo UI. The Campath tab also provides capture,
replace, select-all and anchor buttons.

**After seeking** chooses Timescale, Pause or Continue (1x), with **Timescale 0**
as the default. This applies to preview seek, Goto Start and timeline scrubbing.
The seek temporarily resumes at 1x, asks Source to pause at the target, and applies
the chosen behavior after arrival. Setting timescale before resuming at the target
keeps zero-timescale models and particles frozen. Seeking does not move the
independent preview cursor.

```text
mirv_campath preview afterSeek timescale 0
mirv_campath preview afterSeek timescale 0.25
mirv_campath preview afterSeek pause
mirv_campath preview afterSeek continue
```

The mode and timescale persist in the existing ImGui settings file. Source's
console and menus retain input priority.

## Rotation authoring and classic export

Ordinary wrapped captures still unwrap across +180/-180 using the short route.
Captures with pitch beyond 90 degrees or explicitly unwrapped yaw/roll retain
those authored values. Custom Euler interpolation and numeric editing can express
complete turns: for example, yaw 0 to yaw 720 gives two turns. Editing another
angle component retains the untouched winding; rotation and anchor transforms
retain an equivalent Euler representation near the authored angles. Quaternion
Linear/Classic modes continue to use quaternion orientation, which does not encode
a turn count; use Custom rotation for deliberate spins.

**Export classic XML** exports the selected shot, including a muted alternate,
as sampled ordinary HLAE linear-position/FOV and spherical-linear rotation keys.
Choose **Sample FPS** (default 120). Original key and hold-release times are
included, and extra rotation samples preserve fast complete turns. The file has
no custom tangents, per-key hold or distance-timing extensions. The editing path,
its indices, native save target and savepoint remain unchanged. Exports are bounded
to 100,000 samples and the native reader's 64 MiB file limit; an excessive request
fails without replacing the destination.

```text
mirv_campath exportClassic "opening-classic.xml" 120
```

This is a sampled approximation, not an editable copy of the original curve.
Increase sample FPS for fast or sharply curved movement. The exported file has
additional keyframes and can be used by older HLAE versions. Save path / Save all
remain the formats for retaining the editable project. Export is for one selected
shot; collections retain their own native format and playback rules.

## Gizmos and observing

Choose **Edit** on a timeline key, then enable **Show Gizmo**. **G** selects
translation and **R** rotation when a text field is not active. Local/world modes
and Ctrl snapping are available. **C** toggles free-camera control. Camera Control
also provides FOV, roll and keyboard speed sliders.

The **Observing** tab is a player list with a text filter. Click a player name to
select and spectate them in an STV demo. The command uses the player's explicit
`#userid`, which is different from their entity slot. Selecting a player clears
path preview, free-camera and attachment overrides, disables automatic directing,
and switches to TF2's in-eye spectator camera. There are no buttons inside the
player rows. Native spectator targeting follows living players; POV demos cannot
switch their recorded spectator target, and the panel reports that restriction.

The selected player still exposes named bones and forward/left/up offsets below
the list. Choose a **Bone** (or **Entity origin**) and use **Attach selected** for
the existing TF2 attachment system, including in POV demos. Fly to a composition
and use **Capture relative framing** to follow the target from that composition.
**Detach** clears the attachment.

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

**Save path** / **Save path as** write ordinary native XML for the selected path;
**Save all** / **Save all as** write a named-path collection. Open the corresponding
format with **Open path** or **Open collection**. Files retain custom tangents,
weights, holds, Euler winding and distance timing. Old classic files still load.
Save appends `.xml` when needed. Failed loads preserve current work and saves use
an adjacent temporary file before replacing the target. Collection XML requires
this branch; older HLAE versions do not understand its additional features.

An asterisk marks unsaved edits. Recovery backups are written every 30 seconds by
default under `%LOCALAPPDATA%/advancedfx/camera-recovery` (or HLAE's folder when
that variable is unavailable), and when closing the overlay or loading another
file. Settings controls the periodic backup and interval. Backups do not overwrite
source files or mark changes saved. The Campath tab offers **Recover latest
backup** and **Discard latest backup** when older backups exist. Recovery forces
Save As; older backups remain until discarded. Undo history itself is not persisted.

Settings include a demo browser, UI scale, fonts and docking. Availability of the
extra fonts depends on Windows. UI preferences, panel layout and last-used
folders are stored in HLAE's existing `AfxHookSource_imgui.ini`. Key label size and label visibility are
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

Selection changes keep trajectory and distance caches intact; the drawer updates
selection colors without resampling geometry. A CPU check with 32 shots of 500
keys measured approximately 0.006 ms per playback query and 0.009 ms per selection
update in this cloud environment. These are CPU measurements, not TF2 frame-rate
or GPU guarantees.
