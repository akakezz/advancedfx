# Experimental Open Fortress category-colored studio wireframes

**This is NOT the confirmed working PR #2 build.** It is a separate opt-in research
branch based on the runtime-confirmed all-model-replay commit
`f773d8d20b026e5c7657c091c33212aff60a47af`.

## Evidence and remaining risk

- The 2024 Open Fortress legacy client examined earlier advertised
  `VMaterialSystem080`. The known-good studio replay uses
  `VStudioRender025` and Source's cyan `STUDIORENDER_DRAW_WIREFRAME`
  fallback. Calling `VStudioRender025::SetColorModulation` did not
  change that cyan color in the user's live test, so that attempt remains
  removed.
- Valve's current Source SDK 2013 `imaterialsystem.h` explicitly names
  `VMaterialSystem082` and `VMaterialSystem080` as distinct interfaces.
  The 2015 Source SDK header adapted in
  `sigsegv-mvm/src/sdk2013/imaterialsystem_V081.h` documents two
  vtable methods inserted at offsets 0x104 and 0x108 when the old
  `080` ABI was changed. In that header `CreateMaterial` is at
  slot 72 with those two additions; subtracting them points to **slot 70**
  for the older `080` ABI. This supports, but does NOT prove, the actual
  running Open Fortress `MaterialSystem.dll` layout.
- `main.cpp` has a **default allocator-resolution fallback** that attempts
  to populate `SOURCESDK::L4D2::g_pMemAlloc` from the loaded `tier0.dll`
  for modes besides CSGO, SWARM and Black Mesa. It is incorrect to claim
  that Open Fortress can never acquire the allocator. Runtime presence
  still needs checking. The TF2 KeyValues adapter assumes the Source SDK
  2013 KeyValues field layout, which has not been independently verified
  in this user's exact OF material-system build.

**Unverified runtime details:** 32-bit DLL ABI, `CreateMaterial` ownership,
old `IMaterial` method indices, `$color2` shader parameter behavior, and
the exact `MaterialSystem.dll` and `tier0.dll` binaries actually loaded.
A pointer's executable memory protection is not a guarantee that it has
the correct function signature. Source-extracted fixture tests are NOT a
substitute for validating the game's DLL.

## Changes isolated on this branch

- Introduce `mirv_cache ofColor080 0|1` and an equivalent Scene Cache
  checkbox, **OFF by default, not persisted**.
- When enabled *only in Open Fortress*, query `VMaterialSystem080` and
  validate that its prospective `CreateMaterial` entry is executable
  memory within `materialsystem.dll` before calling it.
- Reuse the existing immutable, per-category `UnlitGeneric` material
  definitions with `$wireframe` and `$color2`, but keep separate
  `080` and `082` caches and cleanup.
- Attempt `080` material creation only once per category per lifetime.
  If unavailable/rejected, fall back to the existing cyan studio wireframe.
  TF2/TF2C still use `VMaterialSystem082` without being altered.
- Retain all-model replay, the resource-outstanding safety guard,
  aggregate diagnostics, and the successful pre-submission snapshots.
  No per-model post-draw pointer logging is reintroduced.

## Testing plan

The existing `tests/cinematic/test_scene_pose.py` now exercises actual
material-creation code against a fake `080` factory, including opt-in
default-off behavior, two category tints, override restoration, and
release of 080 material references. A Linux fixture cannot run Windows
`VirtualQuery` against the game's PE32 module, so ONLY that probe is
stubbed out in the test build. A branch-specific GitHub Actions workflow
has been added; **do not treat it as passed without completed job logs**.

**Do not enable the experimental switch in a game yet.** First obtain and
inspect the actual `MaterialSystem.dll` used by the user's Open Fortress
process (and preferably `tier0.dll`). An offline ABI probe should verify
slot 70, module identity, old KeyValues layout, and `IMaterial` methods
before the first real 080 `CreateMaterial` call.

If those checks pass, build Win32 Release from this *experimental*
branch, use a short offline demo with cached replay, and compare
`mirv_cache ofColor080 0` against `mirv_cache ofColor080 1`. Toggle
back to 0 immediately if rendering artifacts appear. Do not merge this
branch into PR #2 without successful visual and stability tests.
