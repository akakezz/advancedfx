# Open Fortress: supplied demo and binary verification

## Demo timeline

The supplied `dm_ravine` OF demo has demo protocol 3, network protocol 24,
79,720 ticks, and a 0.015 second tick interval. The previous parser failed
inside `dem_stringtables` at file offset 273763, tick 0. The payload is exactly
524288 bytes: 22 tables are declared, but the `Scenes` table is cut off after
14 preceding complete tables and the complete `userinfo` table. This is
consistent with the Source 2013 snapshot writer reaching its fixed buffer
limit. Signon and subsequent network packets parse correctly.

The shared parser tolerates only an OF snapshot of exactly this size when
userinfo has been read completely and a later read is truncated. Other
snapshot errors and all network packet errors remain fatal. It reports a
warning in both the GUI and console. It does not skip the userinfo table or
silently accept a partially parsed network stream.

The actual demo now indexes 100 retained timeline events, including correct
recorder-death identification, through the end. This is the indexer's
selected cinematic categories, not a count of every network game event.
The sample demo and DLLs are not included in the repository or bundle.

## Native engine ABI evidence

The supplied SDK Base 2013 Multiplayer `engine.dll` is PE32, timestamp
`0x60d42fc7`, image size `0x6d8000`, SHA256
`a333a6351a48afa41ac382cef9e19d7597d50baa1b5e157dcf900e2592cd6556`.
The supplied OF `client.dll` is PE32, timestamp `0x66b4fe85`, image size
`0xa27000`, SHA256
`8ba1210f5d721084fdc0c225cee3c40e1812dba996de69d1e5d93028bf869ea1`.
All addresses below are RVAs, not absolute loaded addresses.

- `IEngineSoundClient003`: factory returns the object at `0x3be008`,
  primary vtable `0x2ecd3c`. Slots 11, 16, 17 resolve to `0x578d0`,
  `0x57f90`, `0x57e20`. The ambient emitter constructs the expected
  static flag, source, channel, Sfx, volume, flags and pitch at the same
  offsets as the shared x86 StartSound prefix. Its call at `0x57989`
  resolves to `0x6ce50`, whose smaller stack frame differs from the
  TF2 prologue. `GetActiveSounds` copies 13 dwords (52 bytes) and accesses
  vector size at +12. The OF resolver now accepts that exact legacy
  routine after engine fingerprint, vtable and slot checks; TF2 stays strict.
- `StaticPropMgrClient004`: the factory returns the client subobject,
  vtable `0x330214`; slot 5 is `0x1f8840`. The function takes a vector
  reference and float, stores those values, and returns with `ret 8`.
  Exact interface validation replaces the earlier executable-slot-only
  gate. This verifies the call shape, not the visual effect of factor -1.
- `client.dll` contains `DT_TFPlayer` (not `DT_HL2MP_Player`), including
  eye-angle, pose parameter and overlay field names. `C_TFPlayer` RTTI
  identifies primary vtable `0x664408`, renderable subobject +4 with
  vtable `0x6648f0`, networkable subobject +8 with vtable `0x6649a8`.
  Primary slot 5 returns the renderable, and renderable slot 16 points
  to `0xb2ad0`. The disassembly reads four stack arguments including
  the float time, returns with `ret 16` at `0xb358d`, and matches the
  existing natural SetupBones observer's x86 signature.

The existing angle diagnostic is now exposed in OF. It uses the shared
receive observers, public scalar receive-table descriptors, verified player
renderable calls and natural SetupBones observation. OF memory reads use
bounded ReadProcessMemory; no private animation-state offsets or layer
vector traversal are enabled. Pose receive-proxy installation remains
excluded in OF; scalar pose coverage may therefore be limited to live
samples. Unknown player classes and images must fail closed; safe mode
still disables native integration. Live model metadata and bone behavior
must be verified on Windows. No new launch options are required.

## Verification and limitations

Executed on Linux with extracted production C++ logic:

- `test_demo_index_no_throw.py <supplied demo>`: actual demo, four capped
  snapshot variants, 8571 malformed/truncated inputs; ASan/UBSan.
- `test_recorder_sound.py --of-engine <engine.dll>`: real PE mapping,
  factory registration, sound slots, StartSound resolver, active-sound
  layout and static-prop ABI checks; simulated mute/restore; ASan/UBSan.
- `test_of_diagnostic_abi.py --client <client.dll>`: RTTI and slot
  validation, entry/return bytes, simulated gated scalar reads and natural
  bone callback/queue; ASan/UBSan.
- Ten further tests: OF demo fixes, feature policy, scene capture stage,
  render queue, scene parity, frozen sequence boundaries, recording
  resolution, recording outputs, camera interaction and attachment sampling.

All passed. The complete Windows x86 build and live engine calls have
not been tested. The full dependency-backed overlay/sequencer tests are
not claimed as passed. The prior working commit remains in the branch
history. The updated commit does not enable diagnostics automatically;
start them explicitly from Player Repair in a loaded offline demo.
