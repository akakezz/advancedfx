# Campath curve editing, holds and named paths

This Source 1 feature extends ordinary `mirv_campath`. There is one command tree,
one timestamped keyframe model and the existing camera override stage. There is
no system switch or separate timing track. Keyframe indices are zero based and
use the classic digit texture, placement and world-unit height.

## Capture and automatic playback

At each desired demo time and camera pose, run `mirv_campath add`. This captures
position, rotation, FOV and current time. Seeking between existing timestamps and
adding inserts a keyframe in chronological order. Adding at an existing timestamp
replaces the pose and preserves its handles, hold and segment easing.

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
move the camera without seeking the demo. While a scrub key is held, free-camera
keyboard/mouse translation, rotation and FOV changes cannot move the camera off
the path. Starting and stopping a scrub clear pending camera movement; releasing
the scrub key restores free-camera editing. Capture still uses the actual demo time.

| Key | Action |
| --- | --- |
| E | Capture pose and current demo time |
| Shift+E | Replace selected pose, retaining its timestamp, handles and hold |
| Delete | Delete selected keyframe |
| Up / Down | Visit and select previous / next keyframe |
| Home / End | Visit and select first / last keyframe |
| Left / Right | Scrub interpolated movement in time |
| Tab | Return to the preview cursor |
| Right Ctrl | Seek the demo to the preview cursor; timescale 0 on arrival by default |
| [ / ] | Capture incoming / outgoing handle at the free camera |
| Ctrl+Shift+B | Reset selected keyframe handles |
| Ctrl+B | Cycle linear, smooth, monotone, step and Bezier interpolation |
| Y / Shift+Y | Capture the selected keyframe's release tick / clear its hold |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo a data edit |
| PageUp / PageDown | Select previous / next named path |
| Ctrl+N | Create and select a new path |
| F4 | Toggle drawing |
| F7 | Open/close the easing graph |
| Ctrl+P | Toggle master playback enable |
| Ctrl+A | Select all keyframes using native selection |
| Ctrl+Shift+A | Anchor the whole path: selected keyframe to the current free-camera pose |
| Shift+Escape | End freecam and preview |

Console equivalents include `preview <index>`, `preview interp <0..1>`,
`preview seek`, `preview undo` and `preview redo`. `preview exit` retains the free
camera pose; `mirv_input end` releases it. Undo/redo keep the current viewport and
restore keyframes, handles, holds, interpolation, easing, names and path membership.
History keeps the last 64 data edits, including loads and native timing/placement
commands. Navigation, selection, saving and drawing settings do not create edits.
A new data edit clears redo. The master playback switch is outside undo history.

For anchoring, select one reference keyframe, fly to the desired camera pose, then
press Ctrl+Shift+A. This runs native `select all` and `edit anchor #<index> current`
and restores the selection. Anchoring is one undoable edit.

Seeking always lets Source reach the target before applying the chosen behavior:

```text
mirv_campath preview seekMode timescale 0
mirv_campath preview seekMode timescale 0.25
mirv_campath preview seekMode pause
mirv_campath preview seekMode play
```

The session default is `timescale 0`: the demo is resumed at zero speed after
seeking. `pause` uses native demo pause at normal speed; `play` resumes at normal
speed. Timescale accepts values in 0..1000. The same behavior applies to Right
Ctrl and `preview seek`. Failed seeks restore the prior pause state and timescale.

A small top-left **Campath preview | path name** indicator appears during preview.
Use `mirv_campath preview hud 0` to hide it, or `1` to restore it. It accepts no
mouse or keyboard input and hides while native menus/console own the UI. Preview
bindings/settings, including seek behavior and the indicator, are session options;
put their commands in a cfg to retain them.

Run `preview bind` to list actions. Bindings and existing console commands can be
assigned to chords; session settings can be restored through a cfg:

```text
mirv_campath preview bind holdCapture ctrl+F6
mirv_campath preview bind reset
mirv_campath preview command ctrl+L "mirv_campath edit duration 10"
mirv_campath preview command ctrl+L none
```

Shared `MirvInput` captures ordinary and raw keyboard input during preview.
Console, menus, demo UI and the HLAE GUI retain priority. Escape, grave, F2,
Shift, Alt+Tab and Alt+F4 remain available. Both Shift and F2 reach Source so its
native Shift+F2 shortcut can open the demo UI during preview or graph editing.
Preview still owns the main keys of Shift combinations such as Shift+E and
Shift+Y; their bindings do not need to change. Shift itself is a UI passthrough
modifier, so a Source bind on Shift can also run. `preview passthrough <key> 1`
allows a custom menu or console binding through to Source. Input ownership and held navigation are
released when focus or menus change.

```text
mirv_campath preview allowSource 1
```

This optional mode forwards keys/chords unused by preview and freecam to Source,
so existing demo/game binds remain usable. Built-in and custom preview shortcuts,
free-camera movement and camera speed keys keep priority. A press keeps its input
owner until release, including when modifiers or this setting change. The default
is `0`, which keeps exclusive preview keyboard capture. Explicit UI passthrough
keys still take priority in either mode.

## Curve editing and interpolation

```text
mirv_campath select #1 #1
mirv_campath curve handle out capture
```

Select one keyframe, fly to its desired control-handle endpoint, then capture.
The keyboard alternative is Down/Up followed by `[` or `]`. Handles change only
the position curve and stay attached during native retiming and placement.
Incoming/outgoing offsets are world coordinates relative to the keyframe.

New keyframes use **aligned** spatial handles: moving one rotates the opposite
handle onto the same line, pointing the other way, while preserving the opposite
handle's length. This keeps a continuous tangent direction through the keyframe.
It does not force equal lengths or equal speed across the join. Collapsing a handle
to zero leaves its direction undefined until it is moved away from the keyframe.

```text
mirv_campath curve handle mode aligned
mirv_campath curve handle mode free
```

These commands affect selected keyframes and participate in undo/redo. Enabling
alignment on an existing corner uses its outgoing direction when available,
retaining both handle lengths. Free mode allows independent handles for deliberate
corners. Save/load and pose recapture preserve the mode. Older files with authored
handles and no mode keep those handles free to preserve their original curves;
select those keys and apply `aligned` to convert them. This concerns spatial
handles; the graph's time/value easing handles remain independent.

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

## Easing graph and segment presets

```text
mirv_campath graph
```

This toggles the graph; `graph 0|1` sets it explicitly. F7 does the same in preview.
Close the console to use it. The graph uses HLAE's existing ImGui/Direct3D9 backend
and works without entering preview. Native console, menus and demo UI retain
input priority. While the graph is open, preview/freecam retain their modes and
camera pose, but their keyboard and mouse controls pause. The pointer becomes
visible and free to move throughout the graph, including before its first hover
or click. F7 in preview, Escape, or the window close button closes the graph and
restores the camera controls. Held movement and mouse deltas are cleared at each
handoff. Opening native menus or changing window focus temporarily yields input
without closing the graph.

The horizontal axis is path time in seconds. The vertical axis is progress from
one camera key to the next (indices 0, 1, 2, ...). Orange bars show stationary
holds; the plotted curves include automatic hold easing. These are timing curves;
3D position handles remain editable in the viewport.

- Click a camera key or a segment's square to select its outgoing segment.
- Drag empty canvas space to box select several keys/segments; Shift adds.
- Right-click any segment curve to choose its preset or in/out/inout direction.
  Clicking inside an existing multi-selection preserves it; clicking an unselected
  segment selects that segment. The menu also resets preset parameters.
- Choose **Selected segments** or **Entire path** in Scope. Preset, direction and
  parameter changes apply live; there is no separate Apply step.
- With Bezier easing selected, drag its purple time/value handles directly.
- Middle-drag pans; the mouse wheel zooms around the cursor; Fit restores bounds.
- Undo/Redo buttons and Ctrl+Z / Ctrl+Shift+Z use the same history as preview.
  One handle or parameter-slider drag creates one history entry. Selecting points
  or moving the graph view does not.

Presets include linear, sine, quadratic, cubic, quartic, quintic, exponential,
circular, back, bounce and elastic, with in/out/inout directions. Back exposes
overshoot; elastic exposes amplitude and period. Moving these sliders updates
the actual path and graph immediately, before releasing the mouse. Custom cubic
Bezier handles
provide a freely editable curve with ordered time coordinates in 0..1 and value
coordinates in -5..5. Back, elastic and Bezier can overshoot or reverse progress.
Overshoot continues the current segment's endpoint tangent, without switching to
an adjacent segment. Position, rotation and FOV share the timing curve.

Console equivalents:

```text
mirv_campath edit ease all sine inout
mirv_campath select #1 #2
mirv_campath edit ease selected bounce out
mirv_campath edit ease selected back out 1.7
mirv_campath edit ease selected elastic out 1 0.3
mirv_campath edit ease selected bezier 0.3 0 0.7 1
mirv_campath edit ease selected inherit
```

An outgoing override belongs to its starting keyframe. Selecting keys 1 and 2
changes segments 1→2 and 2→3. The final key has no outgoing segment. `inherit`
uses the path's default; setting the entire path clears segment overrides.
Applying `inherit` to the whole path restores native timing plus automatic smooth
holds. This timing easing is independent of spatial `edit interp`/curve modes.

## Camera holds

Select the arrival keyframe, advance the demo to the desired release tick, then
press Y to capture its release tick (Shift+Y clears the hold), or use:

```text
mirv_campath edit hold capture
mirv_campath edit hold tick 1200
mirv_campath edit hold off
```

A hold preserves the full pose, including rotation and FOV, through its release
tick. After release, travel uses the remaining interval to reach the next
keyframe at its existing arrival time. Smooth interpolation automatically slows
to zero at arrival and accelerates from zero after release. Linear/step retain
their deliberate sharp transitions when every channel uses those modes. Custom
Bezier timing automatically flattens held endpoint tangents while retaining their
time-handle controls. Release must precede that next arrival;
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

Classic XML remains supported. Handles, handle mode, hold durations, easing and names are optional
attributes; numbers retain enough precision to round-trip. Older HLAE builds can
read the basic keys but ignore these extensions. The earlier experimental
`cinematicCamera` schema remains unsupported; retain those files and use the old
build's `exportLegacy` command if conversion is needed.

## Drawing and implementation

Native trajectory, camera, axis and index drawing run through `CCampathDrawer`
for every path. Indices appear on the selected path using the classic methods;
`draw keyIndex <height>` retains its original world-unit meaning. Path names and
hold annotations reuse the drawer's padded alphabet atlas and text pass;
`draw textSize <world height>` controls them (default 20, zero hides them).
Annotations keep a fixed world height and margin above their camera marker, face
the view like indices, and use depth testing/alpha testing so walls occlude them.
Overlapping keyframe indices use the entity-index drawer's shared stacking
layout, with their native digit texture, world size and depth testing. Nearby
world anchors stack upward in keyframe order; distant anchors remain independent
even when their projections overlap. `draw stackLabels 0|1` toggles this (default
1); `draw stackWorldDistance <units>` sets the nearby threshold (default 32).
Path/hold annotations also stack at coincident anchors. Their size changes with
distance, like classic indices. `draw handles 0|1` controls handle lines in the
same native line pass.
Turn drawing off before clean recording.

No new runtime files are introduced. `CamPath` holds optional handles/holds/easing and
state snapshots; `CamPathCollection` in the same files owns ordinary paths and
bounded history. `AfxMath` supplies local interpolators through its existing map
interfaces. Commands delegate to `MirvCampath_ConCommand`, and preview extends the
existing free-camera flow. Rendering queues immutable native snapshots using the
existing device lifecycle. The previously disabled GUI scaffold is enabled with
a SHA-256-pinned Dear ImGui 1.86 dependency fetched automatically by CMake. Its
license is installed in `bin/licenses/imgui.txt`. The graph uses the existing
GUI render cache, input hooks and graphics-reset lifecycle; sample/demo windows
are removed. No patch or manual dependency copy is needed before compiling.

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
