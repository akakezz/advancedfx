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
