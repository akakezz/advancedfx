# Scene Cache 2: particle capture and replay research

Read [SCENE_CACHE_HANDOFF.md](SCENE_CACHE_HANDOFF.md) first for the current workflow, measured baseline, limitations and next tests. The phases below preserve historical findings; their older defaults and instructions describe the implementation at that time.

## Phase 1: particle event feasibility

This opt-in, read-only probe records the public Source SDK 2013 particle tool
messages before the AGR entity-handle guard. It does not yet capture individual
simulated particles, child collection state, material state, shadows, or legacy
effects. It cannot render or suppress historical/live particles. It does not
enable tool recording automatically.

## Runtime test (TF2 first)

1. Before opening the demo: mirv_agr enabled 1
2. Enable the existing full renderable capture, then mirv_cache effects 1.
3. Load the demo, seek to just before an effect-heavy section, pause, run
   mirv_cache clear and mirv_cache record 1, then play continuously for 5–10 seconds.
   Prefer recording before an explosion/trail starts. Do not seek during capture.
4. Pause and run mirv_cache record 0; mirv_cache status; mirv_cache effects status.
5. Run mirv_cache effects at <tick> for several recorded ticks, including before and after an effect.
   Try the same tick again after scrubbing cache preview backward.
6. Compare normal and looking-away captures of the same clip in separate fresh runs.
   Send status and several historical sample outputs. Do not assume a reported
   system is active: stopped systems can have residual particles, some destroy
   messages can be unreliable, and already-active systems may lack creation records.
7. mirv_cache effects 0 disables capture; mirv_agr enabled 0 disables tool mode
   when no other AGR work needs it. Existing AGR docs warn that some games need
   mode enabled before loading a demo and may leak memory with mode enabled.

The journal has a separate fixed 131072-event limit (roughly 23 MiB payload);
it retains the beginning and explicitly reports overflow. It clears with the
scene history and on backward observed ticks. Its timestamps are demo ticks at
delivery, with SDK client time retained separately. Historical queries rebuild
reported system/control-point state without touching the engine. Capture gaps,
seek, non-tool effect paths, suppression, and exact per-particle replay are
not solved. The SDK implementation of the destruction message even appears
to populate its ID from client time; the probe reports unmatched destruction
rather than guessing. Do not treat event totals as complete particle coverage.

If messages are absent, the next step is native collection-manager discovery
in the actual client binary. If they are present, use the coverage results
to guide that same investigation; replaying events alone will not guarantee
exact random-access particle state.

## Follow-up: compacted control-point traffic and native callsite audit

The initial runtime probe received over two million events and filled the journal.
Setter updates with identical values are omitted across ticks; changed setters in
one tick are coalesced to their final value per ID/point/type within each lifetime.
Creation, destruction and emission messages are never coalesced. Raw counts,
paused messages, omitted repeats, replacements and first overflow tick are reported.
A destruction ID equal to integer client time is ambiguous (the public SDK has
that exact assignment); it is counted but does not erase a possibly unrelated
system. A matching legitimate ID can also satisfy this heuristic; treat it as
uncertain, not proof of a bug. Historical samples remain diagnostic, and a cap
hit still marks all later samples incomplete. The journal is separate from the
studio-pose budget, and its event payload excludes the bounded lookup index.

The first decoded message of each type also records a short stack audit of return
addresses inside client.dll as module-relative offsets. No native functions are
called via these offsets, no engine pointers are retained, and an empty stack is
explicitly reported. These addresses can help locate the native particle paths in
the exact tested binary. Please provide the new status output, including the
compaction and client RVA lines. Stop recording before running status to freeze
counts; report the client game/build if possible.


## Phase 2: native position capture (exact supplied TF2 x64 build)

PE timestamp 0x6ad26f17; SizeOfImage 0x133a000; SHA-256
b6f45c74805edacf0cf61d965f658158fc22960f66d583ef3a6549eb56696b95.
The module checks timestamp, image size, architecture and seven code anchors,
not a full runtime file hash. Other builds fail closed.

Disassembly: ParticleMgr getter RVA 0x2d6df0 returns static manager
0x10f5020; initialization flag 0x10f51a0. AddEffect 0x2d4220 and
UpdateNewEffects 0x2d8a00 corroborate manager +0x60 root head and
root +0x27b8 next/+0x27c0 prev. Constructor 0x2d9d80 uses base
collection +0x20 and root vtable 0xb60c70. Recursive count 0x2d4920
reads collection +0x38 active, +0x22b8 child head, +0x2280 next child.
GenerateSortedIndexList helper 0x914350 reads +0x2310 position base
and +0x2510 float stride, then SIMD XYZ at +0/+16/+32 bytes.
Destructor at 0x2da1fc converts client time to integer ID, confirming
the event stream defect for this binary.

Run a fresh short demo capture:
- mirv_cache effects 0 (event recorder unnecessary)
- mirv_agr enabled 0 (if not using AGR)
- mirv_cache clear
- mirv_cache particles 1
- mirv_cache record 1
- Play, then pause and mirv_cache record 0
- mirv_cache particles status; mirv_cache particles at <tick>
- mirv_cache preview 1; mirv_cache particles markers 1
- Scrub with mirv_cache goto <tick>; compare diagnostic crosses.
- mirv_cache particles markers 0 and mirv_cache particles 0 to disable.

Position capture shares the existing engine-thread frame-end capture boundary,
one snapshot per observed scene tick. It does not modify the native particle
manager or require tool recording. Root vtable, link consistency, read success,
count/stride bounds, traversal cycles/depth and finite positions are checked.
No engine pointers survive a capture. The independent rolling position history
is bounded to 64 MiB of frame/point payload and 4096 frames; a sample is exact
tick only. The display draws at most 8192 crosses using existing scene guides.
It does not suppress live particles, interpolate particles, retain identities,
restore materials, render native particle systems, or guarantee that off-screen
collections have been simulated. Capture cost includes a separate native probe
timer; model capture timer includes this cost too. Windows/in-game verification
is required, particularly synchronization with the game's simulation jobs.


## Phase 3: optional native appearance snapshots

Same verified client.dll. In addition to position-only capture, the opt-in
appearance mode copies the current 22 SDK-defined attribute slots in their
original four-wide layout, including constant lanes and integer IDs. Null slots
remain absent. Current collection time, copied definition name, and frame-local
parent indices are retained for empty and nonempty nodes. Definition-name
accessor at RVA 0x8e1530 reads collection +0x58 then definition +0x240; attribute
copy helper at 0x8e35ed corroborates pointer table +0x2310 and stride table
+0x2510; creation-time initialization at 0x8e2646 reads collection +0x34.
Extra code anchors gate appearance reads separately from position-only reads.

Test:
- mirv_cache clear
- mirv_cache effects 0
- mirv_cache particles 1
- mirv_cache particles appearance 1
- mirv_cache record 1
- Play a short effect-heavy section, pause, mirv_cache record 0
- mirv_cache particles status
- mirv_cache preview 1; mirv_cache particles markers 1
- mirv_cache particles styled 1
- Scrub, compare with styled 0. Styled crosses use captured radius (clamped
  for readability), tint and alpha times alpha2. They remain diagnostic geometry,
  not camera-facing native particles. Missing attributes use the previous marker
  fallback; a rejected position prevents incorrect attribute association.
- Send status with per-attribute coverage and named collections.
- Disable appearance capture with mirv_cache particles appearance 0.

A separate per-frame 8 MiB packed-word cap and the existing 64 MiB rolling
history bound memory. Appearance snapshots will retain fewer ticks than XYZ-only
mode; measured capture cost includes all extra reads and allocation. Complete
appearance means readable known slots, not full renderer/simulation state.
No persistent engine pointer, initial-attribute buffers, renderer contexts,
control point data, collection lifetime identity or native rendering is included.
The original position-only mode and uniform markers remain default. No live
simulation or rendering state is modified.


## Phase 4: detached allocation dry run and playback investigation

Actual TF2 x64 allocator component mask 0x68055 at 0x8e2b70 differs
from the SDK: rotation has three lanes, trace_normal slot has one.
Capture the raw native width, not the public SDK width.

Particle marker preview now holds the preceding recorded sample across
at most two absent ticks within retained coverage; it never extrapolates
outside that coverage or selects a future sample. Exact status/at queries
remain exact and also show the resolved preview tick. This can remove
short gaps but cannot restore evicted history. Replay audit counts model
submissions, queue-limit skips, other failures and maximum callbacks per
engine frame. Readiness fields are atomic. No claim that the video proves
queue saturation, duplicate views or a cache gap.

Experimental one-shot: pause the underlying demo, stop cache recording,
select an exact retained appearance tick and run mirv_cache particles status.
Choose a numbered nonempty collection, preferably a simple leaf, then
mirv_cache particles prepare <index>. It executes at the next FRAME_RENDER_END.
Read the native detached probe line in console or status. Factory 0x8e0490,
manager pointer 0x10067e0, destructor 0x8de590, aligned delete 0x8decd0.
Exact build and code anchors required. The native factory temporarily links
the collection to its definition; this is NOT a live CNewParticleEffect.
Validate matching definition tree, zero active particles, native capacity and
sequential varying allocations. Copy only validated varying blocks into the
fresh native memory, read back, compare constants without writing them, and
destroy immediately. No active counts, clocks, control points, renderer
contexts, live particle suppression or native draw are changed. Recursive
renderer identified at 0x8e4840 but queued lifetime still unverified.
Native allocation itself may have engine side effects or fail; test on a
disposable paused demo session first. This is not visual replay.


## Phase 5: queue headroom and longer particle history

Runtime reported 67,106 queue-limit skips, zero other skips, two callbacks
per engine frame, 512 pending. Increase bounded studio retirement limit to
2048, retaining the existing completion-based resource ownership. This is
headroom, not a proof that duplicate views or live overlap are resolved.
Retest the same frozen gib-heavy scene and inspect replay audit deltas.
For an independent diagnostic, compare normal queued rendering with
mat_queue_mode 0 (restore original setting afterwards). Do not change
the user's engine queue mode automatically.

Particle history default budget is 512 MiB, configurable with
mirv_cache particles budget <64..2048> (MiB), and a 32768 frame hard limit.
Budget changes affect current history immediately; increasing it cannot
recover previously evicted samples. Exact duplicate constant attribute
blocks are shared within each frame, and excessive vector capacity trimmed
before retaining appearance snapshots. Raw bits and varying blocks remain
unchanged. Eviction and rejection counts are displayed. Model and particle
history are still separate; they can retain different intervals.

The one-shot probe now accepts mirv_cache particles prepare <tick> <index>.
Use the same exact tick from particles at <tick>, stop recording and pause
the live demo first. Reject empty/unnamed selections and report the exact
selected tick, definition and active count. A zero-copy run is inconclusive,
not verified restoration. No native drawing or live suppression added.


## Phase 6: lossless independent particle history compression

The nonempty detached restore succeeded (45 particles, 9 varying blocks
and 13 matching constants); the expanded render retirement limit also
eliminated measured queue skips in the tested scene. Leave native rendering
and studio submission code unchanged in this storage iteration.

History now stores explicit compact metadata, only present attribute
descriptors, variable-length names, and byte-planed native attribute words.
Position crosses are reconstructed from the packed XYZ when exact bitwise
equivalence was verified during capture; otherwise original positions are
retained. A small bounded match/literal codec compresses each snapshot
independently; incompressible data uses raw packed bytes. No quantization,
sampling reduction, attribute removal or dependence on previous ticks.
One decoded sample is cached for repeated preview requests under the scene
mutex. Stored frame capacity (including record metadata) is budgeted;
decoded scratch and transient encoder memory are separate. Status reports
original, packed and compressed retained payload and decoded scratch.
Capture timing includes encoding; actual compression ratio/cost needs
measurement on the same demo. Temporal cross-frame sharing remains future
work. Repeat capture, random scrubbing, styled markers and the explicit
nonempty prepare test to verify unchanged data after decoding.


## Phase 7: scene-wide native particle replay

Opt in with Scene Cache > Capture > Capture particles (TF2 x64 experiment),
or simply mirv_cache particles 1. This also enables appearance capture,
disables the earlier event probe, and selects native textured preview.
Preview > Cached particles selects native, styled or uniform markers, or off.
Memory > Particle budget MiB sets the existing history limit.
No AGR recording required. Record fresh history, pause or freeze the live demo
(overlay Freeze / demo_timescale 0), enable cached preview, and scrub normally.
The explicit detached probe still requires demo pause; both paths reject seeks. Native replay currently needs immediate material
rendering: use the explicit overlay button or mat_queue_mode 0 once (restore
your original setting afterwards). No automatic global cvar modifications.
At this phase live particles were not suppressed. See phase 8 for the
optional scoped isolation experiment; preserve the user's original cvar value.

Every nonempty recorded root subtree is attempted at the existing main-view
PostRenderAllTools callback. Separate TF2 render passes are excluded. Native
SetupCurrentView stores view ID at 0xfe77c0 (guard at 0x34c926); only view 0
is rendered. Verified factory, owned varying attributes, matching constants,
active count +0x38, padded count +0x30 and scalar/SIMD collection clocks
+0x34/+0x20; native renderer 0x8e4840 with current material context and identity
model matrix. If a recorded root cannot be restored, try its child subtrees
without double-rendering successful trees. If a definition has no material
and recursive render would skip descendants, render reachable child branches
independently. Fresh collections are destroyed within the same synchronous
callback; no simulation advance, queued native pointer retention or live list
insertion. Capture and compression remain unchanged.

This is experimental rendering: counters are submissions, not confirmed
pixels. Missing/mismatched attributes reject a subtree; initial attributes,
control points, renderer-specific context, dynamic lights, sorting across
collections and historical texture/material state are not restored. Native
draw can have engine side effects. Immediate-mode guard is required until
queued resource lifetimes are established. Native particles may appear
incorrectly or fail to render; use styled markers to compare. No automatic
live suppression or per-effect categorization. Test with a disposable demo.

Native draw requires the underlying demo to remain paused or frozen at exactly
zero demo timescale; the cached scene's independent clock can still play or scrub
normally. Active counts are reset
before native destruction. Status uses the same floor-tick selection as preview.


## Phase 8: scoped live particle isolation (user-validated on 499fd28)

Opt in via Preview > Native textured > Suppress live particles (experimental),
or `mirv_cache particles isolate 1`. Default is off; this is not a persistent
preference. `isolate 0` restores ordinary overlapping preview behavior.

The existing TF2 View_Render wrapper (Tf2RenderPass.inl) brackets only the
ordinary render invocation. Read the registered TF2 `r_drawparticles` cvar,
set it to zero for the invocation, verify the write, then restore the exact
saved numeric value on scope exit. The cvar is never intentionally held across
frames. If it was already zero, no writes are necessary. If another component
changes it to a different nonzero value during rendering, preserve that change.
If restoration fails, status warns the user to check the cvar manually.
Do not change mat_queue_mode automatically. Separate TF2 recording passes and
normal demo playback must remain unaffected by this option.

Eligibility: native textured preview enabled, scene view available, focus/device
ready, a complete retained position AND appearance sample resolved using the same
bounded prior-tick policy as replay, and the exact verified TF2 x64 module (PE
identity plus factory/renderer entry anchors). Require paused or zero-timescale
playback, no load/seek/skip, and immediate material rendering. The existing deeper
native replay guards remain unchanged. Missing or partial capture keeps live
particles visible; a genuinely empty complete sample is eligible for isolation.
Styled/uniform marker modes and Off do not isolate. No cache format change.

This is a draw-cvar experiment, not a native collection deletion, simulation
freeze, per-effect filter or additional binary detour. It may also hide legacy
or skybox effects that are not reconstructed in the cache. The 499fd28 runtime test confirmed the direct native renderer remains visible
with r_drawparticles both on and off. Coverage of every live draw is not proven. Even a complete sample can fail a native
restore later in the render callback; some effects may therefore disappear.
Keep the option off if comparison shows lost historical particles.

Validation: test both Pause and Freeze, stationary tick and backward/forward
scrubbing, complete empty frames, missing coverage, switching particle modes,
leaving preview, seeking, focus changes, capture and separate recording passes.
Compare isolation on/off at the same camera/cursor with visibly different live
and historical effects. Query `r_drawparticles` before/after and confirm its
previous value is restored; `mirv_cache particles status` reports requested
isolation and last ordinary-render outcome. Do not interpret cvar success or
native submission counts as pixel coverage. Report new artifacts before tuning
restoration rules; the previous intermittent large wedges are no longer
reproducible as of the 3a9b8fd test and were not specifically fixed.

Regression fixtures exercise eligibility and scoped cvar behavior in
`tests/cinematic/test_overlay.py` and `tests/cinematic/test_tf2_native_render.py`.
A standalone C++ mock of the scope passed ordinary restoration, nested scope,
already-zero, refused setter, missing cvar, intervening edit, and restore failure
cases. Full fixture execution, Windows compilation and runtime verification
remain outstanding at the time of this change.


## Phase 9: special-effect, shadow and decal coverage investigation

User test of 499fd28: isolation works; at tick 2905 all 1941 recorded active
particles were submitted across 37 trees, zero rejected attempts. The handoff
records the full performance baseline. Screenshots show a missing bright
energy/death effect and altered body shading (Engineer), a live-position shadow
while a cached actor moves (Demoman), and user reports persistent live decals.
The exact missing-effect tick and effect name have not been established.

Read-only diagnostics added:
- `mirv_cache particles list <tick> [start [filter]]`: exact appearance tick,
  64 matching records per page, including empty parents, original indices,
  counts, clocks, attribute masks. Prints next page command when needed.
- `mirv_cache particles find <tick> <substring>`: case-insensitive name filter,
  also includes empty parents and reports aggregate matching active count.
Use these rather than assuming the first eight nonempty rows of status are the
whole frame. An absent exact tick is not an empty sample. No cache format,
rendering or isolation behavior changed.

Public SDK leads (not verified offsets for the shipping client):
- [TF ragdoll code](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/tf/c_tf_player.cpp)
  starts named effects including electrocuted_red/blue, and can invoke
  DissolveEntity and dissolve attached cosmetics. Do not assume the screenshot
  is this exact effect. Search electrocut, dissolv, vapor, ash and related names.
- [C_EntityDissolve](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/c_entitydissolve.cpp)
  uses a CSimpleEmitter and SimpleParticle allocations for parts of its effect.
  This is a potential older particle path outside our CNewParticleEffect forest.
  Model fading/material overrides are separate from particle geometry. Inspect
  captured definitions first, then verify actual target binary before extending
  native layouts or hooks.
- [Client shadow manager](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/clientshadowmgr.cpp)
  builds shadow textures/projections from live renderables. Historical studio
  submissions do not automatically replace those projections. Investigate
  drawing from cached pose bones and historically correct projection, depth,
  receiver and lifetime; do not attach cached actors to live shadow handles.
- [IVModelRender](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/engine/ivmodelrender.h)
  exposes separate shadow setup/draw and model decal APIs. World decals need
  independent event/lifetime discovery, including pre-capture marks, seeks,
  removals, static map decals and model-local attachments.

Next manual probes, on a disposable demo with fixed cached cursor and camera:
1. Compare the missing effect at the same real demo tick, with cache disabled,
   and cached preview, with isolation on/off. Record exact tick; compare native,
   styled markers, and collection listings. Test r_drawparticles 0 live to see
   which portions use the live particle draw gate; restore original value.
2. Query r_shadows, test 0, then restore original value. Observe whether the
   detached shadow disappears and whether it returns. Do not change shadow
   texture allocation settings yet. This is isolation only, not historical shadows.
3. Query r_drawdecals (if registered) and r_drawmodeldecals, test separately at
   zero and restore prior values. Confirm existing marks disappear and return.
   Do not use r_cleardecals or modify decal budget/creation limits: those may
   destroy or prevent data and invalidate the comparison.

No automatic shadow/decal suppression or historical reconstruction added in
this phase: draw-cvar availability and semantics on the exact build, and
historical shadow/decal capture paths, still require verification. Prioritize
true historical state, not simply hiding all shadows or marks. Preserve the
working particle isolation baseline.

### Follow-up: verified draw gates and exact-tick collection list

On the diagnostic build 5da6306, the user reported the following:

| Probe | Observation |
| --- | --- |
| particles list 34, Engineer disintegration visible in original scene | 46 collections, 1010 active, position complete 1, appearance complete 1; no identifiable dissolve/electrocution definition |
| r_shadows 0, then 1 | misplaced shadow hidden, then reappeared at the live demo position |
| r_drawdecals 0, then enabled | observed decals hidden, then restored |
| r_drawmodeldecals 0 | no visible model decal difference reported |

The full tick-34 inventory contained doublejump, rocket/pipe trails and
muzzles, dispenser healing, teleporter rings/discs/glows, recent-teleport
particles, sapper sparks, and gutter drips. The active sample is not empty
and the known collection capture did not flag an incomplete read. Absence of
an identifiable name is not proof of a legacy effect: names can be indirect,
timing can differ, or the effect may not register in the traversed list.
Compare exact neighboring samples and the live effect with r_drawparticles
0/1 (restore original cvar). Do not infer a capture failure from a successful
submission counter or assume this identifies a particular weapon.

#### Stronger SDK evidence for the dissolve hypothesis

The public SDK C_TFRagdoll::DissolveEntity in c_tf_player.cpp creates a
C_EntityDissolve, configures effect color and fade intervals, and can dissolve
attached cosmetics. C_EntityDissolve::ClientThink in c_entitydissolve.cpp
sets parent render mode to kRenderTransColor and changes RGB/alpha using
fade functions. Its DrawModel checks frame time/readiness, resolves parent
hitbox-to-world matrices, computes view-dependent skew and emits legacy
SimpleParticle sparks/glows via CSimpleEmitter. This provides a plausible
explanation for BOTH the missing energy particles and the different body
color in the screenshot. It is an SDK lead, not a verified identification of
the exact screenshot effect or the shipping TF2 x64 ABI. Do not call DrawModel
on a live dissolver at an arbitrary cached tick: it can allocate particles
and consult current time, view and live parent bones.

Our SceneCache::Entity currently retains skin but not render RGBA/mode, and
Mirv_Tf2_DrawScenePose submits studio geometry using neutral lighting and
NO_FLEXES/NO_SHADOWS. Investigate a read-only audit of actual historical
render modulation/mode, independent of the legacy emitter inventory. If
verified, capture visual state with stable owner identity, then reproduce it
with scoped studio state and correct opaque/translucent ordering. A legacy
emitter snapshot would need its own validated layout, materials, size,
color, alpha, orientation, lifetime and renderer behavior; do not force it
into NativeParticleSnapshot's CNewParticleEffect layout.

#### Historical shadows: separate projection and silhouette problems

The SDK clientshadowmgr.cpp builds a visible shadow list, allocates shadow
texture atlas space, projects through the engine shadow manager and draws
silhouettes via IVModelRender::DrawModelShadowSetup/DrawModelShadow.
The latter accepts custom bones at setup, but this changes only the caster
silhouette and does not by itself update the world-space projection, atlas
allocation, clipping or receiver list. The current cached model draw is late
in PostRenderAllTools and explicitly uses NO_SHADOWS. Do not remove that flag
as a proposed fix or redirect live shadow handles to historical bones without
owning their projection/lifetime. Historical shadow replay needs a verified
pre-receiver render boundary and independent historical caster/projection
state, plus retirement/restoration when coverage, focus, preview or seek
state changes. Test first with one moving caster and a static receiver, then
attachments, multiple views, empty frames and moving receivers. r_shadows is
a confirmed reversible comparison tool, not a completed feature.

#### Decals

r_drawdecals is now a confirmed reversible gate for the observed marks.
Continue with read-only discovery of engine-side world decal creation,
placement, lifetime, removal/eviction and surface association, including
preexisting marks at capture start. Do not clear or recreate marks in the
live engine just to implement backwards scrubbing. Model decal suppression
is not established by the inconclusive r_drawmodeldecals test.

Next blocker: the exact TF2 x64 client.dll and engine.dll used in the current
runtime test are not available in this working session. Verify their identity
against the existing native guards and inspect the actual binary paths before
adding hooks or guessed offsets. No capture/replay behavior was changed in
this documentation update. Preserve 499fd28/5da6306 as a working baseline.


## Phase 10: exact-binary legacy emitter discovery (2026-11-27)

User supplied both x64 DLLs. Static PE/disassembly/RTTI inspection (no game
execution):

| Module | PE timestamp | SizeOfImage | SHA-256 |
| --- | --- | --- | --- |
| client.dll | 0x6ad26f17 | 0x133a000 | b6f45c74805edacf0cf61d965f658158fc22960f66d583ef3a6549eb56696b95 |
| engine.dll | 0x6ac41507 | 0x822000 | 4802a74ce0abb1175851d00e37fc88609085a96238aa52ac0eb224ae4a08ec80 |

Client hash exactly matches prior native particle research. All addresses
below are RVAs, not absolute pointers. The inspected binaries use preferred
image base 0x180000000; ASLR requires the actual loaded base.

### Legacy particle discovery

- C_EntityDissolve RTTI primary vtable 0xb0fc70; renderable secondary vtable
  0xb10198 (subobject +8), slot 10 points to 0x1fa3b0. This routine tests
  global frame time and resolves the parent before obtaining hitbox bones;
  at 0x1fa506 it references the literal C_EntityDissolve and calls the named
  CSimpleEmitter factory 0x2dc580 at 0x1fa514. The returned emitter is held
  at complete dissolve object +0x810. At 0x1fa668 it references effects/spark
  and resolves its material. Later calls at 0x1fa9e1 and 0x1fad6e go to the
  legacy AddParticle wrapper 0x2dc240. This directly corroborates the SDK
  dissolve implementation, but the user's particular effect still needs
  runtime identification.
- CSimpleEmitter factory 0x2dc580 allocates 0x158 bytes and calls constructor
  0x2dc000. Constructor stores debug name at +8 (0x2dc02a), constructs binding
  at +0x10, calls the existing ParticleMgr getter 0x2d6df0, then legacy
  AddEffect 0x2d42d0; final CSimpleEmitter vtable is 0xb61040 (RTTI-confirmed).
- Manager 0x10f5020, ready flag 0x10f51a0, vtable 0xb607b8. Legacy list
  is at manager +0x38 (node memory), +0x40 allocated count (int), +0x48
  head (u16), +0x4a tail, +0x4e live count. AddEffect at 0x2d42ee/0x2d42f8
  stores sim/manager at binding +0x98/+0xa0; 0x2d4347 onwards links 16-byte
  nodes (binding pointer +0, prev u16 +8, next u16 +10), increments count
  at 0x2d4383, stores list index at binding +0x94. Index sentinel 0xffff.
  Allocation routine 0x161740 uses list +8 capacity and 16-byte stride.
  Binding active count is u16 +0x90 (getter 0x2d65f0, increment 0x2d451b).
  These are independent of modern root head manager +0x60.
- Legacy AddParticle wrapper 0x2dc240 calls binding allocator 0x2d43e0,
  writes position +0x18/+0x1c/+0x20. Allocator links material sentinel
  lists (binding hash +0xb0, material hash next +0x30, embedded sentinel
  +8) and stores subtexture at particle +0x10. The simple emitter update
  functions corroborate velocity +0x28, roll +0x34, die/lifetime +0x38/+0x3c,
  size bytes +0x45/+0x46 and roll delta +0x48. Other fields and renderer
  semantics still require independent verification before snapshot/replay.

### New one-shot audit

Disable cached preview, play/stop at the actual demo tick with the missing
visual effect, then run `mirv_cache particles legacy`. It runs once at
TF2 FRAME_RENDER_END and prints the actual tick, bounded legacy list counts
and exact CSimpleEmitter names/active counts (including C_EntityDissolve).
Try during the effect and just before/after. If paused/frozen frames do not
show the emitter, step the real demo and retry; SDK dissolve drawing itself
can depend on nonzero frame time. This audit is live only, not a new historical
cache. Exact PE/anchor checks, safe reads, list index/ownership/backlink checks,
count/cycle limits and final header recheck; no native calls or writes. Unknown
vtable types are counted but their name/particle layouts are not assumed.
A complete audit means the bounded binding-list traversal passed, not that
all effects were captured or rendered. No new particle history or replay.

### Shadow and decal native leads

Client CClientShadowMgr RTTI vtable 0xb36880; slot 14 PreRender 0x24d290
(string reference 0x24d2ad), slot 27 ComputeShadowTextures 0x2486f0
(string reference 0x24870d). Texture path checks manager +0x118 and two
cvar-like integer fields at 0x2487eb..0x24881a. UpdateBrushShadow string
reference 0x24fb16 and AddShadowToReceiver references 0x2452f5/0x2454a1/
0x2455d8 identify adjacent paths. These corroborate a native manager and
pre-receiver texture path, not an established historical projection ABI.

Engine r_drawdecals registration string RVA 0x395598 xref 0x8aed;
r_drawmodeldecals string 0x3b8e10 xref 0x120ae; r_shadows string 0x368cc8
xrefs 0x914e/0x6d11d/0x6d2f6. The R_DecalCreate null-surface diagnostic at
0x395698 is referenced at 0x117ad2; R_DrawDecalsAll batch-overflow diagnostic
at 0x3956c0 is referenced at 0x11b8cd and 0x11e318. These are diagnostic
string xrefs, not verified callable entry addresses or decal list layouts.
No shadow or decal hooks or cvar changes introduced in this phase.

Windows compilation and in-game verification of the new audit remain needed.


## Phase 11: dissolve legacy history (capture-only experiment)

Runtime confirmation on 20c9cbf: tick 50, 16/16 legacy bindings, 975 total
active, exact CSimpleEmitter 6 with 817 active. Four C_EntityDissolve
emitters contain 235, 24, 26, 475 particles (760 total). Tick 199: 22/22
bindings, 361 active, exact simple emitter 2 with 14 active, no dissolve
emitters. Both complete, no unavailable simple names. This confirms the
legacy dissolve path is active during the reported effect.

Disassembly of client 0x2d43e0 corroborates eight hash buckets at binding
+0xb0, material group pointer +0, circular sentinel +8, hash next +0x30;
particle prev/next +0/+8, subtexture +0x10; group pointer at subtexture +0x10
corroborated by 0x2d4472; UV floats +0..+0xc correspond to SDK subtexture
layout (runtime values still to verify). Position +0x18, velocity +0x28,
roll +0x34, die/lifetime +0x38/+0x3c, RGB +0x40..42, alpha +0x43..44,
size +0x45..46, flags byte +0x47 (SDK lead), roll delta +0x48. Dissolve
renderer stores spark subtexture at global 0x11220b0 (0x1fa674) and
combinemuzzle2 at 0x1071988 (0x1fa699); confirmed literal names at
0xb0fc18/0xb0fc28. Match these pointers only during capture; persist only
a value enum and UV, never a pointer. Other materials remain explicitly
unknown. This is not a complete legacy material resolver.

New opt-in commands: mirv_cache particles legacyCapture 1/0, legacyStatus,
legacyAt <tick>. Start fresh history, enable legacyCapture 1 before scene
recording, play across dissolve and stop recording. Inspect exact ticks
before/during/after, including tick 50 and 199 if reproduced. Separate
64 MiB payload / 32768 frame history, oldest-first eviction, no quantization,
no interpolation, no new renderer. Zero-particle complete samples retained;
failed binding walks or incomplete samples rejected, not mistaken for empty.
Read-only exact CSimpleEmitter named C_EntityDissolve only; other legacy
classes and simple emitters are deliberately excluded. Existing modern
capture/render/isolation remain unchanged. The history is cleared with
scene history, and on backward append; opt-in is session-local. Current
capture uses the existing frame-end scene capture boundary; no native
allocations or calls. Windows build, runtime checks and full tests pending.


## Phase 12: batched visual reconstruction test

User requested fewer compile/test cycles. This batch adds independent opt-in
legacy native material drawing, model RGB/fade replay, and temporary live
shadow/world decal hiding. All rendering options default off. Legacy capture
is still explicit. One recording can exercise all options; no recapture is
needed when toggling replay or isolation. Re-record for new capture fields.

Legacy draw: exact CSimpleEmitter RenderParticles 0x2dc6c0 and native
DrawMaterialParticles 0x2d5250, StartDrawMaterialParticles 0x2d7f00 verified
by static disassembly. The native draw uses binding +0x98 sim, +0xa0 manager,
+0x90 active count; constructs its iterator and mesh builder internally.
The new immediate, main-view, stationary-only experiment uses temporary
scratch emitter/binding, material sentinel and circular particle list in
HLAE-owned memory, no factory/registration, no simulation or native allocation
of particle objects. Native mesh submission does allocate internal draw buffers.
Near/far clip copied from emitter +0x150/154 (binding +0x140/144); material
handles resolved from the current level and UV must match history. Unknown
materials skipped. Save/restore live manager modelview at +0x90 and both
context matrix stacks. Native code uses view-space coordinates, near fade,
color, alpha, size and roll. Global particle drawn statistics may change.
Scratch layout and native draw are experimental, not runtime proven; use
legacyReplay 0 immediately if artifacts/crashes occur. No persistent native
pointers in history. No guarantee of correct cross-material sort or lighting.

Capture model RGB and existing fade alpha via public renderable slots 14/13;
never call ComputeFxBlend. SDK and client base entity vtable corroborate
these getters (0x1e18b0 and 0x1e1e20). Capture alongside skin, choose nearest
sample in interpolated views; opt-in scoped studio modulation restores prior
state after submission. Not material overrides, render modes or correct
translucent sorting. No additional historical illumination.

Scoped r_shadows and r_drawdecals use the validated cvar interface and
restore original values at ordinary view exit, not separate passes. Only
when broad cached model replay eligible and exact stationary immediate TF2
preflight passes. They hide live artifacts, not generate historical shadows
or decals. Model decals untouched. Historical projection and decal creation/
removal discovery still pending; do not mistake hiding for reconstruction.

Commands: mirv_cache particles legacyCapture 1; legacyReplay 1/0;
legacyStatus; legacyAt <tick>. mirv_cache visuals modulation 1/0;
hideShadows 1/0; hideDecals 1/0; visuals status. Corresponding checkboxes
in Capture and Preview. Use same recorded interval to compare all toggles;
verify r_drawparticles, r_shadows and r_drawdecals restored on exit. Full
Windows build and in-game validation outstanding. Portable fixture updated
for near clip fields; mock tests not yet run in this commit.

Batch convenience: `mirv_cache visuals batch 1` enables legacy textured
replay, captured model modulation and both temporary isolation gates; `batch 0`
disables all four. Legacy capture still requires opt-in before recording.
`mirv_cache particles legacyMarkers 1` draws diagnostic crosses from the same
exact-tick history independently, useful if textured replay fails. Turn
markers off for visual comparisons. `visuals status` lists counts of known
and nonneutral historical model modulation and up to eight changed models.

Additional binary corroboration: r_drawparticles registration at 0x155bd/0x155c4
initializes the ConVar object at 0x10f4840. Its parent pointer at +0x38
(0x10f4878) and integer at parent +0x58 are checked by legacy binding DrawModel
at 0x2d5720..0x2d572b. Existing scoped r_drawparticles isolation therefore
also gates the normal legacy binding draw on this binary. The new direct
legacy material draw bypasses that entry. No additional detour is needed.
The capture loop copies a contiguous 0x4c particle and 0x18 subtexture
record in two reads per particle, reducing per-attribute RPM calls. Unknown
material/failed-binding counts are included in last capture diagnostics.

Portable test result: the exact Git blob versions of shared/LegacyParticleSnapshot.h
(8cd2bb5) and tests/cinematic/legacy_particles_test.cpp (b8d7031) were copied
locally, compiled with g++ C++17 -Wall -Wextra -pedantic and ASan/UBSan, and
executed successfully. This validates the synthetic linked-list reader and
history fixture only. Full adapter fixtures and Windows/in-game build not run.

### Phase 12 runtime validation — 54cd55f

User reports the batched build works as intended. Status captured in active
preview at exact cached tick 43, with scene recording requested but suspended
by preview. Last captured live tick 741; do not confuse that pose/audit report
with the historical cursor. All three histories retain 515 frames over ticks
5..741 (gaps possible).

| Metric | Observed |
| --- | --- |
| Model history payload | 42.37 MiB |
| Modern particle payload | 61.46 MiB (149.27 MiB original, 103.41 MiB packed, 2.43x original/retained) |
| Legacy dissolve payload | 8.75 MiB / 64 MiB |
| Combined retained payload | 112.58 MiB (not total process RAM) |
| Outer capture mean / peak | 6.033 / 11.007 ms |
| Modern particle capture mean / peak | 3.654 / 7.275 ms |
| Modern exact sample | 1264 captured positions and submitted particles, 21/21 successful tree attempts, 52 drawable nonempty collections, complete position and appearance, zero rejected reads/layouts |
| Legacy exact sample | Four dissolve emitters, 796 expected/captured/submitted, 622 spark, 174 combinemuzzle2, zero unknown materials or rejected submissions, complete 1 |
| History evictions / rejected frames | Zero for both particle histories |
| Studio replay at cursor | 163/163 submissions |
| Cumulative studio audit | 9562 callbacks, 1556280 attempted/submitted, zero queue/other skips, pending 0, peak 1, max 2 callbacks per engine frame |

All three scoped visibility gates reported disabled during ordinary render and
restored: r_drawparticles, r_shadows, r_drawdecals. This is initial in-game
validation of legacy textured replay alongside modern replay, historical
model RGB modulation, and reversible shadow/decal isolation. At tick 43 all
163 cached studio models have known modulation; four nonneutral models are
the Engineer and three attached cosmetics (Special Eyes, Endothermic Exowear,
Nerd Hair), each RGBA 0.141 0.212 0.259 1.000. Sample confirms historical RGB
on the body and gear; it does not independently verify nonunit alpha replay
or correct translucent ordering. First legacy particle RGB 87/132/161,
alpha 255->0, size 5->0, lifetime 0/0.414, roll 243, UV
0.016/0.008/0.984/0.992. Submission counters are not pixel fidelity measures.

Remaining: last live capture reports 83171 captured / 19570 unavailable pose
attempts cumulatively, one unavailable brush mesh, and 90 of 162 captured
studio poses not observed by the native audit hook at tick 741. Two callbacks
per frame still occur. No claim of full scene coverage. The shadow/decal
controls only hide current live effects; historical reconstruction remains
unimplemented. Other legacy emitter types, nonunit entity alpha, material
state and dynamic lighting need broader validation/development. Preserve
54cd55f as a user-validated experimental baseline. Continue in larger batches
with independently togglable features, rather than requiring separate builds
for every diagnostic discovery. No rendering changes in this follow-up.


## Phase 13A: broader exact simple emitter coverage

Opt in with `mirv_cache particles legacyAll 1` before capture; default remains
dissolve only. The native GetPMaterial tree at manager +0xf8 (24-byte nodes,
name +8, subtexture +16, root +0x108, count +0x10a) was corroborated via lookup
0x2d6639..0x2d66e2 and insertion 0x2d6877. The bounded reader validates parent
links, count and final root/header. Registered material names are copied to
per-sample value tables; replay finds the current registered subtexture by
name and checks UV, never loads a material or keeps a historical pointer.
Dissolve bindings are processed first; additional exact CSimpleEmitter
bindings are optional and separately counted on failure. Sky singleton and
unknown/derived vtables remain excluded. A 2048-particle combined cap still
applies. The existing two material handles remain the dissolve fallback.
No new renderer ABI is introduced. Changing legacyAll clears legacy history.


## Phase 13B: historical surface experiments (not yet runtime validated)

Opt in before scene recording: `mirv_cache surfaces capture 1`. Capture with
mat_queue_mode 0 on the exact TF2 x64 build. Independent historical controls:
`mirv_cache surfaces shadows 1`, `decals 1`, `batch 1/0`, `status`.
64 MiB combined history, exact ticks, unchanged immutable surface vectors
reused. Session world generation prevents replay into other map geometry.
No effect on separate TF2 recording passes or advancing demo preview.

Shadow manager: engine VEngineShadowMgr002 vtable 0x3959f0, CreateShadow slot
0 (0x124900), DestroyShadow slot 1 (0x124b00), ProjectShadow slot 3
(0x127de0), SetShadowTexCoord slot 9 (0x12c1b0), SetFalloffBias slot 15
(0x12b7e0). ProjectShadow argument ABI follows SDK (origin, direction,
world-to-shadow matrix, size, leaf count/list, height, falloff offset/amount,
caster origin). Linked list manager +0x10 base, +0x18 capacity, +0x20 head,
+0x24 tail, +0x2c count, stride 0x160, links +0x150/+0x152, flags +0x88,
bias +0x5c. Engine 0x12a8ff skips shadow decals when falloff bias ==255.
Guarded observer detours install once on first capture; record subsequent
native projection calls and erase on destroy, preserving unknown coverage.
Initial static shadows without observed calls remain explicitly unobserved.
Create temporary world-only native projected blobs using decals/simpleshadow
and observed historical projection data; not historical bone silhouettes.
If all captured projections known and allocated, temporarily gate existing
live nonflashlight shadows via bias (intercept setter to preserve updates);
otherwise keep live originals as partial overlay. Scope cleanup restores
biases and destroys owned handles. Never disable r_shadows while drawing
historical blobs; active historical replay overrides temporary hide toggle.
No native shadow atlas or moving receiver reconstruction yet.

Decals: engine slot table pointer 0x6a4e60, logical count 0x6a4e70, capacity
0x6a4e68. World pointer 0x755738, surface array world+0xe8, count +0xd8,
stride 64, decal head +0x10. Native create 0x117ab0, remove 0x11a580.
Record fields: surface +0x10, material +0x18, position +0x24, optional axis
+0x30 (flag 0x80), scale +0x44, RGBA +0x54, slot +0x66, flags +0x68,
name index +0x6a. Material GetName vtable slot zero corroborated at 0x11677c;
0x11b326 reads decal alpha. Only ordinary non-fading world records; attached,
displacement and dynamic/fading records skipped. Capture material name and
placement as values; at preview match surviving marks by slot + surface +
placement + name, temporarily apply historical alpha or zero for future-only
marks. Restore after ordinary render if same record still present. No list
unlinking, allocation or destruction; evicted/removed historical decals
cannot yet be recreated. A zero alpha gate may not hide every material.
This is historical surviving-mark visibility, not complete decal replay.

Both experiments are opt-in, independent, and preserve previous hiding
controls when no historical path is active. Native ABI/runtime correctness
must be validated; diagnostics report observed/unobserved, projected count,
matched/missing/unsupported marks and restoration warnings. Revert by
surfaces batch 0; keep previously validated 54cd55f available. No Windows
build or runtime validation performed for Phase 13.

Review follow-up: projection observation armed only during live recording; cleanup uses module identity independent of demo transport state, handles destroyed by the engine are forgotten, transient decal draw flags excluded from identity, and immutable payload accounting uses reference counts rather than scanning history. Added portable history fixture.

Portable surface history fixture passed locally using g++ C++17 with AddressSanitizer and UndefinedBehaviorSanitizer, covering unchanged payload sharing, budget eviction, exact tick lookup, world change, clearing, decal identity and shadow validation. Local source and fixture Git blob hashes match the committed versions. This does not validate native engine calls, rendering or Windows compilation. Full historical decals that no longer survive in the live scene and pose-accurate shadow silhouettes are not implemented.


## Phase 14: missing-tick replay and native decal geometry gate

User confirmed projected blobs appear under cached actors on 1590ef1, but
reported constant scrub flicker and nonworking decal history. Status at
requested tick 3451: models interpolated, modern particle replay resolved
3450; legacy/surface paths had no exact sample. All four histories had 2737
frames over ticks 3..3460, no reported particle/surface evictions/rejections.
Legacy 3.41 MiB, surfaces 11.45 MiB, modern 244.08 MiB, model 236.54 MiB.
The log confirms a missing-tick policy mismatch, not every other tick missing.
Decal behavior at exact samples was not established by that status. Both
legacy textured/marker and historical surface replay now use the same
bounded prior policy (up to 2 ticks, never outside retained bounds) as modern
particles. Exact legacyAt stays exact. No temporal interpolation of decal
existence; long gaps intentionally retain existing fallback behavior.

Reinspection of supplied engine SHA-256 4802a74c... confirms native decal
geometry lookup at 0x119490 receives scratch, decal, surface, material and
returns pointer/null. Callers at 0x116fe4, 0x11ae5c, 0x11bd7e, 0x11c967,
0x11d703 immediately branch on null to skip drawing. Replace the previous
alpha-write experiment with an opt-in detour guarded by exact module and
entry/callsite bytes, restricted to those return addresses, ordinary immediate
preview scope and the engine thread. Future-only surviving ordinary decals
are excluded from geometry submission, using live pointer/surface/material/
slot identity only within the current scope. No decal record mutation or
restoration writes. Nonzero historical alpha changes are not reproduced;
removed decals and unsupported types remain out of scope. Status now reports
actual skipped geometry calls, per-subsystem ready-frame counts, nonempty
decal frames and native read failure stage. Native tests pending.

Verification: copied the exact committed surface history header (blob
5c1a5bb) and fixture (8494ddb), confirmed both Git hashes locally, compiled
with g++ C++17 -Wall -Wextra -pedantic and ASan/UBSan, and ran successfully.
All six new decal geometry entry/callsite anchors and four existing decal
capture anchors match the supplied engine binary byte-for-byte. The legacy
gap fixture is added but not executed in this follow-up; native detour,
Windows build and in-game behavior remain untested. Test with the temporary
hideShadows/hideDecals controls off first, to distinguish historical replay
from whole-system fallback hiding, and with r_shadows/r_drawdecals enabled.


## Phase 15A: performance and stability

Preserve historical accuracy: native renderer object lifetimes and draw paths are unchanged. Modern particle replay now holds an immutable lease of the decoded frame rather than copying the vectors per callback. A lease survives history replacement, eviction or clear; no engine pointers are retained. `mirv_cache perf` reports CPU wall-clock last/mean/peak and peak tick for historical sampling, studio submission, particle lookup/decode and replay, legacy and surface capture/replay. It also retains last native particle failure, rejection counts and studio session audit. `mirv_cache perf reset` resets the new counters, not history or existing session studio audit. These are CPU measurements, not GPU timing or proof of pixel correctness. No claim that the intermittent visual corruption is fixed. Windows/in-game validation pending.


## Phase 15B: targeted replay optimization

The user reported approximately 10 FPS in a busy scene. At historical tick 118, last measured model submission was 12.033 ms and particle replay 10.037 ms; peak studio callback 77.328 ms. CPU timers are cumulative and not GPU/whole-frame timings. Reuse CPU-only reconstructed bone matrices for the exact immutable sampled scene frame, including owner identity and transforms. No native resource lifetime changes. Batch detached particle definition-name validation without removing exact name checks. Skip native particle work early on verified non-main views (native rendering already requires view 0). New `mirv_cache perf mainOnly 1` optionally restricts historical textured studio submission to verified main view; default 0 preserves existing multi-view behavior and unknown view IDs fall back. Compare skybox fidelity before adopting. `perf` now separates main, other and unverified view timing and reports non-main skips and last model failure asset/tick. No proven FPS gain or Windows runtime validation yet.


## Phase 15C: lower render workload and consolidated benchmark

Ordinary historical studio suppression no longer performs pass-group/parent classification when there is no hidden POV target. Optional `mirv_cache perf cull 1` conservatively excludes padded off-screen independent models in the verified main view; actors, attached gear, invalid bounds, unknown projection, skybox-named models and active video recording are excluded from this optimization. This is not proof of exact animated bounds; compare visually, default off. `mirv_cache perf bench` runs seven temporary modes at the current cursor and latest retained sample, measuring engine wall time and subsystem CPU totals per frame; `perf bench report` repeats the consolidated report and `perf bench cancel` restores settings. Stop navigation and use a paused/frozen demo with a stationary camera and broad historical preview. Video recording and camera path/rig preview are excluded. No history deletion or actual demo seek. Final rendering and Windows performance remain unverified.


## Phase 15D: asset residency and detached particle write path

Benchmark at historical tick 477 showed 99 submissions/frame and 16.62 ms studio CPU vs 91 and 1.62 ms at latest tick 1112. Residency is a hypothesis, not a confirmed cause: retain at most 256 validated FindMDL references during preview, independently of the existing per-submission header locks and queue retirement. Release on preview exit, focus/device loss or level shutdown. `mirv_cache perf residency 0` disables this. No retained hardware/header pointers. Native particle replay on MSVC x64 can use region-checked, SEH-guarded local copies into validated fresh detached allocations in immediate mode; probe and unsupported regions use original WriteProcessMemory. `mirv_cache perf fastWrites 0` restores previous write path. Report per-main-view factory, validation, copy, renderer, destruction times and write/cleanup failure counters. No persistent native particle objects. Actual Windows performance and correctness remain to be verified.

The same benchmark command now adds an early-tick old/new memory-path comparison (nine modes total) and includes the native particle stage breakdown and write fault counters. Settings are restored. A local-copy fault falls back to the original system write; tool and client shutdown hooks retire residency before native level teardown.


## Phase 16A: prepared replay groundwork (static audit)

At d3d8d38, the preceding benchmark (historical tick 477.15, latest 1112.74) measured live 78.1 FPS, full cached 18.1 FPS (334 studio submissions, 32.40 ms studio CPU, 10.32 ms modern particles, 0.50 ms legacy, 1.45 ms surfaces), main-only 24.8 FPS, main+cull 25.1 FPS (99 submissions, 16.62 ms studio), historical particles off 36.1 FPS, historical particles with live studio 41.1 FPS, latest 59.3 FPS (91 submissions, 1.62 ms studio, 3.01 ms modern particles). Earlier and latest scenes differ; residency, actual draw cost, material synchronization and asset complexity are competing hypotheses. CPU wall time is not GPU time. Phase 15D has not yet been user-tested.

Native topology is now prepared once per replay operation rather than scanning the collection tail in every detached validation; legacy material validation is shared within one callback, with per-particle UV/clip checks retained. Added topology fixture assertions. No native lifetime, draw order, queue-mode or capture-format change. Tests not yet executed in this environment.

Other audit findings: each studio draw still acquires/releases MDL/header/hardware resources, allocates retirement objects, checks pose validity, allocates matrices and sets/restores lighting/modulation; scope batching and owner matrix reuse merit investigation. Broad capture bypasses ordinary visibility at replay; conservative actor-group visibility is future work. Detached particle factory, activation, validation reads, destruction, and repeated surface shadow/decal reconstruction remain costly; persistent collections/queued rendering require exact ABI/lifetime and renderer mutation proof. Do not remove guards. Benchmark isolation can reintroduce live effects, and combined residency/write modes cannot attribute gains. Next: studio stage profiler and separate A/B comparisons, shared studio resources with verified queue completion, bounded particle read and scratch improvements, visibility and native lifetime research. Windows build/runtime and full regression suite remain pending.


### Phase 16A follow-up: studio stage instrumentation

Studio submission now reports exclusive CPU stage totals (preflight including previous resource retirement, asset acquisition/validation, bones, state, native draw, enqueue/restore) and slowest individual asset/tick since perf reset. Failed calls contribute time to the stage where they failed, including unwind. Benchmark retains the earlier modes, labels the live replacement caveats, adds residency-only and fast-writes-only modes (11 total); results are sequential and not cold-cache controlled. Native draw can include driver synchronization. This is not GPU instrumentation. The per-model clocks themselves have a small cost; do not extrapolate absolute results to a noninstrumented build. No change to native resource ownership. Fixture updated for the two new modes; execution pending.


## Phase 16B: immediate studio asset batching and build fix

Found a Phase 15D integration defect: residency functions declared externally in MirvCalcs.h were defined in the anonymous implementation namespace, which can cause ambiguous lookup/unresolved external linkage in the production translation units. Moved their definitions to external scope without exposing private storage. Previous mixed source line endings restored (normalized content verified unchanged).

The immediate textured-view path now obtains studio/MDL interfaces once, drains completed retirements once, and holds at most 256 unique validated model FindMDL references and header locks for that callback. Repeated assets reuse these references rather than allocating per-submission retirement objects. Hardware data, header checksum/count and material override are still checked each submission under the existing per-draw MDL lock; matrices remain studio-owned, state restoration and draw order unchanged. No cached native pointer crosses callback boundaries. The owning batch releases on the engine thread, including early exit. Only immediate material mode qualifies; queued and nontextured draws use the original completion-based path. Toggle `mirv_cache perf batchStudio 0` to restore the previous path (default 1). Benchmark currently holds this setting constant; compare separate runs for batching. Setup and cleanup are included in studio stage timing. Added fixture assertions for immediate reuse, scope cleanup and queued fallback; not yet run on the exact branch. Windows compilation and in-game behavior pending. Do not assume a measured FPS gain.

CI review: branch workflow runs [46039923422](https://github.com/akakezz/advancedfx/actions/runs/46039923422) (d3d8d38), [46050409934](https://github.com/akakezz/advancedfx/actions/runs/46050409934) (b686028), [46050514687](https://github.com/akakezz/advancedfx/actions/runs/46050514687) (5b4e137) and [46050656753](https://github.com/akakezz/advancedfx/actions/runs/46050656753) (a4d831f) concluded success. Run [46050823062](https://github.com/akakezz/advancedfx/actions/runs/46050823062) for 22d2ede passed native particle and overlay fixtures but failed the added studio batching test at info.Skin==1: the test had already reset the historical skin and state hooks. Moved the fixture's reset after the new batch checks. This is a fixture correction, not evidence of a runtime renderer fix. Recheck CI and Windows separately.


## Phase 16C: immutable legacy leases, owner matrix reuse and benchmark controls

Legacy preview now leases one immutable copy of the resolved sample per history revision/tick. A draw keeps ownership across history mutation; the one cached preview copy is scratch outside the existing history budget. Repeated historical attachments reuse their owner's unmerged world matrices for the exact immutable sampled frame (256-owner bound); normal reconstruction and validation remain fallback. No engine pointers or camera-dependent GPU state are cached. The benchmark now runs 13 modes, adding batching-off and batching-on repeat at the earlier tick with both memory optimizations enabled; all original settings are restored. For explicit historical-model-off and historical-particle-off phases, retain the requested live suppression where the original eligibility guards pass, rather than automatically reintroducing the corresponding live objects. If isolation was not requested or coverage is missing, live content may still return. Surface work remains dependent on model replay; live shadows/decals may differ. Report is still sequential, not controlled for cold assets/GPU. Legacy lease fixture and benchmark mode assertions added. Corrected Phase 16B f3f9db2 passed [run 46051082252](https://github.com/akakezz/advancedfx/actions/runs/46051082252). CI and Windows runtime still need verification at the new head.


### Phase 16C follow-up: detached validation and activation accounting

The detached definition pointer/capacity fields are contiguous in the existing verified layout, so one bounded system read replaces three; narrow reads remain fallback. Additional address arithmetic overflow checks reject invalid varying allocations. Render-gate scratch vectors are allocated once per callback rather than once per tree; generation markers retain exact subtree membership for fallback attempts. Added gate, activation and active-reset CPU timing to the benchmark and perf report. No direct reads or persistent native pointers added: Phase 15D's guarded direct writes remain the only new local native transfer path. Added fixture assertions for the bounded read and fallback. Native ABI/lifetime and full Windows compilation still require testing. Further direct-read optimization, persistent collection/renderer-state reuse, visibility, state batching and historical surface changes remain separate work; don't infer gains from source inspection alone.

CI coverage audit: camera-transitions previously omitted the standalone legacy-particle and surface-history fixtures and had incomplete replay-source path triggers. Added both tests on feature/scene-cache-2 and triggers for studio adapter, material queue, TF2 render/surface hooks and scene/particle history headers. Phase 16C commit add9018 passed [run 46051367307](https://github.com/akakezz/advancedfx/actions/runs/46051367307); that older run did not cover the newly added standalone tests. Follow-up 844e559 workflow [46051493916](https://github.com/akakezz/advancedfx/actions/runs/46051493916) was in progress when the coverage change was prepared. Portable CI is not a Windows build or proof of native runtime fidelity.


## Phase 17A: user runtime report and render-driven benchmark

2026-11-27: user reports worst cached scene performance now around 40 FPS following Phase 16, a meaningful improvement but not a same-scene controlled benchmark. The benchmark never returned a result even after extended waiting. Static audit: it deduplicated using client global framecount at FRAME_RENDER_END; frozen demo can stall that counter or the capture stage even when ordinary views render. Move progress to the completed outer ordinary TF2 View_Render hook, with independent nested-call guard, monotonic wall time and phase progress output. The report command now prints partial results line-by-line and current phase/callback counts, and cancels/restores on explicit report if no render callback for ten seconds. No capture or separate recording passes drive measurement. CPU wall time, not GPU present timing. Expanded Phase 16 CI at 0aa4083 passed run 46051603989. New fixtures exercise progress at an unchanged engine frame counter. Runtime cause not independently verified.

User also reports some decals remain in live demo state while scrubbing. Known Phase 14 limitations: ordinary surviving world marks only; displaced/attached/unsupported/fading decals are excluded and removed historical decals cannot be recreated. Diagnostic counts do not establish complete coverage. Track as fidelity defect, not a reason to hide every decal by default. User identifies mat_queue_mode 0 as largest remaining performance concern; native particle and historical surface operations retain immediate-mode restriction pending verified queue/lifetime research.


### Phase 17B: guarded detached validation reads

Use the existing SEH-protected local memcpy only within a verified fresh detached particle tree, with a committed readable page-range check (no guard/noaccess, overflow checked). Fall back to the original exact-size ReadProcessMemory on any range denial or fault. Write policy unchanged; readable/writable page caches are separate and reset per tree. Module/ABI signatures, render-context discovery and registered manager-list checks remain system reads. Both restore-plan validation and render-gate metadata benefit. No change to factory lifetime, active-count cleanup or draw order. Toggle `mirv_cache perf fastReads 0|1` (default 1); benchmark adds a 14th mode to compare reads off while batching/writes/residency remain enabled. Reports direct/system/fault read counts; no measured gain yet. Windows SEH path is not covered by portable CI. Native queue-mode restriction unchanged.


### Phase 17C: queue-compatible studio resource batching

Extend the existing immediate textured-view batching to queued studio rendering without delaying the individual completion markers. Each validated asset (name/checksum, <=256/view) has one shared resource owner. Each queued studio draw submits its own ordered completion functor holding a shared reference; destruction/discard of any token releases only that use. The producer view also owns the asset until its scope ends. Final shared-owner destruction publishes existing lock-free retirement; native header unlock/MDL release remain engine-thread operations. A new view never reuses prior view native handles; hardware and checksum checks per submission unchanged. Immediate draws retain direct view-end cleanup. Separate `perf batchQueued 0|1` (default 1) restores original per-draw resource ownership when disabled; `batchStudio 0` disables batching entirely. Tests cover queue completion/discard before/after producer end and last-use release. This improves the queued studio path, NOT native particle/legacy/surface support under mat_queue_mode 2. No native binary reinspection available; don't remove immediate guard. Portable CI and runtime pending for this commit.


### Phase 17D: comparable queue-mode model-only benchmark

`mirv_cache perf bench models` runs six ordinary-render phases at the current historical tick: live baseline, historical models all views, main only, main+cull, batching off, batching on repeat. Native historical particles, legacy particles, markers and historical/hidden surfaces are temporarily disabled; live effects remain in every phase. Restores original settings, including queued batching, on completion/cancel. Requires stationary demo and verified studio queue capability, not the native particle immediate gate. Reports actual immediate/queued context and aborts on a context-mode change. To quantify global mat_queue_mode cost, run the same camera/tick with mode 0 and the user's normal queued setting separately; no cvar changed automatically. Not a full-fidelity queued particle/surface benchmark. Adds portable fixture for queued model-only eligibility and settings. Phase 17A/B passed CI; native runtime remains unverified.


### Phase 17E: prepared frustum and experimental actor-group visibility

Precompute the same four padded side planes once per verified main view instead of normalizing projection planes per model. Ordinary category culling retains previous fail-open behavior. New opt-in `mirv_cache perf actorCull 1` requires `perf cull 1` and a verified main-view projection outside recording; default off. Build actor/attachment ancestry from the immutable sampled frame, reject only when a player/ragdoll and all recorded descendants are outside the same side plane with 3x bounds plus 256 units padding; invalid/missing pose, skybox/brush geometry, degenerate bounds, duplicate identities and cyclic/deep chains fail open. No capture changes. This is a conservative approximation, not native PVS/occlusion and not a proof of visual coverage; watch camera-edge/attachment behavior. Full benchmark adds phase 15; models-only adds phase 7; enables actor cull only in the final comparison, restores setting afterward. Reports actor-group rejected submissions separately. Portable fixtures cover opposite-side/visible/missing attachment and invalid projection. Native runtime visual verification pending.

CI follow-up: Phase 17C b099363 passed both jobs ([46057971386](https://github.com/akakezz/advancedfx/actions/runs/46057971386)), including shared queued asset retirement fixture. Phase 17D cb1b99a failed compilation of the overlay fixture because GetDemoPlaybackTimeScale is a non-const interface method; use non-const demo pointer. Corrected production call rather than weakening fixture. Phase 17E and the combined correction require a fresh workflow result. Public Source SDK 2013 `src/game/client/particles_new.cpp` (CNewParticleEffect::DrawModel) uses CMatRenderContextPtr; public `src/public/particles/particles.h` exposes Render(IMatRenderContext*, bool, void*), but the SDK does not expose the actual particle renderer implementation, and the exact TF2 client/materialsystem DLLs are not available in this session. These declarations do not prove that queued consumers release detached collection/definition links or legacy scratch/manager state before cleanup. Keep the immediate guards. Required next investigation: exact current client.dll/materialsystem.dll/engine.dll producer/consumer disassembly, material call queue dependencies, definition list traversal and mutation, cancellation and level shutdown. Never simply bypass MaterialSystem_IsImmediate.


### Phase 17F: decal coverage diagnostic and redundant read removal

The decal traversal reports exclusive rejection counts for outside-world receiver, attached records, 0x1100 flag exclusions, displacement and unavailable material, separately for historical capture and current live traversal. Reuse already-read record surface/material addresses when creating the geometry gate; draw hook still checks matching pointers/slot. Do not retain native decal addresses in history. This does not recreate removed marks or suppress unsupported decal classes. User-reported leakage remains an open fidelity bug; `mirv_cache surfaces status` gives the breakdown to distinguish coverage from gate failure. No automatic hiding of all world decals. Fixture added for rejection-count formatting. Combined Phase 17A–E corrected checkpoint 25d29af passed [CI 46058319848](https://github.com/akakezz/advancedfx/actions/runs/46058319848). Windows/runtime validation remains pending.


### Phase 17G: stationary pose validation reuse

Cache validation of exact immutable published model poses for the current sampled frame (2048-entry bound, frame ownership checked; arbitrary/unmatched calls validate directly). No native header/hardware checks removed. Avoid owner lookup for ownerless rows and entity copies unless modulation must be changed. Added fixture for immutable validation and unmatched/new-frame fallback. Windows/runtime validation remains pending. Do not mutate published history in place.


## Phase 18A: consolidated benchmark and Windows build correction

User supplied the MSVC multibuild log: Win32 CameraOverlay.cpp C2079 undefined basic_istringstream, cascading C2440 and C2672. Add direct <sstream> and explicit stream/container includes; headless fixture previously supplied the missing header. Win32 failed before x64 compilation, so no x64 success inferred. Enable existing Windows Release CI on feature/scene-cache-2 (existing staging build, both architectures). Default `mirv_cache perf bench` now runs a single automatic suite: 15 full historical immediate phases, 7 model-only immediate phases, 7 model-only queued phases. It changes mat_queue_mode only at engine FRAME_START, verifies actual context before measurement, suppresses preview during switches, times out on missing callbacks/context, and restores the original cvar at FRAME_START on stop, unless changed externally. `bench full` and `bench models` retain no-switch comparisons. Report saved incrementally to a unique temp-directory text file and includes interval percentiles, CPU stages, queue ownership, rejected particle counts, coverage and surface status. `bench report` gives path and partial results, `bench cancel` restores settings. No automatic queue changes outside explicit benchmark. Runtime switching and Windows compile still require validation; do not claim full queued effects.


### Phase 18B: studio state equality and exact DLL research

Supplied TF2 x64 binaries inspected directly: client SHA256 b6f45c74805edacf0cf61d965f658158fc22960f66d583ef3a6549eb56696b95 (timestamp 6ad26f17, image 133a000); engine 4802a74ce0abb1175851d00e37fc88609085a96238aa52ac0eb224ae4a08ec80 (6ac41507, 822000); materialsystem a58d9afd625cb3b3fe9a3588a8d8192325897c803272075e97600b36383cbd7d (6ac56a08, 141000). MSVC RTTI reconstruction confirms CMaterialSystem primary vtable b7470 slot 148 -> 16bf0 (TLS context then virtual +718); immediate CMatRenderContext slot 227 -> 6f30 returns null, queued CMatQueuedRenderContext -> 16540 returns context+2c0. CCallQueueExternal wrapper 1a570 AddRef, subtracts e8 from external object, calls internal queue 99c0, Release; matches current HLAE internal queue lookup. Internal 99c0 appends functor pointer to list. Client Render 8e4840 updates collection +44 at 8e48a4, dispatches definition renderer array +268/count +278 at 8e4938 via vtable +48 with collection and per-renderer scratch (+2278), recursively calls child Render at 8e496d, and calls optional collection+2788 object vtable+40 at 8e4995. Legacy 2d5250 constructs stack iterator/mesh state and invokes emitter virtual +30 at 2d53c0. Existing client view/factory/render and engine decal anchor bytes verified against supplied PE. These findings do NOT prove the virtual consumers are synchronous with respect to queued work; keep native particle/legacy/surface immediate requirement. Studio tracked setters now skip only known exactly equal values, and restore if actual observed state differs; unknown state retains previous explicit setter behavior. Portable fixture verifies no-op and unknown state behavior. Surface traversal reuses receiver flags per callback, no persistent native pointers. Phase 18A portable CI passed 46061318183; Windows build in progress at time of writing.

### Phase 18C: automatic native renderer census

Benchmark collects bounded client-relative renderer and post-render function targets from verified detached collection definitions, only during warm-up, never invokes them for diagnostics, and includes observed targets in the completion/cancellation report. Per-definition renderer array +268/count +278 and virtual +48, collection+2788 post object virtual +40 are the exact dispatch fields identified in the supplied x64 client. No addresses retained across frames except numeric RVAs for the report, no extra command or user-run probe required. This census is not exhaustive, an allowlist, or proof of queued lifetime safety. Native queued replay remains disabled.


### Phase 18D: particle activation and complete benchmark coverage

RestorePlan now retains the validated definition pointer only for the synchronous tree; gate validation reuses it after owned attribute copies (no engine call intervenes). Coalesce clock/padded count/time prefix (+20..+37) into one guarded transfer, keep active (+38) separate and last; old narrow path remains fallback and fastWrites 0 baseline. Reset active/padded counters only for touched bindings, including partial failures, before synchronous destruction. Portable tests verify exact prefix words and binding definition. No persistent native objects. Full benchmark now has 18 phases (32 total automatic with 7+7 model suites), adding modern-only-off, legacy-only-off and historical surface-off comparisons; live effects may return in surface-off. Original historical and hide flags restored for all stops. Report records requested flags and history counts, saves cumulative renderer RVAs each phase, and times out even if ordinary render continues but FRAME_START is missing. Existing exact native rendering guards preserved. RTTI further maps renderer slot +48 targets: sprites 8f5a00, sprites trail 8f67f0, rope 8f7370, screen velocity rotate 8f98b0. Their subpaths invoke multiple context/mesh methods, not a proven single synchronous consumer. No speculative queued particle enablement or persistent collection reuse. Full Windows build and TF2 validation still pending.


### Phase 18E: architecture-independent Windows validation

Windows Release workflow on feature/scene-cache-2 now runs independent x64 and Win32 AfxHookSource targets (matrix fail-fast disabled), so an unrelated Win32 failure cannot conceal x64 errors. Main/PR staging build behavior unchanged. Successful x64 job stages Source 1 component and uploads the exact-commit AfxHookSource.dll/PDB as a replacement for an existing compatible HLAE staging installation, not a full standalone package. Branch workflow cancels superseded runs. Phase 18A–C portable workflows succeeded; Phase 18D project-roundtrip-and-blends job passed as of this change; overlay and Windows jobs still require confirmed results. Do not claim a Windows build until this exact job succeeds.

### Phase 19A: preserve retained surfaces and parent render scopes

Static review found Surface History::Append could clear all valid history on an unavailable world-zero sample, or drop a same-tick sample before rejecting it. Reject unavailable/oversized samples before any history mutation. Native View_Render recursion previously used active pass as an outer-scope test even for nested ordinary calls; nested callbacks could clear parent studio groups/audit and prematurely release historical shadow/decal scopes. Forward nested callbacks without taking new scope ownership or launching capture passes. Tests cover retained history on unavailable/oversized input and recursive ordinary preview with suppression, audit and single benchmark completion. No new ABI assumptions, native queue enablement or removed-decal reconstruction. Windows and runtime remain pending.

### Phase 19B: independent historical surface sample resolution

If the newest retained surface frame contains only one ready subsystem, resolve the other from its nearest earlier ready frame within two ticks, in the same world generation and retained bounds. Empty ready vectors remain authoritative (never resurrect earlier effects). Compose a pointer-free-value Frame referencing immutable shared vectors for synchronous replay; no native objects retained. Status and benchmark show each source tick or -1. Fixtures cover independent readiness, authoritative emptiness, long gaps, retained bounds and world change. This addresses partial capture flicker; not a complete native historical decal or shadow renderer. Windows and TF2 testing pending.

### Phase 19C: verified fading world decal visibility and destructive creation audit

All three reuploaded PE32+ modules match Phase 18B SHA-256 hashes. Engine 0x117ab0 calls overlap search 0x117d90 and removes an existing result at 0x117b18; capacity rotation removes another live record at 0x117c17 before allocation. Native creation is not safe as a reversible preview operation. Removal 0x11a580 updates surface chains, global counters/table and pool. Do not call it on fabricated records. 0x117ce2..0x117d0b sets fade bit 0x100, copies duration +0x4c and sets start +0x50 relative to engine clock. The draw at 0x11ac29 calls clock 0xa4320 using client state 0x538860; 0x11b33c..0x11b3a2 computes opacity and 0x11ad92..0x11add4 queues expired records for removal. Read guarded tick +0x1b4, interval global 0x755740, mode +0x8c64 and optional fraction +0x8c6c. Include ordinary surviving fading world marks with positive finite duration/start, capture only expiry visibility and stable fade identity, preserve original alpha. Exclude 0x1000 as before; unsupported clock/metadata fail open for unmatched fading marks. No changes to native decal lists, clock, or partial alpha. This improves existence gating, not evicted-decal replay. Exact anchors checked against supplied binary; fixture added, Windows and runtime pending. Materialsystem queued getter 0x16540 and enqueue 0x99c0 independently reconfirmed; sprite renderer 0x8f5a00 reads collection attribute buffers and calls context/mesh methods, but no proof of all deferred consumer lifetimes. Keep native immediate guard.

### Phase 19D: detached removed-world-decal replay experiment

Exact engine 0x119490 returns clipped 32-byte vertices; 0x11b3f2..0x11b502 copies xyz and UV/lightmap UV with stride 0x20, unused +0xc zeroed in history. World dispatcher 0x116ad0 takes context, group, float blend; verified ordinary world return 0xe729d is inside 0xe7010 group loop. Other callers 0xde5da/0xe077c/0x124e20 are not used. Group 3 may translate model matrix (0xe7168..0xe719f), exclude it. Observe native geometry only within immediate main-world dispatch, exact thread, capture armed, no replay; copy before native caller resumes, bound 65536 vertices per observation frame, 3..64 per polygon, match current slot/receiver/material/placement/color/fade fields at frame end. Reuse immutable polygons for unchanged identities; nested ref accounting tracks geometry once across retained snapshots. No native pointers in history. Exact engine single-mark draw 0x11e6c0 accepts vertices, material, surface, count, record, blend; reads record +0x58 proxy, +0x54 RGB, +0x57 alpha, +0x68 fade flags and +0x20 lightmap offset. Calls material context getter, binds receiver lightmap, locks dynamic mesh, copies attributes, draws, ends/releases context. Use zeroed detached scratch with historical evaluated alpha, attachment/fade clear, no allocator, list mutation or temporary native decal creation. Restrict to immediate ordinary main-world groups, current world, exact materialsystem identity/vtable b7470, FindMaterial +0x238 -> 15720, context +0x310 -> 16c20 and immediate context vtable bcab0. Validate receiver, material name, lightmap page, geometry; at most 1024 missing marks/view, once per visited group, skip ambiguous excluded live slots. Does not recreate unobserved geometry, group 3, displacement, attached/model decals, unsupported materials or guarantee native PVS/sort fidelity. The native surviving visibility gate remains independent; `surfaces removed 0` disables detached replay. Full benchmark includes the requested setting and captured polygon counts, off phases disable all surface replay. Portable fixtures cover geometry validity, fade alpha and shared history accounting; Windows/native runtime pending.

Phase 19D validation follow-up: the portable fixture caught stale evaluated fade alpha after setting base alpha to zero; corrected ReplayAlpha and reran the complete surface-history fixture with AddressSanitizer/UndefinedBehaviorSanitizer successfully. Geometry observations now reset on either shadow or decal world-generation transitions and the world must still match at capture dispatch. The Windows/native rendering path remains unvalidated.

### Phase 19E: surviving opacity replacement and queue boundary audit

Reinspection of supplied modules confirms native direct draw 0x11e6c0 uses caller blend at 0x11e9a0..0x11e9e0 and applies positive-duration fade before writing color at 0x11eb06..0x11eb32. The detached draw uses captured evaluated alpha and current group blend (rounding order may differ by one or more alpha units from native, no pixel-equivalence claim). For surviving opacity mismatches with recorded geometry, preflight material/receiver and immediate context before native dispatch. A stack-local gate is limited to the verified main-world group and matches native record/surface/material/slot. Submit replacement only after original dispatch if native geometry actually gated or already invisible; otherwise leave native draw alone. Current world and context rechecked before drawing; failures reported, no live decal mutations. Independent `surfaces opacity 0` disables this, batch enables it; missing geometry and non-main/unsupported marks remain partial. Native 0x116ad0 queries context +0x498, allocates an array of live decal pointers (0x116c51..0x116c7c), packages a functor with consumer 0x116e10 and submits it via queue virtual slot 0 (0x116c81..0x116d00). Immediate fallback calls 0x11aa60 at 0x116d73. Therefore the producer's thread-local scope cannot safely govern deferred world decals; keep immediate restriction. Materials exact vtable b7470 +0x238 and +0x310 verified against uploaded binary, immediate bcab0 +0x498 and +0x718 both resolve to 0x6f30 (null queue). Added portable policy tests for mismatched opacity, missing identity and gated/no-gate selection. Runtime and Windows validation pending.

### Phase 20A: preserve particle histories and reduce repeated restore-plan work

Native and legacy History::Append rejected backward samples after clearing retained history; legacy also dropped a valid same-tick sample on incomplete capture and invalidated its immutable lease. Validate input/budget before any mutation; accepted backward samples still reset interval. Modern storable partial captures intentionally remain diagnostic records. Combine existing detached allocation pointer +0x22e8 and tables +0x2310/+0x2510 into one 0x328-byte read through +0x260f. The factory at client 0x8e04ec..0x8e052c allocates 0x27a7 bytes before alignment and initializes the collection; table span stays within the verified object. Original narrow reads remain fallback, every allocation/stride/layout/constant guard unchanged. Reuse the already prepared recorded child index for root/fallback traversal, validate its complete edge set before native attempts. Tests cover rejected history preserving immutable lease, budget, exact fallback reads and malformed index. Native destructor 0x8de5a4..0x8de601 directly relinks definition+0x150 and collection+0x2778/+0x2780, recursively destructs/frees children at 0x8de610..0x8de644 and frees attribute storage/scratch. The fact that detached objects do not enter the registered manager root list does not make retention across frames or render queue cancellation safe. No change to lifetime, cleanup, or immediate-context restrictions. Windows and runtime remain pending; performance not measured.

### Phase 20B: repair standalone x64 shader generation

GitHub Actions [Phase 18E run 46062340354](https://github.com/akakezz/advancedfx/actions/runs/46062340354): Win32 successful, x64 job 140719347683 fails d3d9Hooks.cpp(32,10) C1083 missing shaders/build/afx_depth_ps20.h. AfxHookSource/CMakeLists.txt generated depth and PGL shaders only for Win32 although d3d9Hooks.cpp includes depth and PGL combo headers unconditionally. Existing MultiBuild builds Win32 first into the shared shaders/build directory, hiding this dependency. Make all six architecture-independent Source 1 shader targets available in both builds. x64 workflow stages the corresponding .acs bytecode beside DLL/PDB; it remains a replacement artifact, not a standalone package. [Phase 19E portable CI 46066625702](https://github.com/akakezz/advancedfx/actions/runs/46066625702) passed both jobs including all particle/surface fixtures and overlay. Phase 20A and this new Windows build require exact result verification.

Phase 20A CI correction: [46066828234](https://github.com/akakezz/advancedfx/actions/runs/46066828234) failed at "invalid tick preserves interval and lease", because the new fixture decoded a different tick before testing lease identity. Reorder checks to verify unchanged lease first, then confirm both historical samples. The overlay job passed. No production behavior weakened; exact rerun pending.

### Phase 20C: texture-aware projected shadows and same-tick surface capture

Verified exact supplied engine SHA-256 and native shadow record stride 0x160. At 0x12bb0b..0x12bb1f, the create/material helper stores world/model material pointers at +0x70/+0x78 and proxy at +0x80. The setter at 0x12c1b0..0x12c1de writes four texture floats at +0x4c..+0x58. Capture those fields from validated slot traversal, resolve material names once per capture, and retain normalized finite texture rectangles only when both names are decals/simpleshadow. Non-simple (including texture-atlas materials) and unknown/invalid appearance preserve prior full-rectangle blob approximation, explicitly counted; no atlas pointer, proxy, or texture content is retained, no claim of pose-accurate silhouettes. Batch slot traversal into one record read (original narrow fallback) and verify list data/head/tail/count at completion. Merge exact same-tick partial surface captures only within the same world, preserving ready-empty semantics, shadow coverage metadata and decal rejection metadata; budget rejection precedes mutation. Portable fixtures cover both policies. Prior exact 1f5ac2ae x64/Win32 and portable CI passed, x64 artifact 15232185074; new native build/runtime pending.

### Phase 21A: observed studio lighting, not global world-light rewind

Uploaded engine SHA-256 matches prior build. CModelRender primary vtable 0x391000 slot 19 -> 0xee0e0. Native helper call 0xee45e -> 0xf2dd0 (return 0xee463); other call 0xec733 is deliberately not observed. DrawModelExecute sets DrawModelInfo_t client entity from state+0x10 and calls IStudioRender025 slot29 at 0xee5ac; model info renderable +0x18, skin/body +0x48/+0x4c, origin +0. Lighting helper has 10 arguments as verified at 0xee41a..0xee45e, writes static flag +0x40, local count +0x8c and descriptors +0x90; 0xf2010 converter uses 0x58 stride and calls studio slot18 at 0xf2463, ambient setter slot16 at 0xf3f7a. Capture fresh ambient/local setter revisions only inside that native helper and only from exact studio instance at engine+0x7aace8, main view, ordinary same-frame audit, no static vertex lighting. Match native renderable against draw state, header IDST/checksum, skin/body, origin within .25 units; ambiguous repeat observations rejected. No native pointers enter retained lighting. SDK LightDesc_t fields verified from Valve source-sdk-2013 src/public/mathlib/lightdesc.h and src/mathlib/lightdesc.cpp: enum word 0, flags word 19, derived reciprocal and squared range words 20/21. Engine converter 0xf20ae..0xf23c0 writes meaningful fields but not private derived words and can leave non-spot angular fields unspecified. Normalize those fields rather than preserve uninitialized bytes; retain meaningful inputs, native flags, spot dot values; recompute derived reciprocal/range as SDK rules. Reject invalid/unsupported descriptors and missing fresh state. Historical nearest lighting replay uses exact observed ambient slot and local light array in existing studio scope (restore known prior state); neutral fallback for missing/unrestorable sample. Not baked vertex lighting, model decal replay, environment/cubemap restoration or world lightmap reconstruction. Capture and replay toggles independently opt-in, included in visuals batch; fresh broad scene capture required. Bench counts sampled coverage; no runtime/pixel fidelity claim. Fixtures include malformed words, no-light sample, identity, pose mismatch, budget, oversized backward frame retention. Exact Windows validation pending.

### Phase 22A: scope detached decals to current ordinary-world receiver list

Rechecked exact engine disassembly: 0xe7010 receives world list, draw flags, water Z; flags 0x840 take separate path, 0xe71ce calls 0xdb5a0 with list and group, 0xe7298 calls decal dispatch. At 0xdb5b3..0xdb5de the pool begins list+0x18, group vector at list+0x78+group*0x20, count at +0x10. 0xdb620 reads group entry's uint16 chain head; 0xdb640..0xdb670 walks signed indices into 0x80-byte chunks, count +2, surface pointers +8; 0xdb7d7..0xdb7f5 follows next uint16, sentinel 0xffff. 0xe4df3..0xe4f0e independently walks the same pool in opaque rendering. Use SDK CUtlMemory/CUtlVector header layout (data +0, allocation capacity +8, count +0x10), explicitly validate and fail closed if actual runtime layout differs. Bounded 32768-entry temporary pool/list copies, validate each index, count <=15, receiver range/alignment, cycle/duplicate chunk, recheck headers; keep only candidate receiver indices. Scope pointer to synchronous world draw, restore nested prior pointer; no persistent native addresses, no queued support. Detached preparation checks this membership before material lookup and live replacement suppression. This only covers the ordinary opaque receiver list, not exact per-pixel occlusion, displacement, translucent or model decals. Unknown membership skips detached replay (including opacity) and leaves native surviving marks. Receiver status counts differentiate unknown from culled. Portable fixture exercises valid traversal and rejection cases. Phase 21B exact portable CI 46069929451 passed; new native build/runtime pending.

### Phase 22B: strengthen receiver ABI and preserve uncertainty

World bucket insertion at 0xd240b reads capacity +8 and count +0x10; 0xd2480 writes allocation capacity and multiplies by 0x80; 0xd24a7 increments count. Group pointer array insertion 0xd2571 scales by 8 and 0xd2592 increments count. These independently verify the Phase 22A CUtlVector offsets. Native call at 0xe10da..0xe10e2 passes list/flags/waterZ to 0xe7010; restrict world-list scope to return 0xe10e7. Record unsupported historical decal slot IDs from the existing successful table traversal, share identical vectors and account once in history; same-tick partial merge and causal lookup preserve source identity. Do not gate unmatched current records in historical uncertain slots. Native geometry 0x11973b..0x11987c also updates global geometry-cache frame/counters and chains; direct use for unobserved detached clipping remains unsafe. No new arbitrary slot creation or live record mutation. Coalesce adjacent copy-plan ranges only if source and destination are exactly contiguous within same detached collection; keep constant comparisons, all capacity/stride guards, and original verification path. Tests cover gaps/boundary, unsupported history retention/eviction semantics, and receiver parser. Runtime gains unmeasured; queued particle and surface guards remain.

### Phase 22C: native projected-shadow clip planes

Valve SDK ishadowmgr.h slots 12 ClearExtraClipPlanes and 13 AddExtraClipPlane; exact VEngineShadowMgr002 vtable 0x3959f0 -> engine 0x1237c0 and 0x1217c0. Clear zeroes uint16 count +0xf8; add writes 3 floats to +0xfc+12*i, distance to +0x12c+4*i and increments count; four planes fit the two adjacent arrays. Consumers 0x123a32..0x123afa and 0x1278af..0x12798a use same offsets and 12/4 strides. Guard exact function bytes/vtable. Capture initialized finite planes from already bulk-read shadow record or narrow fallback; reject count >4, nonfinite, zero normal; never store native pointers. SDK clientshadowmgr.cpp projects first then ComputeExtraClipPlanes (near 2556), replay clear/add on fresh owned handle after projection and before bias. Unknown clipping retains existing approximate projection and disallows complete live suppression; diagnostics count unknown separately. This is projection clipping, not pose silhouette or atlas reconstruction. Existing native immediate mode remains mandatory.

### Phase 23A: producer-side queued decal filtering (limited, experimental)

Verified uploaded engine SHA-256 4802a74ce0abb1175851d00e37fc88609085a96238aa52ac0eb224ae4a08ec80. Native dispatch 0x116ad0 tests context virtual +0x498 at 0x116b23; exact material queued CMatQueuedRenderContext vtable 0xb6c28 slot +0x498 -> 0x2c4a0 returns context+0x3a8 (distinct from +0x718 -> 0x16540 internal queue at +0x2c0). Native calls plain gather 0x11c670 (returns 0x116bfb,0x116c19,0x116c3a), sorted gather 0x11d220 (returns 0x116bb5,0x116bd3,0x116bf4). Both append to native global scratch vector 0x6c55b8; size +0x10, capacity +8, data +0, elements +0x18 (0x11c749..0x11c778, 0x11d2ab..0x11d2da). Sorted emits outer delimiter 0 and material delimiter 0xffffffff before record run; consumer 0x11d670..0x11d686 tests markers and 0x11e1cb..0x11e202 looks ahead to material pointer, so never leave a marker without a real retained record. Filter per category before dispatcher computes lengths and copies array at 0x116c64..0x116c7c; native queue still owns all retained pointer lifetimes. Scope to ordinary main-world dispatcher return 0xe729d, groups 0..2, engine producer thread, verified queued context. No extra deferred functor or native collection retention. Skip fading candidates because consumer 0x11c8f4..0x11c950 / 0x11d68c..0x11d6ec performs expiration bookkeeping. Revalidate full stable record identity before dropping pointer; unknown slot/unsupported types stay live. Fail open on malformed list, changed world/header, nonwritable scratch, unexpected callsite. Portable tests cover marker grammar and same order, overlay comparison phase. Queued geometry/opacity reconstruction, shadows and modern/legacy particle renderer lifetimes remain unsolved. This is the first limited queued surface path, not an unrestricted queue bypass. Native validation pending.

### Phase 24A: queued simple-emitter dynamic mesh path

Verified uploaded client SHA256 b6f45c74805edacf0cf61d965f658158fc22960f66d583ef3a6549eb56696b95 and materialsystem SHA256 a58d9afd625cb3b3fe9a3588a8d8192325897c803272075e97600b36383cbd7d. Native client 0x2d5250 calls 0x2d7f00 to get context and bind live page material (0x2d7fb0, null proxy arg), GetDynamicMesh +0x1c0 (0x2d7fc6), LockMesh +0x90 (0x2d8011) with a stack descriptor. Exact simple-emitter virtual render at 0x2dc6c0 consumes the linked particles synchronously, computes color/size/alpha and camera-space vertices into mesh builder via 0x207350, advances at 0x1fb8f0. Native 0x2d5555 unlocks and 0x2d558f draws; stack descriptor not passed to deferred queue. Materials CMatQueuedRenderContext vtable 0xb6c28, GetDynamicMesh 0x2c4c0 uses context+0x310, CMatQueuedMesh primary vtable 0xbb7d0. LockMesh 0x2cd30 allocates vertex/index buffers via 0x2a5d0/0x2a420 from context+0x348/+0x378; UnlockMesh 0x2edf0 trims owned buffers. Draw 0x2b510 calls 0x2deb0, builds queue functor at 0x2df5a..0x2df7e with engine mesh, vertex/index buffers and counts; clears staging pointers at 0x2df88; 0x2b61c..0x2b648 queues mesh draw with counts. Matrix getter 0x32b40 reads producer matrix stack; exact context matrix and mesh slots verified. Guard both module identities, client callsites, material ownership instructions, known context/mesh vtables, mesh owner and idle allocation/override fields. No new queue functor, scratch pointer retention, or queued live particle isolation. Only Spark/CombineMuzzle supported in queued path; named materials excluded because arbitrary proxy behavior not established. Historical overlay can duplicate live legacy effects; no pixel-fidelity claim. Modern collection render 0x8e4840 still calls arbitrary renderer and post-render virtual methods and mutates native definition-linked collections; remains immediate-only. All queued behavior experimental until actual TF2 validation.
