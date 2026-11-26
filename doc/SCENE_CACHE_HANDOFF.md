# Scene Cache 2 — maintainer handoff

Status: experimental TF2 x64 scene reconstruction for cinematic editing.
Branch: `feature/scene-cache-2`. Known runtime baseline:
[3a9b8fd](https://github.com/akakezz/advancedfx/commit/3a9b8fdaab9f9e5942224c141f420b299ae280b6)
(paused/frozen eligibility patch). Live-particle isolation was subsequently
validated in the user's TF2 test of 499fd28; introduced by [0a4af0a](https://github.com/akakezz/advancedfx/commit/0a4af0a27631db0846c20b3ac5149b63f32cadba).
Do not mistake the experimental branch for complete engine-state rewind.

## Goal and workflow

Record an actual demo interval once, retain observed renderable poses and
particle appearance at demo ticks, then use the independent cached playhead
to navigate backwards/forwards without repeatedly seeking the live demo.
The purpose is camera staging and shot construction, not deterministic
gameplay simulation. Source audio, physics and arbitrary entity state are not
rewound.

1. Compile on Windows with Visual Studio and the existing project dependencies.
   Use the project root and the usual multibuild command:

   ```cmd
   git switch feature/scene-cache-2
   git pull --ff-only
   cmake -DAFX_MULTIBUILD_CONFIGURE=ON -DAFX_MULTIBUILD_STAGING=ON -P cmake/MultiBuild.cmake
   ```

2. Load a disposable TF2 x64 demo. In Sequencer > Scene Cache > Capture enable
   the broad render snapshot option and Capture particles (TF2 x64 experiment).
   Broad capture attempts all registered renderables; category filters belong
   to diagnostic capture. For particles, `mirv_cache particles 1` enables full
   appearance and native preview. No AGR recording is needed for native snapshots.
3. Clear old history if necessary, set an optional capture range, and record
   forward at a reasonable speed. Stop cache recording. Pause the underlying
   demo or use Freeze (`demo_timescale 0;demo_resume`). Frozen preview eligibility
   is implemented; explicitly retest it on the target build.
4. Enable Cached preview, Draw cached models and optionally Suppress live studio
   models. Scrub the cached timeline (or `mirv_cache goto <tick>`). Do not confuse
   seeking the real demo with moving the cached cursor.
5. Choose Native textured under Cached particles. Set `mat_queue_mode 0` using
   the explicit overlay button; note the original value and restore it after the
   test. If needed, compare styled/uniform markers or Off.
6. Optional: enable Suppress live particles (experimental), or
   `mirv_cache particles isolate 1`. Compare against isolation off. This option
   starts off and only applies during eligible native preview frames.
7. Use `mirv_cache status`, `mirv_cache particles status`, and
   `mirv_cache particles at <tick>` to inspect the actual retained coverage.
   A stopped cache is best for reproducible diagnostics. The last replay results
   are not necessarily from the currently rendered frame when preview is off.

Useful recovery: `mirv_cache particles isolate 0`, set Cached particles to Off,
uncheck Draw cached models, disable Suppress live studio models, or leave Cached
preview. Query `r_drawparticles`; restore the original value if an isolation
restoration warning appears. Do not clear the cache until diagnostic comparisons
are complete.

## Architecture and where to start

| Area | Code / documentation |
| --- | --- |
| Scene controls, capture timing, history cursor, profiles, native particle modes and diagnostics | `AfxHookSource/CameraOverlay.cpp`, `CameraOverlay.h` |
| Immutable renderable history, pose reuse, interpolation and diagnostic geometry | `shared/SceneCache.h`; earlier Scene Cache sections of [CINEMATIC_CAMERA.md](CINEMATIC_CAMERA.md) |
| Native particle capture, layout, restore plan, forest fallback and bounded compressed history | `shared/NativeParticleSnapshot.h` |
| Exact-build native capture, detached probe, native replay and isolation preflight | `AfxHookSource/MirvCommands.cpp` |
| Ordinary rendering, model suppression, separate pass integration, scoped live particle cvar | `AfxHookSource/Tf2RenderPass.inl`; `main.cpp` |
| Material queue readiness and synchronous rendering | `AfxHookSource/MaterialSystemHooks.cpp` |
| Research chronology, reverse-engineering offsets, failures and test procedures | [SCENE_EFFECTS_EXPERIMENT.md](SCENE_EFFECTS_EXPERIMENT.md) |
| Portable tests | `tests/cinematic/native_particles_test.cpp`, `test_overlay.py`, `test_tf2_native_render.py`, other cinematic tests |

Capture native poses after the engine has prepared and rendered the frame
(TF2 FRAME_RENDER_END). Do not force bone setup at FRAME_RENDER_START merely
to capture earlier. Stable entity identity includes handle serial; owner/parent
relationships, historical skins, bodygroups and bone poses matter for
attached gear and ragdolls. Broad capture is not limited to visible players.
The studio front end must run on the engine caller; do not submit it from a
worker functor. Retain immutable data and associated resources until queue
completion; the queue limit of 2048 is headroom, not synchronization by itself.
Live suppression for studio models is partial. Brush entities retain wireframes
only if BSP geometry is readable; bounds are fallback.

The particle event probe was a useful discovery tool but insufficient for
random-access replay: it overflowed, missed pre-existing lifetimes, and the
verified binary can report a time-derived destruction ID. Native manager
traversal instead captures root/child collections, their names and clocks,
and current native attributes. The 22 known attribute slots use the actual
SIMD layout, not blindly the public SDK's types (the component mask differs).
Store raw bits; compact shared constants, then independently pack/compress
each frame without quantization. Particle and model histories have independent
budgets and may cover different time spans. Particle history defaults to
512 MiB / 32768 frames, configurable with `mirv_cache particles budget <64..2048>`.
Preview may hold the nearest prior particle sample by up to two ticks inside
retained coverage; exact queries still require an exact tick.

The successful detached restore dry run verified 45 historical particles,
9 varying blocks copied and 13 matching constants. The subsequent native
replay phase creates temporary collections through the verified factory,
restores varying attributes, checks constants, sets active counts and clocks,
and calls the native renderer on the main view with identity model matrix and
immediate material context. Whole trees are tried first; rejected parents can
fall back to children, and material gates are considered. Active counts are
reset and temporary objects destroyed synchronously; no native pointer should
be retained across callbacks. Guard offsets, signatures, cleanup and context
lifetime together if porting the implementation. Full renderer-specific
state, initial attributes and control points are not yet restored.

Target identity (from the inspected binary): TF2 x64 client.dll PE timestamp
`0x6ad26f17`, SizeOfImage `0x133a000`, SHA-256
`b6f45c74805edacf0cf61d965f658158fc22960f66d583ef3a6549eb56696b95`.
Runtime guards check PE identity and code anchors, not the entire file hash.
Do not assume another TF2 update, TF2C or another Source game shares this
layout. See the research log for verified RVAs and their provenance.

## Verified experimental results (user-reported Windows/TF2 runs)

These are observations, not benchmarks against unmodified HLAE or guarantees
for every demo. The status log was taken after preview had been turned off,
so replay counters are retained diagnostics. Submission is not pixel coverage.

| Metric | Short run, tick 334 | Longer run, tick 3392 |
| --- | ---: | ---: |
| Particle frames retained | 346 | 2944 |
| Recorded tick bounds | 5–475 | 6–3392 |
| Retained particle payload | 33.99 MiB | 257.86 MiB |
| Original snapshot payload | 80.52 MiB | 597.14 MiB |
| Packed before compression | 56.75 MiB | 438.94 MiB |
| Original / retained ratio | 2.37x | 2.32x |
| Particle capture mean / peak | 2.997 / 5.634 ms | 2.520 / 6.615 ms |
| Particle submissions at sample | 2185 | 2245 |
| Successful tree attempts | 49/49 | 59/59 |
| Nonempty drawable collections | 82 | 99 |
| Position / appearance rejected reads | 0 / 0 | 0 / 0 |
| Particle history evictions / rejected frames | 0 / 0 | 0 / 0 |

Longer run model history: 2944 frames, 235.72 MiB model payload, 493.58 MiB
combined retained model + particle payload (not total process RAM); 339931
unique retained poses, 159548 unchanged poses reused. Capture profile mean
5.157 ms, peak 14.360 ms; this outer capture timer includes particle capture
when enabled, so do not add the two means as independent costs.
233 registered renderables reported at tick 3392. Of 195 captured studio
poses, 36 matched the observed native draw hook and 159 were not observed
there (off-screen or another submission path); this is not proof of missing
rendering. Skins: 36 native, 128 network, 30 team fallback, 1 unknown.
One brush mesh reported unavailable. The same run reported 499520 captured
and 111872 unavailable pose attempts, so successful replay counters must
not be mistaken for complete capture coverage.

Studio replay audit: 15688 callbacks, 2595182 attempted / submitted, zero
queue skips, zero other skips, pending 0, peak pending 635, max two callbacks
per engine frame. Previous saturation at a limit of 512 produced 67106
queue-limit skips; increasing to 2048 eliminated observed skips on later
tests. This does not prove all views are correct or duplicate submissions
are harmless.

## Known issues and decisions

- Native replay remains an experiment: a submission can be visually wrong,
  and missing attributes, control points, renderer-specific state, sort order,
  material changes and dynamic lights can alter appearance. No historical
  audio or full physics/simulation.
- A brief artifact of large stretched black/white/gray polygons was reported
  with the initial native replay build. It persisted when switching to marker
  modes, so the native renderer alone was not established as its cause. The
  tester could not reproduce it on the later build. No targeted rendering fix
  was made. Preserve this observation rather than claiming it was solved.
  If it returns, keep the camera and cached tick fixed and compare particles
  Off, cached models Off, studio suppression Off, and cached preview Off.
  Compare queue mode with isolation disabled; save status and screenshots.
- Initial native replay commit 6939846 failed only on x64 (C3861: identity
  undeclared). Commit 4d395cc moved the function pointer into enclosing scope;
  it compiled and the user reported native replay working. Commit 3a9b8fd
  accepted exactly zero demo timescale and excluded seek-in-progress. No
  unrelated render changes were made for the transient artifact.
- The new live-particle isolation is opt-in and based on scoped
  `r_drawparticles`. It is not an extra native detour, and does not delete
  live objects or stop simulation. It might hide uncached effect types, and
  the tester confirmed cached native particles are unaffected by r_drawparticles
  in both states. The fallback is the existing overlapping preview. See Phase 8
  of the research log. Coverage of other effect paths remains incomplete.
- Separate TF2 capture passes are excluded from native particle replay and
  isolation. Queued rendering is not yet supported for native replay.
- Gaps, evictions, unsupported client builds, brush geometry, off-screen
  simulation and incomplete pose capture require explicit fallback rather
  than inventing historical data. A complete appearance sample is not the
  same thing as a complete effect renderer state.

## Next maintainer validation

Start from the known-good baseline and isolate changes. Test the new opt-in
on the same verified TF2 x64 build, with live and cached ticks clearly
different. Confirm historical effects stay visible while live-only ones
disappear. Repeat with Pause and Freeze, switching modes and seeking, empty
samples, history gaps, focus/device changes and separate pass recording.
Check the original `r_drawparticles` value outside the render scope and
verify status; if restoration fails manually restore it. Compare the same
scene with the option disabled and preserve logs for both.

Run the native particle and overlay/TF2 render regression fixtures. They
cover state transitions and resource/accounting invariants but cannot prove
actual Direct3D rendering. A standalone mock of the new scope passed seven
cases, including restore failure; full fixture suite, Windows build and
full regression execution remain unverified here. User-reported in-game
validation of isolation succeeded on 499fd28; other transitions remain to test.

Only after isolating the actual draw path should a maintainer consider a more
targeted suppression hook, full native renderer fidelity, or queued-rendering
support. Verify the relevant client code and lifetime first. Keep the
compressed history lossless and report measured tradeoffs rather than reducing
fidelity to fix unproven renderer bugs.

## Runtime follow-up: successful isolation and next coverage gaps (2026-11-27)

On 499fd28 the tester reported isolation working, r_drawparticles did not
change historical native particle visibility whether on or off. Status:
requested 1; live drawing disabled during ordinary render; restored. At exact
tick 2905: 37/37 trees, 80 drawable nonempty collections, 1941 submitted and
captured active particles, 0 rejected reads/layouts or replay attempts. 2663
frames retained (ticks 6–2935), 234.06 MiB from 546.44 MiB original, 401.06 MiB
packed, 2.33x ratio, 0 evictions/rejected frames, mean capture 2.360 ms,
peak 7.494 ms. No assertion of full pixel fidelity.

Three user-observed gaps: the Engineer's bright energy/death effect and body
appearance were absent in cached replay; Demoman's shadow stayed at the live
pose while historical geometry moved; explosion and blood decals stayed at
live demo state. These are separate from the confirmed native collection
isolation. See Phase 9 of the research log for diagnosis and next tests.
