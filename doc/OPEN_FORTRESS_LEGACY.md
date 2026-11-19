# Open Fortress legacy compatibility — experimental

This is an **opt-in compatibility experiment**. Campath drawing and HLAE recording were reported working in Open Fortress after the render-queue fix; other systems remain unverified.

## Source artifacts examined

- User-supplied Open Fortress `client.dll` and `server.dll`: both PE32/i386 binaries with PE build timestamps of 2024-08-08.
- The client contains interface-name strings `VClient017`, `VCLIENTTOOLS001`, `VClientEntityList003`, `VEngineRenderView014`, `VEngineModel016`, and `VMaterialSystem080`.
- Supplied `gameinfo.txt` names Source SDK Base 2013 Multiplayer / Steam app ID 243750 in the filesystem setup and uses DirectX 9.
- The [Ruiner9113 legacy repository](https://github.com/Ruiner9113/Open-Fortress-Legacy-Binaries) contains historical source from before the repository closure in 2020; **do not assume its code exactly matches the user-supplied 2024 binaries**.

## Minimal port path

HLAE already has 32-bit `SourceSdkVer_HL2MP` handling and a `VClient017` client-hook option. For experimental launches, `-afxGame openfortress` or `-afxGame open_fortress` now selects that existing HL2MP path. `-game openfortress` / `-game open_fortress` can also identify it if no earlier executable-name detection takes precedence.

This alias deliberately does **not** activate TF2-specific overlay or entity/bone/scene-cache hooks. In particular, the separate overlay remains disabled on this compatibility path. No engine-side signature or vtable offset has been proven against the Open Fortress runtime.

## First validation: recording and campath drawing

1. Run an **offline** Open Fortress demo through the 32-bit Source SDK Base 2013 Multiplayer executable, using the normal HLAE loader/injection setup and `-afxGame openfortress`. Avoid testing third-party injection on public multiplayer servers.
2. Confirm startup shows a `VClient017` interface and no hook initialization errors.
3. Check `mirv_streams` console registration. Try a simple screen recording profile and verify actual output bytes and frames, not just a successful command message.
4. Create or load a basic `mirv_campath`, enable drawing with `mirv_campath draw enabled 1`, and check that the path displays in 3D. Then verify `mirv_campath enabled 1` actually controls the camera during demo playback.
5. Verify recorded footage has correct FOV, colors, frame pacing, and audio; verify the campath and its guide geometry respect the correct world/view transform.

Do **not** proceed with an all-feature port unless these fundamental paths work.

## Remaining prerequisites

- The Open Fortress engine executable plus `engine.dll` and relevant graphics/engine DLLs (typically from **Source SDK Base 2013 Multiplayer**, rather than the mod directory) would allow signatures and engine interfaces to be examined statically. Files must match the actual runtime tested.
- The user built and tested the patched branch in Open Fortress and confirmed working campath drawing and HLAE recording. Full camera override, recording quality checks, and TF2-specific features have not been verified. A `VClient017` string in a binary does not prove all virtual function indices/layouts match.
- If the minimal path fails, diagnose individual engine/client/D3D9 hooks instead of routing Open Fortress through TF2's x64-specific implementation.

## Targeted material-system render-queue test (2026-11-19)

Sentry minidumps from campath drawing showed an attempted execution of a non-executable pointer from `MaterialSystem.dll + 0x4541D`, reached via HLAE's render-queue path. In the matching June 24, 2021 32-bit `MaterialSystem.dll`, `CMaterialSystem::GetRenderCallQueue` is at **primary vtable slot 143**, not the standard HL2MP slot 148. Slot 148 instead resolves to a secondary-vtable shader-device thunk, explaining the observed failure.

The experimental branch now uses slot **143** only when `g_OpenFortressLegacyMode` is set by `-afxGame openfortress` or `-afxGame open_fortress` (or by a literal short-name `-game` fallback). Standard HL2MP continues using slot 148. The queued-functor ABI and other render-system hooks may require additional validation.

Launch example (for an absolute mod directory):

```text
-game "C:\\Program Files (x86)\\Steam\\steamapps\\sourcemods\\open_fortress" -afxGame openfortress
```

The explicit `-afxGame` switch is required for this Open Fortress-specific selection when `-game` is a full path. After building the 32-bit HLAE hook, test **offline** with a minimal campath: first verify the draw command no longer crashes, then confirm visible 3D path geometry and live camera override. Test recording separately afterward. If another crash occurs, retain the console log and fresh minidump. **User-tested in-game:** campath renderer and HLAE recording work with the targeted render-queue fix. This does not validate all camera, overlay, or Scene Cache paths.

## User test: campath drawing and recording confirmed (2026-11-19)

After selecting `-afxGame openfortress` and updating the Open Fortress-specific render queue accessor, the user confirmed that campath drawing renders a green 3D guide in the game and that HLAE recording works. The earlier campath-drawing hard crash no longer reproduced in that test. The user also reported `mirv_overlay` returning an unavailable message. That is expected with the current `Gui::IsSupported` gate, which supports only TF2 and TF2 Classified, not Open Fortress. A generic diagnostic message was added; the overlay itself remains disabled. A future workspace port should keep TF2-specific player, attachment, and Scene Cache paths gated until separately verified.

## Integrated cinematic overlay port: source and binary audit (2026-11-19)

The objective is **one existing cinematic workspace**, not an Open Fortress-specific duplicate. UI enablement is deferred until its engine dependencies are connected and validated together.

Evidence from the supplied Source SDK Base 2013 MP / Open Fortress binaries:

- `client.dll` advertises `VClient017`, `VCLIENTTOOLS001` and `VClientEntityList003`. MSVC RTTI identifies genuine `CHLClient` and `CClientTools` vtables.
- `CHLClient` vtable slot **35** points into the game's frame-stage dispatcher (the function handles stages 0..6), matching the hook location used by HLAE's TF2 VClient017 path.
- `engine.dll`, `shaderapidx9.dll` and `vguimatsurface.dll` are 32-bit modules; the available interfaces and rendering path support the already working campath drawing and recording.
- These observations do **not** prove every TF2 player/model method is ABI-compatible. In particular, model setup, client tool enumeration, render queues and gameplay-specific repair controls need in-game verification.

Infrastructure change: `main.cpp` installs an **Open Fortress-only** VClient017 frame-stage hook (render start/end), calls the existing completed-frame scene-cache entry point, and constructs the existing `CClientToolsTf2` entity adapter from `VCLIENTTOOLS001` if the interface is present. These are shared-tool interfaces, not a duplicate workspace. They remain opt-in via `-afxGame openfortress`; normal HL2MP and TF2/TF2C behavior is unchanged.

Remaining integration checks before opening `Gui::IsSupported()`:

1. `mirv_cam` game gating, target rebind and entity/bone offsets on Open Fortress player classes
2. Scene Cache entity classification, bone/model capture, queued cached-model rendering, and seek/replay lifecycle
3. Open Fortress camera input/cursor ownership and 32-bit Direct3D9 overlay rendering
4. Sequencer, project persistence, clip transitions, demo observation and recording UI
5. TF2-only `MirvFix` controls: adapt equivalents where possible rather than assuming they work in Open Fortress

**Status:** source committed; not yet compiled or validated in Open Fortress. This is preparatory integration code, not an enabled or completed overlay port. Do not claim parity until runtime tests pass.

## Entity inspection command routing fix (2026-11-19)

The first Open Fortress integration smoke test showed the original recording/campath functionality still works, but `mirv_listplayers`, `mirv_listentities` and `mirv_listbones` all printed `Not supported for your engine!`. Investigation confirmed that the **commands themselves** reject every engine except TF2/TF2C: they never reach the recently wired Open Fortress entity-tools adapter. This message therefore did **not** establish that the hooks or interfaces failed.

The command gates now also accept only an explicitly selected Open Fortress legacy mode, while preserving the CS:GO route and all other engine restrictions. In order of escalating engine calls, test `mirv_listplayers`, `mirv_listentities isPlayer=1`, then `mirv_listbones <valid player index>`. A missing tools warning, an empty result, a crash, or a valid result each provide different diagnostic evidence. The full cinematic overlay remains deliberately gated and is **not** part of this command-routing patch.

## Attached camera integration (2026-11-19)

The user has now confirmed that all three entity inspection commands work in the Open Fortress build and that `mirv_listbones` shows recognizable bone names with plausible world-space coordinates. This validates the path through `VCLIENTTOOLS001`, player enumeration, model information and the per-frame bone transforms used by the existing camera code.

This branch now enables `mirv_cam attach` for explicit Open Fortress legacy mode by using the existing TF2/TF2C `Mirv_Tf2_` entity and bone camera calculations. The unsupported `mirv_cam source handle` path remains blocked, avoiding the CS:GO-style handle route. Pure mathematical attached-camera follow smoothing is allowed, but camera collision and trace-ray hooks remain gated until their ABI is verified.

Retest in an offline demo with a valid entity index from `mirv_listplayers`:

```text
mirv_cam attach <playerIndex>
mirv_cam attach clear
mirv_cam attach <playerIndex> <boneNameFromMirvListbones>
mirv_cam attach follow 0.15
mirv_cam attach follow 0
mirv_cam attach clear
```

Check that a moving target is followed without crashes, that a named bone remains stable during animation, that camera clears return to the appropriate pose, and that a demo seek or target disappearance does not produce a hard crash. Do not turn on collision until a separate verification. The existing overlay support gate stays closed pending complete integration.

**Status:** static source checks passed; awaiting a Windows build and in-game attached-camera tests. These changes do not yet prove target rebinding or cached Scene Cache preview.

## Entity-index visual inspection (2026-11-19)

The existing `mirv_drawentities` command and engine-thread `EntityIndexDrawer` were also explicitly restricted to TF2/TF2C. They now recognize only opt-in Open Fortress mode (while other HL2MP installations remain unchanged). This uses the source-backed entity collector and the same queued D3D9 material-system rendering path whose 2021 Open Fortress vtable mismatch was previously repaired.

For an offline demo, after confirming the three `mirv_list*` commands:

```text
mirv_drawentities enabled 1
mirv_drawentities status
mirv_drawentities enabled 0
```

Check that numeric labels appear above entities, follow movement, and that turning them off restores the original view. Entity category classification may require Open Fortress-specific additions even if indices and positions are correct. Do not assume that this enables the Scene Cache wireframe or full cinematic UI; these remain separate dependencies within the same workspace.

**Status:** source patched; not built or tested in-game yet.

## Repeat-attachment crash: root cause identified and patched (2026-11-19)

The user uploaded `sentry(2).zip` containing four repeat-attachment crashes between 15:44 and 15:52 **Asia/Riyadh** local time (12:44–12:52 UTC), and matching `AfxHookSource(1).pdb`.

**Verified evidence:**

- All four minidumps are x86 access violations (`0xC0000005`) **reading address `0x00000001`** in Source SDK 2013's `tier0.dll + 0x21A48`, with the same HLAE command-dispatch stack: `AfxHookSource.dll + 0x162C00 / + 0x166225 / + 0x18FBC5`.
- All dumps' loaded HLAE CodeView GUID and age (**133**) match the supplied PDB.
- The PDB's public symbol map identifies a literal string at `AfxHookSource.dll + 0x35B080`: `"Attached camera %zu: %s.\\n"`. This string appears on the crash stack, and the log recorded `Attached camera zu: (null).` on the first attachment.
- Source SDK 2013's legacy `Tier0_Msg` printf parser does not recognize `%zu`. It treats `zu` as text and passes the camera index (0 for the first registration, 1 for the second) as the next `%s` argument. Index 0 prints `(null)`; index 1 becomes a bad string pointer to address `0x1` and crashes.

**Fix:** the experimental branch replaces all three affected `Tier0_Msg` camera-index `%zu` format specifiers with `%u`, explicitly casting the bounded camera index to `unsigned`: attached-camera registration, camera list, and newly added camera messages. The unrelated camera-history instrumentation from the initial hypothesis was removed. Console Scene Cache status messages also now stringify `size_t` and 64-bit counts before printing, avoiding this older formatter bug (and the prior `zu frames` output).

The reported `mirv_cam attach follow` issue was **separate**: live legacy attachment and the registered named-camera rig store their follow parameters independently. The Open Fortress-only source change synchronizes those values. **This smoothing fix and the corrected repeated-attachment handling are not yet Windows-built or game-tested.**

Next smoke test on a rebuilt 32-bit hook in an offline demo:

```text
mirv_cam attach <playerIndex> <boneName>
mirv_cam attach <playerIndex> <boneName>
mirv_cam list
mirv_cam attach clear
mirv_cam attach <otherPlayerIndex>
mirv_cam attach follow 0.15
mirv_cam attach follow boneHalfLife 0.15
mirv_cam attach follow 0
mirv_cam attach clear
```

Confirm the second attachment no longer crashes, `mirv_cam list` prints valid indices/names, and smoothing is visible when the subject moves. Retain a fresh dump if any crash remains. The overlay support gate is unchanged; this does not claim the unified workspace is ready.

## Follow smoothing runtime diagnostics (2026-11-19)

The user confirmed that **re-attaching no longer crashes** after the Tier0 `%zu` formatting fix. However, `mirv_cam attach follow` is still not visibly smoothing the camera. The previous Open Fortress-only synchronization of the legacy filter and registered camera motion remains, but we have not established why the live render result appears rigid.

The experimental branch now adds Open Fortress-only runtime counters printed by `mirv_cam attach follow status`. These distinguish settings being accepted from the live render callback receiving the attachment anchor and applying a filtered position. Status reports cumulative `calls`, successful `applied`, `continuous` evaluations, `advancing` evaluations with a positive time delta, `anchor` (1 = currently valid), last scene `t`, last `dt` (-1 = state reset), current configured half-life, and accumulated changed `correction` frames / peak positional difference (Source units). The counters are read-only atomics and do not print per-frame spam or enable unverified trace/collision hooks.

To diagnose with a rebuilt **x86** hook, use a moving player in an offline demo, then enter:

```text
mirv_cam attach <playerIndex> <boneName>
mirv_cam attach follow 1.0
mirv_cam attach follow status
```

After allowing the demo to run for several seconds, call `mirv_cam attach follow status` again and copy the two `OF follow runtime` / `OF follow camera correction` lines and any `Attachment follow edit rejected` or `Open Fortress follow:` warnings. A count of zero applied frames points to input/anchor/routing; continuous but non-advancing time points to the demo-time source; growing advancing frames and zero corrected frames points to the smoothing math, while nonzero peak correction demonstrates an applied change (even if visually subtle). Use `mirv_cam attach follow 0` to reset the setting afterward.

**Status:** source-checked only; not Windows-built or runtime-verified. No unproven change to the filter algorithm has been made, and the unified overlay remains gated.
