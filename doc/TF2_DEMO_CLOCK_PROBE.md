# Experimental TF2 demo server-clock retiming (A/B probe)

**Not an established demo-jitter fix.** Source 1 outer demo-command ticks
and embedded network NET_Tick values belong to different clock domains.
Valid recorded packets may still be scheduled unevenly. This tool tests
whether aligning the clocks improves visual replay. It does not modify
the HLAE DLL or change normal demo playback.

## Reproduce

From the repository root (Python 3.9+):

    python tools/tf2_demo_serverclock_probe.py original.dem --dry-run
    python tools/tf2_demo_serverclock_probe.py original.dem --output original_serverclock_probe.dem --report timing_report.json

The original is read-only; the output must not exist. Put the copy
in TF2's demo directory and compare with the original using the same
TF2 build, view, tick window, timescale, camera settings, and preroll.
Avoid changing interpolation settings or recording FPS between tests.

- Smoother: supports a scheduling contribution, not proof. Non-packet
  command times also change to preserve chronological ordering.
- Unchanged: the clock mismatch probably isn't the dominant cause.
- Worse or unable to load: discard the copy and use the original.

Supports only Source 1 demo protocol 3 / network protocol 24. Reads
first-message NET_Tick packets, fixes a constant relative clock offset,
and maps their outer ticks to the server clock. Other commands between
trusted packets are mapped monotonically into the same intervals.
Signon is untouched. The tool verifies that only four-byte tick fields
are edited and that every network payload and its embedded server tick
remains unchanged. A one-byte-truncated final dem_stop remains untouched.

For 2026-08-01_21-55-15.dem: 11,852 trusted anchors and 22,098 command
timestamps changed (9,833 packet, 11,594 usercmd, 671 console); mostly
-1 tick. Runtime TF2 behavior remains unverified.

Offline test:

    python tests/cinematic/test_tf2_demo_serverclock_probe.py
