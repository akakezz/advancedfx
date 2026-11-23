# Experimental TF2 demo render-clock A/B test

This is an **opt-in, reversible engine experiment**, NOT a proven jitter fix.
The original recorded demo and TF2 network updates are never modified.
The experiment targets the one-tick render-time oscillation observed in
two jitter diagnostic CSV captures of the same gameplay interval.

## Run

Build HLAE from `feature/cinematic-campath`, load the **original** demo,
and compare the same section with an identical viewpoint, timescale, frame
rate, seek preroll and interpolation settings.

Default (engine rendering clock; normal TF2 behavior):

```text
mirv_fix demoRenderClock 0
mirv_fix demoRenderClock status
```

Experimental (render-stage clock aligned to monotonically increasing demo
tick + interpolation fraction):

```text
mirv_fix demoRenderClock 1
mirv_fix demoRenderClock status
```

To reset the current anchor without disabling:

```text
mirv_fix demoRenderClock reset
```

Or use **Campath → Demo fixes → Experimental: stabilize TF2 demo render
clock**, with adjacent Reset and status buttons. This switch is **not**
saved and starts disabled on every HLAE launch. Switch it back to `0`
if an animation, viewmodel or camera starts behaving strangely.

## What changes

TF2's `FRAME_RENDER_START` callback receives a temporarily adjusted
`gpGlobals->curtime` (the same verified offset used by the existing
`WrpGlobalsOther::curtime_get()`). The proposed time is calculated from
the native demo playback tick, interpolation fraction and tick duration,
plus an anchored engine/demo clock offset. It is strictly bounded to
2.5 native ticks from the raw curtime and resets on backward seeks,
large time jumps, paused playback, loading, or skipping. Source's original
`curtime` is restored at `FRAME_RENDER_END`, with a fallback at the
next `FRAME_START`. Restoration is conditional: if Source overwrites
the time itself, do not overwrite that new engine state.

Unlike the earlier offline demo-file retimer, this tests whether a
**render-time** clock correction influences both camera and moving
entities. If it is smoother, we have evidence for a shared rendering/
interpolation cause; if unchanged or worse, the single-clock hypothesis
is incomplete. We must not ship this as a permanent fix without in-game
validation and deeper inspection of TF2's entity interpolation.

## Validation

```sh
python3 tests/cinematic/test_demo_render_clock.py
```

The headless ImGui test also instantiates the actual clock controller with
mock engine state and checks frame-only mutation/restoration and disabling.
Windows TF2 gameplay and full Windows compilation still need confirmation.
