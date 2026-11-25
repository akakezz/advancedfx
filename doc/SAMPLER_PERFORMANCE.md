# Sampler performance — CPU baseline and capture timings

This first optimization is shared by the TF2/TF2C and Open Fortress branches.
It does not change the requested input/output FPS, shutter exposure, frame
strength, sample times, or rectangle/trapezoid selection. The legacy CPU
accumulator remains the implementation; its packed rows use SSE2 on builds
that target SSE2, with scalar kernels on other targets. Different-weight
trapezoid contributions now traverse the accumulator once, keeping the original
per-pixel addition order. No AVX requirement or new recording profile is added.
Fixes: initialize the float-depth accumulator, use matching array deletion,
and give RGBA its four-byte pixel stride.

## Measure a take

1. Use a reproducible demo segment, a single color stream, the same native
   resolution, graphics settings, profile, exposure, and input/output FPS.
   For example, 1200 input FPS and 60 output FPS, full exposure, 15 seconds.
2. Before each take, clear **Recording > Recording log / timings**. After
   stopping, allow the capture and encoder workers to drain before reading
   the summaries. If the game is paused, allow rendering to resume so queued
   teardown can execute. Repeat three times and compare median wall time.
3. Click **Copy log** to copy all retained messages (including offscreen entries)
   and their source headings to the clipboard. Compare the same take with the
   preceding build if available.

`Take timing` measures recording start through stop/queued teardown, not
necessarily final worker completion. `GPU capture timing` measures capture
CPU wall time including acquisition/wait, resolve and `GetRenderTargetData`;
readback is a subset and may include GPU synchronization/render stalls.
`Sampler timing` separates the time spent inside the sampler (including
normalization and buffer allocation) from downstream synchronous output
submission. It does not measure the entire FFmpeg process or GPU render time.
Rows represent individual capture or sampler instances, not disjoint phases;
workers may overlap, so totals must not be added or subtracted to infer FPS.

No GPU accumulation, skipped renders, optical flow, or adaptive sampling is
implemented in this change. GPU accumulation will need a separate backend
that preserves the existing timing/weight decisions, resource lifetime,
render-state restoration, MSAA resolve, format conversion and alpha semantics.

## Portable verification

`python3 tests/cinematic/test_sampler.py --sanitize` compiles the production
sampler with scalar and SSE2 kernels, compares per-row results against scalar
operations and checks identical timeline hashes for BGR/BGRA/RGBA/alpha/float
depth, padded rows, both integration methods, multiple exposure and strength
settings. This does not validate Direct3D or the Windows toolchain.

Optional CPU-only benchmark:

```
g++ -std=c++17 -O2 -ffp-contract=off -I. tests/cinematic/benchmark_sampler.cpp -o sampler-benchmark
./sampler-benchmark
```

The benchmark compares the original scalar pixel loops with the optimized
kernels, using a float accumulator and two BGRA inputs. These are kernel
measurements, not recording speed estimates. The GPU, readback, game rendering,
image transforms, preview presentation, encoding and disk are excluded.
