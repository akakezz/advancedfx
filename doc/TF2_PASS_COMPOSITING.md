# TF2 pass compositing: current limits and proposed direction

The current World/Players/Viewmodel/HUD outputs are object isolates. They are not
an exact decomposition of every TF2 frame into interchangeable RGBA layers.
Normal screen recording is the existing beauty/reference output and should remain
enabled when checking reconstruction.

## Defects addressed in the current update

- Excluded model geometry keeps its native depth/stencil draws with color writes
  disabled. Dropping the draw entirely removed occluders: a prop excluded from
  Players could no longer hide a cosmetic behind it. World also retains player
  depth for native particle occlusion. Material depth-writing rules remain native;
  translucent surfaces are not forced into opaque depth occluders. This restores
  missing depth submissions, not a complete opaque depth prepass: ordering of late
  occluders still needs in-game validation as part of the larger effects work.
- Third person uses Source's native chase position and orientation as one pose.
  A final angle-only override has been removed: changing orientation after native
  placement/collision can point the camera away from its target pivot. The default
  remains a camera behind the target that turns with them, not a new camera rig.
  Independent spectator orbit is a possible later control, not part of this fix.
- `mirv_streams record pass stats` reports attempted/captured/failed GPU frame
  readbacks. It counts a complete black/white pair as one captured alpha frame.
  Counters reset per take. They do not count frames produced inside an encoder or
  sampler, or prove that a media player decoded smoothly.

## Why a cloaked player can turn black

Valve's `cloak_ps2x.fxc` uses a refraction sampler copied from the backbuffer,
including warped/blurred samples and tinting. Removing background color removes
part of the shader input. Replaying that shader against a white matte does not
rebuild its original background dependencies. The result can be a black silhouette
rather than an independently compositable translucent player.

This remains unresolved. Disabling cloak, substituting an opaque player, or keying
out black would change the recorded appearance. None of those is an accurate fix.
A background-dependent effect also cannot be represented for arbitrary replacement
backgrounds by a single ordinary RGBA image.

## Why one world layer cannot place every particle correctly

A particle can be behind one player and in front of another, even within one
frame. Putting the flattened World layer entirely above Players loses the front
player; putting it entirely below Players loses foreground particles. Native depth
holdouts help record correct occlusion, but do not recover the render order after
all effects have been flattened together. Additive blends are another distinction:
ordinary alpha-over is not the same operation as additive composition.

## Proposed implementation, for review

Extend the existing stream/capture system; keep its writer profiles, FPS/sampling
controls, render-target helpers and material filtering where compatible with TF2.
Do not add a separate encoder, camera evaluator or independent recording clock.

1. Define a per-frame capture contract: one final camera pose, demo/render time,
   interpolation state, viewport and frame number for every output. Additional
   outputs must not advance particle simulation or temporal postprocess history.
2. Preserve native scene depth and add effects captures with explicit player/world
   holdouts. At minimum, distinguish effects composited before players from those
   composited after them. Keep additive and alpha-over contributions separate when
   their blend operations differ. Two global depth groups are not a universal
   solution to overlapping translucent/refractive surfaces.
3. Treat refraction explicitly. A first supported workflow can retain refractive
   effects in the beauty shot and use object mattes for grading. Editable background
   replacement needs distortion/coverage/tint or equivalent scene-dependent data,
   plus the proper background at that render stage. This requires TF2 shader/render
   integration beyond the current model-draw adapter; it is not implemented here.
4. Apply bloom, tone mapping and other combined-image effects at a defined stage.
   Applying them independently to every isolate and then stacking the results is
   not equivalent to applying them once to the assembled scene.
5. Publish a supported composition order and compare it against the same-frame
   beauty. Preserve the simple object-isolate workflow for users who only need
   masks or selective adjustments; label its effect/refraction limits clearly.

## Acceptance checks

- Opaque players, wearables, weapons and gibs behind both brush geometry and props.
- A particle crossing in front of and behind players, including overlapping players.
- Cloak transitions against textured backgrounds, fully invisible players, refractive
  cosmetics and soft/additive effects. Test against the beauty, not just a checkerboard.
- Equal output timing at 1080p/60, queued and immediate rendering, and MSAA on/off.
  Compare readback counters, media frame counts, FPS and frame timestamps before
  concluding that depth motion is captured at a lower cadence.
- A reconstruction/difference comparison in a known color space. Explicitly list any
  unsupported scene-dependent effects instead of promising exact reconstruction.

## Diagnosing depth cadence now

Record World and Depth together with the same profile. After recording finishes,
run `mirv_streams record pass stats`. A nonzero Depth failure count identifies a
readback problem; equal counters alone do not rule out later CPU/encoding failures.
Compare the files' frame counts/FPS/duration in an editor or media inspector, then
step matching frame numbers. If those match while real-time playback stutters,
check decoder/playback performance rather than altering capture timing blindly.
