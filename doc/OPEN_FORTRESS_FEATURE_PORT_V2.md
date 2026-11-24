> Historical candidate notes: the consolidated build supersedes the individual launch switches. See [current integrated OF policy](OPEN_FORTRESS_FEATURE_PORT.md).

# Open Fortress feature integration, second candidate

Base: `21cb1408` (first integration), based on the confirmed OF branch
`f773d8d2` and cinematic feature branch `d3506825`. This is a local testing
candidate, not a verified Windows/OF release. Do not replace the known-good
branch until an x86 Release build and offline demo tests have passed.

## Scene Cache parity

- The extended OF collector now feeds the shared capture loop: model category
  controls, player selection, attachment filtering, pose accounting, and capture
  diagnostics use the same policy. The arbitrary 128 pose attempts per tick is
  removed; the existing enumeration and memory budget remain. The outstanding
  render queue cap and complete eligible-model replay are retained.
- OF class/model paths are classified consistently for Scene Cache and render
  groups. Exclude viewmodels and hidden/dormant entities; use moveparent identity
  and a bounded, observed-object owner fallback. Retain deathmatch players without
  requiring TF2's team numbers; keep life state and EF_NODRAW filtering.
- Keep bounds-only fallback and previous session-only opt-in controls. Enable
  `mirv_cache ofExtended 1`, `mirv_cache ofPoses 1`, `mirv_cache ofReplay 1` and
  the desired capture categories, then recapture the interval. Existing cached
  samples do not gain new poses/ownership retrospectively.
- `mirv_cache ofColor080 1` or the experimental color checkbox enables the
  guarded VMaterialSystem080 material adapter from the separate experiment
  (a219277a). It uses independent per-category immutable materials and reference
  cleanup. Default is off, not saved; the known cyan studio fallback remains.
  Its runtime gate requires the known PE32 MaterialSystem, vstdlib and tier0
  versions, specific virtual-function RVAs, allocator identity and code bytes.
  Prior offline evidence is in the experiment branch's
  `doc/OPEN_FORTRESS_MATERIAL080_EXPERIMENT.md`; this session did not have those
  binaries and did not independently repeat PE verification.

## Camera transitions

Frozen outgoing clips evaluate the prior composite at the actual transition
start for deterministic fixed/campath sources and available cache samples.
Historical probing uses copied mutable rig state. Missing live history uses a
recent displayed pose (maximum age 250 ms), otherwise a cut, not a default
world-origin pose. This fixes a demonstrated source-level failure mode; it is
not confirmation that all reported transition behavior in OF is resolved.

## Experimental engine adapters

All new launch options are off by default, including when opening older projects.
Test one at a time, only in an offline demo. Do not infer ABI compatibility from
successful preflight alone.

| Launch option | Scope |
|---|---|
| `-afxOfNativePasses` | Native base, player, viewmodel, HUD and other separate pass dispatch; paused during seeks/loading; use only in offline demos. |
| `-afxOfNativePov` | Native observer selection and third-/first-person path, using explicit selection; restore observer fields before post-render cache capture. |
| `-afxOfNativeCommands` | Native seek/play command observation, requiring new-style command callback flags and executable callback addresses in engine.dll. |
| `-afxOfNativeResolution` | Previous candidate's native resolution, fit preview, HUD fix. |
| `-afxOfNativeCollision` | Previous candidate's tracing/collision adapter. |

New hooks check the known OF client fingerprint and readable executable slots
in the named modules. The public Source SDK interface declarations support these
candidate slots, but do not prove the exact game binary calling convention or
state semantics. Pointer-hook restoration now supplies a valid VirtualProtect
output pointer. A failed restore cannot make the POV hook discard its original
function if the replacement is already installed. Normal draw forwarding avoids
unnecessary model-info inspection when no groups or render pass are active.

**Still missing native OF functionality:** recorder-sound suppression (specific
sound entrypoint/signature), full manual player repair (exact receive-table and
animation semantics), static prop fade suppression (exact engine ABI). TF2
implementations remain in the tree but are deliberately not blindly enabled.
OF deathcam suppression and interpolation remain at their previous experimental
status. New pass/POV/command adapters still require native verification. Exact
engine.dll, client.dll and audio/renderer dependencies from the tested OF
installation are the next evidence needed; a public SDK header alone is not
sufficient. The Git bundle does not carry the submodule source trees.

## Regression testing

`test_of_scene_parity.py` exercises the extracted real classification,
attachment-resolution and material selection/cleanup code. Its fake material
factory substitutes the Windows-specific binary preflight; this is not a PE or
shader test. `test_frozen_sequence_boundary.py` exercises the extracted real
sequence evaluator with deterministic camera fixtures. Existing recording
policy and post-render capture-stage fixtures test OF's new readiness and demo
gates. The CMake Scene Cache and camera-motion targets can run without XML.
See the accompanying validation report for the actual results and blockers.
