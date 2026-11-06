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
per-key full-pose holds, optional distance timing, and raw authored Euler winding.
A `CamPathCollection` in the same shared files manages named shots, savepoints and
bounded authoring history. Playback selects the latest-starting eligible shot.
The editor adds key dragging/box selection, exact numeric inspection, speed display,
and recovery autosaves. Single paths retain native XML; collections wrap those
paths with names and identifiers. No parallel path evaluator was introduced.

Multiple shots, names, hold releases and interval lengths are queued as immutable
properties into the existing Source 1 `CCampathDrawer`. Full text uses the existing
GUI font atlas because the classic hex texture contains only digits; projection,
depth testing, shader and device resources remain in the classic drawer. No second
world renderer or ImGui context is added. With GUI disabled, legacy numeric labels
remain available, but full-text labels need the GUI atlas.
