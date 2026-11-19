# Open Fortress legacy compatibility — experimental

This is an **opt-in compatibility probe**, not a claim of working Open Fortress support.

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
- No Windows build or in-game execution was performed here. A `VClient017` string in a binary does not prove all virtual function indices/layouts match.
- If the minimal path fails, diagnose individual engine/client/D3D9 hooks instead of routing Open Fortress through TF2's x64-specific implementation.
