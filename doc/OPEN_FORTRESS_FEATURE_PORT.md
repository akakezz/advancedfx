# Integrated Open Fortress feature port

This candidate continues V1 (`21cb1408`) and V2 (`1a527be5`) on the same
`of-feature-port-test` branch. It does not replace the verified original OF branch.
V2's fixes and features are all retained; this update consolidates startup policy.

## Current status: overlay cleanup (2026-11-25)

The user reports the `bc3f26ed` integration works broadly as intended, including
category-colored studio wireframes. Normal OF startup now requests category
materials by default. The exact-binary preflight, material lifetime management,
cyan wireframe fallback, and safe-mode defaults remain unchanged. Recording
is still unarmed until explicitly started.

The Scene Cache menu shares the normal TF2 Capture, Preview, Memory and
Diagnostics layout. OF-specific fallback controls are under Diagnostics >
Compatibility and fallback, with all existing `mirv_cache of*` commands
retained. GUI and console share the same setting implementation. The workspace
uses the shared HLAE Camera title while preserving the existing OF ImGui ID.
Routine successful preflight pointer logs were removed; rejection diagnostics
and all preflight memory checks remain.

Known observation: a subset of OF ragdolls displays cached skeleton lines but
no visible cached studio mesh. OF's detachable body parts are a plausible
factor, not a confirmed cause. The cache already stores and replays the model
bodygroup (`Pose::Body`). The animation diagnostics now include that value.
No new dismemberment or mesh-reconstruction algorithm was introduced in this
cleanup. A submitted draw is not proof of visible pixels. Private animation
state/layer-vector reads and any remaining unsupported engine ABI paths remain
excluded; see `OPEN_FORTRESS_BINARY_VALIDATION.md` for offline binary evidence.

The following sections document earlier integration decisions; where they
conflict with this status, the current status takes precedence.

## Normal use (historical consolidation)

Launch Open Fortress as usual. No per-feature `-afxOfNative*` flags are needed.
The five existing native adapters are requested automatically in legacy OF x86:
custom recording resolution/fitted HUD, collision/tracing, separate rendering
passes, native POV spectating, and demo command observation. Each retains its
own client/interface/callback preflight. Resolution also requires the known
client fingerprint and engine-owned mode-list slot before calling it. A rejected
adapter remains unavailable; do not interpret a requested adapter as validated.
Normal recording, resolution changes, POV selection and camera settings still
require their usual UI/console actions. Use offline demos for this candidate.

Extended Scene Cache capture, pose capture and studio replay are enabled by
default, but recording remains unarmed and capture categories remain user choices.
The existing per-stage compatibility checks, bounds fallback and queue limits
are unchanged. The old OF cache toggles remain useful for diagnosis, and the
category-color 080 shader remains an explicit UI/console option (`mirv_cache
ofColor080 1`) because shader behavior and ownership remain runtime-unverified.
Skeleton display is likewise a visibility choice, not a launch prerequisite.

Optional recovery: `-afxOfSafeMode` restores the previous bounds-only Scene Cache
startup defaults and disables the five newly integrated native adapters. It is
not needed for normal operation. Individual Scene Cache compatibility controls
can still be used manually. This is not a substitute for the original known-good
OF branch if the candidate fails.

## Validation limits

The five adapter requests now default on. This increases exposure to unverified
engine ABI paths; the fingerprint and executable-slot checks cannot prove
correct calling conventions or runtime state semantics. There is no Windows
build or live OF test in this environment. Recorder sound suppression, manual
player repair and static-prop fade suppression still lack OF-specific native
implementations; this consolidation does not claim to implement them. Refer to
V2's historical notes for detailed feature and ABI evidence.

## Follow-up: remaining demo fixes

The user reported ordinary demo playback and most integrated features working
with `2d6508f5`; event indexing still failed in their OF demo. The follow-up
adds compatibility-gated recorder sound suppression, manual position/eye-angle
player repair and static-prop fade suppression to the shared demo-fix controls.
They require explicit normal UI/console activation, not new launch switches.
OF-specific activation is session-only; saved TF2 preferences are not applied
automatically. `-afxOfSafeMode` still disables the new native paths.

Sound suppression requires the known OF client and engine-owned sound slots,
plus the existing unique executable StartSound resolver. Static prop suppression
requires the known client and StaticPropMgrClient004 slot 5 in the engine; it
reuses the existing opacity factor override. Manual player repair resolves
the selected live player's actual receive class and validates the required fields
and proxy addresses before detouring. It refuses a different class after
installation; private TF2 animation and bone diagnostics remain disabled.
These guards and source-extracted fixtures are not proof of the actual engine ABI.

SDK reference: Valve Source SDK 2013 `src/public/engine/IEngineSound.h`,
`SndInfo.h`, and `IStaticPropMgr.h`. The runtime engine binary and matching
OF demo have not been inspected for these adapters.

Indexing now includes the header network protocol and failing command, tick,
file offset and network-message bit position where available. It does not guess
unsupported network layouts. A real failing OF `.dem` is required to verify
protocol support. Manual repair demo identity failures also return ordinary
errors rather than throwing on expected missing or invalid input.

New source-extracted tests cover repair installation and selected-entity
forwarding, static-prop activation, and sound resolver rejection; the existing
parser fixture exercises malformed input and diagnostic context. CI now targets
the actual `of-feature-port-test` branch. Windows build and live demo verification
remain required.
