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


## Exact PE32 binary verification (user-supplied 2026-11-21 files)

Analyzed the uploaded files directly, without executing their DLL entry points. **MaterialSystem(1).dll**: PE32/i386, PE timestamp `0x60d42f70` (2021-06-24 07:08:32 UTC), `SizeOfImage=0x157000`, SHA-256 `ecf494be08c8f380959ecbaa615a2a9aa3d90e3ad2321d0f917af4d6e1d0b95f`. **tier0.dll**: PE32/i386, timestamp `0x60d3e2a2` (2021-06-24 01:40:50 UTC), `SizeOfImage=0x50000`, SHA-256 `0728278672172b4e2cb0ef43941b10f902d48e6c03eb28a8c45a22700e463949`.

Resolved real MSVC RTTI `.?AVCMaterialSystem@@` through CompleteObjectLocator to **primary vtable RVA `0xb7b54`**. Entry `70` points to `CreateMaterial` candidate **RVA `0x45b00`**. Disassembly begins with `55 8B EC 56 57 FF 75 0C 8B F9 68` and ends in `C2 08 00` (`ret 8`), confirming the two-parameter thiscall ABI. Resolved RTTI `.?AVCMaterial@@` to **primary vtable RVA `0xb67f0`**: `IMaterial` slot 12 points to `0x3a2e0` (`lock xadd` increment of `this+0x1c`), slot 13 to `0x391e0` (`lock xadd` decrement), and slot 42 to `0x3aa40` (comparison with the global error-material pointer). These match the functions used by the scene cache.

Also verified that uploaded **tier0.dll exports `g_pMemAlloc`**; the exact MaterialSystem DLL imports that symbol from tier0. **Correction to initial interpretation:** `KeyValuesSystem` is imported by MaterialSystem from **vstdlib.dll**, NOT from tier0. The game's actual `vstdlib.dll` was not uploaded, so we have not directly validated its binary interface. Import resolution and the source-level SDK 2013 KeyValues field layout remain the basis of our proposed adapter.

**Stronger runtime fail-closed gate committed:** The opt-in OF/080 path now checks the loaded `materialsystem.dll` PE32 timestamp, machine type, `SizeOfImage`, primary vtable RVA, slot-70 function RVA, executable memory protection, and function prologue bytes. A different binary remains on the current cyan wireframe fallback; no unknown slot gets called accidentally. These checks materially reduce mistaken-vtable risk but do **not** prove shader tint results, KeyValues ownership, or a crash-free in-game material creation.

**Standalone local static verification already passed** on both exact uploaded DLLs: PE format, timestamps/image sizes, RTTI vtables, slot-70 prologue and `ret 8`, CMaterial refcount/error methods, tier0 export and MaterialSystem's vstdlib import. This does not imply the GitHub fixture workflow or a Windows build has passed. PR #2 (`experiment/open-fortress-legacy-2026-11`) remains unchanged and is the user's verified working fallback.
