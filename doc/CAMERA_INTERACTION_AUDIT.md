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
