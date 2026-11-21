# TF2 POV demo visibility continuity: goal and feasibility

Status: investigation and proposed implementation only, 2026-11-22.
No runtime changes or new console commands accompany this document.

## Overall goal

Make POV demos usable for cinematic cameras away from the recorder without map
sections, props and previously observed entities abruptly disappearing. Prioritize
visual continuity over reproducing the recorder's original visibility restrictions.
Keep a usable last-known entity appearance when movement updates stop; a frozen
subject is preferable to an unexplained disappearance for this optional workflow.
If movement updates exist and only rendering was suppressed, show that movement.

This is a rendering policy, not reconstruction of missing server simulation. It
must not require an STV recording, change camera attachment, alter demo playback,
or simulate unknown movement. Preserve normal solid-surface depth occlusion:
expanding visibility must not make players render through walls. Physical doors,
fences, windows and other real geometry should remain in the picture.

Desired coverage:

- Stop distance/screen-size fading of ordinary props, with detail props included
  where the renderer supports them.
- Render available map regions beyond the usual visibility selection, including
  investigating area-portal restrictions and their client-side covers.
- Keep available models visible when their disappearance is only client culling.
- Optionally retain the last valid appearance of entities that become dormant,
  including their weapons/cosmetics and attachment transforms.
- Restore ordinary rendering cleanly when disabled, on demo exit and on failure.
- Apply the same policy to normal viewing and recording passes, with consistent
  color, alpha/matte and depth outputs.

Explicit destruction/removal should take precedence over retention. Distinguish
that event from merely ending transmission. No promise is made for entities never
created or observed in the recording, unknown map assets, or missing geometry.

## Evidence and confidence

Repository inspected: `feature/cinematic-campath`, based on `7233b7d9`.
Public Valve SDK revision used for further research:
`3e957b70b167929f120360115d359d1ecb4c1699`.
Public client source establishes plausible intervention points; it does not prove
retail TF2/TF2C binary layouts, vtable slots or availability of particular cvars.
The closed engine's world/static-prop implementation is not supplied by this SDK.
No in-game feasibility prototype has been run for this proposal.

| Layer | Evidence | Implementation implication / unresolved point |
| --- | --- | --- |
| World PVS selection | `CViewRender::SetupVis` passes `ShouldForceNoVis()` to `ViewSetupVisEx`; default origin is `viewRender.origin`. | A force-no-vis route exists. Native view-origin visibility already follows the rendered view in this source. Identify the actual remaining restriction before replacing visibility origins. |
| Existing HLAE override | `AfxHookSource/csgo_view.cpp` detours `ShouldForceNoVis`; `AfxStreams` routes its value through stream contexts. | Reuse the override pattern and existing render scopes. TF2 requires its own verified interface/hook; never copy CS:GO's slot 42. Existing command support is CS:GO-specific. |
| Area portals | Server `func_areaportal` sets portal state; client world-list construction also accepts custom visibility/area-portal tolerance data. | PVS bypass is not evidence that all closed-area restrictions disappear. Compare force-no-vis with portal behavior in the actual engine; changing the recorded server state is not an initial solution. |
| Area-portal windows | `C_FuncAreaPortalWindow::DrawModel` uses `GetDistanceBlend()` and may draw another brush model. | A fading/opaque portal cover can remain even if geometry behind it is selected. Identify this special cover precisely; do not remove arbitrary glass, doors or brush models. |
| Screen-size fading | `CViewRender::GetScreenFadeDistances` supplies `modelinfo->SetViewScreenFadeRange`; public source exposes `r_screenfademinsize` and `r_screenfademaxsize`. | Investigate existing controls first; inspect runtime flags, effective values and actual fade behavior before choosing neutral values. These are not a universal prop-distance override. |
| Detail props | `detailobjectsystem.cpp` uses `cl_detaildist` and `cl_detailfade`; `OnRenderStart` can constrain them with the map detail controller. | Verify both effective cvar ownership and controller limits. Increasing a cvar once may not remove the limit. Keep detail sprites/models distinct from static BSP props. |
| Render-list rejection | Public `clientleafsystem.cpp` includes frustum and engine occlusion tests. A distance rejection exists under `INVASION_CLIENT_DLL`. | Draw-time hooks alone miss models excluded before submission. Do not assume that conditional distance path is compiled into retail TF2. Preserve useful camera-frustum culling if possible. |
| Dormancy | `C_BaseEntity::UpdateVisibility` requires `ShouldDraw() && !IsDormant()`; `SetDormant` calls visibility and particle handling. | Clearing one late draw check is insufficient: an entity may no longer be in the leaf/render lists. Do not globally falsify dormancy for simulation. |
| Transmission ending | `NotifyShouldTransmit(SHOULDTRANSMIT_END)` unlinks the hierarchy, sets dormancy and removes collision-partition membership. | Retain attachment relationships before this transition if required. Rendering retention must not re-enable collision, prediction, sound or gameplay updates. |
| Existing scene machinery | `MirvCalcs.cpp` contains `SceneRenderable`, `Mirv_Tf2_CaptureScenePose` and `Mirv_Tf2_DrawScenePose`. | Reuse identity, pose validation and bounded resource-lifetime machinery. Capture currently rejects dormant/hidden entities; retain a valid earlier pose rather than asking a dormant entity to animate anew. Preview rendering uses category materials/wireframe and omits features, so it is not a finished full-color substitute. |

### Public source references

All links below are pinned to the revision above:

- [View setup, world lists and screen fading](https://github.com/ValveSoftware/source-sdk-2013/blob/3e957b70b167929f120360115d359d1ecb4c1699/src/game/client/viewrender.cpp)
- [View renderer's ShouldForceNoVis declaration](https://github.com/ValveSoftware/source-sdk-2013/blob/3e957b70b167929f120360115d359d1ecb4c1699/src/game/client/viewrender.h)
- [Entity visibility, dormancy and transmission transitions](https://github.com/ValveSoftware/source-sdk-2013/blob/3e957b70b167929f120360115d359d1ecb4c1699/src/game/client/c_baseentity.cpp)
- [Client render-list construction](https://github.com/ValveSoftware/source-sdk-2013/blob/3e957b70b167929f120360115d359d1ecb4c1699/src/game/client/clientleafsystem.cpp)
- [Detail-prop fading and controller limits](https://github.com/ValveSoftware/source-sdk-2013/blob/3e957b70b167929f120360115d359d1ecb4c1699/src/game/client/detailobjectsystem.cpp)
- [Client area-portal window cover](https://github.com/ValveSoftware/source-sdk-2013/blob/3e957b70b167929f120360115d359d1ecb4c1699/src/game/client/c_func_areaportalwindow.cpp)
- [Server portal state](https://github.com/ValveSoftware/source-sdk-2013/blob/3e957b70b167929f120360115d359d1ecb4c1699/src/game/server/func_areaportal.cpp)
- [Public static-prop interface](https://github.com/ValveSoftware/source-sdk-2013/blob/3e957b70b167929f120360115d359d1ecb4c1699/src/public/engine/IStaticPropMgr.h)

## Proposed approach

### 1. Establish why each disappearance happens

Use a short POV demo with an STV comparison where available. Place a cinematic
camera across a room/portal boundary and observe the same prop/entity with a fixed
camera. Record the demo tick, identity/serial, model, dormancy, renderability,
alpha/fade inputs, and whether a native draw was submitted. Inspect world-list and
portal changes separately from model draws. A missing draw alone does not identify
PVS, fade, occlusion, dormancy or deletion as its cause.

Probe existing engine controls only after runtime discovery. Record their flags
and effective values, restore them after each probe, and compare ordinary playback
with overridden playback at the same tick. Do not document guessed cvar names or
values as a supported fix. Keep the actual cinematic-camera matrices identical.

### 2. Extend visibility and fade controls

Prefer the existing native renderer over manually drawing the whole map. Verify a
TF2 force-no-vis hook or equivalent native visibility call. Check whether compiler
devirtualization means hooking the virtual method alone misses actual calls.
Validate the ABI separately for supported TF2/TF2C builds and architectures; fail
closed when discovery fails. Inspect water/reflection/skybox subviews as well as
the main view. A scoped policy must not leak into unrelated views.

If force-no-vis does not bypass a recorded area mask, investigate the renderer's
area-mask/world-list input as a distinct scoped override. That is a research gate,
not a confirmed hook. Treat portal-window covers separately. Avoid changing global
server portal state or hiding all translucent brush geometry as a shortcut.

For props, distinguish static BSP props, dynamic model entities and detail
sprites/models. Prefer discovered engine controls; otherwise investigate targeted
fade-calculation hooks. Restrict overrides to fade contributions. Preserve real
material transparency, Spy cloak, authored render alpha and intentional effects.
A final forced alpha of 255 on every model is not acceptable.

Use one shared policy routed through existing demo-fix controls and render/capture
scopes. UI and console must invoke the same operations. Restore only values still
owned by the fix, following existing demo-fix preference/ownership patterns. Avoid
adding overlapping stream-only and viewport-only implementations of the effect.
The eventual names and grouping of controls remain undecided.

### 3. Retain dormant appearances

Two possible routes need comparison before implementation:

**A. Retain native render-list participation.** Intercept the render-only visibility
transition and retain the last valid visual state. This could preserve native
materials and lighting, but must account for leaf reinsertion, valid bounds,
attachment hierarchy, bone caches, and model-specific dormancy checks. It must not
change the entity's real network dormancy flag or restore collision-partition
membership. This is preferable only if render and simulation behavior can be
reliably separated in the shipped client.

**B. Replay a last-known pose through the existing scene infrastructure.** Capture
while the original entity is valid, then submit its held visual representation
when the native model is absent due to dormancy. Extend existing scene ownership
and pose logic rather than create another entity/cache/renderer subsystem. A
full-color route would need original material/skin/body handling, flexes where
available, attachment grouping, lighting, transparency and native-pass integration.
Current preview replay is not sufficient for all of these. Explain any required
replacement subsystem and obtain approval before implementing it.

Choose A or B after a prototype comparison; do not maintain two competing retention
implementations by default. Reuse capture data and classification already present.
Do not force dormant entities to run animation or prediction merely to draw them.
Hold their last validated bone/attachment pose until valid updates resume. At
resumption, native rendering should take over without a duplicate held model.

Retention rules required for either route:

- Key entries by demo/level epoch, complete entity handle including serial, and
  appropriate model identity. Slot reuse must never resurrect the old occupant.
- Observe dormancy separately from explicit destruction and recreation. Clear or
  invalidate entries on removal, model change, demo reload and level shutdown.
  Known death/respawn transformations must not leave an old living model behind.
- Preserve parent/child visual transforms together when Source unlinks hierarchy.
- Never retain a live pointer across entity destruction. Use the existing render
  queue/resource ownership patterns for in-flight submissions.
- Bound pose history and resource retention. Report unavailability when a usable
  pose or asset is missing; do not invent geometry or unknown movement.
- For seeks, select only a pose known at or before the requested tick. Never use
  the last pose seen in the future. If coverage is missing, require replay/cache
  coverage rather than silently showing a temporally incorrect ghost.
- Keep retention data separate from ordinary scene-cache history policy while
  sharing its storage/identity/lifetime helpers where appropriate. Whether held
  appearances should be included in scene-cache recording is an explicit design
  decision still to settle, not a reason to weaken the current capture validator.

## Recording and compositing requirements

Use the existing base/isolate classification for retained visuals, including loose
weapons, cosmetics, ragdolls and gibs. A retained player must appear once in Players
and be removed entirely from Base when Players is separated; no invisible player
depth should reintroduce the earlier silhouette defect. Preserve world occlusion
in Players. Depth should describe the same chosen visible representations. HUD
and viewmodel behavior should remain independently controlled by existing passes.

MSAA, material queue operation, alpha matte replay and encoder choices must not
change retention timing. Do not advance held animation independently per pass.
Test water, glass, particles, cloak and shadows; neither visibility expansion nor
pose retention solves the existing limits of compositing interleaved effects.

## Validation and completion criteria

| Case | Expected result |
| --- | --- |
| Camera crosses open/closed portal boundaries away from recorder | Available geometry remains stable; physical doors/brushes remain opaque/occluding as intended. |
| Portal-window cover at varying camera distances | Correctly identified cover behavior; no blanket removal of glass or world surfaces. |
| Static, dynamic and detail props; narrow/wide FOV | No unintended fade/pop with fix enabled, and ordinary behavior returns when disabled. |
| Entity still receiving updates but locally culled | Its recorded movement renders; no unnecessary frozen substitute. |
| Dormant player with cosmetics and weapon | Last validated appearance remains correctly assembled and world-occluded. |
| Updates resume | Native animation resumes without duplicate models or stale attachments. |
| Deletion, death/respawn, class/model change, recycled slot | No resurrection or invalid-resource access; lifecycle-specific retention rules are respected. |
| Pause, slow motion, forward/backward seeks, demo reload | Stable poses, no future-state leakage, deterministic replay for covered intervals. |
| Base, Players, Depth, optional reference, alpha/chroma | Matching frame times, correct subtraction/occlusion and no retained-player silhouette in Base. |
| TF2 and TF2C; supported architectures; queue/MSAA combinations | Verified hooks or explicit unsupported status; no guessed ABI fallbacks. |
| Long recording and repeated toggles | Bounded resources, render-state restoration, no persistent cvar changes or crashes. |

Measure CPU/GPU time, draw count and retained-pose memory against the same shot
with the fix off. Expanding visibility can expose much more map geometry; keep
normal frustum rejection where compatible and provide usable granular controls.
Never imply that all map/prop restrictions have been bypassed solely because the
force-no-vis hook returned true.

Implementation milestones, subject to approval:

1. Instrument and validate world/portal/fade causes in retail TF2 and TF2C.
2. Ship shared map-visibility and prop-fade controls once their individual effects
   and restoration behavior are verified.
3. Prototype dormant visual retention using A or B; settle lifecycle, materials,
   seek semantics and scene-cache interaction before expanding entity coverage.
4. Validate pass compositing and long-session behavior, then expose the complete
   fix through the existing demo-fix workflow with clear capability/status output.

The map/fade portion has concrete native entry points to investigate. Complete
entity retention is plausible but remains a larger, unvalidated extension; this
report is not a claim that one switch can already provide STV-equivalent coverage.
