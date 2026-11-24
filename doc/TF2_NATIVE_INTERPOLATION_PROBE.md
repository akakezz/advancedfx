# TF2 native interpolation diagnostic (experimental)

The matching uploaded TF2 binaries contain BaseInterpolatePart1 with an
inlined Interp_Interpolate loop. Do not hook the inlined loop as a standalone
function. The actual function takes this, a reference to currentTime,
three vector references and an int reference, and returns an int.

| Architecture | PE timestamp | Image size | Function RVA | Last map time | Local origin |
| --- | --- | --- | --- | --- | --- |
| x64 | 6ad26f17 | 133a000 | 1de9c0 | 4c | 368 |
| x86 | 6ad26f10 | ea6000 | 1c79b0 | 2c | 2bc |

All numbers are hexadecimal. Four instruction fingerprints plus the PE
identity are checked before installing. Unknown builds are refused.

The compiled function optionally substitutes final predicted player time,
subtracts one tick and adds the fractional tick, then evaluates the
interpolation map. The probe samples the time argument before and after
this native call and the map's previous/current time and local origin.
The result and no_more_changes are native outputs; a zero result means
the normal interpolation path was bypassed. It does not alter arguments,
entity state, or interpolation settings. Map time is only evidence of
a native evaluation when the function actually evaluates the map.

## Capture

Disable the earlier render clock experiment (mirv_fix demoRenderClock 0).
Load the original demo, seek just before the jitter interval, pause,
and run mirv_interp_probe status. Matching client image must be 1.
Run mirv_interp_probe start <entity index> while paused, then resume.
Capture a few seconds, stop with mirv_interp_probe stop, and save with
mirv_interp_probe save "C:\path\interp.csv". Select the recorded POV
player or another affected player; the index is the actual entity slot,
not the user ID. Do not seek or reload while armed. Restart TF2 to
remove the installed detour. No hook is installed automatically.
The capture is bounded to 65,536 matching calls and only records
on the thread that installed the hook.

The regular angle diagnostic now also records native interpolation,
prediction, smoothing and FPS cvars when available, plus network
tick base if exposed by the receive tables. Unavailable cvars are
marked unavailable. Use matching settings for A/B comparisons.

Static binary inspection validates function identity and signatures
for these two binaries, but full Windows compilation and live
hook installation/capture must still be verified. No DLL is committed.

## Effective clock A/B experiment

Use the same verified client build and the same player as the read-only
diagnostic. Disable the global render-clock experiment first.

    mirv_fix demoRenderClock 0

Pause the original demo shortly before the affected interval. For a
read-only control, use:

    mirv_interp_probe start 2 native

For the targeted correction, use:

    mirv_interp_probe start 2 stable

Resume playback, then inspect status, stop and save each run to a separate
CSV before seeking or starting another capture.

    mirv_interp_probe status
    mirv_interp_probe stop
    mirv_interp_probe save "C:\Temp\interpolation_stable.csv"

The new mode detours the verified GetFinalPredictedTime entry (x64 RVA
0x1f4120, x86 RVA 0x1d9990), verifies its machine-code fingerprint, and
only substitutes its return value for the exact call from the selected
entity's BaseInterpolatePart1 (return site x64 0x1dea98, x86 0x1c7a55).
The original function still executes. The native subtract-tick/add-fraction
arithmetic and interpolation history loop remain untouched. A TLS scope
prevents unrelated/nested entity calls from inheriting the correction.
Outside the scoped call the original return value passes through.

The anchor is the first native effective clock minus interpolated demo
time. Corrections are bounded to 2.5 ticks, with invalid timing,
discontinuities and pauses resetting the anchor. Frame/network guards
stop the capture on entity replacement, demo exits, skips or large tick
jumps. The mode disables itself if a corrected native evaluation fails
to match the expected caller time and map time. Stopping the capture
immediately turns off clock substitution; restart TF2 to remove the
detours. It is never enabled by default or persisted.

New CSV columns expose the mode, predicted-call coverage, whether a
correction was applied, tick interval, raw/returned predicted time and
raw/target effective time. Compare raw_effective_time to effective_time
and map_time, then compare rendered displacement and subjective jitter
in the same interval. A stable map clock without smoother motion is
evidence against the effective-time hypothesis, especially because the
selected player's origin did not change during BaseInterpolatePart1 in
the first diagnostic. This experiment does not correct the upstream
prediction system, other players, or the global engine clock.

Offline re-evaluation of the initial 718-call trace predicted 204
corrections and eliminated 40 backwards effective-time steps, with a
maximum correction of approximately 15.08 ms. This is an algorithm
replay, not a live engine test. Full Windows compilation, detour
installation, and gameplay results remain to be validated.
