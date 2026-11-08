# TF2 demo seeking: native observation and capture experiments

This is the first in-engine preparation for checkpoint-based seeking. It does
**not** restore checkpoints or remove TF2's backward-seek restart. Ordinary
`demo_gototick` remains responsible for playback and reconstruction.

## Capturing checkpoint evidence

The next experiment retains network property encodings by hooking the existing
`RecvTable_Decode` once and combining its consumed input with the previously
owned encoding through TF2's `RecvTable_MergeDeltas`. It does not copy client
objects or implement replacement property codecs. It forwards the original
decoder's arguments and result, and merges into separate owned buffers afterward.
The merge routine was executed successfully on isolated buffers from the recorded
Windows binary; that does not establish correct live checkpoint restoration.

On the verified Windows x64 build, with a demo already playing:

```text
mirv_campath preview checkpoint begin
playdemo "your-demo"
demo_gototick 90000 0 1
```

Wait for the restarted demo to load before entering `demo_gototick`. Wait for
that seek to finish, then capture:

```text
mirv_campath preview checkpoint status
mirv_campath preview checkpoint capture "C:\temp\tf2-checkpoint-90000.xml"
mirv_campath preview checkpoint end
```

Use your actual demo name and desired tick. This is a diagnostic experiment;
it still performs the normal initial reconstruction and does not speed up seeks
yet. `begin` arms collection without restarting playback itself. A new full
entity packet is required before capture; starting midway through a delta chain
is rejected. Restarting playback resets the collected state. `end` releases it.
Hooks stay installed and forward calls after collection ends. Starting a trace
or capture enables the same native seek-lifecycle observer.

`begin` must print **Checkpoint capture armed** before you restart playback.
If it fails, it now identifies the failed check (engine build, RTTI, instruction
binding, unavailable player/entity list, loading/seeking state, or a Detours error).
`status` retains the last command failure and relevant native binding diagnostic.
A failed initialization can be retried after the demo is ready; it no longer
permanently caches a failed attempt. If the build is unsupported, the message
prints the actual and expected PE identifiers. That guard remains enforced: a
different engine needs investigation before using these native layouts.

If status remains `off`, copy the output of `begin` and `status`; do not repeatedly
restart/seek or change output filenames. A normal demo restart clears collected
data while preserving the armed state. Only a successful `begin` arms capture;
`capture` reports separately when collection is off, unseeded, invalid, busy, or
unable to create its output file.

The XML includes:

- Latest merged property encodings for each current network entity, full entity
  handles, class IDs and flattened encoding schemas. Deleted/stale slot contents
  are excluded; a current entity without a matching encoding rejects the export.
- Original server decode tick for each observed property. TF2 receive proxies
  such as `RecvProxy_SimulationTime` interpret values relative to packet time;
  reapplying every old value at the checkpoint tick would be incorrect. An
  unobserved field has the explicit sentinel `-2147483648`. The relationship to
  the client/global clock at proxy invocation must still be verified; the saved
  server tick alone does not establish a correct proxy restoration context.
- Current string-table entries and user data, both entity-baseline banks, and
  known reference-frame ticks and transmitted-entity bitsets.
- Demo filename, current demo/server ticks and input cursor. The cursor is
  evidence only: read-ahead and clock bookkeeping have not been validated as a
  resumable stream boundary.

Arbitrary strings and binary data are hex encoded, with little-endian 32-bit
property tick arrays. Files contain no engine pointers. The file is explicitly
marked `restorable="0"`; reference frames are marked incomplete. Client
interpolation history, receive-proxy side effects, physics, particles, sounds,
and some frame/stream bookkeeping are still missing. There is intentionally no
load/restore command.

Capture requires a completed network-update boundary, idle native seek state,
matching entity identities and a new writable filename. Existing files are not
overwritten. Property payload storage has a 64 MiB budget, each entity encoding
is capped at the engine's 16 KiB limit, and output assembly has an additional
bounded budget. A conservative preflight rejects merges that could overflow the
native writer, whose own error path would be fatal. Collection failures invalidate
the candidate and are reported by `status`; they do not change the original
decoder's return value. Explicit XML export is synchronous and may briefly stall;
leave collection off during normal editing or performance measurements.

## What changes now

The camera editor serializes seek requests: the engine finishes the current seek
before the editor dispatches the latest queued destination. Additional requests
replace that queued destination. It applies the final destination's captured
after-seek setting once; intermediate destinations do not briefly resume/freeze
playback. This controls competing requests, not the cost of a single long seek.

After explicitly enabling the probe by starting a trace or capture on the verified native
engine, completion requires the native seek call to have
executed and the loading/skipping state to clear. A temporary reload does not
discard the pending request. A different playback session or an external seek
cancels the editor's pending after-seek action. A seek times out after two minutes
without observed tick progress, rather than two minutes of total elapsed time.

On unsupported engines the existing tick-based completion approach is retained,
with the queue and progress timeout. It cannot provide the same native lifecycle
acknowledgement or external-session detection. No new SDK vtable slots are added
to the wrappers shared by other Source games.

## Recording a diagnostic trace

Use a short trace around the operation being tested:

```text
mirv_campath preview seekTrace status
mirv_campath preview seekTrace start "C:\temp\tf2-seek-01.csv"
demo_gototick 90000 0 1
demo_gototick 89500 0 1
mirv_campath preview seekTrace stop
```

Wait for each ordinary command above to reach its destination. Then make a
separate trace using the editor's seek button or timeline: forward/backward,
repeated targets, tick zero, near the end, and several requests while a seek is
already running. Test both POV and STV recordings and the desired after-seek
behavior. Camera/path data should remain intact across the reload.

The directory must exist and the filename must be new. Existing files are never
overwritten. Stop closes the file and prints trace status. Tracing and native
hooks are off by default; no network upload or automatic recording occurs.
Starting the first trace or checkpoint capture validates and enables the native observer. Before that,
the editor uses its ordinary tick-based completion.

CSV rows contain:

- Native seek arguments and entry/return events, including relative/pause flags.
- Playback-start entry/return and a playback identity counter. A restart inside
  a native seek retains the identity; a separate playback start changes it.
- Demo ticks, pending destination, playing/loading/skipping states, and seek serial.
- Entity-packet delta reference, baseline bank/update flag, update count, and
  handler return value.
- Retained frame count before the message, whether its referenced frame exists,
  string-table count, and the number of `instancebaseline` strings.

`-1` means unavailable or inapplicable. The entity message's accepted field is
the native handler's return value, not an assertion that every presentation
system has been reconstructed correctly. No baseline payloads, entity memory,
demo cursor or complete checkpoint are saved in this trace.

The buffer holds 4,096 fixed-size records. Entity-message callbacks only collect
data; file writes happen on editor updates and full batches at the existing TF2
network-update frame boundary, at most 256 records per call. This also drains
the buffer during fast-forward when normal rendering may be skipped. Stop
drains the remaining buffer. The footer/status reports dropped records and file
write failure. A trace with drops or a missing footer must not be treated as a
complete event history. Diagnostic reads/writes add overhead, so these timings
are not clean seek-performance benchmarks. Use a local file and short captures.

## Supported native build and binding checks

The native probe currently supports the inspected **Windows x64 TF2** engine
only. It refuses TF2 Classified, x86, other Source games and unrecognized builds.

Recorded engine.dll SHA256:

```text
4802a74ce0abb1175851d00e37fc88609085a96238aa52ac0eb224ae4a08ec80
```

Steam depot `232251`, manifest `3447145031938049636`.
Runtime checks use PE timestamp `0x6ac41507`, image size `0x822000`, checksum
`0x4fb7d1`, RTTI lookup and instruction checks. This is a build guard, not a
cryptographic verification of the loaded file.

Binding reuses HLAE's `FindClassVtable` and Detours. The demo-player pointer is
resolved from the actual `IVEngineClient013::IsPlayingDemo` wrapper's RIP-relative
load. No absolute virtual addresses are embedded. Internal layouts and vtable
indices remain specific to the guarded build. Hooks install only on an explicit
trace/capture start, after validation. Capture additionally resolves decoder/merge
functions from diagnostic strings and their chained unwind entries, and the
entity-list accessor from the inspected native copy routine. They keep
the original arguments, execution order and return values; the native player
and decoder data are not rewritten. Stopping the trace disables collection;
installed hooks continue forwarding calls until process exit.

Static anchors in that recorded file (preferred base `0x180000000`):

| Role | Preferred VA |
| --- | --- |
| CDemoPlayer vtable | `0x18036a788` |
| Playback start | `0x180074ed0` |
| Seek | `0x180074e10` |
| IsSkipping | `0x180073d90` |
| CClientState message-handler vtable | `0x1803832d8` |
| ProcessPacketEntities message handler | `0x1801faf60` |
| Lower-level packet entity processor | `0x180089550` |
| RecvTable_Decode | `0x18018b740` |
| RecvTable_MergeDeltas | `0x18018bca0` |

The CClientState message handler receives its subobject pointer at base + 16;
the probe compensates before inspecting the frame manager. Chained PE unwind
records must be followed when identifying function entries; the region around
an error string is not necessarily the function entry.

## What this must establish before restoration

Checkpoints need more than a demo file offset: matching class/string tables,
both instance baseline banks, retained delta-reference frames, client entity
state, and sequence/timing bookkeeping. The trace establishes a way to observe
those dependencies around real seeks without replacing their behavior.

The next restore experiment should reproduce one earlier boundary and compare
against sequential/native restart playback. Network values alone are
insufficient: interpolation, player animation, particles, ragdolls, projectiles,
sounds, and editor-state preservation must be compared as well. Until that
passes, falling back to ordinary native restart/catch-up remains necessary.

## Validation scope

The seek queue has native-independent regression tests covering reloads, stale
ticks, command acknowledgement, latest-request selection, external playback
changes, long progressing seeks, stalled seeks, and legacy fallback. The actual
preview/overlay test harness also runs the changed controller. Changed native
translation units were cross-compiled for Windows x64/x86; binding checks were
inspected against the recorded engine binary.

No complete TF2 playback or live hook invocation was tested in the cloud: the
installation has client binaries only, as requested. This experiment requires
compiling HLAE and collecting the native capture in a complete TF2 installation.

`test_demo_startup.py` runs the actual startup/control functions against engine
and OS stubs: failed-start retries, build rejection, missing decoder bindings,
loading/seeking, preserving the armed state on reset, and specific capture/file
errors. It is registered with the Unix test suite when Python is available.
It does not invoke the real live hooks.

`test_cinematic.cpp` also checks capture ownership, property timing retention,
entity-slot reuse, invalid updates and memory-budget rejection. An optional Linux
x64 test executes the actual merge routine from the external recorded DLL:

```sh
g++ -std=c++17 -O1 tests/cinematic/test_native_demo.cpp -o /tmp/tf2-native-demo-test
/tmp/tf2-native-demo-test /path/to/recorded/engine.dll
```

It checks the diagnostic-string bindings, including shared strings and chained
unwind records, maps the verified PE at its preferred base, and supplies the memory/error
imports required by this isolated call. It never runs the DLL entry point or
initializes Steam, the engine or a game world. The test covers 1,007 merges,
all six flattened property types, unsigned variable integers, field insertion
and replacement, changed-field indices, non-byte-aligned input, empty inputs,
and input-payload isolation. Synthetic schemas do not prove fidelity for every
real TF2 property flag, receive proxy or packet boundary.
