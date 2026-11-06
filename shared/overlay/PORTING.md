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
| Seven-channel curve editor | Paper's custom Hermite interpolation for X/Y/Z, pitch/yaw/roll and FOV, plus Auto/Flat/Linear/Free tangents |
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

The custom interpolator derives Auto tangents from the whole path. Adjacent
intervals share derivatives at ordinary keys, including unequally spaced keys.
Authored positions and timestamps still determine speed; this is not a constant
speed retime. Flat tangents and deliberate reversals can still stop movement.
Classic interpolation settings and native console commands remain available.

The sequencer reads total demo ticks through the existing engine wrapper. Both
editors support pointer-centered wheel zoom, middle-drag panning and Fit controls.
Neo Sequencer retains its overview scrollbar, and its ruler visits only visible
tick marks rather than iterating over the entire demo each frame. Native preview
and console edits share the overlay's bounded undo history. Observing reuses TF2
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
