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

## Follow runtime result: no active live source (2026-11-19)

The user's `mirv_cam attach follow status` after a 1.0-second half-life showed **calls 8552, applied 0, continuous 0, advancing 0, anchor 0, half-life 1.000, zero position correction**, and **live source attached 0**. The setting was accepted, but the legacy render filter never received a live attachment identity and entity. It is incorrect to interpret that output as an optical or smoothing-math failure.

Source review identified two camera routes: the live legacy `g_MirvCam` calculator and the separately registered `g_CameraRigs` camera controller. Under explicit Open Fortress mode, `mirv_cam attach follow` now updates the **active attached named rig** if it is controlling the camera (`CameraSource::Rig`), otherwise the legacy registered attachment. TF2/TF2C are unchanged. No unverified collision tracing is enabled.

The status now prints `Open Fortress camera route: ...` with controller, preview state, whether a live source and an attachment calculator exist, their pointer identity match, and whether the selected/legacy registered camera is attached. For an active attached rig it also prints the rig's own follow half-life. No recovery of stale entity pointers is attempted. The legacy evaluator telemetry remains unchanged.

**Next test, after rebuilding the x86 hook:** In an offline demo with a moving target, execute `mirv_cam attach <playerIndex>` and then **immediately** `mirv_cam attach follow 1.0` and `mirv_cam attach follow status`; let the demo play and repeat the status, then try `mirv_cam list`. Copy `Open Fortress camera route:`, `Open Fortress active rig follow:` (if present), `OF follow runtime:`, and `OF follow camera correction:` lines. If the controller is legacy with no live attachment, reattach to a valid entity in the current demo rather than expecting follow to create an attachment itself.

**Status:** static source review complete; no Windows build or in-game validation yet. The unified cinematic overlay remains gated.

## Open Fortress named rig follow jitter (2026-11-19)

The user's second follow status confirmed the active camera route is **rig**, not legacy: `source 0, last attachment 0, live match 0; selected attached 1, legacy attached 0`, and `Open Fortress active rig follow: 1.000 s, bone 0.000 s`. They observed that the camera started responding to the attached player but repeatedly jittered/corrected itself. The `OF follow runtime: applied 0, anchor 0` readings are expected when the legacy filter is inactive; they do **not** imply the named rig was unfiltered.

Source inspection found that `MirvCam_EvaluateRig` warms **all** attached camera definitions by calling `EvaluateNamedRig` before it evaluates the current selected rig again to draw it. On explicit Open Fortress mode, the selected named rig now **skips the redundant background warm** and is evaluated only for the displayed view. Nonselected cameras continue warming; TF2 and TF2C are untouched. This is a plausible, but **unproven**, cause of repeat correction from double target/bone sampling at the same timestamp.

The `mirv_cam attach follow status` report now shows actual `OF rig filter` and `OF rig correction` counts for the active named-camera route, including evaluations, successful filter applications, target losses, resets, same-time samples, advancing-time samples, last delta, last/peak positional correction, history validity, and current `PreserveFraming`. The old `MirvCameraAttachedApply` telemetry appears only when the legacy camera route is selected, avoiding confusion.

For the next x86 build, use a moving player and compare:

```text
mirv_cam attach <playerIndex> <boneName>
mirv_cam attach follow 1.0
mirv_cam attach follow status
mirv_cam list
mirv_cam edit <cameraIndex> follow preserveFraming 0
mirv_cam attach follow boneHalfLife 0.10
mirv_cam attach follow status
```

Watch movement for several seconds, then repeat status. `preserveFraming 0` removes screen-direction correction but retains root smoothing; `boneHalfLife 0.10` separately smooths animated bone motion (previously 0 = rigid). Test each change independently if possible. Use `mirv_cam edit <cameraIndex> follow preserveFraming 1` and `mirv_cam attach follow boneHalfLife 0` to restore previous settings. If jitter remains, supply both `OF rig filter` and `OF rig correction` status lines and any `Camera mixer unavailable` warnings to identify continuity resets or lost targets.

**Status:** source patches validated by inspection only, not Windows-built or runtime-tested. The unified overlay remains gated; no separate workspace is being introduced.

## Open Fortress single-sample attachment inputs (2026-11-20)

The OF-only live named rig now reads the root-facing angles together with the physical bone pivot and root origin from the cached `CalcVecAng` attachment sample. Previously `CameraAttachmentTarget` performed a second entity calculator read to obtain the body angles after sampling the bone anchor. If OF updates animation/render state between those reads, the rig passes a mismatched pivot/root rotation to the existing follow filter, potentially causing visible re-corrections. TF2/TF2C remain on their previous path, and `MirvCameraAttachedEvaluate` (the smoothing algorithm) is unchanged. This is an integration hypothesis, not yet verified in a live build.

Rebuild the x86 hook from this branch. On the same moving OF demo, run `mirv_cam attach follow 1.0`, watch the shot, then `mirv_cam attach follow status` after several seconds of uninterrupted playback. Capture `OF rig filter`, `OF rig correction`, and `Open Fortress active rig follow` lines (not the legacy-only `OF follow runtime` counters). Compare jitter with the previous build at the same demo moment. If the problem persists, use `mirv_cam edit <cameraIndex> follow preserveFraming 0` as an independent diagnostic, then restore with `preserveFraming 1`.

## Deferred: OF attached-rig smoothing jitter (2026-11-20)

User testing of the active named camera showed a valid attachment and a working follow filter: calls/applied 45485/45485, failed 0, lost target 0, 23845 repeated-time evaluations, 21084 advancing evaluations, 556 resets (554 backward-time, 2 invalid history, zero long gaps). Switching the experimental `reuseSameTime` setting on/off made no visible difference. The remaining visible jitter may involve the framing correction (`preserveFraming 1`), but this has **not** been verified. Do not rework the shared smoothing algorithm on speculation. Follow smoothing is parked while other OF features are developed.

## OF recording hotkey (2026-11-20)

Added `mirv_streams record toggle` for explicit Open Fortress legacy mode only, dispatching to the existing `mirv_streams record start` and `end` operations depending on the active recorder state. This is designed for a manual Source key binding, for example `bind F9 "mirv_streams record toggle"`. The command does not alter recording FPS, recording settings, stream selection, or existing TF2/TF2C/HL2MP command behavior. It does not auto-assign a key. This is source-reviewed only; verify in a rebuilt x86 Open Fortress demo with an enabled screen recording profile, including that the first press produces an actual take and the second stops it. Because starting non-CS:GO recordings is queued, allow the recorder to enter its active state before toggling again.

## Initial OF cinematic workspace and mirv_aim entity support (2026-11-20)

The existing DX9/ImGui cinematic overlay now accepts `SourceSdkVer_HL2MP` **only** with `g_OpenFortressLegacyMode`. It remains the same workspace, not a separate OF overlay. Start by verifying opening/closing with `mirv_overlay 1` and F8, then Campath, Cameras, Recording, Project, Settings, and timeline interactions. Runtime/render validity are not yet demonstrated by this source change.

The remaining TF2-only gate in `Aiming::RebuildCalc` now also accepts explicit OF and uses the `NewTf2EntityIndexCalc` / `IClientTools` path already verified for OF camera attachments. Test `mirv_aim entityIndex <playerIndex>`, `mirv_aim active 1` and `mirv_aim camera @<id> entityIndex <playerIndex>`. OF does not acquire CS:GO's handle-specific net/view behavior.

Unverified paths are still restricted in OF: the TF2-only demo-fix controls and player-repair editor/timeline track, camera collision UI, and cached studio-model rendering remain disabled or hidden. Scene Cache entity and pose capture, model playback, cursor ownership and other UI interactions require focused offline testing; no full workspace parity is claimed. No other engine's support gates change. Source-reviewed only: no x86 compilation or runtime validation has yet been performed.

The Scene Cache capture/preview menu is temporarily read-only in the initial OF workspace smoke test to prevent accidental invocation of unverified Source studio-pose APIs. It will be restored within the same shared UI after validation. This is separate from the already working camrig/campath drawing.

## PR #1 workflow port and OF demo fixes (2026-11-20)

Ported compatible portions of draft PR #1 (`fix/cinematic-workflow-2026-11`, head `12989323c70ef40c16a37a35e3b23a562b8b9a73`) into **only** the OF development branch. Shared `CamPath`, `RecordingSettings` and `AfxStreams` changes were copied from the fix branch because the pre-port OF versions were byte-for-byte identical to the PR base. `Gui.cpp` is based on the fix branch with the explicit `g_OpenFortressLegacyMode` compatibility gate preserved. `CameraOverlay.cpp`, `MirvCommands.cpp` and `MirvCam.cpp` were selectively adapted to preserve OF camera routing, selected-rig follow diagnostics, shared overlay, and integration restrictions. No merge or write was made to the fix branch.

Ported features: global chronological campath/camera undo/redo; latest-outgoing-pose fallback for explicit and sequenced transitions, with cached outgoing reconstruction only when there are usable prior scene frames; recording profile registry and actual selected profile; recording hotkey capture with modifiers; record auto-resume, pause, close/stop-on-open preferences; native system cursor overlay; observing selects players without implicitly changing camera, with explicit STV spectate button.

Demo fixes: the non-detour `mirv_fix playerInterpolation` path is experimentally enabled for OF using Source engine ICvar if `cl_interpolate`, `cl_interp` and `cl_interp_ratio` are present and readable/writable. The OF frame-stage hook performs native interpolation lifecycle maintenance and persisted preference application at `FRAME_START`, without invoking TF2-only repair hooks. In OF, the Demo fixes panel exposes the interpolation option only. The TF2 recorder-sound mute, POV deathcam proxy detour and player-repair functions remain disabled: their hooks and/or class layouts have not been verified for this OF client build. On OF, pref application does not arm those TF2-only hooks. Try `mirv_fix playerInterpolation status` before enabling and `mirv_fix playerInterpolation default` to restore values.

**Validation pending:** This port has not been compiled with Windows x86 MSVC or tested in OF at runtime. Existing Scene Cache capture/replay remains unvalidated; the outgoing transition recovery code cannot manufacture missing cached frames. For recording tests, configure a screen profile and enabled screen recording before starting a take. Test modifier hotkeys with console closed and no text field active. Keep the branch isolated and verify each feature before promoting it to stable status.

### Optional OF POV deathcam experiment

The OF VClient017 client pointer is now made available to the existing receive-property lookup, but no observer proxy detour installs automatically. The existing `mirv_fix povDeathcam 1` / `0` command or the OF-only experimental checkbox explicitly installs/activates the proxy if `DT_BasePlayer.m_iObserverMode` exists. A missing or invalid property is reported and rejected. The hook only substitutes deathcam/freezecam states for the local player during non-STV demo playback. **This has not been runtime-tested and may be incompatible with the exact OF receive-proxy ABI; keep disabled unless testing an expendable demo session.** OF preference reapplication does not auto-enable the hook and the OF UI does not persist it. TF2/TF2C behavior is unchanged; standard HL2MP cannot enable it. This does not port recorder sound muting, player repair, or other TF2-specific hooks.

## OF overlay cursor, UI cursor and recording-folder crash triage (2026-11-20)

User runtime testing confirmed the initial OF cinematic workspace and most PR #1 workflow features are functional. They reported a hidden/flickering hardware cursor while the overlay owns input, `mirv_input camera uiCursor` failing to yield to an open Source UI (such as `demoui`), and a crash after clicking **Open recordings folder**. Supplied Sentry ZIP contains four x86 access-violation minidumps: three fault in `tier0.dll`, one later fault in `AfxHookSource.dll` reading at address `0xE4`. These dumps do not include the matching PDB, so they are not enough to prove the recording button is the faulting call. The console log showed recording start and stop before one crash; it also showed that `cl_interp_ratio` is explicitly locked by OF while playing.

The OF workspace now selects ImGui software cursor rendering while it owns input and suppresses the native pointer; TF2/TF2C retain the PR #1 OS cursor implementation. The `VGUI_Surface030` visibility probe now accepts explicit OF `SourceSdkVer_HL2MP` mode, so `mirv_input camera uiCursor 1` should suspend freecam input when the engine reports the cursor visible. Test with `mirv_input camera uiCursorStatus` when `demoui` is visible, allowing for the console itself to influence visibility.

The recordings folder button now queues its action until after the ImGui frame and opens an existing folder through `CreateProcessW` with an explicit Windows Explorer executable instead of calling `ShellExecuteW` inside the widget callback. Missing/unavailable directories report an overlay message rather than attempting a launch. **This is a targeted hardening change, not a symbolicated crash root-cause confirmation.**

The initial OF native interpolation attempt required the absent `cl_interp` cvar. The attempted `cl_interp_ratio / cl_updaterate` fallback is now **read-only diagnostics**: OF's own console rejects changing `cl_interp_ratio` while playing. `mirv_fix playerInterpolation status` may report the estimated ratio interval but `MirvFix_PlayerInterpolationAvailable()` refuses to enable this path during demo playback; the unsupported OF setting is disabled in the overlay. No claim is made that network cvars control actual OF POV demo entity smoothing. A later interpolation improvement likely requires validating an OF engine presentation/interpolation hook, not writing protected cvars.

The OF POV deathcam suppression experiment was reported to work in-game, so no changes were made to its hook in this patch. All commits were made only to `experiment/open-fortress-legacy-2026-11`; other branches are untouched.

### Suggested targeted runtime checks

1. Load an OF demo and open `mirv_overlay 1`. Move the mouse over and outside UI panels, toggle with F8, and verify a stable visible pointer without a duplicate system cursor.
2. `mirv_input camera`; `mirv_input camera uiCursor 1`; open `demoui` and move the cursor. Inspect `mirv_input camera uiCursorStatus` (preferably after closing the console again); observe whether freecam movement pauses when the VGUI cursor is visible. Turn off with `mirv_input camera uiCursor 0`.
3. Set an existing recording directory in the Recording tab, stop any active recording, then click **Open recordings folder**. Verify Windows Explorer opens and the game remains stable; test a missing directory separately.
4. Run `mirv_fix playerInterpolation status`. A ratio-only OF build should explicitly say read-only/unavailable during demo playback; do not assume effective smoothing solely from cvar values.

Windows x86 compile and these in-game checks remain pending.

## Live recording-profile editor port (2026-11-20)

Based on PR #1 through `e5c756e71`, the existing shared Open Fortress Recording tab now supports the live HLAE sampler/FFmpeg profile editor, without merging or closing PR #1. Protected built-ins are read-only; duplicate to a uniquely named editable profile, adjust sampler output FPS, rectangle/trapezoid method, exposure, cross-frame strength, downstream output profile or FFmpeg/FFmpegEx options, then Apply or Revert. Switching, duplication, editing and deletion are blocked while recording; sampler dependency cycles are rejected. Capture FPS controls input; sampler Output FPS is independent (e.g. 480 to 60 FPS). Custom profile definitions remain session-only; recreate via startup commands in `autoexec.cfg`. Shared recording-settings and headless test-harness code is identical to PR #1; the UI insertion was adapted to preserve Open Fortress's own overlay fixes. **Source-integrated, not yet Windows x86 compiled or in-game validated.**

### Latest user runtime observations (2026-11-20)

- Open Fortress software cursor now stays visible without flicker, with some render-frame latency. Preserve the working ownership model for now.
- `mirv_input camera uiCursor 1` works with engine UI.
- **Open recordings folder** no longer crashes in the latest tested build.
- `mirv_fix playerInterpolation status` reports `cl_interp` absent, `cl_interp_ratio=1`, `cl_updaterate=60`, and a ratio-only 0.0166667 second estimate. OF blocks ratio cvar changes during playback. The estimate is **not** measured rendered delay; do not force protected cvars or claim a smoothing improvement.
- Open Fortress attached-rig follow jitter and full Scene Cache model/pose replay remain deferred. Do not change the shared smoothing algorithm or enable unverified model/engine hooks as part of this port.

### Suggested x86 validation for the new profile editor

Rebuild Open Fortress x86, open Recording, duplicate a built-in sampler to a custom name, select input 480 FPS and sampler output 60 FPS, adjust exposure/strength, Apply, record a short take and inspect output. Test Revert, FFmpeg options, invalid/cyclic downstream profiles, and blocking edits while recording. Confirm the stable cursor, `uiCursor` and folder behavior did not regress; keep the PR experimental until in-game validation.

## Scene Cache capture safety gate (2026-11-20)

The OF workspace intentionally hides Scene Cache recording/model replay, but the shared `mirv_cache record 1` console command previously could still arm the TF2-oriented `Mirv_Tf2_CollectSceneEntities` and `Mirv_Tf2_CaptureScenePose` route. That enumerator uses client-networkable and model/renderable virtual calls not yet verified against OF. Now the console explicitly rejects enabling OF Scene Cache capture; the completed-frame observer independently prevents OF capture even if an internal flag were set. OF also skips TF2-specific Scene Cache render-resource retirement. TF2/TF2C remain unchanged, and the existing non-capturing cached-view logic is preserved. This is source-inspected only, not x86-built. Do not interpret this as implemented OF Scene Cache; its ABI validation/port is still outstanding.

## Experimental OF rendered-motion probe (2026-11-20)

The user reports that the newest Open Fortress x86 build works as intended. This includes the live Recording profile editor and the Scene Cache capture safety gate; a complete regression pass and native interpolation improvement have not been established.

The next step is a **read-only, opt-in, Open Fortress-only player motion probe**. The existing `mirv_fix playerInterpolation` cvar control stays disabled in OF where `cl_interp` is absent and `cl_interp_ratio` is playback-locked. No cvar, engine memory, camera settings, player positions or bone data are modified. The OF VClient017 completed-frame hook samples the known `Mirv_Tf2_GetSceneEntity` render-origin interface for a selected index once per rendered frame, comparing adjacent samples with the demo tick and client render time. It records same-tick continuous root updates versus repeats, tick-boundary jumps, gaps and discontinuities without per-frame console spam. Frame-start hooks and TF2/TF2C behavior are unchanged.

**Offline demo test** after rebuilding the x86 hook:

```text
mirv_listplayers
mirv_fix playerInterpolation probe <movingPlayerIndex>
mirv_streams record start
// play several seconds with an actual moving player at a high capture FPS
mirv_streams record end
mirv_fix playerInterpolation probe status
mirv_fix playerInterpolation probe off
```

When testing with the overlay recording controls, use the same capture FPS and profile as a normal take; the probe does not itself initiate recording. For an initial baseline, a 240 FPS capture of a 60-tick/s demo gives approximately four rendered frames per tick when the engine can supply them. Ideally gather results from steady movement and compare with normal 60 FPS playback; don't compare across seek/fast-forward. `same-tick advancing pairs` with zero movement suggests repeated *root render origins*, while nonzero movement proves sub-tick root updates are present. It does **not** prove all bones, animation layers, particle effects or POV motion are interpolated. This hook samples after `FRAME_RENDER_END` and is not an exact GPU-present timestamp.

**Why not patch interpolation directly yet?** Source SDK 2013's `C_BaseEntity::GetInterpolationAmount` has special local-demo, predictable-entity and animation branches; the actual Open Fortress binary must be matched before a detour to avoid corrupting its ABI. User reports that the OF developers removed `cl_interp` deliberately and require ratio-based interpolation; their Discord explanation is useful context, but not proof of the exact binary control path. Do not bypass protections in live multiplayer. The eventual override should be reversible, default-off, OF-only and offline-demo-only, with a comparison against this baseline before any port to other branches.

**Status:** source patch committed; awaiting Windows x86 build and runtime probe output. No new branch created.

## 480 FPS clean target results and extended probe (2026-11-20)

Same Open Fortress build, now recording **player entity 5**, another player who killed the POV recorder (the recorder subsequently respawned away from the target). Player 5 remained accessible to the probe; no source failure:

```text
successful samples 10281; unavailable 0; duplicate callbacks 0; discontinuities 46
advancing same-tick pairs 6260; moved 6247; static 13; unchanged-time pairs 3009; max step 13.0944 units
tick-boundary moved 964; static 1; max step 11.2881 units
```

**Interpretation:** 6247/6260 = **99.79%** of *advancing same-tick comparisons* moved. The position is demonstrably changing across sub-tick output frames, so a blanket engine interpolation override is not yet justified. However, **3009 unchanged-client-time comparisons** (about 29.3% of all consecutive samples) were previously counted without checking whether the render origin still moved. The existing **46 discontinuities** count tick skips, tick rewinds and/or time rewinds; it does *not* count unusually large changes in position. Player 5 remained resolvable when the POV recorder died, but observer changes may influence animation culling and presentation, so repeat on a stable interval if needed. A 13.0944-unit same-tick step still deserves timing context; it is not proof of an engine jitter bug.

**New source-only diagnostic extension:** the same `mirv_fix playerInterpolation probe` command now also breaks frozen-time comparisons into **moved/static**; counts whether Source's engine `interpolation_amount` fraction changed; records minimum/maximum advancing same-tick client-time deltas plus tick/time/frame-gap context of the largest same-tick root step; and splits discontinuities into backward ticks, skipped ticks (max gap), and backward client time (non-mutually-exclusive). Zero new engine hooks, cvar writes or overrides. Same command syntax; the status output adds three lines.

**Next offline validation:** Windows x86 rebuild, repeat a short uninterrupted 480 FPS movement segment probing player 5, and provide the full extended status output. Compare with an uninterrupted steady POV segment if useful. Only after isolating any actual frame stalls, time/phase inconsistencies or bone animation defects should we attempt a reversible demo-only OF engine interpolation detour. Existing PR #1 stays untouched.

## Long 480 FPS player 5 test — clock rewinds (2026-11-20)

The user repeated the OF player 5 probe for a longer 480 FPS recording: 17,012 successful samples; zero missing entities or duplicate callbacks; 11,218/11,275 (99.49%) **advancing same-tick** pairs moved, with 57 static. The other 3,917 comparisons had unchanged HLAE client time: **all 3,917 also had unchanged root origin and engine interpolation fraction**. Timing steps for advancing same-tick pairs were 0.00207519531 to 0.00718688965 seconds, compared with the nominal 0.00208333 seconds at 480 FPS. The probe saw 58 **backward HLAE-time** events with zero backward/skipped demo ticks. The last event was at tick 1917 with delta time -0.0121917725 seconds. Largest same-tick root step was 13.8176 units and largest tick-boundary step 15.783 units. A valid render-origin interface does not prove entity 5 was visible; entity culling remains unverified.

These results establish intermittent static/repeated *player-root* samples and non-monotonic sampled HLAE time, not the precise source of visual jitter. There is no evidence yet that changing `cl_interp_ratio` will address these symptoms; keep existing OF interp cvar read-only.

**Next read-only diagnostic patch:** log the last 16 rewind events in a bounded ring (not each frame to console), capturing demo tick, engine framecount, HLAE time delta, raw `gpGlobals->curtime` delta, HLAE paused-offset delta, root displacement and interpolation fraction changes. The existing probe status adds raw engine rewind/offset change counts, player-root moved/static at rewind and frozen-run counts and longest streak. `mirv_fix playerInterpolation probe events` dumps the recent events. This isolates an engine clock rewind versus an HLAE offset change and whether a root snap coincides. Frame tracing is gated to explicit Open Fortress demo mode; no new branches, cvar writes, model hooks or demo manipulation.

**x86 test:** rebuild, arm `mirv_fix playerInterpolation probe 5`, play 5–10 seconds or longer of continuous movement at host_framerate 480, run `mirv_fix playerInterpolation probe status` and `mirv_fix playerInterpolation probe events`, then `probe off`. Read-only x86 source change awaits build and runtime verification; don't assume it fixes visual smoothness.

## First opt-in native OF demo smoothing experiment — pre-demo ratio (2026-11-20)

The most recent 480 FPS OF probe on player 5 returned **20,456** readable samples. Of 15,948 advancing-time same-tick comparisons, **12,988 moved** and 2,960 were static. There were also 1,934 unchanged-time/static-root pairs, and **two frozen runs**, the longest spanning 1,459 pairs (~3.04 seconds at an actually sustained 480 FPS). Such a long stationary run may be death, idling, culling or gameplay and is *not* proof that the global engine clock stopped. Maximum same-tick root step was 16.625 Source units and maximum tick-boundary step 1,534.18 (compatible with teleport/respawn but unconfirmed).

The **93 raw engine time rewinds** had no backward or skipped demo ticks, no HLAE paused offset changes, and zero same-tick rewinds. Event samples show transitions like demo tick 2666→2667 with `gpGlobals->curtime` step −0.0129089355 seconds and engine interpolation fraction 0.924696→0.0635848. **This is compatible with the normal cyclic tick/interpolation phase, not evidence of a broken game clock.** The event ring is a diagnostic, not something to fix by forcing monotonic `curtime`.

The user requests a real change rather than additional probing. The first practical OF-only opt-in experiment therefore targets the native interpolation setting, **but sets it only while at the disconnected game menu**: the game's `cl_interp_ratio` is intentionally locked during playback. The new console command:
```text
mirv_fix playerInterpolation ofRatio 2
mirv_fix playerInterpolation ofRatio status
# start the demo, perform a normal high-FPS capture and visually compare
# stop the demo; HLAE attempts to restore the prior ratio after disconnect
mirv_fix playerInterpolation ofRatio status
mirv_fix playerInterpolation ofRatio default
```


- Supports integer ratios **1 through 8**, rejects writes while playing, loading, connected or in-game. No TF2, TF2C, other HL2MP, player or multiplayer change; OF explicit opt-in only.
- Saves the prior ratio on first successful write; readbacks must match requested value, otherwise reject and attempt rollback. Subsequent ratio edits preserve the original when still owned by HLAE.
- Marks the ratio as used upon observing offline demo playback; after demo end and disconnect, attempts to restore only if the value remains HLAE-owned. Does not force protected writes during playback or after user/game changed the ratio.
- \`status\` reports actual cvar, updaterate, nominal delay, ownership and readiness. Example at 60 updates/s: ratio 1 ≈ 16.7 ms; ratio 2 ≈ 33.3 ms; ratio 3 ≈ 50.0 ms. These are **nominal estimates**, not proven changes to effective render interpolation.
- **Important:** the engine may reset/clamp a ratio at demo launch; the readback during playback will show if the requested value persisted. Even persistence does not prove better captured motion. This is an experimental native setting override, **not** a binary-level interpolation hook or guaranteed smoothing fix. It cannot reconstruct missing demo snapshots or fix unrelated bone/POV animation.
- Run A/B comparisons of the **same uninterrupted moving scene at 480 FPS**, default ratio 1 versus explicitly prepared ratio 2. Focus on actual output footage, not more probe logs.

Code source-only; Windows x86 compilation and in-game validation are not yet performed. No new branches or modifications to PR #1. If OF does not accept the pre-demo ratio or the footage does not improve, do not blindly patch `client.dll`; first verify the exact OF x86 interpolation function/ABI in the supplied binary before any true engine detour.


## OF interpolation ratio experiment outcome — rejected as a smoothing improvement (2026-11-20)

The user rebuilt and tested the experimental native pre-demo `cl_interp_ratio` setter (`85d954f`, polished in `90c32f5`). **The ratio was accepted but yielded little or no visible improvement**, and manually setting the cvar already provides the same ability. Therefore **this is not the desired HLAE interpolation fix** and the experimental `ofRatio` command has been removed from the active source in the next commit, restoring the last known working read-only probe implementation from `2b3a8bc`. This does not undo any of the user's preceding test results. The PR description retains a record of the experiment, but no longer advertises it as an active feature.

**Next implementation must change the effective compiled interpolation path, not simply automate allowed cvars.** Valve's Source SDK 2013 `C_BaseEntity::GetInterpolationAmount` treats predictable entities, client-created objects and locally recorded demos differently and can return a fixed tick interval irrespective of `cl_interp_ratio`. It also updates interpolated-variable watcher intervals via `Interp_UpdateInterpolationAmounts` / `CheckCLInterpChanged`; changing a cvar may not be sufficient to affect latched history. Source: https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/c_baseentity.cpp ; contemporaneous jitter-fix background https://forums.sourceruns.org/t/permanent-interp-fix/1948 .

**Strict hook safety requirements:** Determine the correct compiled OF x86 client entity interpolation function/ABI for the *user's installed build*; do not assume Source SDK method offsets or compile-date signatures. Prefer a narrowly scoped opt-in change that works only in explicit Open Fortress mode while playing an offline demo, enables original-call fallback, restores on unload, and leaves multiplayer and other game branches untouched. Then perform matched 480-FPS video A/B (both root position and bones) before porting. Avoid further non-actionable clock/probe experiments unless a specific hook needs verification.

**Binary access limitation:** The project previously included OF's 32-bit `client.dll`, but the presently available project-file record does not provide authorized raw-byte materialization. A separate `/client.dll` is x64 and **not** the OF binary. Without the exact x86 client module or reliable code disassembly, it is unsafe to commit a guessed detour/vtable index. The user may need to attach the exact OF 32-bit client module again to enable binary-level engineering.


## Verified OF x86 compiled interpolation bypass experiment (2026-11-20)

The user has rejected the prior `cl_interp_ratio` automation, correctly noting it does not bypass OF's demo playback lock and gave little/no visual benefit. The user re-uploaded **the actual OF client.dll (PE32 i386)**; its file SHA-256 is `8ba1210f5d721084fdc0c225cee3c40e1812dba996de69d1e5d93028bf869ea1`, PE timestamp `0x66b4fe85` (2024-08-08 17:21:09 UTC), image size `0x00A27000`. It is **not** the previously documented 2021-06-24 release.

Offline disassembly and direct CALL cross-reference inspection establish:
- RVA `0x1091A0` = **GetClientInterpAmount()**-like client interpolation routine, returning native float via x87. It is called at RVA `0x0BFF79` inside the per-entity calculation and at `0x0BD7A7` in the engine's interpolation-value change handling.
- RVA `0x0BFF10` = **C_BaseEntity::GetInterpolationAmount(int flags)**-like method: its assembly matches Source SDK 2013's predictable/client-created short-circuit and differing networked/local demo interpolation branches. Direct callers at `0x0BC63F`, `0x0BD882`, `0x0C1BD7`, `0x0C1C5D`.
- Its callers set the actual per-variable interpolation interval through virtual interpolator methods, instead of exposing a ConVar entry. Source SDK comparison: https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/c_baseentity.cpp
- Strict runtime gating checks PE machine, timestamp, image size and fixed, relocation-independent code signatures including two x86 CALL-site references; incompatible OF builds fail closed **before any patch**. The precise file SHA is informative, not enforced at runtime.

**New opt-in source patch:** hook both compiled routines with Microsoft Detours (default off). The client interpolation detour returns a requested 0.016–0.25-second time **only if greater than the native value**, preserving longer native history; the second detour optionally extends entity-local/predicted short intervals in `all` mode. This changes return values at the compiled interpolation decision point without editing `cl_interp_ratio` or other cvars. The client interpolation hook should also drive the normal interpolation-history update comparison when an override is switched on/off. Verify through footage and the method-call status counters; it is an experiment, not yet proven smoothness.

```text
# Load offline OF demo first. Same build/date only.
mirv_fix playerInterpolation ofEngine 0.10
mirv_fix playerInterpolation ofEngine status
# capture an identical moving-player segment at host_framerate 480
mirv_fix playerInterpolation ofEngine default

# If normal networked mode doesn't improve things, replay the same segment:
mirv_fix playerInterpolation ofEngine 0.10 all
mirv_fix playerInterpolation ofEngine status
mirv_fix playerInterpolation ofEngine default
```


**Safety:** only explicit OF legacy mode (not TF2/TF2C or ordinary HL2MP), only active offline demo, refuse loading/skipping, and auto-disable bypass when playback ends; `default` returns native results immediately. Installed trampolines remain dormant for the session, but HLAE passes through the original routines outside demos. No new branch. **Not Windows x86 compiled or in-game tested**; binary function semantics were reverse-checked from supplied DLL, but actual image-hooking stability and history recomputation must be validated on Windows. If call counts stay zero, if disabling fails to restore original movement, or if artifacts appear, do not promote this as a successful fix.


## OF native interpolation override in the persistent Demo fixes overlay (2026-11-20)

**User-verified runtime:** `mirv_fix playerInterpolation ofEngine 0.10 all` successfully hooked the actual OF 2024 x86 client even during demo playback: 13,469 client interpolation calls, 2,802 entity interpolation calls, 75 entity intervals extended. Compared with default and regular 0.10, completed recordings looked largely similar. Slow-motion live viewport playback visibly shifts entity positions backward on enable, akin to the TF2/TF2C cvar interpolation fix. Remaining preview jitter is tolerable because it doesn't appear in captures. The user considers the bypass a sufficient OF improvement and asks to return to porting other features; **no further interpolation probe or demo-rewriting is currently planned**.

**New overlay integration:** the OF Demo fixes panel now offers `Smooth player movement (OF engine override)`, an editable seconds value (0.016–0.25, default 0.10), and an `Extend all entity intervals (experimental)` checkbox mirroring `ofEngine <seconds> [all]`, plus its own status button. The separate TF2/TF2C cvar-based `Smooth player movement` UI remains unchanged for those games. The OF UI uses the same `HLAEOverlayPaths` ImGui INI persistence and `PreferencesFrame()` callback as existing fixes, retaining the user-enabled preference, delay and mode across sessions and automatically applying it during a compatible offline OF demo **even when the overlay window is closed**. It never installs the hook at the main menu or on a multiplayer server.

INI keys: `FixOfEngineEnabled`, `FixOfEngineAll`, `FixOfEngineSeconds`. Enabling/disabling/editing from the overlay immediately attempts to update the running demo; editing when no compatible demo exists saves the preference without patching. Successful `ofEngine <seconds> [all]` console commands also save the same settings, and explicit console `ofEngine default` disables the saved preference to prevent the next periodic preference reapply. Auto-disable on demo exit remains a runtime-only safety step: the **saved preference is retained** for the next offline demo. Failed manual commands do not overwrite a stored preference.

**Status:** Implemented on the existing `experiment/open-fortress-legacy-2026-11` branch only; PR #1 and TF2/TF2C untouched. Source-side checked; the overlay addition has **not yet been Windows x86 built or in-game validated**. Minimal validation: enable 0.10/all in OF overlay, check engine status, quit/restart HLAE, start another offline demo and check the persisted UI/auto-enabled status; explicitly run `ofEngine default` to ensure the checkbox stays off. Do not port further until validated.

## Initial OF Scene Cache capture / preview port (2026-11-20)

The first OF stage enables the existing shared `SceneCache` history for **camera snapshots and player bounds**, reusing the read-only `Mirv_Tf2_GetSceneEntity` render origin/angles/bounds and `Mirv_Tf2_GetPlayerInfo` stable demo-local user IDs already tested in OF. It records one observed frame per demo tick using existing range/memory/append logic and supports timeline preview, camera frame sampling, bounds and movement trails through the common DX9 line renderer. The existing studio submission `SceneCanDrawModels()` remains OF-disabled; a separate `SceneCanDrawLines()` gate enables safe cached bounds in OF. This implementation does not invoke `SetupBones`, studio headers, TF2 class categorization or queued model rendering for OF.

The OF Scene Cache panel is enabled with explicit *partial port* messaging. It has **Capture OF player bounds** (off by default; camera-only when off), a player filter, capture controls and bounds/trail preview. Unsupported model/entity-category controls are hidden. `mirv_cache record 1|0`, `mirv_cache preview 1|0` and `mirv_cache status` are available in OF. The TF2/TF2C studio-capture branch is otherwise unchanged.

**Offline x86 verification after rebuilding:** In the Scene Cache overlay enable Capture OF player bounds and Draw cached bounds, record a short demo interval, stop, enable preview, and scrub. Verify nonzero frame count and OF bounds-only diagnostics in `mirv_cache status`; optionally test trails and camera snapshots. Studio meshes/bones and non-player entities are still unported. This code is source-inspected but not yet x86-built/runtime-tested; do not claim full Scene Cache parity or enable the unverified model renderer. No new branch and PR #1 untouched.

## Open Fortress Scene Cache bulk extended port (2026-11-20)

Following successful OF camera/player-bounds testing, a single bulk experiment integrates extended **OF-specific** entity categories, optional full studio-bone pose capture, cached animated skeleton visualization, and opt-in studio-model replay. All new engine-facing features are session-only and default off; camera/player-bounds-only remains the working fallback. No new branch; PR #1 untouched.

**Scope:** player, projectiles, weapons, cosmetics, ragdolls, gibs, buildings and dynamic props/objectives, classified from OF client classes/model names rather than TF2 receive-table assumptions. Captured entities use stable demo-local user IDs for players, serial-bearing entity handles for others, and validated origin/angles/bounds. Unsupported or unseen objects are skipped; category heuristics may need refinement with real OF examples. Optional pose captures call the existing SceneCache pose adapter with a bounded 128 attempts per recorded tick. This is not a change to the core SceneCache sampler or TF2/TF2C.

**OF compatibility gate:** require explicit OF legacy mode, known PE32 client timestamp 0x66b4fe85 and image size 0x00a27000 (same user-supplied binary already used to test native interpolation), plus VModelInfoClient006. Actual model rendering is separately gated on exported VStudioRender025, MDLCache004 and availability of the material-system render queue. Those interface version strings were observed in the uploaded OF 2024 client binary, but their complete vtable compatibility is not proven until live rendering. Studio operations may still crash on unexpected ABI differences. The safe fallback does not call them.

**New controls:** Scene Cache -> Capture has `OF extended entity categories (experimental)` and `Capture OF studio poses / animated bones (experimental)`; all standard TF2 category checkboxes become available only in advanced OF mode. Scene Cache -> Preview has `OF cached animated skeleton lines` and `Draw OF cached studio models (experimental)`, separate from the existing renderer option. The skeleton is reconstructed from immutable cached bones and sent to the existing 3D line renderer; it does not invoke studio model rendering. Model replay uses the existing Source IStudioRender path only after active user opt-in. Live sessions do not auto-enable experimental engine operations after restart.

**Console:** `mirv_cache ofExtended 1`, `ofPoses 1`, `ofSkeleton 1`, `ofReplay 1` (and zero to disable), plus existing `mirv_cache status` to display compatibility flags. Enable all at once if desired, but first make a short disposable offline demo capture; use `mirv_cache ofReplay 0` to disable studio drawing if artifacts appear.

**Pending:** Windows x86 compile and runtime validation. This is a wide first attempt, not a guarantee of parity. Actual copied model poses, categories and studio submissions must be tested. Do not claim an end-to-end model port works until playback is observed.

## OF extended Scene Cache compatibility rejection fix (2026-11-20)

The user reports that `mirv_cache ofExtended 1`, `ofPoses 1`, `ofReplay 1`, etc. reject with the blanket error `OF advanced Scene Cache unavailable: requires the verified 2024 x86 client/interfaces.` A specific defect was identified in the new gate: it compared the **function entry prologues** of RVA `0xBFF10` and `0x1091A0`, which the successfully tested `ofEngine` Microsoft Detours hook overwrites for the lifetime of the process. With the saved `ofEngine` preference, the extended cache can incorrectly reject **the correct OF client** whenever the hook is installed, even with `ofEngine default`.

**Fix:** Validate the two previously verified **non-entry x86 CALL instruction patterns** at RVA `0x0BFF79` and `0x0BD7A7`, which are not rewritten when the entity/interpolation targets are detoured; retain the strict 2024-08-08 PE32 timestamp, machine, and image-size checks. Separate three compatibility levels: extended **entity bounds** requires only explicit OF mode + binary identity + working VCLIENTTOOLS001; **studio poses** additionally require VModelInfoClient006; **studio replay** additionally requires VStudioRender025 and MDLCache004. The queue capability remains checked at actual draw submission, not at the time of enabling a UI toggle, since queue readiness depends on render timing. This allows entity-history capture even when model-info services are unavailable.

`mirv_cache status` now prints each exact blocking reason or `ready`; rejected console and overlay controls show the specific reason. The pure cached skeleton visibility toggle no longer requires the live game ABI just to enable it. The optional studio capture path guards pose calls if the studio prerequisites disappear. The OF class/model name reader now reads bounded chunks with a one-byte fallback instead of requiring a full 95/259-byte allocation to be readable beyond a short NUL-terminated string.

**Status:** source-side reviewed but not Windows x86 compiled/tested. After rebuilding, the user can run `mirv_cache status` once and enable all commands if compatible. If `VModelInfoClient006` or another interface is genuinely absent, the console will name it without masking a supported entity-bounds fallback. Do not claim runtime success until tested. No changes to PR #1, no new branch.


## OF cached studio replay crash isolation and batch diagnostics (2026-11-20)

**Runtime report:** `mirv_cache ofReplay 1` previously crashed when reentering cached preview, including with `mirv_cache record 0`. Two matching Sentry dumps and PDB mapped the access violation to `AfxHookSource::CameraOverlay::SceneCacheDrawModels()` (old line 638), while inspecting a cached entity's `ModelPose`. This identifies the faulting location, not necessarily the original memory corruption.

**First isolation patch:** `fe927ece` preflights readable entity, pose and model metadata; copies candidates before studio submission; and limits OF to one cached studio draw per callback. The user confirmed that cached preview no longer crashes and the first model submission repeatedly returns `submitted=1`, with the ordinary wireframe fallback when `VMaterialSystem082` is unavailable. This confirms the one-model diagnostic path can submit successfully; it does **not** validate all models or full replay.

**Log correction and controlled batch trial:** The original `Tier0_Msg` implementation did not interpret `%zu`, displaying literal `zu` and shifting subsequent printf arguments (including pointer and queue output). Diagnostics now use `%u` with explicit unsigned casts; identity uses a decimal string. OF replay can optionally submit more than one preflighted model, but the **default remains one**. The session-only `mirv_cache ofReplayBatch <1..8>` command and the Scene Cache preview slider use the same atomic setting. Each candidate's entity and owner are copied before the first studio submission. `mirv_cache status` prints the selected replay batch.

**Next offline smoke test:** Rebuild Windows x86 Release, start with `mirv_cache ofReplayBatch 1`, confirm stable preview, then try `mirv_cache ofReplayBatch 2` using the same short cached interval; use larger values only after the previous value proves stable. The console prints `[OF replay] submit N/M ...` before each submission and `[OF replay] returned N/M ...` afterward. If batch two crashes, the last completed stage and a **matching new PDB/minidump** can determine whether a particular model or repeated studio API use is implicated. Restore `mirv_cache ofReplayBatch 1` or disable `ofReplay` to return to the tested path. This is an opt-in diagnostic, not a finished OF studio renderer. PR #1 and TF2/TF2C rendering are unchanged.

## OF cached studio replay: visible-model ceiling diagnostic (2026-11-20)

**New runtime result:** Increasing `mirv_cache ofReplayBatch` past two no longer crashes the game, but only two wireframe models are visible simultaneously. The pasted log repeatedly reports `submitted=1`; it also appears to show an outstanding render-work counter climbing from 294 to 462, with historical peak 512 (the maximum outstanding scene submissions). Importantly, that same log contains impossible batch counters, such as `submit 10/8` and `returned 9/1353371222`, so those counters cannot be fully trusted until the message formatting is corrected.

**Targeted inspection:** `SceneCacheDrawModels()` selects the *first* N eligible studio candidates on every draw. Larger batches might therefore submit models that overlap, are not visible, or are lost in the renderer; the log did not identify their model names or origins. `Mirv_Tf2_DrawScenePose()` reports successful submission, not guaranteed visible pixels. Queued callbacks only release pinned MDL/material resources after render-completion and engine-thread retirement, so consistently increasing outstanding counts could signal a callback/retirement problem, but require trustworthy counters to confirm.

**Follow-up code:** The OF-only, session-local `mirv_cache ofReplayStart <0..127>` setting selects which eligible candidate window to submit, while preserving the `ofReplayBatch <1..8>` default of one and existing preflight/candidate-copy safeguards. The Scene Cache preview overlay exposes the same start setting. The console now prints complete preformatted strings (one `%s` argument to Tier0), including the total eligible count, candidate window, selected entity/index/category/model name/origin, and outstanding/peak counts before and after submission. No modification to TF2/TF2C model rendering or the shared SceneCache container.

**Next runtime smoke test:** Rebuild Windows x86 Release. Keep a short saved cache with known distinct captured models. Try `mirv_cache ofReplayBatch 8`, `mirv_cache ofReplayStart 0`, preview; compare against `mirv_cache ofReplayStart 8`, then optionally 16 if the eligibility count supports it. Observe whether the visible pair changes, the number of eligible models, and whether queued count continues increasing across draws without draining. `mirv_cache ofReplayStart 0; mirv_cache ofReplayBatch 1` restores the previously successful diagnostic mode. This is not evidence that all OF studio replay works or that queued submissions have finished rendering.


## OF replay candidate-window experiment rolled back (2026-11-20)

**Confirmed regression:** The candidate-window/logging patch from commits `0000ec5b` and `ed2fde09` caused Open Fortress to crash again immediately after entering cached preview. Sentry reports on 2026-11-20 at 20:52 UTC and 20:54 UTC consistently fault at `AfxHookSource.dll + 0xF0CB3` with exception `0xC0000005` (invalid read). The user-supplied matching PDB has age 154, and maps the fault to `AfxHookSource::CameraOverlay::SceneCacheDrawModels()` at `CameraOverlay.cpp:777`, in the expression composing a per-candidate diagnostic log. The last completed console entry was `returned 1/2 submitted=1`, before any successful log of the next candidate. This does not prove the renderer itself faulted, nor whether the earlier submission damaged memory or the logging dereference exposed a preexisting issue.

**Safe rollback:** Commit `1b76763` restores `AfxHookSource/CameraOverlay.cpp` byte-for-byte to commit `0d63b71`, the revision the user last reported to enter cached preview without crashing. This retains the existing OF frame preflight and `mirv_cache ofReplayBatch <1..8>` setting (default 1), but **removes `mirv_cache ofReplayStart`, the candidate-start overlay slider, and the added model-name/position diagnostic string concatenation**. The earlier section describing the start-selector documents a failed trial that is no longer available. All other cache enhancements and TF2/TF2C model paths remain unchanged.

**Remaining open issues:** Only two OF studio wireframes were visibly rendered in the user's successful batch test, despite higher batch values. The outstanding queue reported substantial growth in the earlier, partially malformed diagnostics. Both observations remain unexplained; do not expand replay again without first verifying queued callback retirement and distinguishing completed studio draws from successful submission function returns. This rollback is code-reviewed but must be rebuilt and tested in Win32 Release. If the last known good build is still available, it can be used instead of the later broken build.


## OF studio replay: offline retirement audit and status counters (2026-11-21)

**What is now regression-tested offline:** `tests/cinematic/test_scene_render_queue.py` extracts the actual `SceneModelResources`, `Mirv_Tf2_SceneRenderFrame`, and `MaterialSystem_ExecuteOnRenderThread` / wrapper implementations and compiles them against fake Source cache/material interfaces. Tests cover queued callback execution, discarded callbacks whose destructors signal completion, concurrent render-thread completion versus engine-thread retirement, 512 outstanding submissions with no callback or destruction, empty-queue synchronous execution, submission-helper failure cleanup, and 3-slot/4-slot functor refcount paths. The local C++ fixture passed normal and ASan/UBSan runs. A dedicated GitHub Actions workflow attempts the same tests on relevant PR/branch changes; confirm an actual workflow run before claiming remote CI success.

**Finding:** In the tested lifetime algorithm, model submission increments `g_SceneOutstanding`; only execution or destruction of the queued `SceneModelResources` functor publishes a completion for engine-thread retirement. If the queue neither executes callbacks nor releases the functors, outstanding work reaches the fixed limit of 512, even without a crash. This reproduces the observed counter pattern as a *possible* consequence; it does not establish the behavior of Open Fortress's real call queue or explain why at most two wireframes were visible.

**Minimal runtime telemetry:** `mirv_cache status` now includes an OF-only `OF replay lifecycle (session totals)` line with `submitted`, `callbacks invoked`, `completion signaled`, `resources retired`, `outstanding`, and `peak`. The counts are atomic and do not inspect cached entities. `submitted - completion signaled` estimates work whose callbacks have neither executed nor been destroyed. `completion signaled - resources retired` estimates work awaiting engine-thread retirement. `callbacks invoked` distinguishes executed callbacks from callbacks abandoned and completed only via destructor. Snapshots can differ transiently during concurrent updates, and `submitted` does not establish visible models. Session totals are not reset when switching preview on and off.

**Next one-shot offline-demo check (after Windows x86 build):** Use the known-good baseline configuration, `mirv_cache ofReplayBatch 1`, with a short recorded cache; run `mirv_cache status` before and after preview. Capture those two status lines and the count of visible wireframes. Avoid increasing batch size while testing lifetime. If `submitted` increases but `completion signaled` does not, audit OF queue invocation/discard ownership; if signals increase but `resources retired` does not, audit engine-thread retirement scheduling; if all counters advance with backlog near zero, pursue render-state / model visibility separately. This diagnostic is not a model-rendering fix. Existing TF2/TF2C renderer operations and OF selection logic remain unchanged.


## OF studio replay visibility and bodygroups: safer aggregate diagnostics (2026-11-21)

**Additional user runtime evidence:** Even with `ofReplayBatch` above 2, at most two studio wireframe models are visible. Mercenary studio wireframes sometimes show only the head and sometimes the full body including head. Studio wireframes remain uncolored across the builds tested. The in-preview queue lifetime status recorded submitted=94868, callbacks invoked=94866, completion signaled=94866, retired=94864, outstanding=4. Those values reconcile: two were still awaiting callbacks and two awaited engine-thread retirement. The peak of 512 is historical; it is not evidence of a persistent leak at that sample.

**Code audit:** Category coloring currently uses `VMaterialSystem082` for private wireframe override materials; OF emits the explicit fallback status indicating this interface is unavailable, so studio fallback rendering uses the default uncolored `STUDIORENDER_DRAW_WIREFRAME` flag. Source's studio bodygroup integer chooses mesh variants, and pose capture stores `renderable->GetBody()` and sets `DrawModelInfo.Body` from the pose, while `DrawModelInfo.Skin` is left at its zero default. The user's head-only/full-body observations suggest checking bodygroup selection against the actual OF model; neither a corrupted body index nor an ABI mismatch is proven. Source's wireframe flag does not guarantee a distinct visible model for every successful submission.

**Low-risk discriminating telemetry:** `CameraOverlay.cpp` now keeps aggregate atomic, session-total OF replay counts in `mirv_cache status`: frame callbacks that reached the submission loop (`calls`), studio submission attempts (`attempted`), calls returning success (`accepted`), failed calls (`rejected`), last eligible candidate count and configured batch. No new model dereferences, message formatting, selection controls or engine render operations were added. This tells us whether batch >2 is *actually submitting* more models or stopping early; the separate lifecycle telemetry continues to count callbacks and retirement. Before/after status snapshots with batch 2 or 8 are sufficient; `accepted` still does not prove visible pixels. Keep the successful baseline as a fallback and use a disposable offline demo. This change is source-reviewed; Windows x86 compilation and in-game testing remain pending.


## OF replay confirmation: batch works, next issues are appearance (2026-11-21)

**Observed runtime confirmation:** User reports that more than two studio wireframe models are visibly drawn with `mirv_cache ofReplayBatch 5`, correcting the previous observation of an apparent two-model ceiling. `mirv_cache status` in cached preview showed 5,076 replay draw callbacks; 25,380 attempted, 25,380 accepted, zero rejected; 70 eligible candidates; 25,380 model submissions, 25,375 callbacks invoked, 25,375 completion signals, 25,370 retired; 10 pending and peak only 15. Every measured callback submitted exactly five models; the remaining queue work is small and internally consistent. Do not continue treating a permanent two-model limit or a callback-resource leak as demonstrated.

**Remaining quality issues:** The user sometimes sees a Mercenary as only a head and sometimes as a full-body wireframe; bodygroup/skin/model-specific submesh interpretation has not been verified against `models/player/mercenary.mdl`. Capture stores `renderable->GetBody()` in the pose, and replay restores `SceneStudioInfo.Body`, but `Skin` defaults to zero. Do not change bodygroups blindly; inspect model bodypart definitions and the captured body index first. Uncolored cached studio wireframes are explained by the current material code requesting `VMaterialSystem082`, while the already inspected OF legacy client lists `VMaterialSystem080`. The latter is a different interface version: validate vtable layout, material creation/ownership, and safe engine allocator usage before attempting a version-specific adapter. The current default uncolored studio wireframe and colored line/box fallback remain safe.

**Next development approach:** No new multi-candidate traversal or verbose per-draw dereferences. Prototype separate source-level tests for OF 080 material ABI and real Mercenary bodygroup decoding before adjusting runtime rendering. Keep `ofReplayBatch 1` as the conservative default and do not touch PR #1.
