# Classic campath capture with curve editing

`mirv_campath system new` uses HLAE's existing timestamped `CamPath`, console
commands, playback stage and renderer. Its addition is editable position-curve
handles. It has no separate timing keys, path collection or playback engine.

## Capture and play

```text
mirv_campath system new
```

At each desired demo time and camera pose, run `mirv_campath add`. This captures
position, orientation, FOV **and current time**, exactly as classic HLAE does.
Adding at an earlier time inserts a keyframe in time order. Adding at an existing
time replaces that keyframe. Default cubic interpolation needs four keyframes,
just like classic. All native interpolation requirements still apply.

```text
mirv_campath enabled 1
```

Playback follows demo time automatically, including seeking backward and forward.
There is no play command and no requirement to enter editing. Native `hold`,
`offset`, `select`, `remove`, `clear`, `print`, `edit start`, `edit duration`,
`edit position`, `edit angles`, `edit fov`, `edit rotate`, `edit anchor` and
`edit interp` retain their classic meanings.

`system classic` switches back to classic. Each system retains its own path and
enable state. Drawing settings are shared. Curve editing is opt-in to the new
path; classic, GoldSrc and Source 2 evaluation keep their native interpolators.

## Shape the curve

```text
mirv_campath select #1
mirv_campath curve handle out capture
```

Select one keyframe, fly to the desired control-handle endpoint, and capture it.
The handle changes the position curve, retaining recorded times, orientation and
FOV. Incoming/outgoing offsets are stored in world coordinates relative to the
keyframe. Handles follow native rotation/anchor placement and stay attached when
native timing edits move the keyframe.

```text
mirv_campath curve type bezier
mirv_campath curve handle in -32 0 0
mirv_campath curve handle out 32 64 0
mirv_campath curve reset
mirv_campath curve type default
```

`curve type` accepts `default`, `linear`, `smooth` (native cubic), and `bezier`.
Capturing a handle automatically enables Bezier editing. Unedited segments use
the original position interpolator exactly. Unset handles on an edited segment
are derived from its native tangent; explicit handles define cubic Bezier control
points. `curve reset` restores the selected keyframe's automatic handles. Choosing
a native curve type temporarily ignores stored handles, which remain available
when Bezier mode is re-enabled. Rotation and FOV always use their native time
interpolators.

## Optional keyboard editing

```text
mirv_campath edit on
```

Editing enables the existing free camera and drawing. It is one keyboard-editing
session, with no preview mode to switch into. Playback enable state is retained;
freecam controls the camera until `edit off` releases it, as with ordinary classic
freecam. Moving freely after visiting a keyframe keeps it selected for handles.

| Key | Action |
| --- | --- |
| Insert | Capture current pose **and demo time** with native `add` |
| Shift+Insert | Replace selected pose at its existing time, preserving handles |
| Delete | Remove the selected camera keyframe |
| Up / Down | Visit previous / next keyframe |
| Home / End | Visit first / last keyframe |
| Left / Right | Scrub the existing path in time |
| Tab | Revisit the editor cursor |
| Ctrl+Enter | Seek the demo to the editor cursor |
| [ / ] | Capture incoming / outgoing handle at the camera |
| Ctrl+Shift+B | Reset selected keyframe handles |
| Ctrl+B | Cycle position curve mode |
| F4 | Toggle drawing |
| Ctrl+P | Toggle native campath enable |
| Shift+F2 | Open Source's native demo UI |
| Shift+Escape | Exit freecam/editing |

Console navigation equivalents are `edit goto <index>` and `edit seek`. Indices
are zero based; ordinary native selection remains `select #<index>`. Select one
keyframe before capturing/resetting its handles. Inserting between points needs
no extra operation: seek the demo between their timestamps and press Insert.

Bindings remain configurable through `edit bind <action> <chord|none>`. Run
`edit bind` to list them, or `edit bind reset` to restore defaults. `edit command`
can assign a one-shot existing console command or cfg macro to another chord:

```text
mirv_campath edit command ctrl+L "mirv_campath edit duration 10"
mirv_campath edit command ctrl+L none
```

Duplicate bindings and reserved UI/OS chords are rejected. Chords accept
ctrl/shift/alt, letters, digits, F1..F12, Enter/navigation keys, brackets or
`vk<number>`. Settings last for the session; use a cfg to restore preferences.
There are no separate undo, animation-track, shape-generator or scheduling
shortcuts in this classic workflow. Use the existing `mirv_aim` for aiming.

Shared `MirvInput` owns normal/raw keyboard events during editing. Console,
menus, demo UI and the active HLAE GUI retain priority; Escape, grave, Alt+Tab and
Alt+F4 remain available. For a custom console/menu key, use
`edit passthrough <key> 1`, deliberately allowing its Source binding. Focus/menu
handoffs release held scrubbing. The existing UI-cursor suspension setting is
enabled temporarily and restored on exit.

## Drawing and files

`draw enabled 0|1`, `draw keyAxis`, and `draw keyCam` use the existing renderer.
`draw handles 0|1` controls handles; `draw keyIndex <pixels>` adjusts new-system
labels (default 32, zero hides them). Text has a screen-space margin above and to
the left of camera markers. Only keyframe indices are shown; experimental path
names, timing banners, speed annotations and direction arrows are gone. Classic
index-label height retains its original world-unit meaning.

Drawing works outside editing when enabled and appears in recordings. Turn it
off before clean capture. Classic and new drawing use the same immutable path
snapshots, trajectory/frustum code, render queue and graphics-device lifecycle.
The existing padded antialiased label atlas is retained for new-system indices.

```text
mirv_campath save "Movement.xml"
mirv_campath load "Movement.xml"
```

These are native classic XML commands, including their ordinary overwrite and
filename behavior; supply `.xml` explicitly. Bezier mode and handle offsets are
optional XML attributes. Unextended classic files load normally. This branch's
classic system preserves optional handles when saving but ignores them during
playback. Older HLAE versions can read the basic keyframes but do not understand
curve handles. Failed or unsupported loads preserve the existing path.

The previous experimental **collection XML is retired**, rather than silently
converted. Before replacing the previous test DLL, export any movements that you
need with that build's `mirv_campath exportLegacy "Movement.xml"`, then load the
classic file here. That export is sampled and lossy; keep the original collection
XML. No on-disk user files are modified by updating the branch.

## Architecture and verification

The separate `shared/CinematicPath.h/.cpp` model, progress/timing tracks and
collection controller are removed. The selected system owns an ordinary CamPath.
Normal commands call `MirvCampath_ConCommand`; the same view-override stage handles
both systems. Curve editing adds optional position handles to CamPath, using
AfxMath vectors and native RapidXML persistence. Recording autosave and BVH import
operate on the selected path. Changes/pushes remain restricted to
`feature/cinematic-campath`.

402 checks now exercise the actual shared CamPath rather than the retired model. They
cover native capture/replacement/order, enable/offset/hold, retiming, interpolation
parity, handles, native rotations/placement, XML round trips and rejected imports.
Sanitizer runs exposed existing scalar deletes of spline arrays; those are
corrected to `delete[]`. Const iterator references and test-only generated CRT
compatibility shims allow the same shared source to be checked on Linux.

Build HLAE on Windows using BUILDING.md. Standalone tests:

```text
cmake -S tests/cinematic -B build/cinematic-tests -A x64
cmake --build build/cinematic-tests --config Debug
ctest --test-dir build/cinematic-tests -C Debug --output-on-failure
```

Use a separate directory with `-A Win32` for x86; omit `-A` on Linux. Windows/MSVC
also builds the real shared MirvInput normal/raw keyboard tests. Cross-compiler
checks cover command integration, CamPath and the full renderer on x86/x64 using
external shims for MSVC-only headers and generated shader declarations. They do
not establish a complete MSVC build or in-game rendering/input correctness.

Windows/in-game review still needs capture at four demo times, capture between
existing times, automatic enabled playback without editing, both native and
Bezier curves, handle capture/reset, native retiming/selection/placement, saving
and reloading, recording autosave, system switches, keyboard/menu/focus handoffs,
label spacing at multiple resolutions, graphics resets and classic regression.
