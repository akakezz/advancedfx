# Integrated Open Fortress feature port

This candidate continues V1 (`21cb1408`) and V2 (`1a527be5`) on the same
`of-feature-port-test` branch. It does not replace the verified original OF branch.
V2's fixes and features are all retained; this update consolidates startup policy.

## Normal use

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
