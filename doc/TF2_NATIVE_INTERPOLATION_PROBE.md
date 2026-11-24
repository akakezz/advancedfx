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
