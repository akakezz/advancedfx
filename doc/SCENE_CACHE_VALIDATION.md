# Scene Cache — consolidated TF2 x64 validation

Use one chosen exact-commit x64 build, not each intermediate checkpoint.
This is a replacement for a compatible modified HLAE install, not standalone.
Back up the original DLL and matching shader resources; close HLAE/TF2 before
replacement. BUILD_IDENTITY.txt identifies the commit and SHA256 hashes.

## One capture

1. Open a representative offline TF2 x64 demo. Choose a short combat interval
   with players, gear, explosions, trails, ragdolls, blood marks and shadows.
   Begin before effects appear; keep the relevant action in view.
2. Pause the demo; note the original mat_queue_mode and set mat_queue_mode 0.
   In a fresh session (or after explicitly running mirv_cache clear),
   run mirv_cache validate setup. It requires actual immediate context,
   empty history and no active operations. It does not seek or start capture.
3. Run mirv_cache record 1, resume the demo, record the interval once.
   Stop with mirv_cache record 0 and pause/freeze. Do not seek the live demo
   during the retained take test.

## One preview and benchmark

4. Run mirv_cache preview 1 and mirv_cache goto <earlier retained tick>.
   Scrub and move the camera; inspect models, gear, lighting, particles,
   decals and projected shadows. Note visible errors and their ticks.
   Use a busy earlier tick and fixed camera for the benchmark.
5. Run mirv_cache perf bench once. Close console and leave camera/transport
   unchanged. The automatic suite compares full immediate, models-only
   immediate, models-only queued, optional queued decal visibility and a
   queued legacy spark/muzzle overlay comparison.
   It saves partial results and restores the original benchmark queue cvar
   and scene settings. Use mirv_cache perf bench report for progress/path
   or mirv_cache perf bench cancel to abort.
6. After completion run mirv_cache validate report. Upload the single text
   file whose path is printed, plus screenshots/video of visible errors and
   the relevant tick. If TF2 crashes, send the already-written partial
   benchmark report and the last visible symptom instead.

## Limits and recovery

Native submission is not pixel validation. Queued visibility only filters
surviving nonfading ordinary world decals and limited removed world geometry
when a verified live proxy-free material witness and receiver are available. The separate queued legacy overlay
supports verified spark/muzzle materials and retains live effects (duplicates
are possible). Surviving decal opacity, modern particles, named legacy
materials and historical shadows still need immediate rendering.
Model/displacement decals, pose-accurate atlas silhouettes, baked lighting
and world lighting remain incomplete. Unsupported effects may stay live.

If rendering behaves unexpectedly, cancel the benchmark (if active), run
mirv_cache preview 0 and mirv_cache record 0. Restore your original
mat_queue_mode manually if necessary; check queue restoration warnings.
Do not clear retained history just to diagnose a visual error. If the game
crashes, revert to the backed-up DLL and matching resources.
