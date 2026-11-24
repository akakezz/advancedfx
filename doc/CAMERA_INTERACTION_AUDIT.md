# Camera interaction audit

This audit follows a saved camera through attachment, motion, framing, aiming,
shared modifiers, clip modifiers and collision. The systems continue to use the
existing follow controller, aiming implementation, modifier evaluator and trace helpers.

## Confirmed state and ordering problems

- Saved rigs passed a constant generation to the attachment controller. Explicit
  collision/follow resets therefore invalidated legacy attachment history but not
  the saved rig's history. Saved rigs now subscribe to explicit user resets. Legacy source/bone-binding resets stay local, so switching another camera does not cancel a saved rig's return.
- Applying motion settings cleared the complete live follow state, including arm
  length, retraction and return damping. Parameter edits now retain the last live
  sample; the controller already handles changed half-lives, teleports and continuity.
  Cached replay checkpoints are still discarded when settings change. Bypassing
  the controller with collision/follow disabled invalidates stale live history.
- Preserve framing solved against the raw bone pivot after position damping, and
  used raw bone orientation after orbit damping. These corrections could undo both
  filters. It now transports composition in the filtered boom frame and uses the
  filtered subject frame. The physical subject still participates in the positional
  safe-margin constraint; the result is not an exact per-frame raw-bone pixel lock.
- The positional framing cone also ran while aiming owned the view, using attachment
  angles rather than the actual aim direction. Explicit aim now owns this constraint
  too. Soft deactivation uses the existing aiming controller's actual active state,
  rather than treating its configuration switch as a permanently active controller.
- Camera/clip modifiers could move a collision-corrected source back into geometry.
  Both stacks now finish with the same sweep/clearance helpers, using the final FOV
  for optional near-plane protection. A geometric reframe preserves the requested
  projection when safety changes position, without advancing follow or aim twice.
  Subject-overlap checks reuse the controller's existing predicate; when a modifier
  cannot leave a valid subject gap, a verified pre-modifier position is retained.

The simple obstructed-boom return case worked before these changes too. The exact
reported persistent in-game failure has not been reproduced end-to-end. The reset
and edit-state defects are independently demonstrated and are plausible contributors;
this is not a claim that every live/demo-dependent issue is resolved.

## Collision ownership

Global settings remain the default. **Cameras → Attached camera collision → Use
per-camera collision override** creates an override for the selected cameras.
Turning it off restores inheritance. Editing a field across a selection preserves
other fields on each camera. Freecam and campath collision remain global.

The override is the same settings value and controller used globally, not another
collision implementation. It participates in camera undo/redo and cinematic project
format 8; versions 1–7 still load with global inheritance. New format-8 files require
this build to load.

```text
mirv_cam edit 0 collision radius 8
mirv_cam edit 0 collision returnHalfLife 0.4
mirv_cam edit 0 collision attached 1
mirv_cam edit 0 collision status
mirv_cam edit 0 collision inherit
```

The same fields used by global collision are accepted, except `free` and `campath`.
`recoveryDelay` edits the existing blocked-follow recovery-cut delay. `status` prints
inheritance, effective clearance/return settings, state validity, reset generations,
scene time, arm length, allowed length, retraction and framing. It does not edit the
camera or advance evaluation. Names may be used instead of displayed camera indices.

## Verification and limits

The interaction fixture extracts the production controller and return interpolation,
with an obstructing plane rather than an always-clear trace. It covers obstruction
release, repeated evaluation at one time, framing toggles on a stable identity,
follow delay, noisy bone position/yaw, aiming ownership and final modifier safety.
The former scene-adapter fixture aborted if return interpolation ran; it now uses
the production interpolation as well.

An alternating ±4-unit bone input produced about ±2.28 degrees of unwanted yaw in
the old damped framing case. The filtered-subject fix removes that correction in
this fixture. With alternating ±10-degree bone yaw, filtered orbit framing stays
below 0.4 degrees after settling in the tested setup. These are synthetic tests,
not measurements from a live TF2 demo.

Native adapter, overlay and project tests additionally cover reset generations,
retaining collision-return state across motion edits, override UI/history and
project round trips. Cached evaluation stays separate from live evaluation and
never traces against unrelated live geometry. Historical world collision remains
outside the cache contract. Geometry changes can still require immediate safety
corrections, and explicit blocked-recovery cuts remain controlled by their delay.

Legacy view-level aiming, offsets and FOV overrides still follow the configured
`mirv_cam order` after source evaluation. Camera/clip modifier safety uses their
final source FOV; optional near-plane clearance must still allow for a later global
FOV override. Global viewport overrides are distinct from scoped saved-camera aim
and modifiers.

For a live retest, keep one saved camera and toggle framing while it is retracted,
then release the obstruction and compare return half-life/speed. Repeat with aiming,
position/FOV modifiers and a second camera using different override settings. If a
camera becomes stuck again, capture its `collision status` output before creating
another camera, including whether the demo was paused or being scrubbed.

## Follow-only jitter investigation

The supplied slowed video uses follow half-life about 0.5, other controls at
defaults, with aiming and collision disabled. Those systems cannot explain this
reproduction. Follow half-life filters entity-root translation; zero bone and
orbit half-lives intentionally leave those channels rigid. The video alone does
not establish whether bone or orbit motion is the remaining source.

The existing controller now honors the turn-rate cap even with zero orbit
half-life while following, evaluates explicitly configured rotation-only smoothing,
and filters roll consistently with preserve-framing. Legacy inactive defaults
remain inactive. New applications of Soft follow also set orbit half-life 0.15.
For a live comparison on the same camera, keep follow at 0.5 and try bone 0.08
and orbit 0.15. Existing authored zero values are not silently changed.

Regression fixtures cover rate caps at slowed playback cadence, both framing
modes, rotation-only saved-camera evaluation and alternating bone roll. These
are controller tests, not proof that all live-demo jitter has been eliminated.

## POV pose-space and framing follow-up

A live retest found the previous channel fixes did not change the supplied POV
jitter, including with bone/orbit smoothing enabled. It affects attachments to
both the recorder and other players. STV's separate intermittent snaps correlate
with preserve-framing. The earlier smoothing recommendation is not a diagnosis.

The live attachment sampler previously combined `IClientTools::GetAbsOrigin`
with `SetupBones` positions and render-facing angles. It now uses finite
`GetRenderOrigin` values for both root-only attachment and the root of a bone
attachment, matching the existing scene-cache render-space convention. Absolute
origin remains the fallback when a valid render origin is unavailable. This
prevents differences between predicted/absolute and rendered roots from entering
the bone-offset channel. Capture and ordinary attachment share this sampler.

The preserve-framing cone previously compared the damped camera against a rigid
pose at the raw root/bone. In a 60 Hz test with follow half-life 0.5, a 200-unit
lateral update moved the camera 121.11 units on the next frame; with framing off
it moved 1.48 units. The cone still checks the real entity root, with filtered bone/orbit inputs,
but its correction now goes through the existing critically damped follow spring.
The same framing-on test moves 1.66 units on the next frame, then returns the
subject toward the frame smoothly. Collision safety,
explicit lag bounds and teleport resets remain active. Framing no longer demands
an instantaneous correction to every raw snapshot.

Production-sampler tests vary absolute and rendered roots independently across
tick boundaries, for root-only and bone attachment, including finite-data
fallbacks. Controller tests cover abrupt lateral snapshots, repeated paused
evaluation, both framing modes and continued recovery at 30/60/120 FPS. A moving
subject also returns inside a narrow frame. Live POV/STV playback still needs retesting.

For further live diagnosis, `mirv_cam edit 0 collision status` now also reports
sample count, backward time steps, root changes at unchanged time, and the largest
forward time step, even when collision is disabled. These counters observe the
existing controller without changing its clock or reset behavior. Play a short
problematic section and collect this output if jitter remains; backward seeking
intentionally contributes to the rewind count. Counters are transient and are
not stored in cinematic projects.

The render-root change makes the sampler consistent with Valve's
`C_BaseAnimating::SetupBones`, which builds its parent transform from
`GetRenderAngles` and `GetRenderOrigin`. Standard players can already have the
same absolute and rendered origin; this change alone does not prove the cause
of the persistent POV-only reproduction. Timing diagnostics are included so a
live retest can distinguish pose differences from discontinuous sampling time.

Framing recovery reuses the existing `SpringStep` implementation and motion
half-life (root follow first, otherwise bone/orbit damping). Its transient offset
and velocity belong to each controller state, keeping live and cached replay
independent. The existing lag bounds are applied again after the correction,
preventing delayed feedback from exceeding an explicit limit on direction changes.
The frame margin is a soft recovery goal; physical clearance and explicit safety
bounds still take priority.

## POV client-clock corrections

The follow-only retest reported 113 backwards time steps during uninterrupted
forward playback (39,388 controller samples). Previously, live follow used
client simulation `curtime`; each backwards step failed the continuity check
and reseeded the position, bone and orbit filters at the raw target. The
0.374-second largest forward step was below the existing ten-second continuity
window; backwards steps are the relevant reset trigger in this report.

Live attachment now resolves sample time through the existing
`GetCurrentDemoTime` helper used by the sequencer: demo playback tick plus its
fractional interpolation tick. The same resolver serves saved-camera follow,
legacy attachment and live camera aim. Authoring/clip times are not changed.
Cached/baked evaluation continues using its explicitly supplied scene times,
without reading the live demo clock. No second spring, synthetic time accumulator
or backwards-time clamp is introduced. Actual backwards demo seeks still reach
the existing reset path. Unsupported/missing demo timing retains the supplied
time as a fallback.

`mirv_cam edit 0 collision status` distinguishes effective sample-clock rewinds
from client input-time rewinds. During forward POV playback, client rewinds may
still be reported while effective follow-clock rewinds should stay at zero.
Backward seeking intentionally increases the effective rewind count.

Regression coverage injects recurring client-time corrections while demo time
advances at 0.33x, 1x and 2x, checks retained lag, paused evaluation and real
backwards seeks, and compares saved/legacy follow entry points with aiming
enabled. Cached stepping must not query the live clock. A live TF2 POV retest is
still needed to confirm the remaining visible jitter is eliminated.
