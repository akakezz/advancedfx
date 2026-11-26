# Scene Cache 2: particle event feasibility (phase 1)

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
No AGR recording required. Record fresh history, pause live demo, enable cached
preview, and scrub normally. Native replay currently needs immediate material
rendering: use the explicit overlay button or mat_queue_mode 0 once (restore
your original setting afterwards). No automatic global cvar modifications.
Live particles are not suppressed yet; optional r_drawparticles 0 may help
isolate the test; restore r_drawparticles 1 afterwards if it was enabled.

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

Native draw requires the underlying demo to remain paused; the cached scene's
independent clock can still play or scrub normally. Active counts are reset
before native destruction. Status uses the same floor-tick selection as preview.
