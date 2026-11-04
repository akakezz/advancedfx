# Cinematic camera paths (Source 1)

This editor is an opt-in extension to `mirv_campath`, initially integrated with
`AfxHookSource` (including TF2 Win64 and the existing Source 1 Win32 hooks).
It does not replace the classic implementation in GoldSrc or Source 2.

A path is a movement, with its own shape, timing, placement and camera animation.
There are no mandatory shots, projects, names or setup objects.

## First movement

Open a demo and enter:

```text
mirv_campath edit on
```

Fly with the normal `mirv_input` free-camera controls. Press **Insert** at each
camera pose you want to capture. The first capture automatically creates a path.
It also places the path's anchor at that first position. Orientation and FOV are
captured with every point. Paths default to a smooth curve and five seconds.

Press **Tab** to preview. Press **Tab** again to return to free-camera editing,
retaining the preview pose. Three captures are enough; one or two also work.

With the console open:

```text
mirv_campath duration 10
mirv_campath rename "Bridge approach"
mirv_campath save BridgeApproach
```

The saved file is `BridgeApproach.xml` in the game's current working directory.
Use an absolute quoted path if you prefer an explicit location. Saving does not
overwrite a file unless you add `overwrite`.

`mirv_campath` prints the complete command reference while editing. All cinematic
commands are also available as `mirv_campath cinematic <command>` when editing is
off. Commands are case insensitive; path names are case sensitive.

## Editor controls

| Key | Action |
| --- | --- |
| Insert | Append current camera pose |
| Shift+Insert | Replace the selected point, preserving its holds/segment settings |
| Delete | Delete selected point |
| Tab | Toggle free-camera editing and preview |
| Ctrl+Z / Ctrl+Y | Undo / redo a data edit |
| Up / Down, in preview | Select previous / next point |
| Home / End, in preview | First / last point |
| Left / Right, in preview | Scrub movement time backward / forward |

Arrow/Home/End keys are reserved only during preview. Editing keys are consumed
before Source receives them. The game console, game menu and opted-in UI cursor
retain priority. Focus loss cancels held scrubbing. Scrubbing uses wall time and
works while the demo is paused. Shift+Escape exits freecam/preview through the
existing input handler. Classic preview's dedicated toggle key is suspended while
the new editor is active.

Console equivalents:

```text
mirv_campath add
mirv_campath select 1
mirv_campath replace
mirv_campath insert 1
mirv_campath delete
mirv_campath point 100 200 64 0 90 0 80
mirv_campath preview enter
mirv_campath preview 2.5
mirv_campath preview seek
mirv_campath preview exit
```

Indices are zero based. `point` replaces the selected point with an explicit
**world** position, pitch/yaw/roll in degrees, and FOV. `select` changes selection
without moving a point. In preview it visits that point's first arrival time;
a custom motion track can make some points unreachable. `preview seek` seeks the
demo to path start plus preview time; scrubbing alone does not seek the demo.
Independent animation tracks still apply at the selected time.

## Multiple paths and undo

```text
mirv_campath new
mirv_campath new "Tower orbit"
mirv_campath paths
mirv_campath use 0
mirv_campath use "Tower orbit"
mirv_campath duplicate
mirv_campath remove confirm
mirv_campath undo
mirv_campath redo
```

New paths and copies are unscheduled to prevent accidental competing movements.
Duplication preserves geometry, animation, constraints and placement. A duplicate
gets its own stable ID. Names are optional; duplicate names require selection by
index. The list reports selection, length, duration, start, scheduling and unsaved
changes. Selection and preview navigation do not mark the collection dirty.

Data edits, path creation/deletion, imports and placement changes are undoable.
Undo retains 64 edits; preview state, current key presses, file writes and timing
capture begin marks are not undo entries. An invalid data edit is rejected before
it changes the collection or its undo history.

## Shape and placement

```text
mirv_campath curve linear
mirv_campath curve smooth
mirv_campath tension 0.25
mirv_campath curve bezier
mirv_campath select 1
mirv_campath handle in -32 0 0
mirv_campath handle out capture
```

Linear joins are straight. Smooth uses a cardinal/Catmull–Rom spline through
captured points: tension 0 is the smooth default; tension 1 flattens endpoint
tangents. Bézier handles are offsets from their point in local path coordinates.
`capture` uses the current free-camera position as a handle endpoint. New Bézier
handles default to zero; capture or set them to shape the curve.

```text
mirv_campath orbit 100 200 64 256 xy 360
mirv_campath orbit 100 200 64 256 xz 180 -90 9
```

Orbit arguments are world center, world radius, local plane (`xy`, `xz`, `yz`),
signed sweep in degrees, optional starting angle and optional point count (default
9). The circle remains analytic, including between its visible points. Changing
its curve to linear/smooth/Bézier materializes its points, after which they can be
edited normally. Orbit creation replaces the selected path's geometry and point
holds/segment settings; undo restores the previous movement. Capture a first
point or use `new` before generating an orbit.

```text
mirv_campath translate 100 0 0
mirv_campath rotate 0 45 0
mirv_campath scale 0.5
mirv_campath anchor 100 200 64 0 90 0
mirv_campath anchor capture
```

Translation is a world displacement. Rotation is applied around the path anchor.
Scale is an absolute uniform factor, not a multiplier. Anchor sets absolute world
position and rotation; `capture` uses the current free-camera pose. Placement
transforms geometry, handles, orientation tracks and fixed aim targets together,
without rewriting captured local coordinates or altering timing.

```text
mirv_campath grid 16
mirv_campath lock z 0
mirv_campath constrain
mirv_campath lock z off
mirv_campath grid 0
```

Grid and axis locks are persistent local editing constraints. New/replaced points
and handle endpoints obey them. `constrain` applies them to existing points.
They do not quantize evaluation every frame. Thus a planar smooth path stays
smooth rather than jumping between grid cells. Convert a circle before using
point constraints. Grid/plane units scale and rotate with the path.

`corner stop` selects linear geometry and zero velocity at the selected point;
`corner round` selects smooth geometry and removes that point's stop flag. Curve
selection affects the whole path. Use `stopPoint 0|1` to change a point's stopping
behavior without changing the global geometry mode.

## Timing independently of shape

```text
mirv_campath duration 10
mirv_campath select 1
mirv_campath hold 2
mirv_campath stopPoint 1
mirv_campath segment 0 3
mirv_campath segment 0 auto
mirv_campath ease smooth
```

The duration includes **all holds**. For three equally spaced points, duration 10
and a 2-second hold at the middle means travel 4s, hold 2s, travel 4s. A stop makes
velocity reach zero momentarily; a hold allocates real time at a point. Holds also
stop incoming and outgoing translation. Independent orientation/FOV animation
can continue while position is held.

`segment <point> <seconds>` reserves an exact travel time for that point's outgoing
segment. The remaining time is distributed over automatic segments by arc length.
If every segment has an explicit duration, those durations plus holds must equal
the total duration. Edits that overbook the duration are rejected. Increase total
duration before adding a hold that would otherwise exceed it.

Without easing, automatic travel has approximately constant physical speed across
smooth/Bézier curves. Geometry is evaluated through a 128-sample-per-segment
arc-length table; circles use exact angular distance. This is a numerical
approximation for arbitrary curves, not symbolic arc-length integration.

Easing choices are `linear`, `in`, `out`, `smooth`, `smoother`, and `custom`.
They apply within each segment. Stops use zero endpoint derivatives. Smooth and
smoother ease at every segment boundary; use linear easing for continuous travel
through intermediate points, adding stops only where you want them.

```text
mirv_campath speed set 0.5 0.25
mirv_campath ease custom
```

Custom speed keys describe **cumulative progress**, not raw velocity: halfway
through each segment's travel time this example covers one quarter of its length.
Endpoints must remain `(0,0)` and `(1,1)` and values must be monotonic. Flat portions
hold progress. Use `speed delete <time>` or `speed clear` to edit the curve.

For complete control, including reversing along a path:

```text
mirv_campath motion set 0 0
mirv_campath motion set 0.5 1
mirv_campath motion set 1 0
```

A motion track maps normalized time across the **whole movement** to normalized
arc distance. Keys interpolate linearly and clamp outside their range; repeated
values create holds. It replaces automatic segment timing, stops and easing.
Clear point holds and explicit segment durations before enabling it; conflicting
settings are rejected. `motion clear` returns to automatic timing. Tracks accept
`delete <normalizedTime>` as well as `set` and `clear`.

## Capturing time

```text
mirv_campath start capture
mirv_campath end capture
```

Mark the first moment in the demo, then the last. Start sets scheduling; end sets
duration relative to start. Start can also be typed directly: `start 12.5`.
Times are measured from demo playback ticks plus the engine interpolation fraction,
not `mirv_time`'s optional resume-paused clock.

```text
mirv_campath mark begin demo
mirv_campath mark end
mirv_campath holdMark begin wall
mirv_campath holdMark end
```

`mark` captures duration; demo marks also set start and schedule the movement.
`holdMark` sets the selected point's hold. `wall` uses a monotonic real-time clock
independent of demo speed. The end defaults to the begin mark's clock; explicitly
mixing clocks, changing paths, or changing the selected hold point is rejected.
Wall-clock marks measure the time between commands, including time spent entering
them. Begin marks do not alter saved data until a valid end is committed.

## Independent orientation, aiming and FOV

```text
mirv_campath rotation set 0
mirv_campath rotation set 0.5 0 90 0
mirv_campath rotation set 1 0 180 0
mirv_campath rotation interp smooth
mirv_campath fov set 0 90
mirv_campath fov set 1 50
mirv_campath fov interp linear
```

Rotation keys without angles capture the current camera orientation. Explicit
angles are world pitch/yaw/roll at capture time; they are stored relative to the
path anchor so the animation follows later placement changes. Orientation uses
shortest-arc quaternion interpolation. FOV is interpolated separately. Each track
supports `linear`, `smooth`, and `step` interpolation, `delete <time>` and `clear`.
Track times span 0..1 of total duration. A single key holds its value throughout.
Without these tracks, rotation and FOV interpolate between captured point poses.
A single spatial point plus orientation/FOV keys produces a stationary animation.

```text
mirv_campath aim 100 200 64
mirv_campath aim off
```

A fixed world target is converted to path-local space, moving with its anchor.
It overrides the orientation track and uses zero roll. Existing `mirv_aim`
entity/bone tracking remains available at its existing view-override stage.
Its own smoothing may depend on frame history; the new evaluator itself is pure.
Custom override ordering, aim, camera offsets, FOV overrides and imported camera
sources can still affect the final camera, so use the usual override commands
when diagnosing a conflict.

## Scheduled movements and transitions

```text
mirv_campath use 0
mirv_campath start 10
mirv_campath duration 5
mirv_campath finish hold
mirv_campath use 1
mirv_campath start 15
mirv_campath transition 1
mirv_campath overlaps
mirv_campath play
mirv_campath stop
```

`play` evaluates all scheduled paths against the current demo time. Seeking in
either direction reevaluates directly; there is no playback cursor to wind back.
A path with `schedule 0` is excluded. A newly created or duplicated path is not
scheduled until you set its start or explicitly enable it.

If paths overlap, the latest start wins. Stable path ID breaks equal-start ties.
The overlap command lists competing intervals. A finished latest movement releases
the camera with `finish release`, or holds its last evaluated pose with `finish hold`.
An older overlapping movement does not resume after a newer one ends.

`transition <seconds>` blends from the preceding scheduled path's evaluated pose
to this path during the opening part of this path's duration. The preceding pose
clamps at its end if necessary. This crossfade uses smoothstep and quaternion
blending, can bridge gaps, and consumes time inside the incoming duration. Zero
means a direct cut. Transitions are pose blends, not guaranteed tangent-continuous
geometric joins. There is no blend from the native camera into the first path.

Playback takes ownership at the existing input stage and suppresses classic
campath. During gaps it restores the current frame's native camera input, rather
than a pose cached when playback started. `play` ends free-camera control; `stop`
releases scheduled control. `preview exit` instead retains its pose as freecam.

## XML, imports and reusable movements

```text
mirv_campath save Movement
mirv_campath save Movement overwrite
mirv_campath saveAll Collection
mirv_campath load Movement
mirv_campath load Collection append
mirv_campath load Collection replace
mirv_campath exportLegacy BakedMovement
```

`save` writes the selected path; `saveAll` writes the collection. Both use versioned
XML preserving geometry, anchors, constraints, point timing, schedules and animation
tracks. No proprietary extension or mandatory project object is required.

Load defaults to append and remaps IDs to avoid collisions. Replace refuses to
discard unsaved work unless you specify `replace discard`. Invalid files are parsed
and validated before a state change. Imports and replacement can be undone.
Only `saveAll` marks the entire collection saved; saving one path cannot account
for edits to others. Writes use a sibling temporary file and a checked atomic move.

Classic `<campath>` XML is accepted as an import. Its positions and captured
quaternion/Euler rotations, FOVs, key times, offset and hold setting are read.
Import uses **linear spatial interpolation** with independent linear time tracks;
classic cubic interpolation settings are not reproduced exactly. To retain an
original file's exact native interpolation, continue loading it in classic mode.
`exportLegacy` bakes the selected movement at up to 60 samples/sec, capped at 20,000
intervals. This is an explicitly lossy compatibility export, not the editable new
schema. Step discontinuities and very fast motion are limited by that sampling.

To reuse a movement, load it, change its anchor/scale and duration, then set a start
time in the new demo. No saved file contains an entity pointer or map dependency.

## Drawing

New labels default to 32 pixels. Use `mirv_campath draw keyIndex 48` to enlarge
them, or run `mirv_campath draw keyIndex` to print the current value. Classic
`draw keyIndex` retains its original world-height setting.

`draw <layer> 0|1` accepts `all`, `paths`, `cameras`, `labels`, `handles`,
`constraints`, and `direction`. Drawing is active only while editing or previewing.
All paths are shown. Selected paths are cyan; selected points yellow; start/end
markers green/red; inactive paths gray. Camera wireframes show recorded orientation
and FOV using the existing campath frustum drawing and current viewport aspect ratio. Bézier handles and locked planes
have separate colors. Names/indices use a small ASCII font; non-ASCII labels display
replacement glyphs, although XML preserves their UTF-8 names.

Geometry and text are copied to immutable render snapshots; the rendering thread
never reads mutable editor state. Projection checks reject unrelated view passes.
Drawing uses `CCampathDrawer`'s existing render queue, shaders, line buffer,
frustum helper, label projection and device lifetime. Classic index labels retain
the existing hex font; path names use an alphabet atlas owned by the same drawer.
Render state is restored and font textures are released on device reset. It is a diagnostic overlay and **appears in recordings**.
Turn drawing off before capturing clean footage. It is depth-independent so paths
remain visible behind walls. Visualization has a 100,000-line budget per snapshot.

## Compatibility and validation

```text
mirv_campath edit off
mirv_input end
```

Classic campath commands/files and its own preview are available when editing is
off. No new path is implicitly converted into the classic mutable `CamPath`.
The integration adds no required dependency to Source 1 builds: the portable core
uses C++17; drawing uses existing Windows/GDI/Direct3D libraries.

Build HLAE on Windows using `BUILDING.md`. For standalone core tests on either
Windows architecture, configure `tests/cinematic` separately:

```text
cmake -S tests/cinematic -B build/cinematic-tests -A x64
cmake --build build/cinematic-tests --config Debug
ctest --test-dir build/cinematic-tests -C Debug --output-on-failure
```

Use another build directory and `-A Win32` for 32-bit. Linux can use
the same standalone CMake target (omit `-A x64`). To enable sanitizers on GCC/Clang,
configure with `-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`
and `-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"`.
If LeakSanitizer cannot run under the host's tracing/sandbox, set
`ASAN_OPTIONS=detect_leaks=0`; address/undefined checks remain enabled.

Limits are 128 paths, 4096 points per path, 16,384 points per collection, 16,384
keys per track, 65,536 keys per collection, 16 MiB XML, and 24 hours per movement.
Unsupported XML versions, external entities, malformed numbers and invalid timing
are rejected. These bounds protect interactive evaluation and undo memory use.

The standalone tests validate the model, not a running game. Windows/MSVC full
builds, in-game keyboard routing, actual game-update compatibility, release back to
the demo camera, graphics-device resets and visual overlay behavior require the
Windows/in-game checks listed below.

## Windows review checklist

Use a clean checkout of `feature/cinematic-campath`. Initialize submodules and
build using `BUILDING.md`, testing both `Win32` and `x64` where the games support
them. No game binaries or MSVC build were executed in the Linux implementation
workspace. The core tests can run independently of HLAE and should run in both
Windows build architectures.

1. Load a TF2 demo, enable `mirv_campath edit on`, and capture three points with
   Insert. Check automatic path creation, visible curve, frustums and labels.
2. Toggle preview with Tab. Verify all navigation keys, paused-demo scrubbing,
   retained freecam pose on exit, and Shift+Escape exit. Verify arrows do not
   scrub in freecam editing. Check normal Source binds do not also fire.
3. Open the console, menu, and optional `uiCursor` UI while a scrub key is held.
   Alt-tab out and back. Scrubbing must not stick. Ctrl+Z/Y must reach console
   text editing while the console is open, and editor undo while it is closed.
4. Replace/insert/delete/select points, duplicate/remove paths, undo/redo each
   operation, and verify all inactive paths remain visible.
5. Exercise linear, smooth, Bézier handles captured with freecam, and analytic
   XY/XZ/YZ orbits. Re-anchor, rotate and scale without altering local geometry.
   Enable Z locking/grid, then capture/replace points and constrain existing ones.
6. For a straight equally spaced 3-point path: duration 10, middle hold 2. Check
   arrivals at 0, 4 and 10; departure at 6. Verify stops differ from holds.
   Add explicit segment timing, custom speed and reversing motion tracks.
7. Animate rotation and FOV on a single point. Rotate the anchor and confirm
   orientation keys and fixed targets move with it. Check native `mirv_aim`
   behavior with and without this feature enabled.
8. Capture start/end/hold duration with demo time and wall time. Reject a reversed
   or mismatched capture without losing existing data.
9. Schedule two paths with cuts and blends. Seek directly into each path and
   transition, backward and forward. Overlap selection must be stable. Gaps and
   `finish release` must show the current demo camera, not an old pose.
10. Save one path and a collection. Check extension defaults, spaces/UTF-8 file
    paths, refusal to overwrite without `overwrite`, and protected replacement of
    unsaved work. Invalid/truncated/unsupported XML must preserve the editor.
    Verify append, replace, undo import, classic import and baked legacy export.
11. Toggle every draw layer. Check main view/skybox filtering, aspect ratios,
    console/menu rendering, engine rendering with multicore on/off, and graphics
    device resets/resolution changes. Overlays must not alter subsequent game
    rendering. Disable overlays when recording clean footage.
12. Disable editing and end freecam. Load an existing classic campath and verify
    its commands, exact interpolation and classic preview still work.

The sections above describe command semantics, numerical approximations, legacy
import/export differences, limits and validation boundaries.

## Campath improvements from testing

Open backlog from the user's first Windows/in-game review, November 4, 2026.
These are future changes, not completed features. Add or revise items as testing
continues. Existing implementation/validation documents describe current behavior.

### Required direction for all future changes

- [ ] Follow HLAE's existing architecture, command structure, naming, formatting,
  help style, and visual conventions. Treat possible upstream integration as a
  design constraint, rather than a formatting pass at the end.
- [ ] Before introducing or replacing a subsystem, identify the existing HLAE
  implementation, explain any missing capability, and reuse or extend it where
  practical. New functionality must have a concrete reason to be separate.
- [ ] Audit the current implementation's duplicate vector/quaternion operations,
  XML reader/writer, drawing/text code, input handling, and command/help parsing.
  Compare against `shared/AfxMath.*`, the existing RapidXML persistence,
  `CampathDrawer.*`, `EntityIndexDrawer.*`, `shared/MirvInput.*`, and the existing
  command argument/help infrastructure. Preserve coordinate conventions, files,
  deterministic timing and meaningful tests during refactoring.
- [ ] Keep the genuinely new geometry/timing distinction, independent animation
  tracks and placement capabilities without assuming that every supporting
  primitive needs a new implementation. Reuse must not re-couple geometry to
  demo keyframe times or break classic behavior.
- [ ] Restrict future commits/pushes to `feature/cinematic-campath`. Do not modify
  any other branch or its contents.

### Keyboard ownership and menu access

- [ ] Fix Source binds firing while the editor owns keyboard input. This is a
  user-observed in-game defect/behavior gap, not a passing validation result.
- [ ] Route camera movement and editor actions through the appropriate existing
  HLAE input path before suppressing game input; blocking game binds must not
  disable free-camera movement.
- [ ] Account for key-down, key-up, repeat, character/system events, focus changes,
  and any additional Source input paths established by investigation. Prevent
  duplicate actions, missed releases and stuck keys.
- [ ] Preserve deliberate access to the console, game menu, demo UI and other
  menus. Visible UI must retain its intended input ownership; do not indiscriminately
  swallow the keys required to open/close or operate it.
- [ ] Test the user's reported leakage with bound editor keys and other bound
  keys, in both freecam editing and preview. Include held keys across menu opening,
  UI cursor handoff, alt-tab, editor exit and system switching.

Evidence: `MirvCinematic_KeyEvent` currently consumes only a whitelist of reserved
editor keys, passing other keys onward. That confirms incomplete keyboard
ownership. The exact paths responsible for any reserved-key leakage still need
in-game diagnosis; do not claim a specific root cause without that evidence.

### One `mirv_campath` command tree and explicit system selection

- [ ] Provide `mirv_campath system classic` and `mirv_campath system new`.
  Use `new` as the system name; do not require an additional branded name.
- [ ] Dispatch the `mirv_campath` command tree, subcommands and help according to
  the chosen system. Users should not have to insert `cinematic` into routine
  commands when outside edit mode.
- [ ] Separate chosen system, editor activation, preview, and playback state.
  Disabling the editor must not implicitly select classic or make loaded new paths
  inaccessible. Define camera handoffs explicitly and avoid unexpected stops.
- [ ] Retain each system's data when switching and preserve classic command/file
  behavior. Shared subcommand names must resolve predictably for the selected
  system, including classic `edit start/...`.
- [ ] Resolve the earlier playback workflow confusion with a consistent enable
  control and clear status/help, following classic conventions where appropriate.
  Whether timing capture should automatically enable playback remains a design
  decision; do not silently assume it was requested.
- [ ] Update docs/examples and handle any compatibility alias for the current
  `cinematic` prefix deliberately; its retention/removal has not been decided.

### Readable, contextual help

- [ ] Replace the single dense command dump shown in the user's screenshot with
  HLAE-style hierarchical help.
- [ ] Print one complete command/subcommand usage per line, with concise wording
  and indentation/spacing consistent with existing HLAE output.
- [ ] Root help should expose immediate subcommands; subcommand help should expand
  only the relevant branch. Include current values where HLAE normally does.
- [ ] Avoid pipe-separated lists of unrelated commands, unexplained shorthand and
  unnecessarily long lines. Ensure the output is understandable at ordinary game
  console widths/resolutions.
- [ ] Keep keyboard help discoverable without repeating the entire command tree.

### More keyboard editor operations

- [ ] Add convenient keyboard access to movement timing begin/end capture and
  selected-point hold begin/end capture.
- [ ] Add keyboard access to selecting/capturing incoming and outgoing curve
  handles with freecam, with clear feedback about what is being edited.
- [ ] Review point/path selection, insertion/replacement, and other frequently
  repeated operations for keyboard coverage.
- [ ] Choose and document the keymap with the user; specific additional bindings
  are not agreed yet. Reuse existing HLAE binding/input facilities where possible
  rather than creating an unnecessary parallel binding system.
- [ ] Keep console equivalents available. Minimize command entry for ordinary
  editing without making advanced controls or state changes undiscoverable.

### Larger and adjustable in-world labels

- [x] Increase default text size for the new system's in-world point indices/path
  names, checking readability at common resolutions.
- [x] Expose size adjustment through the new system's `mirv_campath draw`
  subtree, following existing HLAE naming and value/help conventions.
- [ ] Reuse/extend existing rendering and text support where it meets the need;
  preserve device-reset handling, render-state restoration and thread safety.
- [ ] Check scaling, spacing, label placement and clipping. Decide whether point
  indices and path names need independent settings as part of that review.

Clarification: classic campath already provides
`mirv_campath draw keyIndex <fHeight>` for its index-label height. New labels now default to 32 pixels and support `draw keyIndex <fHeight>`.
Both systems share `CCampathDrawer`; the new name-label alphabet atlas is an
extension of that drawer, not a separate rendering lifecycle.
This is separate from the Source console's own font size: restructuring HLAE help
and scaling in-world labels do not automatically change the console font.

### Completion criteria for the next revision

- [ ] Root/subcommand help is readable and follows the selected system.
- [ ] Both command trees remain usable without data loss or ambiguous routing.
- [ ] Ordinary editing can use the expanded keyboard controls without firing
  gameplay binds; menus and demo UI remain accessible.
- [ ] Confirm the enlarged, adjustable labels are readable in game.
- [ ] Reuse decisions and remaining new infrastructure are justified against
  existing HLAE code; style and integration have been reviewed.
- [ ] Re-run affected core checks, then validate the changed integration in actual
  Windows/Source sessions. Portable or cross-compiler checks alone cannot establish
  keyboard/menu/graphics behavior.

## Architecture and consolidation

The new runtime code consists of `shared/CinematicPath.h/.cpp`, which implement
untimed geometry, timing, placement and collection history. Supporting systems
are reused rather than maintained as separate replacements:

- `shared/AfxMath.*` supplies vector/quaternion arithmetic and conversions.
- The repository-pinned RapidXML dependency supplies XML parsing, escaping and
  serialization. Schema/bounds validation remains local to the new model.
- `MirvCommands.cpp` handles commands and editor key callbacks beside the classic
  campath commands. Declarations live with the existing view/input integration.
- `Hook_VClient_RenderView` owns both classic and new path state.
- `CCampathDrawer` owns both drawing modes, sharing the render queue, device
  lifecycle, shaders, line buffer, frustum helper and label setup. Only alphabet
  glyph support is added for path names, since the existing hex atlas cannot draw
  arbitrary names.

The former `CinematicDrawer.cpp`, `CinematicEditor.cpp/.h` and
`CinematicPathXml.cpp` are removed. The separate backlog, review and validation
notes and shell runner are consolidated here and in the standalone CMake target.
Relative to the original base, five added files remain: two runtime model files,
two standalone test files, and this guide. The previously agreed command-tree,
help and keyboard improvements remain in the open backlog above; this refactor
does not claim those behavior changes are finished.

## Validation after consolidation

The original 795 checks plus 17 additional compatibility/validation checks pass:
812 checks in one core test executable. The new checks exercise XML saved by the
first implementation, numeric XML references, malformed references/text, excessive
nesting and default initialization when using HLAE's existing math types.

Linux CMake/CTest and AddressSanitizer/UndefinedBehaviorSanitizer pass. Leak detection
is disabled only because the host's tracing/sandbox prevents LeakSanitizer from
running. Windows x86/x64 compile checks cover the command integration and the
existing `CampathDrawer.cpp`; the check uses temporary compatibility adaptations
for upstream MSVC-only header syntax and generated shader-combo declarations.
It does not execute shaders or establish rendering correctness. A full MSVC build,
actual input/menu behavior, classic/new drawing and device resets still require
the Windows review checklist above.
