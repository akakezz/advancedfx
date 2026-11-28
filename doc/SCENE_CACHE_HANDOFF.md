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

### Confirmed diagnostic follow-up (after 5da6306)

The tester listed the entire exact particle sample at Engineer disintegration
tick 34: 46 named collection records, 1010 active particles, both position and
appearance complete. No identifiable dissolve/electrocution collection was in
the list (other effects included rocket and pipe trails, teleporter/dispenser,
sapper sparks and gutter drips). This supports, but does not prove, a separate
legacy emitter path. Check adjacent exact ticks and the live draw gate before
concluding a particular implementation is absent. A complete sample describes
only the traversed native collection system.

- r_shadows 0 hides the misplaced shadows; restoring 1 restores them at the
  original live pose. This verifies a reversible draw gate, not cached shadows.
- r_drawdecals 0 hides the observed decals; restoring the previous enabled
  setting restores the marks. Existing live decals survive the comparison.
- r_drawmodeldecals 0 had no visible effect in the tester's scene. Model decal
  control remains unverified, not disproved.

The next native investigation needs the exact running x64 client.dll and
engine.dll. Do not reuse old offsets or substitute public SDK layouts for the
shipping binary. No additional rendering changes were made. Phase 9 in the
research log records the more specific shadow and dissolve leads.

### Exact-binary research follow-up

Both supplied modules are PE32+ AMD64; client hash matches the existing guard
baseline. Engine identity, verified legacy list fields, CSimpleEmitter factory
and dissolve callsites, shadow manager vtable and decal string xrefs are
recorded in Phase 10 of the research log. New `mirv_cache particles legacy`
command requests a one-shot read-only inventory of the **actual live** demo
at frame end (cached preview must be disabled). It is a diagnostic, not a
legacy cache or replay implementation. No historical shadow/decal changes;
Windows build and target runtime test pending.

### Runtime confirmation and legacy capture-only experiment

Tick 50 audit found four C_EntityDissolve emitters, 760 particles total
(235/24/26/475); tick 199 found none. Both list walks complete. Phase 11
adds opt-in exact-tick dissolve-only historical capture: `mirv_cache particles
legacyCapture 1`, record actual demo, then `legacyStatus` or `legacyAt <tick>`
under the same command prefix. The new 64 MiB history is separate and
pointer-free; no legacy rendering or entity fade reproduction yet. Known
spark/combinemuzzle2 material identities are tracked by verified handle
comparisons; others reported unknown. Test before/during/after dissolve and
compare capture counts with the live audit. No shadow/decal changes.

### Batched visual experiment

Phase 12 adds optional native textured legacy dissolve replay, cached model
RGB/alpha modulation and scoped live shadow/decal hiding. Capture once with
legacyCapture 1; enable legacyReplay 1, visuals modulation 1, visuals
hideShadows 1, visuals hideDecals 1 independently (all rendering options off
by default). The latter two are temporary isolation only. True historical
shadow projection, decal lifecycle/replay, material overrides and legacy
classes outside dissolve remain pending. New scratch native draw was subsequently user-validated on 54cd55f;
see Phase 12 and rollback/toggle instructions.

For one-build testing: capture both modern and legacy particles, freeze/pause,
select cached preview, set mat_queue_mode 0, then use `mirv_cache visuals batch 1`
to enable the four new replay/isolation experiments. Enable the existing live
particle isolation separately. Compare each switch independently at the same
cursor; `batch 0` disables all four without recapture. Legacy diagnostic
markers and exact-tick status help distinguish capture from render failures.
The temporary shadow/decal gates do not replace historical reconstruction.

### Validated batched baseline (54cd55f)

User-reported successful Windows/TF2 run with all four batch features and
live particle isolation enabled. At exact cached tick 43: 1264 modern and
796 legacy particle submissions (622 spark, 174 combinemuzzle2), zero
reported rejections, 163/163 studio submissions. Engineer and three cosmetics
share historical RGB modulation 0.141/0.212/0.259, alpha 1. All three live draw
gates report suppression/restoration success. 515 frames retain 112.58 MiB
combined model/modern/legacy payload, including 8.75 MiB legacy history;
outer capture mean 6.033 ms. See Phase 12 runtime validation for complete
measurements and limitations. This is a new known-good experimental baseline,
not proof of arbitrary scene fidelity. Historical shadows and decals remain
pending; nonunit model alpha is not established by this sample. Future work
should batch related implementations and preserve independent runtime
switches so one recording/build can test several features.

Phase 13A adds opt-in `mirv_cache particles legacyAll 1` for other exact
non-sky CSimpleEmitter bindings, registered material names and separate
skipped additional-emitter counts. Existing dissolve-only default retained.
Derived legacy renderer types still excluded. Record fresh history after
changing the option.

Phase 13B introduces independent historical surface capture and preview
experiments: `mirv_cache surfaces capture 1`, record, then `surfaces batch 1`
(or shadows/decals separately), `surfaces status`. Native projected blob
shadows use observed historical projection calls, not true actor silhouettes;
world decal visibility only applies to surviving ordinary marks. Missing
projections, removed decals, dynamic fade, model decals and moving receivers
remain limitations. See Phase 13B exact ABI notes and scope cleanup diagnostics.

Phase 14: user validated projected blob placement but saw scrub flicker and
no working decals. Missing tick 3451 confirmed a gap in all histories;
modern particles resolved 3450 while legacy/surfaces skipped. Both now use
bounded prior samples, max 2 ticks. World decal experiment replaces alpha
mutation with scoped geometry skipping at five verified native draw callsites.
See Phase 14 for limitations and diagnostics; no removed decal recreation.


## Performance continuation — Phase 16A

Read Phase 15A–16A of SCENE_EFFECTS_EXPERIMENT.md before further changes. Branch checkpoint d3d8d38 was not user-tested. Prior benchmark: live 78.1 FPS, full historical 18.1, main+cull 25.1, latest tick 59.3. Early versus latest studio CPU differs by ~10x despite similar submission counts. Prepared particle topology and callback-local legacy material lookups reduce redundant work without changing history or native ownership. No Windows or runtime validation yet. Preserve queue/resource lifetimes and exact client guards; do not retain native detached objects or enable queued particle rendering until verified. Subsequent optimizations should be batched and independently testable.

Phase 16A follow-up: `mirv_cache perf bench` now runs 11 modes including separate residency/write comparisons and reports studio stages; `mirv_cache perf reset` clears stage/slowest-asset counters. It is sequential, not cold-cache controlled; disabling historical replay can reintroduce live models/particles. `perf` shows slowest asset/tick and CPU mean per callback. Use the same fixed camera and cached scene with immediate mode. No FPS improvement established yet.

Phase 16B fixes external linkage of the Phase 15D residency functions and introduces immediate textured studio asset batching (`mirv_cache perf batchStudio 0|1`, default 1). This only shares references/header locks within a single synchronous view; queued and wireframe paths retain original retirement semantics. Compare with batching off if corruption or regression occurs. No engine pointers added to historical data; no new ABI offsets. Tests/build/runtime remain pending.

CI review confirmed d3d8d38 and initial Phase 16A commits passed the camera transition regression workflow. Initial Phase 16B batching fixture failed due to premature test skin/state reset; the fixture was corrected. Check the subsequent branch workflow for the exact new checkpoint. Portable CI is not the target Windows build or in-game validation.

Phase 16C reuses a legacy particle snapshot lease and owner matrices across repeated attachment reconstruction. Benchmark now runs 13 modes including batching-off and batching-on repeat, retains requested live isolation in the explicit model/particle-off diagnostic modes when coverage/device guards allow it, and restores batching on stop. Live surfaces may differ in model-off mode. No reduction in capture fidelity or persistent native particle ownership. Corrected Phase 16B f3f9db2 passed its portable CI workflow. Check the exact branch workflow before Windows testing.

Phase 16C follow-up batches contiguous detached header reads with narrow fallback, reuses per-callback particle gate scratch, checks allocation-address overflow and reports gate/activation/reset time. Native guard and cleanup rules remain; no persistent particle objects. Compare full stage totals from the same fixed camera and sample.

CI now includes the existing legacy-particle and surface-history standalone fixtures and triggers on the replay adapter/queue/history files. Earlier Phase 16C add9018 passed the previous workflow; use the new branch workflow for broader coverage and verify the actual Windows build separately.


Phase 17A: runtime feedback (2026-11-27) worst cached scene approximately 40 FPS; no controlled bench result because command never finishes. Benchmark progress now driven by completed outer ordinary View_Render calls rather than global demo framecount; prints one line per phase, `perf bench report` gives partial lines and progress or cancels/restores if no callback for ten seconds. `perf bench cancel` remains available. Expanded Phase 16 CI passed. User reports live-state decals leaking through historical preview; existing surviving-world-only experiment excludes several classes and cannot recreate removed marks. Prioritize performance, preserve fidelity, investigate unsupported paths separately. Immediate queue mode is still the primary user concern; do not lift native particle/surface guard without proving lifetimes and render context behavior.

Phase 17B introduces guarded detached validation reads (`perf fastReads 0|1`, default on) with page checks, SEH and original system-read fallback. Bench now runs 14 phases and reports direct/system/fault reads. Exact ABI and lifetime checks remain unchanged; Windows native path pending runtime validation.

Phase 17C enables queued textured studio batching using shared per-view asset ownership and individual ordered completion tokens. `perf batchQueued 0` restores old queued path; `perf batchStudio 0` disables all batching. This does not yet enable full native historical particle/surface replay in queued mode. Native lifetimes must be verified before lifting their guards.

Phase 17D: `mirv_cache perf bench models` is a six-phase comparable geometry benchmark for either immediate or queued contexts. Temporarily disables historical effects, leaves live effects, restores settings, and reports actual context mode. Compare manually selected `mat_queue_mode 0` against the normal queued setting at the same cursor/camera. The 14-phase full benchmark still needs immediate mode; no automatic cvar change.

Phase 17E precomputes frustum planes and adds opt-in conservative grouped player/ragdoll/attachment culling (`perf cull 1; mirv_cache perf actorCull 1`). Default actor cull off; compare final benchmark phase (15 full, 7 model-only) and check edge fidelity. No changes to recorded history.

Phase 17C queued studio fixture passed CI. Phase 17D initially failed overlay compilation on non-const demo interface; corrected pointer. Public SDK normal particle render context usage is not proof for detached collection/legacy scratch lifetime in queued mode; exact target DLLs unavailable here, so immediate particle/surface guard remains. Next native queue research requires those binaries and completion/thread/lifecycle verification.

Phase 17F: decal capture/live replay status now breaks down unsupported classes; reuses validated record fields for geometry gating, with unchanged hook checks. Use `mirv_cache surfaces status` at a problematic cursor. Full decal reconstruction still unimplemented; no unsupported marks hidden automatically. Corrected Phase 17A–E checkpoint passed portable CI.

Phase 17G caches validation for immutable sampled poses and avoids redundant owner lookup/entity copies. No native header validation is removed. Portable CI and native runtime are separate validation steps.


Phase 18A fixes reported missing <sstream> (Win32 build stopped before x64), enables existing Windows Release CI on this branch, and consolidates diagnostics: run `mirv_cache perf bench` once from stationary broad preview, close console, keep camera fixed. Automatic full immediate + model-only immediate + model-only queued suite, incremental unique temp report; `perf bench report` displays path/partial results, `perf bench cancel` aborts. Queue changes/restoration at engine FRAME_START; original value restored unless externally changed. `bench full` or `bench models` is a no-switch fallback. No native queued effects enabled by this diagnostic. Windows and runtime unverified.

Phase 18B inspects all supplied TF2 x64 DLLs and confirms queue getter/functor layout; native particle virtual renderer and post-render consumers remain unproven in queued mode. Studio setter equality elision only when known through hooks, unknown behavior preserved; per-traversal decal receiver flags reused. Exact binary notes in experiment document. Phase 18A portable CI passed; Windows build still pending.

Phase 18C automatically captures the bounded native renderer/post-render target census during benchmark warm-up and includes it in the same report. No separate user diagnostic build required; target RVAs are not proof of safe queued lifetime.

Phase 18D batches native activation prefix writes with narrow fallback, reuses validated per-tree definition and cleans only touched bindings. Consolidated full benchmark now 18 phases (32 automatic total); independent modern/legacy/surface comparisons, original settings recorded/restored, cumulative RVAs saved per phase and timeout if FRAME_START stalls. Start once with `mirv_cache perf bench`; send the report file path printed at start (or `perf bench report`). No further separate diagnostic commands required. Full queued native effects remain research; no runtime gain claimed.

Phase 18E: Windows CI builds x64 and Win32 Source 1 targets independently. Successful x64 job uploads exact-commit DLL/PDB for replacement in a compatible existing staging installation (`bin/x64/AfxHookSource.dll`, optional `pdb/x64/AfxHookSource.pdb`). Full staging build still available locally; artifact is not a standalone HLAE package. Confirm exact CI result before testing, and use only the latest consolidated checkpoint.

### Phase 19A: history and nested render scope correction

Reject unavailable/oversized surface samples before changing retained history, including same-tick and world-zero failures. Nested ordinary View_Render callbacks inherit the outer scope and no longer clear its studio groups/audit, end its historical surfaces, or launch separate capture batches. Added portable regressions; target runtime not verified. This is a correctness fix, not a new decal creation path or queued native-effects implementation.

Phase 19B: resolve shadows and decals independently within the existing two-tick causal bound; an empty ready sample is authoritative. Both resolved ticks are included in surfaces status and the benchmark. No samples are extrapolated outside retained coverage or across worlds. This reduces avoidable partial-capture gaps but does not reconstruct removed marks or unobserved shadows. Portable fixture added; runtime validation pending.

Phase 19C: exact engine binary reinspection confirms native decal creation can remove existing live marks (overlap/capacity eviction); do not use it for temporary replay. Extend surviving-world visibility capture to verified 0x100 positive-duration fading decals, with guarded native clock reads and fade-start/duration identity. Expired historical samples gate geometry; unmatched fades with invalid historical fade coverage fail open. No live decal mutation, removed-decal recreation, or partial opacity restoration. 0x1000, attached, outside-world and displacement remain excluded. Runtime/Windows validation pending.

Phase 19D: opt-in detached reconstruction of removed ordinary world decals from observed clipped vertices. Fresh surface capture observes only immediate main-world groups 0..2 (group 3 water transform excluded); immutable polygon history uses shared nested accounting. Native single-mark mesh submission at the verified world decal dispatcher receives copied geometry, historical RGB/alpha/lightmap offset and current validated receiver/material; no engine decal pool insertion/removal. `surfaces batch 1` enables this experiment; `surfaces removed 0` independently disables it. Missing geometry, unsupported live slots, material/receiver guards and unvisited groups remain reported limitations. No displaced/attached/model decals, full PVS equivalence, historical surviving fade opacity or pixel guarantee. New native code and Windows build require validation; don't claim working runtime before test.

Phase 19D follow-up: zero base alpha remains invisible even if a prior evaluated fade value is present; reset decal observations on shadow-detected world transitions and verify the capture world at dispatch. Exact shared-header/fixture portable sanitizer regression passes after alpha correction; native runtime still untested.

Phase 19E: optional surviving decal opacity replacement (`surfaces opacity 1`, included in batch). Validate detached mesh/material/receiver/context before the ordinary dispatcher; scoped gate suppresses a matching surviving record only during that group. Submit after original dispatch if gated or already invisible, otherwise retain the native result. Apply current dispatcher blend, not a hardcoded 1. No live-record writes; post-dispatch context failure can omit a replacement for that frame and is reported. Unobserved, unsupported and non-main groups remain incomplete. Portable regression fixtures added; native runtime and Windows build pending.

### Phase 20A: particle history reliability and restore-plan read batching

Reject invalid ticks and oversized modern samples before backward-history reset; legacy rejects incomplete/oversized samples before replacement/reset and keeps its immutable lease on rejected input. Valid backward capture still resets history. Modern partial snapshots remain diagnostic records. Batch the detached attribute allocation base and pointer/stride tables into one 0x328-byte bounded read, falling back to original field reads. Reuse the already built and validated child index for forest traversal. Portable regression fixtures updated. No persistent native collections or queued native effects; runtime performance unmeasured.

### Phase 20B: independent x64 build dependency fix

Retrieved actual push workflow results via the branch Actions collection. Phase 18E run 46062340354: Win32 passed, x64 failed C1083 in d3d9Hooks.cpp because afx_depth_ps20.h was not generated in a clean x64 checkout (Win32-first multibuild hid the dependency). Generate the architecture-independent depth and PGL shaders in both architectures; stage all six matching Source 1 .acs files alongside the x64 DLL/PDB artifact for replacement in a compatible existing installation. Phase 19E portable CI passed both jobs (46066625702); exact newer Windows build and full native runtime remain pending. Intermediate Windows jobs canceled on superseding commits are not build failures.

Phase 20A CI follow-up: run 46066828234 failed the new native history lease assertion; the fixture called At(10) before comparing the leased tick 11, which legitimately replaces the single-entry decode cache. Check lease identity before querying other ticks. Overlay CI passed. Production history implementation unchanged; rerun exact regression workflow before claiming success.

### Phase 20C: shadow appearance capture and same-tick history merge

At the successful 1f5ac2ae checkpoint, portable CI 46067007297 and both Windows Source 1 jobs 46067007302 passed; exact x64 replacement artifact 15232185074 published. Preserve texture rectangles from verified simple-shadow material pairs; non-simple or unknown appearance still uses the existing projected-blob approximation (reported separately), not historical atlas/silhouette reconstruction. Read shadow slot records in one bounded copy with narrow fallback and recheck list head/tail/count/data. Same-tick partial surface captures now merge missing subsystems in the same world without replacing an authoritative empty sample; reject oversized combined frames before any mutation. New fixtures and runtime validation pending.

### Phase 21A: experimental historical studio model lighting

Phase 20C portable CI passed, and its x64 build passed; confirmed 1f5ac2ae remains the earlier full Windows/portable fallback. Exact engine native model lighting helper (0xf2dd0, ordinary DrawModelExecute return 0xee463) observes fresh studio ambient and local light setter calls within the helper. Capture only main-view same-frame renderable/model checksum/skin/body/origin matches, reject static vertex-lighting or ambiguous observations. Store pointer-free ambient faces and up to four normalized Source LightDesc_t values in scene entities; charge sample memory, sample nearest, preserve neutral fallback. Replay via existing studio state scope only when previous ambient/light state is known for that studio. Opt-in `mirv_cache visuals lightingCapture 1` before recording and `mirv_cache visuals lighting 1` for preview; batch includes both. Bench/status reports sampled coverage, not pixel proof. World/brush lightmaps, baked vertex lighting, cubemap/ambient environmental state and unobserved models remain unsupported. Added portable lighting policy/cache fixtures. New Windows build and TF2 runtime pending. Scene cache now rejects oversized backward frames before clearing a valid prior take.

### Phase 21B — lighting integration and storage

Fixed the portable integration fixtures for the new audit API and studio ownership tracking. Added native-adapter fixture coverage for historical float4 ambient and local light submission in immediate and queued mock modes, exact restoration, and neutral fallback when previous light state is unknown. Equal consecutive lighting samples share immutable storage for matching entity/model identity, charged once across retained frames and released with the last reference. Preserve observer-unavailable status rather than replacing it with misleading zero coverage. No claim of in-game verification.

### Phase 22A — current-world receiver gating for detached decal replay

Exact ordinary world draw 0xe7010 scopes its current render list to its own nested dispatcher. Traverse current group opaque receiver buckets read-only; only submit detached polygons (including surviving opacity replacements) whose receiver is present. Unknown or malformed list disables detached draws and replacement gates for that group, not native surviving visibility. No change to retained geometry or live decal lists. Bounded temporary reads and diagnostics for receiver culled/unknown counts. This is a conservative ordinary opaque receiver check, not full visibility equivalence for all surface classes. Portable fixture covers malformed/cyclic list, out-of-range receiver, empty group. Exact Windows/native validation pending.

### Phase 22B — unsupported decal slot retention and safe contiguous particle copies

Historical capture retains sorted unsupported native slot IDs as immutable budgeted values, not native pointers. Same-tick merge and bounded preview carry them with their source decal sample. An unmatched live slot that was unsupported historically is left live, avoiding false future-only suppression. Verified exact world bucket builder offsets in engine 0xd240b..0xd25d3 and ordinary world draw caller 0xe10e7, added byte guards. Native particle restore plans coalesce only contiguous source and destination words within a single verified collection, never across gaps, constants or native collection boundaries. No new queued effects or native resource retention; portable fixtures and exact native build required.

### Phase 22C — historical shadow projection clip planes

Capture the verified native shadow slot extra clip planes (count +0xf8, up to four normals +0xfc, distances +0x12c), validating finite values and preserving known-empty versus unknown coverage. Replay through shadow manager slots 12/13 only on the owned historical shadow, after projection, matching SDK call order. Unknown clip coverage prevents suppressing live shadows under the existing partial-overlay policy. Reports include restored planes and unknown counts. This reduces historical shadow poke-through/back-casting; no shadow atlas/silhouette capture yet. Portable fixtures and exact native build pending.

### Phase 23A — experimental queued ordinary world decal visibility

Phase 22C full portable CI 46070910889 passed. Split exact stationary-demo eligibility from the immediate particle guard. New opt-in `mirv_cache surfaces queued 1` (included in surfaces batch) supports only visibility of surviving nonfading ordinary world decals in exact queued context, using immediate-recorded surface history. At the native producer's plain/sorted gather functions, compact only the newly appended scratch pointer-vector tail before category counts and native queue copy; preserve retained pointer order and nonempty sorted material/outer boundaries. Validate vector header, identity, current world, writable scratch range, caller, context, and malformed marker grammar; fail open on uncertainty. Never alter actual decal records, native queue ownership, or retain engine pointers past the ordinary view. Fading records remain native to preserve expiration bookkeeping. No queued removed geometry, opacity, shadows, native modern or legacy particles. Automatic benchmark adds one queued decal phase when requested, same actor culling as preceding control; full immediate suite unchanged. Portable fixtures added; native Windows and TF2 validation pending.

Phase 23A follow-up: freeze the optional queued visibility benchmark phase in the initial configuration and restore its toggle on stop. Reset queued phase status until evaluated; count rejected initial gather headers. Correct phase 14's legacy replay gate: it previously disabled both particle paths despite the modern-only phase label. Keep the ordinary preview master-toggle behavior unchanged. The overlay fixture now exercises actual legacy draw dispatch with modern replay disabled in phase 14 and verified non-main view skipping.

### Phase 23B — consolidated validation handoff

Fix the non-main counter fixture to compare a delta, since the legacy-only test also increments it. New `mirv_cache validate setup` requires empty history, supported stationary TF2 x64 immediate context and no active operation; it enables broad capture/replay without seeking, changing queue cvar or starting recording. Blanket surface hiding remains off. `mirv_cache validate report` saves one combined coverage, native status and benchmark report without interrupting benchmark or transport. x64 artifact includes a current validation guide and SHA256 build manifest. No individual intermediate runtime tests needed. Native queued particles/shadows, queued detached decals, displacement/model decals and atlas silhouettes remain limitations. Exact CI/native build and runtime pending.

### Phase 24A — limited queued legacy particle overlay

69de337b portable regression workflow 46072496059 passed. Exact client simple-emitter path consumes scratch emitter and particles synchronously into ordinary native dynamic mesh vertices/indices; verified queued material context owns the staging buffers and enqueues mesh data plus counts, not historical particle objects. Add explicit `mirv_cache particles legacyQueued 1`, limited to verified spark/muzzle materials and exact queued material system/context/mesh methods; broad named legacy replay and modern particles remain immediate-only. Live effects stay native in queued mode (overlay, not exact replacement). No historical native objects survive callback. Skip modern snapshot decode in unsupported queued context. Consolidated validation profile opts in and automatic benchmark adds one legacy overlay comparison with same preceding decal/model config (34 phases when both queued experiments requested). Portable fixture exercises dispatch/phase isolation; exact native build/runtime pending.

### Phase 24B — experimental queued removed-world decal geometry

Phase 24A corrected portable workflow 46073406331 passed. Extend the existing queued decal option plus removed toggle to submit absent historical ordinary world polygons through verified synchronous direct mesh construction into native queue-owned staging buffers. Restrict to current-world receiver list, exact queued context/mesh, no debug-material override, already-precached proxy-free ordinary material with an unambiguous surviving native record as lifetime witness. Do not load new materials or queue historical records/polygons. Surviving opacity replacement remains immediate-only; unknown slots stay live. Same queued decal benchmark phase now includes removed geometry when requested. Native runtime and new Windows build pending.

### Phase 24C — queued replay ownership hardening

Phase 24A checkpoint 5a8ea46e passed portable CI 46073406331 and Windows x64 job 140755488017; exact DLL artifact published. Revalidate queued decal witnesses against current native slot table, and split static direct-render ABI checks from per-draw idle mesh validation to avoid redundant signature reads. The alternate debug-material path is rejected when both verified cvars select it. Legacy queued replay now requires a private matrix getter installed during immediate rendering, TLS scoped to the exact simple-emitter transform callsite; the queued path never writes the live manager matrix. Require precached proxy-free native material pages for queued legacy replay. Ordinary immediate fallback remains when the optional observer is unavailable. Native tests pending.

### Phase 24D — modern particle recorded-only preflight

Reject already impossible detached trees before calling the native factory: missing/unterminated name, invalid clock, recorded count beyond verified capacity, incomplete attribute mask or invalid word block. Propagate incompatible descendants to ancestors while preserving existing root-first fallback to independently valid child subtrees. This only moves existing rejection conditions earlier; no recorded words are altered and no renderer/lifetime restriction is relaxed. Bounded name handling also protects explicit probe. Benchmark reports factory attempts avoided. Modern sprite 0x8f5a00 performs material variable setters at 0x8f5a76/0x8f5b97 and dispatches other orientation-specific renderer functions, in addition to collection-level post-render callbacks; a general deferred replay contract is still not established. Keep modern immediate-only until shared shader-state and all renderer lifetimes can be proven. Runtime performance unmeasured.

### Phase 25A — conservative live particle isolation

Phase 24D portable CI 46075187161 passed. Blanket isolation requires recorded detached prerequisites and consistent particle totals, not just capture flags. Cache eligibility on the immutable decoded lease. Ordinary preview also requires complete legacy coverage when requested, and latches failed/incomplete modern submissions on the current decoded sample to retain live particles on following renders. A new decoded lease invalidates this decision; the first draw can still fail. Benchmark ablations keep recorded-only isolation eligibility independent of prior phase outcomes. Combined report records eligibility. No pixel validation or change to queue restrictions.

### Phase 25B — coordinated capture epoch transitions

Move model frame acceptance ahead of effect history updates. Rejected backward/new-take model frames preserve the entire previously retained take; a pending transition barrier prevents subsequent effect updates from contaminating it. A later accepted frame explicitly starts a new model epoch even if the demo has advanced beyond the old endpoint, and clears all companion effect histories together before recording their new samples. Ordinary forward rejected model frames retain existing independent effect capture behavior. The first frame after a reset may lack same-view surface geometry/projection observations (discarded with the prior native observation generation); later frames repopulate them. Combined report includes pending transition flag. Tests exercise rejected backward transition and accepted later sample. No changes to native render queue eligibility.

Phase 25B follow-up: block backward/pending-take diagnostic particle messages from resetting or contaminating the previous journal. Early messages are intentionally omitted until the first new model frame is accepted; the new diagnostic journal is not a complete lifecycle trace. Extend the decoded-lease failure latch to legacy replay. Benchmark ablations retain recorded-only eligibility. Headless fixtures cover both cases.

### Phase 25C — persistent isolation failures within retained history

Track failed immediate-mode particle submissions by resolved tick and history revision, not the one-entry decoded lease. Scrubbing or diagnostic lookups cannot silently re-enable blanket hiding for a known failed tick. History mutation invalidates the failure generation, allowing fresh capture to be evaluated. Ignore stale draw results after history changes; queued legacy overlay outcomes do not establish immediate-mode isolation failure. Benchmark ablations remain independent. Headless fixtures cover decode and scrub transitions. No change to native queue eligibility or pixel-coverage guarantees.

### Phase 26A — reuse immutable modern replay preparation

Cache recorded-only hierarchy, subtree active totals, prerequisite reasons and isolation eligibility once per decoded sample. Reuse the immutable preparation for native attempts and isolation, while retaining all exact-build, material-context, per-allocation, definition, copy and renderer checks on every submission. The preparation holds only a weak frame reference; no native pointers or extra decoded appearance buffers are retained. Mismatched leases cannot use the prepared traversal. Explicit probe and unprepared fallback remain available. Acquisition timing now includes preparation. Regression fixtures compare prepared and ordinary fallback order, lease identity and lifetime, malformed topology, and overlay reuse. Native performance impact unmeasured.

### Phase 26B — immutable shared shadow leaf storage

Share exact ordered BSP leaf lists between repeated observed shadow projections when the same native handle reports identical values. Historical shadows retain shared immutable integer lists, not engine addresses; projection/clip/appearance values remain independent. Surface history charges nested leaf lists once across distinct shadow vectors, releases them on the final reference and validates standalone budget before mutation. No quantization, native shadow atlas reconstruction, or queued shadow changes. Portable tests cover equality/order, changing projections, duplicate nested references, eviction and invalid leaves.

### Phase 26C — limited named queued legacy subtextures on anchored pages

Allow unambiguously registered historical named subtextures only when their current page is the exact page held by a verified live spark or combine-muzzle global. The existing queued opt-in, material page precache/proxy checks, UV identity, exact native simple-emitter and mesh ownership checks, private matrix and immediate-only general fallback are unchanged. Do not infer lifetime for arbitrary pages or load absent materials. Immediate named resolution also rejects ambiguous dictionary names instead of picking the first address. Status reports named selection eligibility/rejections (not pixel coverage). The existing consolidated queued legacy benchmark tests this extension; no additional user steps. Portable selection fixtures added. Exact native runtime pending; no new ABI offsets introduced.

### Phase 26D — legacy manager-wide isolation coverage

Record the complete observed legacy manager active total independently of selected exact simple emitter capture. A selected sample may be complete while unsupported/unselected active emitters remain; when legacy replay is requested, blanket isolation now additionally requires a known complete manager traversal and equality of observed, expected and captured counts, with no rejected bindings or unknown materials. Partial historical overlay remains available, but live effects stay visible when full coverage cannot be established. Intentional benchmark ablations retain their existing recorded-only comparison policy; explicit modern-only preview policy is unchanged. Console status and combined report include known active count, uncaptured count and manager-wide readiness. Portable fixtures cover selected-empty samples with unsupported active emitters.

### Phase 27A — preserve live modern particle geometry accounting

Reattached exact TF2 x64 DLLs match all three previous SHA256 identities. Native sprite/trail/rope geometry contributes to the live particle manager's +0x1e0/+0x1e4 counters and +0x1e8 over-budget byte, including an inline path. Scope those nine bytes across synchronous immediate detached replay; verify exact write/reset anchors, original values, same manager/current immediate context and monotonic counters before restoring, then verify readback. Unexpected resets or invalid state are not overwritten and count as cleanup failures (isolation fails open on following renders). No queued modern replay or change to recorded words. Portable fixture covers no-write, restore and uncertain transitions. Native validation still required.

### Phase 27B — limited queued nonfading surviving decal opacity

The exact engine dispatcher gathers categories 0/1/2 before native queue copy. Extend existing queued/opacity options to nonfading ordinary surviving world decals with captured polygons, current visible receiver and unambiguous precached proxy-free material. Prepare before dispatch; verify full live slot identity and base alpha before filtering; authorize detached replacement only if all three ordered gather categories succeed and the native pointer was actually removed. Recheck witness, alpha, context and mesh ownership before native detached submission. Fading survivors keep native bookkeeping and remain immediate-only for opacity. If a later category or post-dispatch guard fails after removal, that frame may omit the replacement; no pool mutation or historical pointer is queued. Existing benchmark includes this extension. Portable policy fixtures cover category completeness, failure, ordering and fading exclusion. Native runtime pending.

### Phase 27C — bounded batched legacy name traversal

Reverified exact material RB-tree lookup/insertion (24-byte records, name +8/subtexture +16). Batch the already used node fields into one bounded read with original narrow fallback, validate address arithmetic and recheck capacity as well as root/count/data. Replace per-character emitter/material name reads with bounded 32-byte chunks and single-byte fallback across unreadable boundaries. Preserve printable/nonempty/terminated requirements and no persistent native addresses. This reduces repeated queued legacy named material lookup overhead without changing supported pages. Portable fixtures exercise chunk/fallback, malformed name, overflow and node padding/read failures. Native performance unmeasured.

### Phase 28A — verified studio ambient overload ABI correction

The supplied studiorender.dll is exact TF2 x64 (SHA256 5dbf854792167261a7769b19634bb20273cdeb31d1cbcbd6b5b409c991cb8983, timestamp 6ac412f0, image 43d000). RTTI CStudioRenderContext primary vtable 0x81120 establishes slot 16 -> 0x599e0 (six RGB vectors; writes alpha=1 to each face), slot 17 -> 0x59940 (six four-float vectors, copies 96 bytes). Previous implementation and mock reversed these overloads, including neutral replay and ambient observation, risking overread and wrong colors. Correct input lengths, selector, stride, hook slots and fixture together. Exact PE/vtable/body guards before x64 studio-state hook installation. Unknown binary leaves hooks disabled. Rebuild and native verification required; no claim of model decal reconstruction yet.

### Phase 28B — Studio model decal inventory (capture only)

Exact-build immediate main-view observation of the native decal pass records selected LOD, nonempty material batch count, vertex and index totals per model identity. Shares existing lighting capture audit scheduling and consolidated report. Unknown/ambiguous samples remain unknown, not zero. No native decal handle or geometry is retained and no historical model decal rendering is enabled. Queued observation is intentionally disabled pending ownership/thread analysis.

### Phase 28C — model decal geometry decoder

Added portable bounded decoder for actual studio material vertex and segment lists. Copies finite position/normal/UV and all six native model selectors (including flex fields), flattens local segment indices into owned arrays, rejects invalid links/counts and out-of-range indices, and accepts material identity through a resolver. No live pointers or native padding retained. Decoder tested with reverse-linked vertex order and two independent segment index spaces. Runtime hookup and budgeted history are a separate step.

### Phase 28D — budgeted studio decal geometry history

Immediate main-view observer now captures validated position/normal/UV, native model selectors, material names and flattened triangle indices. Restricts material names to exact-build CMaterial (or its queue-friendly wrapper), rejects unknown types and malformed/oversized records. Per-model 256 KiB, per-audit 4 MiB, existing scene cache payload budget; equal consecutive immutable geometry shares storage, trimmed with the last frame reference. Optional geometry is dropped before it could reject a fitting frame. Counts remain useful when geometry cannot be captured. Consolidated report distinguishes geometry coverage from inventory. No native replay enabled, and native performance/coverage still need the eventual consolidated test.

### Phase 29A — limited immediate historical rigid model decals

Historical decals setting now enables captured rigid, single-bone, no-flex model geometry on the exact captured LOD. Uses native material-batch draw within cached model's native decal callback, no registered decal handle or pool changes. Reports requested/submitted material batches; multi-bone, flex, queued, alternate configs and missing material remain unsupported. No in-game validation yet.

### Phase 29B — direct animated decal skinning inputs

Capture resident fat source vertex positions/normals, validated weights/bone indices for directly mapped model decal vertices after the original immediate draw. Keep no studio pointers; reject malformed/unsupported batches independently of raw geometry. Model decal geometry memory accounting now includes optional skin source arrays. Portable fixture checks exact MDL ranges, invalid selectors/bones, weighted pose-to-world, normal offset and UV/index preservation. Animated native submission still pending.

### Phase 29C — limited animated historical model decal rendering

Owned source vertices can now be software-skinned against the actual historical pose-to-world array and submitted via the native immediate dynamic mesh. Exact private-record writer detours bypass only native multi/flex vertex-cache lookup for our own already evaluated vertices. Live model rendering unchanged. Requires all batches directly skinnable, matching selected LOD/skin/body and ordinary proxy-free material. Flex weights are not restored (base mesh only), no generated/clipped-vertex reconstruction or queued path. Validation report shows directly skinnable captured batches and total requested/submitted model decal batches. Runtime and Windows validation pending.

### Phase 29D — studio frontend state preservation and compressed model decal fix

Use verified native context snapshot before textured replay; decline setter changes on un-restorable state rather than restoring guessed defaults. No direct writes to native context. Model-decal guard now uses model topology bone count for both raw and compressed poses. Portable parser tests; native runtime still pending.

### Phase 30A — projected shadow atlas metadata

Exact client build: 0x24c190 initializes the material pair `decals/rendershadow` (0x24c1ba) and `decals/rendermodelshadow` (0x24c1f3), then initializes `_rt_Shadows` (0x24c20e), 1024 by 1024 (0x24c21e, 0x24c244), through texture reference at +0x60. Preserve bounded historical UV rectangles only for this verified name pair, separately from simple shadows and other unknown textures. Atlas classification and UV participate in existing history equality; replay deliberately uses the full simple blob rather than sampling stale live atlas contents. Report counts atlas coordinates separately from reconstructed silhouettes. No texture pixels or native texture pointers are retained; exact-tick GPU capture, texture lifetime and private atlas binding remain unresolved.

### Phase 30B — optional lossless shadow atlas history

Added bounded 1024×1024 four-channel atlas codec (run/literal words, exact decode, reject oversize or malformed); immutable payload follows the exact shadow sample, never a native texture. Optional 16 MiB atlas-only budget drops oldest atlas payloads without evicting projection/decal samples. 2 MiB compressed per-image cap; adjacent identical images share storage; no borrowing stale pixel data for newly captured projections. No native capture or atlas rendering in this commit. Portable codec/history regression fixtures.

### Phase 30C — opt-in native shadow atlas pixel capture

Exact client 0x248a6e pushes `_rt_Shadows`; 0x248cc6 pops after tile drawing. Hook immediate context pop at materialsystem 0x34a30, exact caller 0x248ccc. Verify context stack top (+0x78/+0x88, 0x38 stride), CTexture vtable/name/dimensions/RT flag, native handle array +0x88 and shader API handle +0x60, single copy +0x22, texture flag +0x5a. Compare actual raw D3D9 current RT container with shader texture identity; require unmultisampled 1024x1024 ARGB/XRGB render target. Flush(false) before readback, system-memory surface and pitch-aware lock, bounded lossless encode; no render-target mutation or native texture retained. Same engine frame, demo tick, world generation, projection revision and slot witness required; ambiguous repeated passes rejected. Command `mirv_cache surfaces atlasCapture 1` is opt-in, independent 16 MiB budget. CPU capture only, no atlas restoration. Native readback cost and coverage unknown.

### Phase 30D — atlas owner witness and replay constraint

Client constructor callsite 0xe054/0xe05b identifies CClientShadowMgr singleton +0x10e4800. Require exact vtable +0xb36880, initialized byte +0x118, its texture +0x60, and current world/model material refs +0x20/+0x28 matching the atlas pair before readback. Skip GPU readback when no observed eligible atlas projection exists. Distinguish a failed install from intentionally disabled capture in sample status.

Replay is intentionally still blob-only: the exact client has distinct CShadowProxy (vtable +0xb36e90, OnBind +0x24d250) and CShadowModelProxy (+0xb36ec0, OnBind +0x24d170). Both set the texture from singleton +0x60. The model proxy additionally indexes the live client shadow pool (+0x10e4838, stride 0x88) using the low 16 bits of proxy data, then queries the engine handle at +4 and writes UV/falloff/world-to-shadow parameters. Existing detached engine shadow handles use synthetic proxy data 0xffff; passing that to the model proxy is not safe. Atlas replay therefore needs a private binding path (or verified proxy replacement), separately scoped for world and model receivers, rather than simply switching the materials or rewriting the live atlas. No such native path is enabled here.

### Phase 31A — native particle capture and history decode hot paths

Batch the live appearance pointer/stride tables with original narrow fallback.
Reuse the diagnostic XYZ payload only when its successful read and the current
appearance table agree on address, stride and exact word count; the constant
and varying paths preserve their original 32-bit lane values. No additional
native lifetime assumptions beyond the existing quiescent capture boundary.
Replace byte-at-a-time history match decoding with bounded nonoverlapping
copies, including overlapping matches, without changing the compressed format.
Portable fixtures compare broad/narrow capture, count XYZ payload reads, and
exercise explicit overlapping and nonoverlapping match tokens. No measured FPS
gain yet. Modern queued rendering remains disabled: shared renderer material
mutations and deferred ownership must be established separately; no queue-mode
switch, lifetime extension, or live isolation relaxation is introduced.

### Phase 31B — reuse per-callback detached plan storage

Reuse copy-operation and binding vector capacity across native tree attempts.
Reset all logical entries, counts and errors before planning a fresh allocation
and before the owner destroys that allocation (after active-count cleanup).
No address, native validation result, or detached object is reused across
attempts; no cross-callback persistence or queued-render eligibility change.
Portable fixtures cover success, early failure and retry with reused capacity.
This targets allocation churn during repeated historical rendering; native
FPS and Windows build remain unverified.

### Phase 31C — modern sprite queue contract research and bounded census

Reattached all TF2 binaries; client, engine, materialsystem, studiorender and
shaderapidx9 x64 SHA-256 identities exactly match Phases 18B/28A. Native
CMaterialVar already has queue-aware integer and matrix setters: the
orientation integer is copied into its functor, and the orientation matrix
is copied in full (64 bytes). This resolves temporary argument storage for
the exact ordinary setter branches, not ownership of the raw material
variable pointer, alternate/proxy paths, or shared material state. Exact
queued context camera queries are synchronous and copy producer-side camera
values. See Phase 31C in SCENE_EFFECTS_EXPERIMENT.md for addresses.

Extend the existing immediate benchmark warm-up census with active attempted
node/particle totals, exact single C_OP_RenderSprites vtable/target, orientation,
factory scratch flag, post-render callback presence, and existing ordinary
precached proxy-free material guard. Report a narrow intersection (orientation
0, no auxiliary scratch branch or post callback, ordinary material) solely to
prioritize future renderer investigation. Counts may include fallback attempts;
they are not coverage or permission to queue. No additional user command or
queue-mode change. No queued modern rendering enabled, no new native calls,
no retained pointers. Portable fixture validates census accounting.

### Phase 31E — factory-independent ordinary billboard CPU geometry

Implemented a portable pointer-free geometry path: decode owned position,
radius, rotation, tint, alpha and alpha2, then construct view-space quads
with deterministic optional distance sorting, full UVs and 16-bit indices.
Missing/malformed attributes, invalid camera, excessive counts and nonfinite
vertices reject the collection; no historical native address is needed.
During existing immediate warm-up only, compare current unambiguous narrow
live witnesses with historical names and build geometry using exact guarded
camera getters. Report CPU time and generated vertex/index counts, without
submission or material mutations. This is an approximate ordinary no-sheet
billboard kernel (not native pixel equivalence); sheets, shaders, z-offset,
special visibility, trails and ropes remain excluded. Actual queued draw,
material binding/lifetime and mesh descriptor contract are the next step.
No changes to runtime queue eligibility or live isolation.

### Phase 31F — first experimental independent queued sprite submission

Portable exact mesh packing (world XYZ, native BGRA, float2 UV, 24/32-byte
stride) and strict lock-descriptor validation. New opt-in
`mirv_cache particles queuedBillboards 1`, default off, uses independent
geometry only in actual queued context with normal native preview enabled.
Requires a current unambiguous registered ordinary leaf sprite witness,
no sheet/post callback/auxiliary scratch or z offset, exact native material
identity, already precached proxy-free real material and embedded wrapper,
no orientation vars or alternate shader flag, exact supported vertex format,
valid camera and existing identity model matrix. Pins the original and target
materials with the native wrapper's increment / ordered decrement calls.
Use the engine's own dynamic mesh allocation, lock, copy, unlock and draw;
restore the original material, no detached factory or particle renderer.
Any malformed lock descriptor discards instead of drawing, and stops
additional submission in that callback. Live particles are NOT hidden.
Independent geometry uses world positions to avoid overriding the view;
approximate visibility/rotation/sorting, no native pixel guarantee.
Full automatic benchmark adds a separate queued billboard phase only when
explicitly enabled; no setup default change. Requires in-game validation.
