# Campath curve editing, holds and named paths

This Source 1 feature extends ordinary `mirv_campath`. There is one command tree,
one timestamped keyframe model and the existing camera override stage. There is
no system switch or separate timing track. Keyframe indices are zero based and
use the classic digit texture, placement and world-unit height.

## Capture and automatic playback

At each desired demo time and camera pose, run `mirv_campath add`. This captures
position, rotation, FOV and current time. Seeking between existing timestamps and
adding inserts a keyframe in chronological order. Adding at an existing timestamp
replaces the pose and preserves its handles and hold.

```text
mirv_campath draw enabled 1
mirv_campath enabled 1
```

Fresh paths use local smooth position interpolation with linear FOV and spherical
linear rotation: two keyframes are enough to draw and play. Loaded classic files
keep their saved interpolation, including the four-key requirement of classic
cubic. Playback follows demo time automatically and needs no preview session.
Freecam takes priority while active; use `mirv_input end` to return to playback.
There are no separate play/stop commands.

Native `offset`, `hold`, selection, removal, placement, rotation, anchoring and
retiming remain available. These commands operate on the currently selected path.
The master `enabled` switch controls playback of all paths.

## Preview and keyboard editing

```text
mirv_campath preview enter
```

Preview enables the existing free camera, drawing and keyboard capture. It also
works with an empty path. Visit a keyframe, then fly freely to capture a handle or
replacement pose; the selected keyframe stays selected. Scrubbing and visiting
move the camera without seeking the demo. Capture still uses the actual demo time.

| Key | Action |
| --- | --- |
| Insert | Capture pose and current demo time |
| Shift+Insert | Replace selected pose, retaining its timestamp, handles and hold |
| Delete | Delete selected keyframe |
| Up / Down | Visit and select previous / next keyframe |
| Home / End | Visit and select first / last keyframe |
| Left / Right | Scrub interpolated movement in time |
| Tab | Return to the preview cursor |
| Ctrl+Enter | Seek the demo to the preview cursor |
| [ / ] | Capture incoming / outgoing handle at the free camera |
| Ctrl+Shift+B | Reset selected keyframe handles |
| Ctrl+B | Cycle linear, smooth, monotone, step and Bezier interpolation |
| F6 / Shift+F6 | Capture / clear selected keyframe hold release |
| Ctrl+Z / Ctrl+Y | Undo / redo a data edit |
| PageUp / PageDown | Select previous / next named path |
| Ctrl+N | Create and select a new path |
| F4 | Toggle drawing |
| Ctrl+P | Toggle master playback enable |
| Shift+F2 | Open the native demo UI |
| Shift+Escape | End freecam and preview |

Console equivalents include `preview <index>`, `preview interp <0..1>`,
`preview seek`, `preview undo` and `preview redo`. `preview exit` retains the free
camera pose; `mirv_input end` releases it. Undo/redo keep the current viewport and
restore keyframes, handles, holds, interpolation, names and path membership.
History keeps the last 64 data edits, including loads and native timing/placement
commands. Navigation, selection, saving and drawing settings do not create edits.
A new data edit clears redo. The master playback switch is outside undo history.

Run `preview bind` to list actions. Bindings and existing console commands can be
assigned to chords; session settings can be restored through a cfg:

```text
mirv_campath preview bind holdCapture ctrl+F6
mirv_campath preview bind reset
mirv_campath preview command ctrl+L "mirv_campath edit duration 10"
mirv_campath preview command ctrl+L none
```

Shared `MirvInput` captures ordinary and raw keyboard input during preview.
Console, menus, demo UI and the HLAE GUI retain priority. Escape, grave, Alt+Tab
and Alt+F4 remain available. `preview passthrough <key> 1` allows a custom menu or
console binding through to Source. Input ownership and held navigation are
released when focus or menus change.

## Curve editing and interpolation

```text
mirv_campath select #1 #1
mirv_campath curve handle out capture
```

Select one keyframe, fly to its desired control-handle endpoint, then capture.
The keyboard alternative is Down/Up followed by `[` or `]`. Handles change only
the position curve and stay attached during native retiming and placement.
Incoming/outgoing offsets are world coordinates relative to the keyframe.

```text
mirv_campath curve handle in -32 0 0
mirv_campath curve handle out 32 64 0
mirv_campath curve reset
mirv_campath edit interp all smooth
```

`edit interp all linear|smooth|monotone|step|cubic|bezier` chooses compatible
position, rotation and FOV interpolation together. Smooth is local Hermite
interpolation; monotone constrains each coordinate and FOV to avoid overshoot;
step holds one pose until the next keyframe. Smooth, monotone, linear and Bezier
work with two keys. Step supports a single stationary key. Classic cubic remains
available with its original four-key requirement.

Native `edit interp position|rotation|fov` still controls individual channels.
Position/FOV also accept `smooth`, `monotone` and `step`; rotation adds `sSmooth`
and `sStep`. `curve type` changes position alone and accepts `default`, `linear`,
`smooth`, `monotone`, `step`, `cubic` or `bezier`.

Capturing a handle enables Bezier evaluation. Only segments with an explicit
handle change; other segments retain their chosen native interpolator. Unset
handles on an edited segment follow native tangents. `curve reset` restores
selected handles to automatic. Choosing another position interpolation ignores
stored handles until Bezier is re-enabled. Use `mirv_aim` for aiming.

## Camera holds

Select the arrival keyframe, advance the demo to the desired release tick, then
press F6 or use:

```text
mirv_campath edit hold capture
mirv_campath edit hold tick 1200
mirv_campath edit hold off
```

A hold preserves the full pose, including rotation and FOV, through its release
tick. After release, travel uses the remaining interval to reach the next
keyframe at its existing arrival time. Release must precede that next arrival;
conflicting holds, insertions and timing edits are rejected. A release equal to
arrival clears the hold. A hold on the final key extends the shot; one held key
can form a stationary shot. Native retiming moves/scales holds with their keys.

The renderer writes `hold -> tick N` above held keyframes, with a seconds fallback
when no demo is available. This is separate from native `mirv_campath hold 0|1`,
which retains its original meaning: hold the whole path before/after its range.

## Named paths and files

```text
mirv_campath path rename "Wide shot"
mirv_campath path new "Close shot"
mirv_campath path list
mirv_campath path use "Wide shot"
mirv_campath path enabled 0
mirv_campath path remove "Close shot"
```

Names are unique; unnamed creation chooses `Path N`. `use` and `remove` also accept
zero-based path indices. Keep at least one path; native `clear` empties it.
`path enabled` mutes/unmutes the selected path independently of master `enabled`.

Among enabled, evaluable paths covering the current demo time, the latest
starting path plays automatically. A later list entry wins equal start times.
The editing selection does not choose playback. When a shorter overlapping path
ends, another path still covering that time resumes. Gaps return to the demo
camera unless a path has native start/end holding enabled. Offsets and final
keyframe holds count toward these ranges.

```text
mirv_campath save "Movement.xml"
mirv_campath saveAll "Sequence.xml"
mirv_campath load "Sequence.xml"
```

`save` writes the selected path using native campath XML. `saveAll` wraps native
paths in a versioned collection, preserving names, muted paths and selection.
`load` detects either format: a single path replaces the selected path; a
collection replaces the collection. Failed loads preserve existing data. Loading
does not change the master playback switch. Recording autosave includes all
paths when more than one exists. BVH import targets the selected path and can be
undone. Supply `.xml` explicitly; saving overwrites the specified file.

Classic XML remains supported. Handles, hold durations and names are optional
attributes; numbers retain enough precision to round-trip. Older HLAE builds can
read the basic keys but ignore these extensions. The earlier experimental
`cinematicCamera` schema remains unsupported; retain those files and use the old
build's `exportLegacy` command if conversion is needed.

## Drawing and implementation

Native trajectory, camera, axis and index drawing run through `CCampathDrawer`
for every path. Indices appear on the selected path using the classic methods;
`draw keyIndex <height>` retains its original world-unit meaning. Path names and
hold annotations reuse the drawer's padded alphabet atlas and text pass;
`draw textSize <pixels>` controls them (default 20, zero hides them). Annotations
have a screen-space margin above/left of the camera markers. Coincident labels
stack. `draw handles 0|1` controls handle lines in the same native line pass.
Turn drawing off before clean recording.

No new runtime files are introduced. `CamPath` holds optional handles/holds and
state snapshots; `CamPathCollection` in the same files owns ordinary paths and
bounded history. `AfxMath` supplies local interpolators through its existing map
interfaces. Commands delegate to `MirvCampath_ConCommand`, and preview extends the
existing free-camera flow. Rendering queues immutable native snapshots using the
existing device lifecycle.

Build normally using [BUILDING.md](../BUILDING.md). Standalone checks exercise the
actual shared implementation:

```text
cmake -S tests/cinematic -B build/cinematic-tests -A x64
cmake --build build/cinematic-tests --config Debug
ctest --test-dir build/cinematic-tests -C Debug --output-on-failure
```

Use another build directory with `-A Win32` for x86; omit `-A` on Linux.
Windows/MSVC also builds the shared normal/raw keyboard tests. Linux core tests,
address/undefined-behavior sanitizers and x86/x64 cross-compilation checks support
review but do not establish a complete MSVC build or in-game correctness. Review
preview input/menu handoffs, overlapping playback, holds/releases, label spacing,
recording autosave and device resets in a Source 1 demo before relying on a shot.
