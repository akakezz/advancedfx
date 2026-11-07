# TF2 demo seeking: native observation experiment

This is the first in-engine preparation for checkpoint-based seeking. It does
**not** restore checkpoints or remove TF2's backward-seek restart. Ordinary
`demo_gototick` remains responsible for playback and reconstruction.

## What changes now

The camera editor serializes seek requests: the engine finishes the current seek
before the editor dispatches the latest queued destination. Additional requests
replace that queued destination. It applies the final destination's captured
after-seek setting once; intermediate destinations do not briefly resume/freeze
playback. This controls competing requests, not the cost of a single long seek.

After explicitly enabling the probe by starting a trace on the verified native
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
Starting the first trace validates and enables the native observer. Before that,
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
trace start, after validation. They keep
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
compiling HLAE and collecting the native trace in a complete TF2 installation.
