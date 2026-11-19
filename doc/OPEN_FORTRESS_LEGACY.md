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
