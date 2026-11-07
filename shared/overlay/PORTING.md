# Paper overlay port for TF2

This port is based on the user-supplied `paper-advancedfx-main.zip`, originating
from https://github.com/papesgit/advancedfx. The ZIP does not contain Git metadata;
its source is identified by SHA-256:
`5c19cec8a85d9aebbd2ea858adb51e36f96da3a32a6c6b0ac20e5e81def8e2a8`.

The project base is **3906a61e493038fdf7897e8366c225148ade1a56**, built on
upstream **HLAE 2.192.10**, commit **8abc2719**. It includes the
existing TF2 camera attachment, entity and preview tools. Development takes place
on `feature/cinematic-campath`; no upstream merge or change to another branch is
required. Earlier experimental per-segment easing is replaced in this branch.

## Imported and adapted

| Feature | Source and TF2 adaptation |
| --- | --- |
| Camera panel, capture, load/save | Paper's DX9 UI, native `CamPath` and XML; wide file paths and XML filters |
| Sequencer, tick navigation, selection and retiming | Paper's DX9 sequencer and bundled Neo Sequencer; TF2 tick interval and existing demo-time conversion |
| Seven-channel curve editor | Paper's custom Hermite interpolation for X/Y/Z, pitch/yaw/roll and FOV, plus Auto/Flat/Linear/Free tangents; shared Spline/Catmull-Rom/PCHIP automatic policies and path-wide tension |
| Weighted, linked curve handles | Paper's controls, with Bezier time-coordinate solving so graph and playback agree; normal drag links slopes, Alt permits independent sides |
| Translation/rotation gizmos | Paper's Source-to-ImGuizmo coordinate conversion and bundled ImGuizmo; existing TF2 view data and native commands |
| Camera FOV, roll and speed | Paper's camera panel using the existing `MirvInput` setters |
| Docking workspace | Source 2 workspace concept, adapted to a single TF2 window through the existing HLAE renderer |
| Observing and attachment controls | Source 2 observing concept, using this base's TF2 player queries, `spec_player`, `mirv_cam attach`, relative framing and bone tools |
| Recording and encoder profiles | Paper's Source 1 screen workflow; live TF2 readbacks, correct screen settings, validated FPS and valid encoder options |
| Fonts, UI scaling and directories | Paper's settings and file browser; available Windows fonts and settings persisted in the existing ImGui INI |
| Undo/redo | Bounded snapshots of native keys, tangents and path settings; one entry for a drag |

The existing HLAE GUI supplies the engine-thread UI, immutable draw-data cache,
Direct3D9 rendering and device-reset handling. Its cursor hooks also cover capture,
confinement and visibility. The Win32 backend's duplicate cursor-shape updates are
disabled so the existing ownership hooks are the sole cursor controller. TF2's
`vguimatsurface.dll` also uses these hooks, covering direct surface cursor changes.
No second ImGui context or rendering backend is added.
The overlay automatically closes when recording starts, and stale draw caches are
not rendered after closing. Other Source engines do not activate this TF2 UI.

The custom interpolator derives Auto tangents from the whole path. Scalar and
Euler channels share one automatic-tangent implementation with Spline,
nonuniform Catmull-Rom and shape-preserving PCHIP policies. A path-wide tension
modifier scales automatic derivatives, while authored manual tangents are kept.
The graph reads the evaluator's cached slopes and weights directly. Policy and
tension are included in XML and native undo snapshots. Adjacent
intervals share derivatives at ordinary keys, including unequally spaced keys.
Authored positions and timestamps still determine speed; optional distance retiming distributes travel time by arc length and preserves
spatial shape. Flat tangents and deliberate reversals can still stop movement.
Classic interpolation settings and native console commands remain available.

The sequencer reads total demo ticks through the existing engine wrapper. Both
editors support pointer-centered wheel zoom, middle-drag panning and Fit controls.
Neo Sequencer retains its overview scrollbar, and its ruler visits only visible
tick marks rather than iterating over the entire demo each frame. Native preview and the GUI use one controller and cursor. Console and GUI edits
share document history over native CamPath states; navigation is excluded. Observing reuses TF2
player/model queries for selection and named bones. Its trace picker uses a small
Source SDK 2013 `EngineTraceClient003` adapter with 32/64-bit layout checks; it
does not call the CS:GO-only `mirv_traceentity` command.

## Engine-specific features

CS2 Game State Integration, Counter-Strike radar assets, bomb/round HUDs, the CS2
event-parser integration, CS2-specific depth-of-field APIs, Direct3D11 capture,
external platform windows and Vulkan are not TF2 features in this base. Their code
is not installed as nonfunctional controls. The Source 1 recording UI uses screen
capture, not CS:GO material/matte/depth streams.

## Source and dependency notices

- Paper/HLAE: the supplied MIT license is retained as `LICENSE.paper.txt`.
- Dear ImGui: official **v1.89.9-docking**, fetched by SHA-256-pinned CMake; its
  MIT license is installed with the Source 1 build. The archive's missing ImGui
  submodule is therefore not a manual prerequisite.
- Neo Sequencer: source files from the supplied archive, retaining their Matty
  attribution and distributed with the archive's MIT notice.
- ImGuizmo: supplied sources retain the complete Cedric Guillemet MIT notice.
- ImGui File Browser: supplied header and upstream Zhuang Guan MIT notice.

Build and runtime instructions are in [the TF2 guide](../../doc/CINEMATIC_CAMERA.md).

## TF2 spectator targeting reference

The player list passes explicit `#userid` arguments, not entity indices. Valve's
public Source SDK 2013 documents this resolution in
[`UTIL_PlayerByCommandArg`](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/util_shared.cpp).
The same parser is used by
[`C_HLTVCamera::SpecPlayerByPredicate`](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/hltvcamera.cpp)
and the server's `spec_player` handler. Native commands and the original project
base are retained; no SDK source is imported into the project.

## Native path document extensions

The existing `CamPath` now supports exact subdivision of Custom/Linear curves,
per-key full-pose holds, optional position-only / whole-pose distance timing, and raw authored Euler winding.
A `CamPathCollection` in the same shared files manages named shots, savepoints and
bounded authoring history. Playback selects the latest-starting eligible shot after mute/solo filtering.
Per-shot visibility is independent; duplicates start muted. Flags round-trip in
single-path files and collections.
The editor adds key dragging/box selection, cancellable drags, exact numeric inspection, speed display,
and recovery autosaves. Single paths retain native XML; collections wrap those
paths with names and identifiers. No parallel path evaluator was introduced.

Multiple shots, names, hold releases and interval lengths are queued as immutable
properties into the existing Source 1 `CCampathDrawer`. Full text uses the existing
GUI font atlas because the classic hex texture contains only digits; projection,
depth testing, shader and device resources remain in the classic drawer. No second
world renderer or ImGui context is added. With GUI disabled, legacy numeric labels
remain available, but full-text labels need the GUI atlas.

The graph now lives in a dockable window and reports axis / cursor measurements.
Only active Custom channels expose handles; manual/baked tangents have distinct
markers and reset controls. The existing Neo Sequencer receives small read-only
range/marker and cancel APIs, used for named reference tracks, holds and playback
cut intervals without retaining pointers into inactive path data. Native / graph selection is
synchronized with active-track highlights, and selection updates no longer queue
duplicate console commands. Label widths
are bounded to leave time-axis space, and the name column scrolls vertically.

Preview seeks share one controller and apply timescale, pause or continue after
Source reaches the target. Console, keyboard and overlay capture/replace/hold
controls share the same actions and history. Right Ctrl alone seeks on release;
using it as a modifier does not. Existing HLAE anchoring is reused.

Classic export samples the real evaluator into the original linear XML format,
without branch extensions or changes to editing data. Authored winding and high
pitch captures are retained for Custom rotation; ordinary quaternion keys preserve
short-route unwrapping. Rotation subdivision protects complete turns during baking.

The classic drawer shares immutable trajectory samples across selection-only
updates and computes selection colors from the current key selection. Its existing
numeric renderer and GUI-atlas text share a bounded spatial overlap layout. Names,
hold labels and interval labels have independent visibility controls. World depth
testing and device resources remain in the same renderer. GPU / in-game behavior
still needs verification in TF2; cloud tests cover core evaluation, editor input,
portable label layout, and targeted Windows x86/x64 compilation.


## Whole-path timing and selective modifiers

The native CamPath now stores an integrated whole-path speed profile and bounded
range noise modifiers. The same Eval result drives playback, preview, graph,
classic world drawing and sampled export; EvalUnmodified exposes the base for
authoring and comparison. Noise uses a seeded smooth lattice function and local
quaternion offsets, with explicit time ranges and smooth envelopes. No mutable
per-frame random state, second evaluator subsystem or renderer was introduced.

The existing scalar/Euler editable interpolation classes can cache a global
minimum-jerk quintic curve from AfxMath. The solve uses a symmetric band matrix
and shared derivatives. Saved high-order derivatives preserve exact subdivision
and geometry under retiming. Classic scalar derivatives can be exposed as editable
handles while retaining Classic quaternion rotation. UI controls report effective
modes and distinguish preserving authored tangents from resetting Auto.

All pose editing routes use validated native map replacement. Gizmo updates are
one pose operation and retain selection; timeline edits use tick displacement
rather than quantizing all keys. Undo retains current save targets. Partial
clearing retains offsets, final-shot removal resets flags, and before-start hold
selection does not depend on an unused shot's presence. Headless checks cover the
new controls, cancellation, save/load, export and deterministic evaluation.

## Review fixes: speed editing and transforms

Speed gestures retain only profile controls and an owning path ID/revision; they
never restore a full path snapshot per frame. Closing the overlay, changing the
active shot, or an external path edit invalidates the gesture. A click alone does
not retime, time locks cover profile editing and distance retiming, and the chart
clips drawing and constrains handles to its displayed range. Regression tests
exercise interrupted drags, external capture, cancellation and locked controls.

The quintic-smoothed speed profile has an analytic integral and a bracketed
inverse, avoiding missed narrow ramps from uniform sampling. Zero-speed plateaus
map to the start/end of the corresponding travel arc. Distance retiming rejects
colliding arrival timestamps atomically and reuses affine-rescaled arc samples.
Tangent baking walks the expanded key map once rather than repeatedly searching
it. An optimized cloud CPU benchmark reduced repeated speed edits at 1,000 keys
from about 160 ms to 5 ms; this excludes UI rendering and TF2 runtime overhead.

Rotate/Anchor bake and rotate positional derivatives, including minimum-jerk
accelerations. Independently weighted scalar time curves are not closed under
arbitrary spatial rotation: incompatible varying XYZ weights are rejected before
mutation. Signed axis permutations and compatible weights preserve geometry.
Existing FOV/rotation interpolation metadata and unselected key orientations are
retained; Custom Euler orientation interpolation remains a key-pose transform.
Finite offsets are validated at setter/load/save boundaries, and FOV noise keeps
legacy out-of-range base values continuous at modifier fades.

## Graph evaluation and endpoint-hold display

Graph range sampling is shared across channels and cached by geometry revision;
it no longer reevaluates a whole-path grid for every key and channel. Evaluated
curve drawing uses one cached visible-time grid plus key, hold and modifier
boundaries, while ordinary editable cubics retain their Bezier drawing/handles.
Offscreen segments are skipped. Distance lookup uses binary search with cached
moving-time prefixes, and modifier edits retain unchanged base arc-length tables.
The native evaluator, persistence and export remain the source of camera poses.

Playback/cut spans split at the last key even when classic endpoint holding is
enabled. Held extensions are dimmed and named separately; playback semantics are
unchanged. UI labels use Disable playback / Play only marked paths while native
mute/solo commands and saved fields retain compatibility. A missing demo-info
object now safely rejects demo-time queries. Headless tests exercise these cases
and verify cache reuse, playback/sample agreement and absence of UI seek commands.

An optimized headless benchmark with 100 keys, distance timing, one noise modifier
and all seven channels visible (handles locked) reduced idle UI CPU time from
about 126 ms/frame to 1.1 ms/frame, and modifier-edit frames from 129 ms to 2.5 ms.
This excludes D3D9 rendering, world-path drawing and TF2 audio; in-game validation
is still needed for the reported audio interruptions.

## Editing consolidation, range effects and TF2 repair controls

Graph keys, speed points, handles, modifier envelopes and the gizmo share gesture
ownership/cancellation. A shortcut edit ends an older gesture before changing
camera data. The key-drag/capture data-loss case has a regression test, including
Escape and external edits with unchanged key counts. Incoming/outgoing handle
code now calls one native validated operation; the legacy tangent setters reuse
the same member tables and validated replacement. Arrival/release indices are
built with the expanded hold map, avoiding repeated linear map walks.

CamPath distinguishes positional trajectory changes from other pose changes.
The classic drawer keeps its existing rendering and Ramer–Douglas–Peucker
simplifier; a bounded adaptive native sampler replaces fixed 1,024-point
interval evaluation. Geometry edits still refresh samples, while selection,
offset and FOV changes can reuse them. Pose-clock changes invalidate evaluated
graph caches without rebuilding base spatial-distance tables.

The existing modifier structure now supports Noise, Oscillation, Smooth and
Spring. NoiseModifier remains an API alias, and old noise XML loads unchanged.
Optional parameters and normalized strength-envelope points round-trip in native
paths, collections, history and recovery; classic export bakes the evaluator.
Smooth uses forward/backward exponential passes; Spring integrates the damped
response across the whole cached input, with no camera-segment restart. Both
reuse AfxMath's monotone tangent solve and Hermite evaluation for continuous
cached output. Equivalent Euler representations reuse CamPathValue's orientation
handling. Filters use a bounded sampling grid, skip inactive scalar channels,
and allow at most eight enabled filter stages. Playback, UI graphs, world drawing
and export use the same native evaluator.

The TF2 movement control under mirv_fix playerInterpolation uses the already
registered L4D2/CSS cvar interfaces, which TF2 can expose in this HLAE base. It does
not use CS:GO's encoded ConVar layout. The console and overlay share a reversible
operation, normal engine setters, rollback on rejection and frame-stage cleanup.
It restores owned settings when playback ends and preserves subsequent manual
edits. Tests use both supported interface shapes and cover invalid inputs,
engine clamping, restoration, manual edits and another-game rejection. This is a
native interpolation control; effectiveness against a particular TF2 demo must be
checked in-game. No animation-state binary patch is claimed.

## Remaining TF2 demo investigations

- HUD player-model animation clock: [Valve issue 3685](https://github.com/ValveSoftware/Source-1-Games/issues/3685). Needs the current TF2 HUD model-panel call site and a recording fixture; the CS:GO Panorama hook does not supply that implementation.
- Persistent particles after seeking: the published TF2 Medigun code has effect-update/rebuild routes. A failing before/after-seek fixture is needed to identify the correct entities and state; clearing all particles is not a reconstruction fix.
- Missing taunt props: [Valve issue 6107](https://github.com/ValveSoftware/Source-1-Games/issues/6107) describes inventory-dependent behavior. Availability of recorded item/model data needs investigation.
- Ragdolls after seeks: the published TF2 client explicitly suppresses ragdolls created while skipping. Reconstructing their physics needs a separate runtime test; no seek behavior was changed here.
- Spectator switching: the observing action already uses spec_autodirector 0. A TF2 blockObserverTarget hook needs a reproduction that remains after that setting.
- High-framerate physics and limb-animation glitches: existing CS:GO fixes are not assumed to support TF2. Current-build signatures and TF2 fixtures are required before adding equivalent hooks.
- Fast backward scrubbing, checkpoint restoration and reverse playback remain deferred until last, as requested.

Optimized native CPU measurements against bbfb3679 at 1,000 keys reduced a full
seven-channel incoming/outgoing tangent lookup pass from about 82 ms to 0.5 ms.
The classic drawer's trajectory-evaluation workload (distance timing plus one
noise modifier) fell from about 426 ms to 13 ms with adaptive sampling. These
measurements exclude D3D9 drawing and the final simplification pass; they are not
in-game frame-time measurements.


## Idle overlay work and modifier controls

Dirty-state comparisons cache immutable snapshot identities, including selection-only
changes, without retaining obsolete snapshots. Recovery skips unchanged documents,
removes clean backups once, and defers periodic saves during mouse drags. Saving
changed content still uses the existing synchronous XML writer; background threads
were not introduced into the DLL lifecycle.

The graph culls offscreen key hit targets and bounds modifier range lanes. A visible
stack reflects native processing order, with matching graph numbers/colors. Batch
field adjustments use the same native modifier validation and authoring history;
a rejected field rejects the complete batch.

An optimized headless idle test against eb598ba7 (seven channels, 20x time zoom,
selection changed after save, 30 frames) measured median CPU frame times of about
0.53 -> 0.13 ms at 100 keys, 5.21 -> 1.05 ms at 1,000 keys, and 32.43 -> 4.88 ms
at 5,000 keys. At 5,000 keys the graph/UI vertex count fell from about 1.05 million
to 253,000. These measurements exclude D3D9 submission, TF2 and filesystem costs;
they are not evidence that an in-game freeze is fully resolved.
