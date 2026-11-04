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

`draw <layer> 0|1` accepts `all`, `paths`, `cameras`, `labels`, `handles`,
`constraints`, and `direction`. Drawing is active only while editing or previewing.
All paths are shown. Selected paths are cyan; selected points yellow; start/end
markers green/red; inactive paths gray. Camera wireframes show recorded orientation
and FOV with a representative 16:9 aspect ratio. Bézier handles and locked planes
have separate colors. Names/indices use a small ASCII font; non-ASCII labels display
replacement glyphs, although XML preserves their UTF-8 names.

Geometry and text are copied to immutable render snapshots; the rendering thread
never reads mutable editor state. Projection checks reject unrelated view passes.
Drawing uses the existing Direct3D 9 hooks, preserves rendering state and releases
its texture on device reset. It is a diagnostic overlay and **appears in recordings**.
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
`tests/cinematic/run.sh` (GCC, AddressSanitizer and UndefinedBehaviorSanitizer).
If LeakSanitizer cannot run under the host's tracing/sandbox, set
`ASAN_OPTIONS=detect_leaks=0`; address/undefined checks remain enabled.

Limits are 128 paths, 4096 points per path, 16,384 points per collection, 16,384
keys per track, 65,536 keys per collection, 16 MiB XML, and 24 hours per movement.
Unsupported XML versions, external entities, malformed numbers and invalid timing
are rejected. These bounds protect interactive evaluation and undo memory use.

The standalone tests validate the model, not a running game. Windows/MSVC full
builds, in-game keyboard routing, actual game-update compatibility, release back to
the demo camera, graphics-device resets and visual overlay behavior require the
Windows/in-game checks listed in `tests/cinematic/REVIEW.md`.
