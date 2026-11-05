# Cinematic camera paths (Source 1)

This opt-in `mirv_campath` system separates geometry, timing and placement. It is
integrated with AfxHookSource, including the existing TF2 Win32/x64 hooks. Classic
GoldSrc and Source 2 campaths keep their existing implementation.

## First movement

```text
mirv_campath system new
mirv_campath edit on
```

Fly with the existing `mirv_input` camera controls and press **Insert** to capture
spatial points. The first capture creates a path automatically. Capture position,
orientation and FOV together; geometry defaults to a smooth curve.

There is one editor. **Up/Down** selects and visits spatial points, **Home/End**
visits the first/last point, and **Left/Right** scrubs the movement. You can move
freely after visiting or scrubbing; there is no preview mode to enter or leave.
**Shift+Left/Right** moves directly along path distance, including portions not
visited by the current timing track. **Tab** revisits the selected path position. **Ctrl+Enter** seeks the demo to it.

To insert between points, select the point that should come *after* the new one,
fly to the new pose, and press **Ctrl+Insert**. The console equivalent is
`mirv_campath point insert <index>`. Indices are zero based. Shift+Insert replaces
the selected point while keeping its handle/automatic timing settings.

## Timing keys

Timing keys pair a position along the path with an absolute demo time. They are
separate from spatial points: changing timing does not add, move or delete geometry.

1. Select a spatial point, or scrub to the desired position along the path.
2. Position the demo at the desired time, using the demo UI or native demo controls.
3. Press **F6**. The current path position and demo time become a timing key.
4. Repeat elsewhere. Keys are sorted by demo time, so another F6 can insert a key
   between existing timing keys. A capture at exactly the same time replaces it.

Two keys define travel. Two keys at the same path position but different times
create a hold. Decreasing path positions create reverse travel. The first and last
keys determine the schedule and duration automatically. One key holds that position
for a provisional five seconds until another key defines the range.

**Ctrl+Up/Down** selects timing keys, **Shift+F6** recaptures a selected key at the
current demo time/path position, and **Ctrl+Delete** removes it. Ctrl+Enter seeks
to the selected key's demo time. Shift+F6 rejects a collision with another key.

Console equivalents:

```text
mirv_campath time add
mirv_campath time add 0.5 15
mirv_campath time select 1
mirv_campath time replace
mirv_campath time delete
mirv_campath time list
mirv_campath time clear
mirv_campath edit position 0.5
mirv_campath edit scrub 2.5
mirv_campath edit seek
```

An explicit position is normalized arc distance, 0..1, and explicit time is demo
seconds. `time list` prints seconds, corresponding demo ticks when available, and
path position. Seeking rounds to the nearest demo tick; stored timing retains
subtick precision. A selected spatial point that is not visited by the timing
track cannot be sought until it has a timing key, but it can always be visited and
captured. Selecting a spatial point visits its stored pose; scrubbing evaluates
independent orientation/FOV tracks too.

Capturing the first explicit time key replaces the older automatic point holds,
stops and explicit segment durations. Repeated positions on the new time track
express holds instead. The change is undoable. Geometry and camera animation keys
are retained. Delete/clear all timing keys to unschedule the movement and return
to automatic timing. Geometry edits preserve normalized timing positions; they
are not permanent attachments to particular spatial-point indices.

## Easing and drawing

**F8** cycles linear, in, out, smooth and smoother easing. Console:

```text
mirv_campath timing ease smooth
mirv_campath draw timing 1
mirv_campath draw easing 1
mirv_campath draw keyIndex 40
```

Orange crosses mark timing keys; the selected key is yellow. Labels show key
index, demo seconds and ticks. Repeated positions stack their labels. White marks
on the selected path sample equal amounts of time: close spacing means slow
travel, wide spacing means fast travel. Their label states the easing and time
interval. Holds produce coincident marks; reversals revisit earlier positions.
This samples the actual evaluator, including custom speed and older motion tracks.

Timing-key easing applies between adjacent keys. For example, smooth easing slows
at each key; use linear for continuous travel through intermediate timing keys.
Custom easing uses the existing monotonic cumulative-progress speed track.

Labels default to 32 pixels. `draw keyIndex <pixels>` adjusts them (0..200; zero
hides text). Name, index, timing and speed labels share the existing campath font
texture and drawing pass, with separate text rows to reduce overprinting.
The padded antialiased atlas and explicit sampler/render states are retained.
ASCII is drawn; XML preserves UTF-8 names, with replacement glyphs for non-ASCII.

`draw enabled 0|1` is the master switch. Layers are `paths`, `cameras`, `labels`,
`handles`, `constraints`, `direction`, `timing`, and `easing`; `draw all 0|1`
toggles them together. Drawing runs during editing, is visible through walls, and
appears in recordings. Disable drawing or leave editing for clean footage.
Immutable snapshots use `CCampathDrawer`'s existing render queue, device lifetime,
shaders, line buffer, frustums and label projection. No new renderer is introduced.

## Editor shortcuts

| Key | Action |
| --- | --- |
| Insert / Shift+Insert / Ctrl+Insert | Append / replace / insert before selected point |
| Delete | Remove selected spatial point |
| Up / Down; Home / End | Previous / next; first / last spatial point |
| Left / Right; Shift+Left / Right; Tab | Scrub time; move along path distance; visit cursor |
| F6 / Shift+F6 | Add / recapture a timing key |
| Ctrl+Up / Ctrl+Down; Ctrl+Delete | Previous / next timing key; delete it |
| Ctrl+Enter | Seek demo to selected path position/time key |
| [ / ] | Capture incoming / outgoing Bezier handle at the free camera |
| Ctrl+B; F8 | Cycle curve type; cycle easing |
| F9 / Shift+F9 / Ctrl+F9 | Create orbit / line / rectangle at free camera |
| F10 | Place selected path anchor at free camera pose |
| Ctrl+N / Ctrl+D | New path / duplicate path |
| PageUp / PageDown | Previous / next path |
| Ctrl+R / Ctrl+F | Capture independent rotation / FOV key at cursor time |
| F4 / Ctrl+P | Toggle drawing / scheduled playback enable |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Shift+F2 | Open Source's native demo UI |
| Shift+Escape | Exit editing through the existing free-camera handler |

Handle capture automatically selects Bezier interpolation. PageUp/PageDown now
select paths; numpad 7/1 still adjust FOV. Up/Down, Home/End and arrows are editor
navigation keys throughout editing; numpad movement/view controls remain available.

```text
mirv_campath edit bind
mirv_campath edit bind timeAdd F7
mirv_campath edit bind anchor none
mirv_campath edit bind reset
mirv_campath edit command ctrl+L "mirv_campath curve scale 2"
mirv_campath edit command ctrl+L none
```

Built-in bindings accept ctrl/shift/alt chords, letters, digits, F1..F12, named
navigation keys, Enter, brackets, or `vk<number>`. Duplicate chords and menu-key
conflicts are rejected. Unbind an occupied action before reusing its chord.
`edit command` binds any quoted engine console command, including existing HLAE
commands such as `mirv_aim`; it uses the existing console executor once per press. This also gives
keyboard access to parameterized operations, saving/loading and cfg macros without
adding separate implementations. Bindings last for the session; store preferred
commands in a cfg. Up to 64 custom commands of 4096 characters are supported.

The editor extends shared `MirvInput` keyboard ownership, including normal, raw,
character, repeat and release events, while retaining its camera controls. Escape,
the console/grave key, Alt+Tab and Alt+F4 remain available. Console, Source menus,
demo UI and active HLAE GUI take priority. UI-cursor suspension is temporarily
enabled and restored on exit. Focus/menu handoffs release held navigation.

For a differently bound console/menu key, use `edit passthrough <key> 1`.
Passthrough deliberately permits Source binds for that key. While a menu is
visible, all its normal input is allowed. Windows/in-game verification of the
keyboard handoffs remains necessary.

## Shapes and placement

```text
mirv_campath curve create
mirv_campath curve create orbit
mirv_campath curve create arc 256 xz 180
mirv_campath curve create line 512
mirv_campath curve create rectangle 256 128
```

Each shape uses the current camera pose as placement; coordinates are unnecessary.
The orbit center is the camera position, with default radius 128, local XY plane,
360-degree sweep, start angle 0 and nine visible points. Arc defaults to a
180-degree sweep. Optional orbit/arc arguments are radius, plane (`xy`, `xz`, `yz`),
sweep, start angle and point count. The anchor's camera orientation rotates the
local shape plane. Line extends forward; rectangle closes in the local XY plane.
A shape fills an empty selected path or creates another path if it already has
geometry. Creation is one undoable operation and retains the current free camera.

Circles remain analytic. Use Ctrl+B or `curve type linear|smooth|bezier` to convert
to editable points. Smooth interpolation uses the existing cardinal curve, with
`curve tension 0..1`. Bezier handles are offsets from their spatial point;
`curve handle in|out capture` uses the camera as endpoint. Grid/axis constraints
apply to captures and handles. `curve constrain` applies them to existing points.

Placement commands remain `curve anchor capture`, `curve translate <x y z>`,
`curve rotate <pitch yaw roll>`, and `curve scale <absolute factor>`. Placement
moves local geometry, handles and orientation together without rewriting timing.

## Playback, paths and independent animation

```text
mirv_campath enabled 1
mirv_campath edit off
```

Enabled scheduled paths follow demo time automatically, including seeks in either
direction. Editing temporarily suspends playback. Its enable flag persists through
editing and system switches. `enabled 0` disables it; `edit status` reports state.
Timing capture schedules the path but does not change the global enable flag.

`path list`, `path use <index|name>`, `path new [name]`, `path rename <name>`,
`path duplicate`, and `path remove confirm` manage movements. New/duplicate paths
are unscheduled. Latest start wins overlaps; stable ID breaks ties. `timing overlaps`
lists them. `timing finish release|hold` controls the end; `timing transition <s>`
blends from the preceding movement during the incoming duration. Gaps release to
the current native demo camera; older overlapping movements do not resume.

Rotation and FOV keys use normalized movement time 0..1:

```text
mirv_campath rotation set 0
mirv_campath rotation set 1 0 90 0
mirv_campath fov set 1 50
```

Ctrl+R/F capture at the editor's cursor time. Rotation/FOV tracks support
`set`, `delete`, `clear` and `interp linear|smooth|step`. Rotation captures world
angles and stores local quaternions so placement follows. A single spatial point
with independent animation produces a stationary movement. Use the existing
`mirv_aim` for aiming; no new aim command is provided.

Advanced automatic timing remains for paths without explicit time keys:
`timing duration`, `segment`, `hold`, `stop`, `finish`, `schedule`, `ease`,
`speed`, `motion`, `holdMark`, and `transition`. Holds are included in total duration;
automatic segment time is allocated by arc length. `motion` maps normalized time
to normalized path distance and permits reversals. Older motion tracks remain
linear regardless of segment easing; new time keys opt into easing explicitly.
These advanced controls are not required to capture timing keys. Custom progress
tracks conflict with nonzero automatic holds/segments. Arc-length evaluation uses
128 samples per segment (exact angular distance for circles).

## Files and compatibility

```text
mirv_campath save Movement
mirv_campath saveAll Collection
mirv_campath load Collection
mirv_campath load Collection replace discard
mirv_campath exportLegacy BakedMovement
```

Files default to `.xml` in the game's current working directory; quoted absolute
paths are supported. Saving refuses overwrite unless `overwrite` is supplied.
Loading defaults to append, remaps IDs, and validates before committing. Replace
requires `discard` for unsaved work. Undo/redo covers data changes, imports and
placement, with 64 entries. Selection and file writes do not create data edits.
Writes use a sibling temporary file and atomic rename. Only saveAll marks the
whole collection saved. Timing keys reuse the progress track and an optional XML
easing flag; earlier new-system files remain readable.

Classic is initially selected. `system classic/new` switches command trees and
retains each system's data and enable state. Classic commands, files and preview
are unchanged. New-system `preview`, `play`, `stop`, `start`, `end`, `mark`, and
`aim` no longer perform operations; they print migration guidance. Use edit
navigation, enabled, time keys and native mirv_aim instead. `curve orbit` moves to
`curve create orbit`; the old flat, coordinate-explicit `orbit` spelling remains
available for old cfgs. Other flat aliases and the `cinematic` prefix remain for
compatibility. Old fixed-target XML is readable for compatibility; no duplicate
fixed-target editor control is exposed.

Classic XML import uses linear geometry with independent linear time tracks;
classic cubic interpolation is not reproduced exactly. `exportLegacy` samples up
to 60 Hz, capped at 20,000 intervals, and is lossy. Keep the editable new XML for
reuse. Load a movement, place its anchor, and capture timing keys in the next demo.

## Review notes and validation

The November 5 testing feedback is recorded in this revision: insertion between
spatial points already existed but needed discoverability; timing capture becomes
independent visible keys; navigation and seeking belong to one editor; aiming
reuses mirv_aim; easing is visualized; shape creation captures camera placement;
playback uses one enable control; more operations have default/custom editor binds.
Future work should reduce steps without adding duplicate HLAE infrastructure.

Runtime model code remains in `shared/CinematicPath.h/.cpp`; commands and editor
callbacks stay in MirvCommands; view state stays in Hook_VClient_RenderView;
drawing stays in CCampathDrawer; input stays in shared MirvInput. Math and XML use
HLAE's AfxMath and repository-pinned RapidXML. No new files are required. Changes
and pushes are restricted to `feature/cinematic-campath`.

Build HLAE on Windows using BUILDING.md. Standalone tests:

```text
cmake -S tests/cinematic -B build/cinematic-tests -A x64
cmake --build build/cinematic-tests --config Debug
ctest --test-dir build/cinematic-tests -C Debug --output-on-failure
```

Use a separate build directory with `-A Win32` for x86. Linux omits `-A x64`.
The portable target has 889 checks covering timing, geometry, persistence, validation
and schedules, including independent timing-key edits, easing, holds, inverse
seeking, XML round trips, undo/redo and preservation of earlier motion tracks.
Windows/MSVC also builds the shared MirvInput keyboard tests. Linux sanitizer
checks use address/undefined sanitizers; LeakSanitizer alone is disabled where the
host's tracing/sandbox prevents it. Cross-compiler command/renderer checks use
external compatibility shims for upstream MSVC headers and generated shader
combo declarations; they do not run a game or shaders.

Remaining Windows/in-game checks:

- Compile both supported architectures with MSVC and run CTest.
- Capture/insert/delete spatial points, navigate and scrub without a mode switch,
  seek with Ctrl+Enter, then move freely and capture handles.
- Capture timing keys out of order and between existing keys; exercise holds,
  reversals, recapture, deletion, undo, XML reuse and automatic playback.
- Check timing ticks against the demo UI, easing marks against actual camera
  speed, label rows/readability/clipping and device resets at several resolutions.
- Exercise keyboard ownership with bound gameplay keys, custom commands, menus,
  console, demo UI, focus loss, held keys and system switches.
- Test shape generation at translated/rotated camera poses, all draw layers,
  independent tracks and native mirv_aim. Verify classic behavior afterward.

Limits remain 128 paths, 4096 spatial points per path, 16,384 points per collection,
16,384 keys per track, 65,536 keys per collection, 16 MiB XML and 24 hours per
movement. Invalid numbers, malformed XML, unsupported versions and external
entities are rejected before replacing saved state.
