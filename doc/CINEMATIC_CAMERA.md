# HLAE cinematic camera overlay — Paper port

This feature is based on Paper's overlay and the original HLAE base commit
`3906a61e` (upstream HLAE 2.192.10, `8abc2719`). It uses ordinary `mirv_campath` keys and the classic path renderer.
There is no separate cinematic system or per-segment travel-blending layer.

## Build and open

Update `feature/cinematic-campath`, initialize the existing Git submodules as
usual, and follow [BUILDING.md](../BUILDING.md). CMake downloads the pinned ImGui
release automatically; no patch or manual copy from Paper's repository is needed.

In TF2, close the console and press **F8**, or run:

```text
mirv_overlay
mirv_overlay 1
mirv_overlay 0
```

F8 toggles the whole workspace. Source menus, the console and Shift+F2 demo UI
retain priority. Enable the free camera in Camera Control, then hold **right mouse
outside panels** to move it with ordinary free-camera controls. Releasing right
mouse restores the pointer. Entering or leaving UI ownership clears pending camera
movement. Panels can be moved and docked inside the game window.

## Capture and playback

1. Move the camera and advance the demo to the desired time.
2. Click **Add point** in Campath. This captures position, angles, FOV and time.
3. Repeat. A new overlay-created path uses **Spline**, which works with two keys.
4. Enable **Campath enabled** and release the free camera to let native playback
   follow demo time. Use `mirv_input end` when necessary.

Choose **Interpolation** in the Campath tab or curve editor:

| Option | Behavior |
| --- | --- |
| Spline | Original whole-path spline with a smooth start and stop |
| Catmull-Rom | Shared tangents based on neighboring keys and their time spacing; travels through keys and uses nonzero endpoint slopes |
| Shape-preserving (Monotone) | PCHIP interpolation limits overshoot in each automatic channel; peaks, reversals and plateaus can flatten a channel |
| Minimum jerk | A global quintic solve shares velocity and acceleration through ordinary keys; supports two keys |
| Classic cubic | Original HLAE interpolation with its four-key requirement |
| Linear | Straight interpolation with two keys; corners can change velocity abruptly |

Spline, Catmull-Rom and Monotone share one automatic derivative at each key
between its incoming and outgoing intervals. The **Interpolation** menu chooses the position and FOV profile.
**Rotation** independently selects editable Euler curves, Classic quaternion spline,
or quaternion linear interpolation. The effective native modes are printed below
the controls; profile changes retain the chosen rotation backend. Editable Euler
rotation uses the same Auto profile as position/FOV. **Tension** adjusts Auto tangents across the entire path:
0% preserves their full slopes; 100% flattens them and deliberately stops at keys.
Intermediate values reduce the slopes. Existing Free, Flat and Linear tangent
edits are retained. **Recalculate Auto when changing profile** explicitly resets
position/FOV tangents and editable Euler tangents when selecting a new profile. Return those tangents to Auto to apply the chosen profile.

For flowing travel, start with Catmull-Rom at 0% tension. Authored positions and
timestamps still determine speed; these profiles do not promise constant speed
between arbitrarily timed positions. Classic console capture with
`mirv_campath add` remains available. Existing files keep their interpolation.
**Edit Classic position / FOV tangents** converts those scalar curves to editable
curves using their exact calculated derivatives, retaining their shape and the
Classic quaternion rotation backend. Holds still require conversion to Custom;
the editor now explains that restriction explicitly.

Minimum jerk minimizes integrated squared jerk over the entire channel, using
shared velocity and acceleration at keys. It can overshoot and is not a constant
speed mode. Linked, unweighted slope edits become derivative constraints in that
solve. Broken slopes or weighted handles use the existing cubic evaluator on the
affected channel; Shift-weight dragging is disabled for minimum jerk. Explicit
holds use zero velocity and acceleration at arrival and release. Insertion and
distance retiming preserve quintic shape by retaining its effective derivatives.

Profile and tension settings save in the same XML and participate in undo/redo;
older XML files use the original defaults.

## Sequencer and curve editor

Enable **Show Sequencer**. The timeline spans the entire demo, including future
ticks, and displays its current visible range. Scroll the wheel to zoom around the
pointer. Middle-drag or Shift+wheel to move through the timeline, or drag its
overview bar. **Fit demo** restores the full range. Navigation does not retime keys.
Named paths appear on separate tracks. The editing track has draggable keys;
other tracks show reference keys and holds. Click another path's name or range to
make it the editing path. **Playback / cuts** shows which eligible shot wins each
time interval, including overlaps and returns to an earlier shot. It displays the
planned sequence while global playback is disabled. Yellow boundaries identify
switches; the Campath tab also names the current playback winner.

Wheel over the time area zooms; wheel over track names scrolls a tall collection.
Long names are clipped in the name column and shown fully in tooltips. The
sequencer supports selecting and dragging keys and exposes a right-click menu:

- **Remove** deletes the key.
- **Get** releases path preview and moves the free camera to the key's pose.
- **Set** captures the current pose at its timestamp, preserving tangent metadata.
- **Edit** selects the key for the viewport gizmo.

Use **Play**, **Freeze**, the speed buttons, or scrub the timeline to navigate the demo. Play resumes Source at 1x; Freeze uses `demo_timescale 0` and resumes
Source's pause state. Speed buttons also resume Source, so they work after either
pause or timescale freezing. The preview
slider evaluates the path without seeking. **Preview path** in either panel and
`mirv_campath preview` operate the same cursor and camera. Opening or closing the
overlay retains that cursor; right-mouse flying explicitly exits preview.
Free-camera movement and smoothing do not alter the evaluated pose. **Seek to
preview** seeks the demo to that cursor.

**Insert keyframe at preview** subdivides an existing Custom/Linear path without
changing its curve, including weighted handles and Euler winding. It requires an
unoccupied time inside the path. To preserve all existing intervals, automatic
tangents are frozen to their effective Free tangents; use **Reset selected tangents
to Auto** when you want them recalculated. Classic quaternion/cubic interpolation
cannot be split exactly by this editor and insertion is rejected there. **Add
point** remains normal pose-and-demo-time capture.

Replacing an existing key from the free camera keeps the equivalent Euler branch
nearest that key's current evaluated angles. This prevents a wrapped capture near
±180° from inserting an unintended revolution, including after subdivision.
Explicit numeric winding and authored multi-turn paths remain available.

Retiming keys in the sequencer, inspector or graph keeps editable Auto handles
automatic. Handles already saved as Free by an older version stay Free; reset
those to Auto once if you want them solved automatically. Their curve is recalculated at the new times. Authored Free handles
keep their normalized interval shape: velocities use each side's duration ratio
and accelerations use its square. Hold durations and explicit angle winding are
retained. Crossing key order, collisions and invalid hold timings reject the
whole edit. With editable Euler rotation, Classic position/FOV curves may still
convert through the exact editable conversion to retain their shape.

Recapturing an existing camera pose rebuilds tangents on rotation channels whose
angles changed, including the facing handle on each neighbouring key. Baked
accelerations on those channels are discarded so the solver can recompute them.
Other channels and the neighbours' opposite handles remain intact. Position-only
replacement and recapture of an equivalent orientation preserve angular values
and handles. Explicit graph edits retain the user's authored handles.

To investigate an edit that still spins, enable the opt-in recorder **before**
editing, reproduce it, then stop and export:

```text
mirv_campath debug history begin
mirv_campath debug history end
mirv_campath debug history save "euler-edits.csv"
```

`begin` starts a fresh recording with a baseline. `status` reports recording
state and retained snapshots; `clear` discards them. The CSV records every
successful geometry change, including zero-based key indices, times, channel
values, authored-Euler flags, raw quaternions, tangent modes, slopes, weights,
accelerations, holds and interpolation settings. It stores at most 128 snapshots
or 4 MiB; discarded snapshots are reported in the export. Paths above 2048 keys
are skipped and counted. The recorder is disabled by default, performs no file
I/O during edits and remains outside saved path/undo state. `end` retains the
recording for export; use a new writable CSV filename.

The sequencer keeps classic zero-based key indices when zooming or panning past
hidden keys. Zoom changes visibility, not numbering.

### Command track

The sequencer has a **Commands** row and a **Commands** tab below the timeline.
Choose **Add command at playhead**, enter a name, tick and command text, then
**Apply command edits**. The tick field and **Use playhead** move a marker;
editing the draft does not execute it. **Run command now** explicitly runs the
saved version. Multiple commands at the same tick run in creation order.

**Enable command execution** arms future playback crossings. It works with the
overlay closed, using HLAE's existing `mirv_cmd` dispatcher. Loading a command
track or starting another demo disables automatic execution. The global
`mirv_cmd enabled` setting also gates it. Automatic execution requires the
TF2/TF2C native seek observer; the UI reports when that observer is unavailable.

Ordinary commands are skipped during forward/backward seeks and demo rebuilds.
**Run on seek** is an opt-in for persistent settings: after a seek, all enabled
markers with this flag at or before the destination execute in tick order.
Include initial/reset markers when a setting must be restored on backward seeks.
This cannot undo arbitrary console-command effects. Replaying a previously
visited interval normally executes its commands again.

**Save command track** and **Load command track** use a separate XML file; a
`<demo>.commands.xml` name is suggested beside the indexed demo. Apply drafts
before saving. Existing `mirv_cmd save/load` also includes these markers alongside
its legacy commands. Import never enables execution. Console access is available:

```text
mirv_cmd markers add 1200 "Slow motion" "demo_timescale 0.5"
mirv_cmd markers list
mirv_cmd markers edit <id> runOnSeek 1
mirv_cmd markers enabled 1
mirv_cmd markers save "shot.commands.xml"
```

Use `mirv_cmd markers` for the remaining edit/remove/run/load commands. These
markers use integer demo ticks; camera key indices and camera undo history are
unchanged.

### Demo events and navigation

The **Demo events** row indexes events across the entire demo, including ticks
not yet played. **Open demo...** is available directly in the sequencer and uses
the same file picker as Settings. Native `playdemo`/`timedemo` commands also
supply their filenames for automatic background indexing, including Source UI
actions that issue those commands. Relative names resolve against the game
directory; a missing extension defaults to `.dem`. An unchanged completed index
is reused. **Automatically index opened demos** can disable this behavior.

For a demo already playing when HLAE starts, or a UI that opens playback without
issuing an observed console command, choose **Index demo events** in the
**Events** tab. Indexing is read-only and never executes embedded demo console
commands. Progress and cancellation are available; completed results stay in
memory, with no continuous file parsing while idle.

Right-click the ruler or a timeline track for **Go to** at the clicked tick.
Right-click a command/event marker to target its exact tick. **Go to with
lead-in** seeks the configured number of ticks earlier, clamping at zero. The
**Go to tick** field and **Go** button provide exact navigation; Enter also seeks.
All these routes use the existing after-seek policy.

Filter deaths, rounds, objectives and player events; search names, weapons or
other event fields. **Recorder deaths only** uses the recorded header name when
it can be matched to a player. Names resolve from the demo's user-info snapshots
and retain their historical values. High-frequency damage/healing notifications
are excluded from the cinematic event track.

Click a marker to inspect **all events at that tick** below the timeline,
including their individual fields. **Previous event**, **Next event** and
**Seek to event** provide navigation. **Event lead-in (ticks)** also affects
**Seek to event**; it defaults to 132 ticks, so a kill can be viewed with its
lead-up. Set it to zero to seek to the exact event tick. Point markers have
12-pixel hover targets and display one tick in their tooltips. Dense markers
share a pixel when zoomed out; zoom in or use event navigation to inspect them
individually. Existing wheel zoom, pan and overview work across all tracks.

Check the indexed filename shown in the tab. The native engine interface only
lets the overlay compare the playing demo's map and tick count to the index;
these checks cannot distinguish different files with identical metadata.
Unsupported, truncated or changing files report an error without publishing a
partial index. This reader supports Source demo protocol 3, network protocols
15–24; TF2C-specific event coverage still needs testing with real TF2C demos.

The portable C++ reader uses the public Source demo layout and network/event
layouts studied in [Valve's Source SDK](https://github.com/ValveSoftware/source-sdk-2013)
and [demostf/parser](https://codeberg.org/demostf/parser). It adds no external
parser runtime or build dependency.

### Experimental manual player repair

The sequencer's **Player repair** tab tests correction of a single player's
network position and eye pitch/yaw. It does not rewrite `.dem` files or delete
packets. Marked receive values are replaced before Source processes them, so the
engine owns subsequent interpolation, animation and camera attachment. Unmarked
channels and other entities pass through unchanged. This is a runtime prototype,
not a general demo-file repair tool or skeletal-animation reconstruction.

1. Open the demo normally with HLAE loaded. If its filename is unknown, Start
   opens **Choose the demo currently playing**. Select that `.dem` and collection
   continues automatically without reopening or seeking it. Event indexing is not
   required. **Choose playing demo file...** can also identify the file without
   starting collection, for example before enabling a saved repair. Console
   access is `mirv_fix playerRepair demo "C:\full\path\file.dem"`.
   The selected file's map and available tick count are checked against playback;
   cancelled or mismatched selections preserve the existing samples.
2. Explicitly choose a valid player from **Repair player**, then **Start new
   player collection**. Empty slots and nonpositive user IDs cannot start a
   collection. A matching indexed demo binds automatically when no file is bound; otherwise
   Start asks for the playing demo file.
   This replaces
   the previous player's samples and marks; save existing work first.
3. Seek backward to tick zero or before the troublesome section, then play
   through it and far enough beyond it to collect a good future anchor. The
   collector records incoming network values, not smoothed render-frame poses.
   Delta-unchanged fields retain their last received original value. A backward
   seek while collecting restarts the sample history to avoid mixing passes.
4. Choose **Stop player collection**. Inspect the original values by tick,
   previous/next sample, or clicking blue sample marks on the repair track.
   Network ticks can precede the visible glitch because of interpolation delay;
   inspect the raw values rather than selecting only by the rendered frame.
   A partial mask means not all channels have been received yet. Seeking to the
   beginning while collecting helps capture initial full values.
5. Select a sample, enter the inclusive first/last bad ticks, or drag a range
   across the repair track. Choose **Position**, **Eye angles**, or both, then
   **Exclude marked player samples**. Red spans mark exclusions.
6. Choose **Use for comparison** on a marked repair, then **Compare original from
   before range** or **Compare repaired from before range**. Both seek 66 ticks
   before that repair and play automatically at normal speed. If no marked range
   is selected, comparison uses the earliest repair, not an unmarked inspector
   selection. Your usual after-seek setting is preserved for other seeks.
   Compare again after every edit or application toggle: changing the checkbox
   cannot retroactively replace values already latched by Source.
7. Save the repair sidecar. It contains original samples and editing marks;
   loading it leaves application disabled. Open the same unchanged demo before
   enabling it. The binding uses the canonical filename, header fingerprint and
   file size, plus the recorded player user ID; a different path needs a new
   collection. Only one player repair is active in this first version.

If no samples appear, run `mirv_fix playerRepair status` immediately after Start.
A successful start reports `collect 1` and a positive user ID. The status also
shows the demo binding, client interface, observer hooks, properties and entity
selection. Stop preserves setup errors and explicitly reports a zero-sample
collection; it does not imply collection succeeded. Collected samples stay in
memory after Stop.

**Smooth bounded reconstruction** uses monotone cubic slopes from neighboring
valid samples; disabling it uses linear interpolation. Angles take the shortest
wrapped route. Exclusions never become future anchors. Missing anchors,
life-state boundaries, manual cuts, or bridges longer than **Maximum bridge
(ticks)** preserve the original channel and report why. The default maximum is
330 ticks. These controls are under **Advanced repair boundaries**. Use **Toggle
discontinuity at sample** only for a genuine teleport or cut; it blocks repairs
across that tick, rather than marking bad data. These boundaries draw in purple
and are listed with individual **Remove boundary** buttons. **Undo player repair**
and **Redo player repair** cover exclusions and manual cuts separately from
camera-path history.

Exclusions are red when the selected channels have a usable reconstruction and
amber when they are blocked or partly blocked. Hover a span or inspect its list
entry for the exact anchor ticks and reason. Readiness checks are cached until
the sample history, edits or bridge limit changes. A ready bridge still needs
**Apply marked player repairs** or **Compare repaired from before range**, then
playback through the interval. Enabling is refused when nothing is marked or
all ranges are blocked. If only some channels or ranges are blocked, the usable
ones can still apply and status warns about the others.

The `writes` counter counts substituted receive callbacks, even when the
replacement equals the original. **Actually changed** (`changed updates` in the
console) counts callbacks with at least one numerically different input value.
Counters reset when a repaired comparison is enabled. Neither counter measures
rendered quality. An ON state with no substitutions means no eligible repair
updates have arrived yet; replay the marked interval with the sampled player
present. Substitutions with no changed values have had no numeric repair effect.
The menu separates recording, marking, and comparison; raw masks and internal
life epochs are no longer shown in the ordinary sample readout.

During a controlled comparison seek, a same-file demo restart or transient
no-demo state preserves the requested repair. A different or unknown file,
explicit restart outside a controlled seek, or leaving playback disarms it.
Samples and marks are retained, and playback interruptions are reported.

A life-state rejection identifies the first recorded sample in the new epoch;
split or narrow the exclusion rather than bridging a death/respawn. Manual cuts
identify their exact tick. Maximum-bridge rejection reports the actual distance
between the valid anchors, which can be wider than the marked interval itself.
Enabling repair never bypasses these constraints. Blue sample gaps alone do not
prove missing data: updates are change-driven and dense points collapse on the
ruler. This version does not invent packets where no update was received.

The collector is capped at 100,000 original sample ticks. It stores decoded
changes that were actually observed; it does not invent network messages where
none existed. Corrected inputs may improve both player rendering and attached
cameras, but do not repair animation sequences, ragdolls or individual bones.
The POV recorder's separate command-info camera is not changed; channels that
were never networked for that player remain unavailable. TF2/TF2C receive
metadata must expose the required origin, eye-angle and life
properties; otherwise collection/application refuses to start. Live builds must
be tested: offline tests cannot prove that every rendering/attachment mode
consumes these corrected inputs.

#### Investigating a visible angle snap

If network eye pitch/yaw look normal but the rendered player snaps, use
**Investigate angle snapping** in the Player repair tab. This is an observation
experiment, not an automatic animation fix:

1. Identify the playing demo with **Choose playing demo file...** if necessary,
   choose the player in **Repair player**, and stop sample collection.
2. Position the playhead at the visible glitch, expand **Investigate angle
   snapping**, and click **Use current tick +/- 66**. Adjust the window if needed
   (maximum 2,000 ticks).
3. Click **Record original angle diagnostic**. It disables repair, rewinds to
   tick zero, and plays at normal speed. Existing repair samples/marks remain.
   The seek temporarily uses Continue; other after-seek preferences are retained.
4. Keep the player visible and let playback pass the end of the diagnostic
   window. Capture stops automatically, or use **Stop angle diagnostic**.
5. **Save angle diagnostic CSV...** to a new filename and share the CSV together
   with the tick at which you saw the snap. Existing files are not overwritten.

The CSV includes demo tick, engine frame, client time, player identity, repair
state, field, values, status and event number. It now covers:

- Raw, forwarded and written receive values for eye angles, origin and life state.
- Live pose slots resolved from receive-table metadata, even when the server
  never updates them. The recorded player is checked before each read.
- Sequence, cycle, playback rate, animation/simulation times, sequence/reset
  parity, client-animation flags, interpolation-frame flags, health/team/life,
  model index/scale, flags, origin/velocity and rotation where metadata exposes them.
- Animation-layer sequence/order, current/previous cycle, weight and playback rate.
  CUtlVector layout is validated from its receive-proxy metadata; its current
  bounded contents are copied with a checked process-memory read. Missing slots
  have `inactive_layer` status, rather than being confused with weight zero.
- State at network-update end, before the engine's render-start callback, after
  that callback, and at render end. Frame/tick interval, interpolation amount,
  demo timescale/pause/skip/HLTV state and interpolation cvars accompany the data.
- Bone matrices from the selected renderable's **natural SetupBones calls**.
  A bounded mutex-protected queue supports worker calls. No additional bone
  evaluation or AGR recording mode is requested. Each call includes success,
  output-buffer presence, mask and sample time. Matrices include every available
  bone, with model generation, names, parents, flags, and pose names/ranges.

For scalar rows only `value` is populated. For bone rows, `value,v1,...,v11`
contain the row-major world-space 3x4 matrix (translation is at columns `v3`,
`v7`, `v11`). `event` joins a matrix to its SetupBones call information. Bone
frames and client times are stamped during the call; the demo tick is that of
the main-thread drain, so use captured frame/time when matching late worker
output. Entity render angles are not the same as eye direction or head-bone
orientation. Pose values are normalized slots; model metadata supplies their
names and ranges when available.

The file ends with `coverage` counts and `summary` rows, including capture
status, requested ticks, resolved layer-field count, matrix-row count and dropped
bone records. Statuses are `ok`, `unavailable`, `unobserved`, `invalid_layout`, `inactive_layer`,
`nonfinite` and `masked_out`. Masked-out matrices may be stale; the call mask
and model bone flags explain which output was requested. Blank values are
unavailable, never implicit zeroes. Begin reports resolved pose slots, layer
fields and bone observer availability; the UI reports actual pose/matrix coverage afterward.

No file I/O runs during capture; export happens after stopping. Storage is
capped at 250,000 rows and the worker queue at 16 calls. Overflow is counted;
more than eight model generations stops the capture with an explicit message.
A rewind clears rows, counts and remembered bindings to avoid mixing passes.
Opening another demo or leaving playback stops capture. The UI's tick-zero
replay captures initial state; the console entry point only arms recording.

Layer discovery follows Valve's `DT_BaseAnimatingOverlay -> overlay_vars ->
DT_OverlayVars -> m_AnimOverlay` wrapper before inspecting the vector metadata.
The SDK reference is [c_baseanimatingoverlay.cpp](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/c_baseanimatingoverlay.cpp).
A zero `layer_fields_resolved` is missing layer coverage, not evidence that the
player has no active animation layers.

For a focused investigation of blending itself, Valve also supplies
`r_sequence_debug <entity index>`. In the public SDK its output includes the
base sequence, transition queue, and accumulated overlay cycles/weights, which
can supplement the stage snapshots. For the same diagnostic pass, note the
previous values of `developer`, `r_sequence_debug`, and `con_logfile`, enable
`developer 1`, set `r_sequence_debug` to the selected entity index, and set
`con_logfile` to a new filename. After capture, restore the previous values.
This native log is separate from the CSV and may add console/file overhead; it
is optional and is never enabled automatically by HLAE. The public reference
explains the intended behavior; available output can differ in shipped builds.

This is diagnostic coverage, not an animation fix. Fields missing from public
receive/model metadata remain unavailable, and private animation-state internals
are not guessed from unverified offsets. Stage differences and actual skeleton
output should help distinguish interpolation, animation blending and bone
processing without repeatedly extending the capture for each likely cause.

```text
mirv_fix playerRepair diagnostic begin <entity index> <first tick> <last tick>
mirv_fix playerRepair diagnostic status
mirv_fix playerRepair diagnostic end
mirv_fix playerRepair diagnostic save "new-player-angle-trace.csv"
```

The same storage is available under `mirv_fix`:

```text
mirv_fix playerRepair begin <entity index>
mirv_fix playerRepair end
mirv_fix playerRepair exclude <first tick> <last tick> position|angles|all linear|smooth
mirv_fix playerRepair enabled 0|1
mirv_fix playerRepair cut <tick>
mirv_fix playerRepair undo
mirv_fix playerRepair redo
mirv_fix playerRepair save "player.repair.xml"
mirv_fix playerRepair load "player.repair.xml"
mirv_fix playerRepair status
```

AGR is useful as a validation export: TF2's existing recorder writes rendered
world transforms and skeletal matrices, so original/repaired AGR captures can
be compared. It is downstream of interpolation and animation and cannot replace
the original network sample history used by this tool.

Reference checks: [p2sr/demofixup](https://github.com/p2sr/demofixup) replaces
obsolete Portal 2 class/table entries while preserving class IDs;
[VolvoWrench](https://github.com/Traderain/VolvoWrench) places its Demo doctor under
GoldSource. These structural repairs do not implement the runtime player-sample
reconstruction described here.

The overlay uses ImGui's own drawn cursor, including the I-beam over text fields.
Windows cursor changes and Source UI queries are avoided in the busy cursor hooks.
Focus loss and free-camera mouse-look restore the game's cursor and end an
active editing gesture. F8 opens the overlay above Source menus and the console.
While open, it captures keyboard and mouse input so Source binds and menu
controls underneath do not activate; close it with F8 to use those controls.
RMB free-camera movement remains available outside panels when no Source menu
is open. Alt+Tab and Alt+F4 retain their normal system behavior.
The renderer consumes the newest queued UI frame
to avoid an additional stale-frame delay. It retains that snapshot if rendering
gets ahead of UI updates, avoiding blank overlay frames; an explicit empty update
or device reset clears it. The software cursor remains bound to the game's
rendering frame rate.

Enable **Show graph**, or **Show Curve Editor** in the sequencer, to open the
dockable **Campath Graph** window. It displays X/Y/Z, FOV, pitch/yaw/roll individually or
in combination; X is visible initially. Normalization makes channels with different
units readable. Wheel zooms time around the pointer; Ctrl+wheel zooms values.
Middle-drag pans both axes. **Fit graph** resets the view. The sequencer window
can be resized vertically; zoom and pan no longer require sliders. Graph time
labels show demo ticks / path seconds; value labels and cursor tooltips show the
focused channel (or first visible channel). Tooltips also report evaluated values
for every visible channel. Holds have release markers, including final holds and
single-key stationary shots.
Click a graph key to select it, Ctrl-click to extend/toggle selection, or drag a
box on empty canvas. Right-click a channel checkbox to focus editing when
normalized channels overlap. Drag keys to change time and that channel's value;
Shift locks the dominant axis, and Alt snaps time displacement to demo ticks.
Selection is shared by native commands, graph editing and timeline highlights.
**Frame selected** fits the selected keys in time and visible values. **Delete
selected keys** or **Delete** removes the native selection in one undoable edit;
it retains the path offset. Timeline dragging preserves fractional times and
leaves unselected timestamps untouched. A rejected drag also retains selection.
The inspector's **Lock key times** protects graph/timeline retiming and editor hold
capture. Checked X/Y/Z/FOV/Pitch/Yaw/Roll boxes lock those channels for graph,
inspector, replacement and gizmo pose edits. Anchoring requires unlocked pose
channels. Explicit console commands remain available independently of editor locks.
Selected keys move together. **Escape** cancels an unfinished mouse drag,
including handles, modifier envelopes, speed points, timeline retiming and the viewport gizmo. Colliding times and overlapping holds are rejected
without deleting keys. The **Selected key inspector** provides exact path time,
demo tick, position, angles, FOV and hold duration. Numeric changes apply as an
offset to all selected keys (hold duration applies the same duration).

Right-click a key for Auto, Flat, Linear or Free tangent modes, applied to the
incoming side, outgoing side or both. Clicking a selected key preserves the
selection for batch tangent changes. **Reset selected tangents to Auto** resets
visible/focused channels, including their handle weights. **Reset whole path to
Auto** recalculates all channels without changing selection. Circles identify Auto
tangents; squares identify authored/baked tangents, and the interpolation controls
report their counts. Insert and retime preserve shape by baking automatic tangents;
changing profile or tension then affects only tangents still set to Auto.

Handles and tangent menus are available only for Custom channels. Linear and
Classic curves are drawn from their actual evaluator and expose no inactive
handles. Distance-timed channels are also drawn from playback; disable distance
timing to edit their underlying handles directly.

- Drag a curve handle to edit the shared slope through that key.
- Box-select key points in one or several channels. Hold **Shift** while dragging
  a selected handle to move the matching incoming/outgoing handles across those
  selected key/channel pairs. Both time length and value position follow the same
  screen displacement, using each channel's scale. Unselected, hidden and locked
  channels stay unchanged; keys without a matching handle are skipped.
- **Ctrl-click** toggles a key/channel pair; **Ctrl+box-select** adds pairs. Native
  keyframe selection and the inspector still identify the associated full poses.
  Selection from the sequencer or Select All includes their visible channels.
- Hold **Ctrl** to change length/influence without changing slopes; **Ctrl+Shift**
  applies that to the selected group. Minimum-jerk handles have fixed time weights,
  so only their slopes can be moved.
- Hold **Alt** to edit a side independently for an intentional corner.
  Choose modifiers when starting the drag. By default opposite slopes stay aligned
  and the opposite handle retains its length. Group selection is preserved, each
  drag is one undo step, and Escape cancels the complete group.
- **Undo / Redo** or **Ctrl+Z / Ctrl+Shift+Z** restore data edits while the overlay
  owns input, and in native `mirv_campath preview` mode with the overlay closed.
  Console edits enter the same history, starting with the first edit. A drag is
  one edit. History retains up to 64 entries within a 64 MiB budget. Preview,
  selection, panel navigation and playback enable changes do not consume history. Console equivalents are
  `mirv_campath undo|redo` and `mirv_campath preview undo|redo`.

The graph reads effective tangents directly from the path evaluator, including
automatic and tension-modified tangents. Weighted curves solve their time
coordinate during playback, so the graph and camera use the same curve. Euler channels unwrap adjacent angles to avoid ordinary
360-degree discontinuities. Authored Euler values retain winding after insertion.

**Travel speed / distance timing** plots the evaluated positional speed. **Retime
by distance** preserves the spatial curve, distributes travel duration by arc
length, and retains hold durations and stationary intervals. Custom/Linear
channels are required; the total duration must leave time for travel. This enables
**Distance timing**, which maps each travel interval by distance and smoothly
ramps into and out of explicit holds. Ordinary keys share the overall travel
speed after retiming; geometric corners can still change direction abruptly.
Changing positions, tangents or holds afterward can require another retime.
New paths apply distance timing to **position only**. Angle and FOV curves keep
their own time coordinate within the retimed key intervals. **Apply distance timing
to angles / FOV too** restores the shared pose clock when desired. Earlier files
without a mode retain their original whole-pose clock; new files store the choice.
Retime still changes the arrival timestamps of existing keys for every channel.
Turning Distance timing off restores ordinary interpolation in the new time layout.

**Editable speed profile** is in Travel speed / distance timing. Choose **Constant
speed** or **Ease start / end** as a starting point. Drag the speed points;
double-click to add one and right-click an interior point to remove it. The X axis
is normalized moving time across the whole path, excluding explicit holds and
stationary intervals. Y is relative speed. The system integrates and normalizes
that profile to cover the same spatial distance in the requested duration. Zero
speed is allowed, but an entirely zero profile is rejected. Integration is analytic
between speed points, so narrow ramps retain their area during playback and reload.
Zero-speed intervals remain stationary.

Clicking a speed point leaves timing unchanged. Dragging changes the profile;
Escape cancels that gesture. Closing the editor, switching shots or changing the path
through another editing action ends the drag without restoring stale shot data.
**Lock key times** also locks speed editing and distance retiming. Speed points
stay within the visible chart, and the plotted profile is clipped to its bounds.

Editing this profile immediately retimes the existing camera keys and preserves
the spatial curve. It does not restart easing at camera keyframes. Arrival times
and hold release times move; hold durations remain unchanged. The current total
path duration is retained. Use Total duration / Retime by distance to choose a
new duration. Noise ranges keep their explicit path-second coordinates when the
base path is retimed. The upper speed display shows the resulting evaluated
movement; the editable profile describes the base travel timing.

The graph shares cached evaluated samples across its visible channels and uses a
screen-sized sampling grid plus key/hold boundaries. Zooming or changing camera
data refreshes the samples; idle frames reuse them. These are display samples;
playback and export continue to use the native evaluator.

The speed-point graph also draws camera-key markers at their normalized moving
times, excluding holds and stationary spans just like native retiming. Labels use
classic indices; hover a marker for its index, demo tick, path time and ramp
percentage. Markers update after retiming, so their positions match the current
arrival times.

World-space key indices, path names, hold releases and segment durations use
world-position-based stacking. Only nearby 3D anchors share slots. Offsets stay
along world Z as the view rotates or moves; unrelated projected overlaps cannot
push a label into an empty area. All text continues through the classic depth-tested
renderer.

## Optional camera collision (TF2 / TF2C)

Open **Camera collision** in the Campath tab. **Free camera collision** and
**Campath collision** are independent and default off. **Attached camera collision**
is a third independent switch, also off by default. Campath collision
also applies to the shared preview camera. The equivalent console commands are:

```text
mirv_cam collision free 1
mirv_cam collision campath 1
mirv_cam collision attached 1
mirv_cam collision ignoreLocal 0
mirv_cam collision returnHalfLife 0.2
mirv_cam collision returnSpeed 300
mirv_cam collision attachedSoftDistance 24
mirv_cam collision radius 6
mirv_cam collision players 1
mirv_cam collision response slide
mirv_cam collision status
mirv_cam collision reset
```

Use `free 0`, `campath 0` or `attached 0` to disable each independently. Radius is the
half-width of a swept axis-aligned camera cube, from 1 to 64 world units. Clearance and player filtering are shared. Stop/slide applies to swept camera
movement; attached cameras also use the boom response described below.
`response stop` stops the requested movement at a solid; `slide` retains movement
along obstacle surfaces. Free/path angular and FOV animation are not changed. Free-camera
corrections feed back into its input position, avoiding accumulated movement
behind walls while retaining unobstructed camera smoothing and existing offsets.

This reuses TF2's `EngineTraceClient003` hull tracing with `MASK_SOLID`, including
solid map geometry and props. Player collision uses current client-side solid
hulls, not visible meshes or hitboxes. The engine's local player is ignored by default to
allow POV cameras to leave that player's hull. Use `ignoreLocal 0` to include it.
`players` controls other players independently: `players 0; ignoreLocal 0` still
collides with the local player. The subject of an attached-camera boom is excluded
from the pivot sweep, because
that sweep starts inside its hull. The final camera hull is checked against the
subject independently, even when `players 0` or `ignoreLocal 1` is set. Invisible
TF2 spawn-room team barriers are ignored; the physical moving door still collides.
Disable `players` to pass through other players while retaining
world/prop collision. Particles, non-solid models
and decorative surfaces have no solid collision representation. Camera clearance
does not guarantee that the near-plane edges cannot clip at every FOV/aspect.

Campath correction is a live view adjustment. Keys, handles, drawn paths and
native/classic XML path exports remain authored data; a live camera recording
sees the corrected view. This does not plan a new route around a wall or predict
where moving entities will be at other demo times. It uses previous corrected
positions during continuous playback, so start playback consistently before a
recording if reproducible collision response matters. Editor seeks, backward
movement, time/cursor jumps over 0.25 seconds, shot switches, and geometry edits
invalidate the old anchor. A new clear shot can start across a wall without
sweeping from the previous shot. Use `reset` for other deliberate camera jumps.

When a campath is held back by collision, its authored clock continues advancing.
After the obstacle clears, the camera now returns with exponential damping and
a speed cap, sweeping every return step. Defaults are a 0.2-second return half-life
and 300 units/s. This speed cap is the camera's absolute movement limit while
recovering, so a faster path can remain ahead until it slows or stops. Half-life
0 removes damping; speed 0 removes the cap. Setting both to 0 restores immediate
return. A paused path does not advance recovery. A forward stall while already
blocked does not discard that anchor and snap through the obstacle. Explicit
editor seeks and backward/cut resets still initialize a new shot.

### Attached camera follow and collision recovery

`mirv_cam collision attached 1` uses a boom trace from the stabilized attachment
pivot toward the desired camera position, plus a sweep of the camera's actual
movement through the existing stop/slide solver. Bone damping remains effective
through this stage: retraction does not mix raw head bob back into the camera.

`mirv_cam collision attachedSoftDistance 24` supplies an anticipatory soft zone
in world units (default 24, range 0..256, capped at half the current boom length).
Continuous approaches shorten the boom progressively before contact. Set it to
0 for direct boom contact. An obstacle entering the view from the side retracts
the camera gradually while its hull remains clear; physical contact still uses
an immediate safety correction. Clearing an obstacle restores boom length gradually
using `returnHalfLife` and `returnSpeed`; those controls do not cap the subject's
translation. The selected attach mode supplies the desired pose; enabled follow
coordinates the orbit and view as it approaches that pose.

The pivot sweep ignores the attached subject because it starts inside the
subject. Endpoint and movement checks include its solid hull independently of
other-player exclusions. If an endpoint falls inside the subject, the solver
looks for its exit along the chosen boom, rather than jumping along arbitrary
axes to find another viewpoint. A zero-offset camera inside its subject pulls
back along the camera heading when available (otherwise world -X). Tight gaps
may not have a valid camera-sized solution. Sudden geometry overlap can still
require an immediate safety move;
blocked continuous movement may also leave the subject occluded. This does not
plan an alternative route through a doorway or guarantee a full-body composition
in confined space.

```text
mirv_cam attach follow 0.15
mirv_cam attach follow boneHalfLife 0.08
mirv_cam attach follow orbitHalfLife 0.15
mirv_cam attach follow orbitSpeed 360
mirv_cam attach follow recoveryDelay 1
mirv_cam attach follow maxDistance 128
mirv_cam attach follow teleportDistance 1024
mirv_cam attach follow status
mirv_cam attach follow reset
```

Entity follow and bone motion damping are independent, optional critically damped
springs. Both default to 0 (rigid). `follow <seconds>` smooths entity translation,
including small jumps caused by missing demo snapshots. `boneHalfLife <seconds>`
smooths the bone's offset relative to the entity, softening animation such as
head bob without making the entity's whole movement more sluggish. A stationary
bone offset keeps its full height; this does not shrink the skeleton or move the
camera down to the entity origin. Larger values smooth more and introduce more
positional lag. Both springs reuse the existing campath modifier spring math;
they do not change `mirv_input cfg smooth` settings.

The camera keeps a filtered focus, orbit direction, boom length and screen-space
composition. Sharp entity turns therefore move the camera and its view together.
`orbitHalfLife` controls the orbit/composition response (default 0.15 seconds,
range 0..10); 0 restores immediate turning. `orbitSpeed` caps orbital turning at
360 degrees/second by default; 0 removes that cap. Both apply while root follow
is enabled. `follow 0` restores rigid orbit and view behavior even when collision
remains enabled. Bone translation damping remains independent.

Root and bone translation use the existing critically damped spring. From rest,
a half-life closes half a stationary positional correction. The orbit uses
quaternion interpolation over the shortest rotation, with consistent yaw wrapping.
The focus is filtered too, so missing snapshots do not immediately jerk the view
back toward the raw subject position. The subject can move within the frame while
the shot catches up; this does not promise an exact target lock for every FOV.
Larger half-lives introduce more lag. Use `mirv_aim` if an explicit target lock
is needed.

Bone/entity/aligned modes use the same coordinated framing, including captured
and manual offsets. Roll and intentional composition changes follow the orbit
response. An upside-down Euler solution is rejected for an upright view, and
near-coincident target directions retain the preceding view. A zero-offset
attachment smooths its authored view directly. Near a vertical sight line,
preserving roll can limit exact framing. Partial source-angle overrides are
respected. `world` mode retains its fixed heading.

An active `mirv_aim` override retains its selected target after final position
correction, including with manual camera offsets. The aiming controller advances
once per frame; the correction transports its resulting composition and does not
run the angular smoothing controller a second time. Source framing also yields
to the aiming controller during soft deactivation.

`maxDistance` bounds entity translation lag. Follow also smoothly limits lag along
the chosen offset using filtered velocity, targeting a quarter of its length,
and transverse lag to half its length. This avoids copying raw snapshot jitter
back into the camera when the limiter is active. A half-boom safety envelope
along the offset handles acceleration transients. This retains the selected
camera side relative to the smoothed orbit during backstepping; fast movement
is followed more tightly rather than letting the subject pass through the delayed
camera. Bone-position damping is applied independently. A zero-length
camera offset has no meaningful side constraint.

`teleportDistance` resets the shot when the **subject** moves farther than the
threshold between updates (default 1024 units; 0 disables it). It measures a
subject teleport, not camera separation. Explicit editor seeks, backward time,
attachment/entity changes, loading and gaps over 10 demo seconds also reset the
shot. Missing attachment poses invalidate history. All follow state advances in
demo time and freezes when paused or at timescale 0. Use `reset` to reframe while
paused. For repeatable recording, begin from the same earlier point and allow
preroll. Set both `follow 0` and `follow boneHalfLife 0` to disable smoothing.

A closed door can leave a solid camera with no route to its subject. With follow
and attached collision enabled, `recoveryDelay` bounds persistent blocked movement:
after 1 second of demo time by default, the camera cuts to a verified clear pose
reachable from the subject's pivot. An obstructed view across a thin door also
counts, even when the camera is only a few units from that pose. The whole shot
is rebased at the cut to prevent another catch-up jump. This deliberately relocates the camera; it
does not simulate travel through the obstacle. The old and new endpoint checks
include the subject hull. If no safe destination exists, it remains obstructed.
Set `recoveryDelay 0` to disable recovery cuts and retain strict collision, or
choose a delay up to 60 seconds. Paused rendering does not advance the timer.
Physical geometry overlapping the camera can still require immediate correction.

`attachMode entity` uses entity-facing angles at the bone position; `bone` uses
bone angles; `aligned` initially aligns with the entity and then follows bone
rotation. This distinction also applies after `mirv_cam capture`: entity
mode preserves the camera relative to entity facing, without inheriting bone
pitch, yaw or roll. Bone damping controls position, while bone/aligned modes still
inherit their chosen bone rotation, approached through the orbit response when
follow is enabled. The `world` mode follows the entity/bone position but keeps
the camera heading it had when attached. Its offset stays in that fixed orientation
rather than orbiting around the subject as the subject turns. Head bob and other
bone translation still affect the camera and can be softened with `boneHalfLife`.
A separate `mirv_aim` override can still change the final angles.

Set the orientation mode before attaching; changing it does not rebuild an
existing attachment. A model/class change rebinds a selected bone by its full
name, including selections originally made by index. If that bone is absent or
its original name was unavailable, attachment evaluation stops until a compatible
model returns or you select a new attachment. Model changes also reset follow
history and uncaptured aligned-mode calibration.

For example, replace the player and bone with your target:

```text
mirv_cam attachMode world
mirv_cam attach "Player name" "head" -60 0 0
mirv_cam attach follow 0.15
mirv_cam attach follow boneHalfLife 0.08
```

`mirv_cam capture` also works in `world` mode: the captured camera heading
and world-space offset stay fixed while the target translates. Recapturing the
current attached view preserves its authored pose and filter history, avoiding
repeated collision compression. Capturing a newly moved free camera starts fresh.
Later manual camera rotation/offset controls remain available. The Observing tab exposes the
orientation choice; the Attached camera follow section exposes smoothing and
blocked recovery controls. Collision switches and clearance remain in Camera
collision.

These attachment controls require `mirv_cam attach` to remain the active position
source, with all three source-origin components enabled. A later free-camera,
campath, preview or imported camera override takes precedence. Settings are
available alongside collision controls in the overlay and remain runtime
preferences rather than campath keyframe data.

A moving solid overlapping the camera triggers a bounded search for a nearby
clear position, up to 64 units away; complex corners may stop rather than slide.
Deeply embedded starts cannot always be resolved. If stuck, disable collision,
move to clear space, then enable it again. Loading/disconnected views suspend
collision. Preferences remain runtime settings rather than saved path data.
Other Source games reject enabling this TF2-specific trace adapter.

## Range modifiers

Open **Range modifiers** in the graph. Use **Draw modifier range** and drag across
the graph, enter exact path seconds or demo ticks, or use **Range from selected
keys**. The range can cover part of an interval or a hold; it is not restricted to
camera key boundaries. **Add noise to range** initially affects visible/focused
channels. Per-channel amplitudes can then be edited independently.

The **Modifier stack** lists every modifier in processing order, starting at index
0: the top entry is applied first, the bottom last. Click a row to edit it; **Move
up / Move down** changes the actual order. Disabled entries are gray. Matching
numbers/colors identify graph ranges; the selected range has a yellow border.
The graph shows at most four range lanes around the selected entry so a large
stack cannot cover the curve. Select another stack row to reveal its lanes.

To edit several numeric settings together, hold **Shift** and drag vertically
across their input fields. Selected fields have a yellow outline; the list scrolls
when the pointer reaches its top or bottom. Click one selected field, type a
value, and press **Enter** to set every selected field to that same value. Clicking
an unselected field replaces the selection. **Escape** cancels unfinished typing.
Selection stays within one modifier; range endpoints, seed and octave count
remain separate. Invalid values reject the whole edit, and Undo/Redo restores it
as one action. Numeric field edits commit on Enter, avoiding a calculation for
each intermediate digit. Ordinary modifier toggles and envelope drags remain live.

Choose **New modifier type**, then add it to the selected range. Existing modifiers
can change type through **Modifier type**. Noise and Oscillation amplitudes use
world units or degrees; Smooth, Spring and the timing modifiers use channel strengths from
0 to 1. The global **Modifier strength** multiplies every enabled channel.

| Type | Controls and behavior |
| --- | --- |
| Noise | Deterministic smooth noise; 1–8 octaves and roughness add smaller, faster layers. The weighted result is normalized to the chosen amplitude. One octave retains the earlier noise behavior. |
| Oscillation | Sine-wave position, angle or FOV offsets. Frequency is Hz; phase is in cycles. |
| Smooth / stabilize | A bidirectional exponential filter across the complete input curve, controlled by **Smoothing window (s)**. |
| Spring | A damped spring following the complete input curve. Frequency is Hz; damping below 1 oscillates, 1 is critically damped, and values above 1 settle without oscillation. |
| Expo timing | Shapes a positive travel rate across the chosen range. In accelerates, Out decelerates, In-out does both. Power 0 is neutral; 0–16 is supported. Fades, channel strengths and envelopes shape the rate; normalization keeps the range duration fixed without backward travel. |
| Bounce timing | Rebounds through the sampled range. Adjust cycles (up to 16), damping and In/Out/In-out direction. Reversals are intentional. |
| Elastic timing | Overshoots and oscillates through the sampled range. Cycles, damping and direction control the effect. Beyond-range progress extends the displacement between the range endpoints, instead of reading outside the path. |
| Value limits | Lower/upper bounds with an optional soft knee. Use full strength and zero fades for strict bounds, including both range endpoints. Soft width is at most half the bound span. |
| Slew / rate limit | Caps each selected channel's change per second. Position uses units/s; Euler angles and FOV use degrees/s. It limits components, not the total quaternion angular speed or total XYZ speed. |
| Stepped | Samples and holds each selected channel at the chosen Hz, aligned to the range begin. Use full strength and zero fades for discrete holds. |
| Repeat / ping-pong | Reuses the range's sampled movement for the chosen number of cycles. Ping-pong makes each cycle a complete forward/backward trip. Matching endpoint poses are needed for a continuous ordinary repeat. |
| Value remap | Applies `pivot + (input - pivot) * scale + offset` to selected channels. Useful for reducing roll, changing FOV or amplifying vertical motion without moving keys. |

The five control modifiers use the same stack, range, strengths, envelope and
history as existing modifiers. Zero fades let them affect both range endpoints;
partial strengths and fades blend toward the original signal and relax bounds
or rate caps. A partial-range slew limiter can require an entry/exit transition;
use fades to avoid a jump, accepting that the transition is not rate-limited.
Sampled slew reconstruction is linear so it cannot introduce an inter-sample
rate overshoot. These are value controls, not replacements for retiming.

Stepped boundaries and repeat effects share the bounded sampling budget. Very
long ranges or high frequencies can exceed its temporal resolution; classic XML
export also approximates abrupt jumps at its sampling resolution.

Console types are `limits`, `slew`, `stepped`, `repeat`, and `remap`. The additional
`modifier set` fields are `lower`, `upper`, `softness`, `rate`, `scale`, `offset`,
`pivot`, and `pingPong` (0/1). Frequency is Hz for Stepped and cycles per range for
Repeat. Select channels with their existing strength fields, e.g. `fov` or `yaw`.


**Modifier preset → Add preset to range** appends editable settings; it does not
replace the existing stack. Presets are Handheld, Impact shake, Expo in, Expo out,
Expo in-out, Bounce out (Z), Elastic out (FOV), Stabilize angles, and FOV punch.
Bounce out needs changing Z values; Elastic out needs changing FOV. For effects
on a stationary pose, use Handheld, Impact or FOV punch instead. Every setting
remains editable, and creation participates in ordinary Undo/Redo.

The control presets include descriptions in the chooser:

| Preset / console name | Example use |
| --- | --- |
| FOV soft bounds / `fov-safe` | Keeps FOV between 35 and 110 degrees with a 5-degree soft knee. |
| Pitch soft bounds / `pitch-safe` | Bounds pitch to -85..85 degrees with a soft knee; does not repair Euler winding. |
| Pan rate / `slow-pan` | Limits yaw changes to 45 degrees/s in the full-strength region; its fades relax the cap. |
| Stepped camera / `stepped-12fps` | Samples all camera channels at 12 Hz, independently of recording FPS. |
| Loop position twice / `loop-position` | Repeats position twice; matching endpoint positions are needed for a seamless loop. |
| Ping-pong position / `ping-pong-position` | Plays position forward and backward once while leaving angle/FOV timing intact. |
| Half roll / `reduce-roll` | Reduces roll by half with blended range edges. |
| Widen FOV / `widen-fov` | Adds 10 degrees of FOV with blended range edges. |

These are starter settings, not enforced recommendations. Channel selection,
strengths, fades and all type-specific controls remain editable after insertion.

Timing modifiers remap the input of their stack stage once across the entire
range, including any interior keys. They never restart easing at each key.
Channels with strength zero are untouched. Matching X/Y/Z strengths follow the
same sampled spatial curve inside the range; selecting axes separately can change
its shape. Elastic extrapolation can leave that curve. Holds and original key
arrival times can move within the affected channels; the authored keyframes and
handles remain intact. Outside the range and at its two boundaries, the modifier
returns the original stage input. Expo envelopes shape travel rate rather than
an additive offset: a zero envelope locally restores the original rate, while
progress can retain the offset accumulated earlier in the range.

The existing Spring modifier follows a changing signal; Elastic timing changes
when that signal is sampled. These are distinct effects. Timing modifiers do not
repair a discontinuity already present in the input interpolation.

```text
mirv_campath modifier presets
mirv_campath modifier preset expo-in-out 0 10
mirv_campath modifier set 0 power 8
mirv_campath modifier set 0 direction out
mirv_campath modifier preset fov-punch 2 3
```

Times above are path seconds, before the path offset. `preset` appends an entry;
use `modifier list` to find its index before editing. `direction` accepts `in`,
`out`, or `inout`. Bounce/Elastic `frequency` means cycles across the range, unlike
Noise/Oscillation/Spring frequencies in Hz. Native save/load, recovery and classic
XML baking include the new settings and evaluated effects.

Each modifier has begin/end, fade-in/out durations and an enable control. Noise
has a seed. Noise and Oscillation can apply positional offsets in camera space.
Filters act on the chosen world-position and Euler-angle channels. They evaluate
the whole path before applying their range envelope, so they do not restart at
each camera key. They can change the evaluated pose at camera keys and during
holds when the range covers them; the authored keys remain intact.

Open **Strength envelope** to draw the influence curve. Its X axis is normalized
range time (0–1) and its Y axis is strength (0–1). Drag points, double-click empty
space to add, right-click an interior point to remove, or reset the envelope.
Points stay inside the graph; Escape cancels the drag. Moving/resizing the
modifier range keeps the envelope normalized to that range. These control points
do not change camera-key indices.

Filters cache deterministic samples, interpolate them with shared Hermite
slopes, and reuse the result when seeking or rendering frames out of order.
Their sampling grid is bounded; very long paths and very rapid effects have a
finite filtering resolution. A path supports 64 modifiers, including up to eight
enabled sampled stages (all types except Noise and Oscillation). Modifier order determines each filter's input. Drag the boundaries and fade markers in the shaded strip
above the graph, or use exact numeric values. Envelope edges use smooth fades.
The dim curve is the unmodified evaluated path; the bright curve includes the
modifier, including movement during an explicitly covered hold. A stationary
base pose remains unchanged underneath. Modifiers apply in list order and can be
reordered, bypassed or removed.

Position noise can use world space or the camera's forward/right/up axes.
Rotational noise is composed as a local orientation offset; quaternion components
are not modified individually. Noise is deterministic at each path time, so
scrubbing and recording do not change it. Finite ranges, amplitudes, frequencies
and seeds are validated. Native files, collection files, undo and recovery retain
the modifier settings; classic export bakes their evaluated result. FOV noise
returns smoothly to the authored FOV at its range boundaries, including legacy
paths whose base FOV lies outside the normal noise limits.

```text
mirv_campath speedProfile list
mirv_campath speedProfile point 0 0
mirv_campath speedProfile point 0.25 1
mirv_campath speedProfile point 1 0
mirv_campath speedProfile reset
mirv_campath modifier noise 10 12 position 3 2
mirv_campath modifier list
mirv_campath modifier set 0 yaw 1.5
mirv_campath modifier set 0 fadeIn 0.4
mirv_campath modifier set 0 cameraSpace 1
mirv_campath modifier set 0 octaves 3
mirv_campath modifier set 0 roughness 0.5
mirv_campath modifier remove 0
mirv_campath modifier oscillation 10 12 roll 2 1.5
mirv_campath modifier smooth 10 12 position 0.8 0.3
mirv_campath modifier spring 10 12 angles 0.6 2
mirv_campath modifier set 0 type oscillation
mirv_campath modifier set 0 phase 0.25
mirv_campath modifier envelope 0 point 0.5 0.2
mirv_campath modifier envelope 0 remove 0.5
mirv_campath modifier envelope 0 clear
```

Speed-point times use 0..1 and speeds use relative nonnegative values. Modifier
range/fade times use path seconds; position amplitudes use units and angle/FOV
amplitudes use degrees. The console exposes the same validation and evaluator as
the graph. No separate camera system or renderer is introduced.

Console equivalents remain under the native command tree, for example:

```text
mirv_campath edit interp auto catmullrom
mirv_campath edit interp tension 0.2
mirv_campath edit interp position custom
mirv_campath edit interp rotation custom
mirv_campath edit interp fov custom
mirv_campath select #1 #1
mirv_campath edit tangentmode x both auto
mirv_campath edit tangent x both 20
mirv_campath edit tangentweight x both 1
```

`edit interp auto spline|catmullrom|monotone|minimumjerk` chooses the profile and enables
Custom interpolation on all channels with one command. `edit interp tension`
accepts 0..1. The existing per-channel interpolation commands remain available.

Tangent commands accept `x|y|z|fov|pitch|yaw|roll` and `in|out|both`, applying to the
selection or to all keys when none is selected.

## Named paths and holds

The Campath tab selects one named path for editing, with New, Duplicate, Rename
and Remove controls. Native keyframe indices remain zero based within that path.
**Campath enabled** is the shared playback switch. Playback automatically chooses
the evaluable path with the latest start at the current demo time; ties go to the
later path in the list. Selecting another path changes the editing target, not
playback priority. All paths use ordinary native keys and the classic drawer.
Each path has **Disable playback**, **Play only marked paths** and **Visible in world**.
Duplicates start disabled. Disabled paths are excluded; when any paths are marked,
only marked paths are eligible. Disabling the only marked path does not silently
enable other takes. Visibility controls world drawing independently. The existing
console commands and file fields still use `mute` / `solo` for compatibility.
These flags save with both single-path files and collections and participate in
undo/redo.

Names, hold releases and interval durations are drawn with the classic text
projection and depth test, using the existing GUI font atlas for full text.
**Key label size** also scales these labels; zero hides text. Indices and extended
labels share world-position-based stacking, with offsets along world Z.
**Path names**, **Hold labels** and **Segment durations** can be hidden separately
in the overlay's **Campath** tab. Turning off **Segment durations** hides only the
interval tick/second labels; it keeps the path and other labels visible.
World projection and depth testing remain in the classic drawer; no second
world renderer is used.

In the inspector, set **Hold (seconds)**, **Capture hold release** at the current
demo time, or **Clear hold**. A held key keeps its complete pose from arrival until
release, then travels to the next key by its arrival time. Release cannot pass the
next arrival; sharing it is allowed only for an identical pose. Smooth custom
interpolation arrives and departs with zero velocity. Linear remains linear.
Adding a hold converts Classic cubic/quaternion channels to Custom, which supports
endpoint derivatives. This is separate from classic `mirv_campath hold`, which
continues to hold a whole path outside its bounds. The Campath tab exposes it as
**Hold first / last pose outside path**. In **Playback / cuts**, endpoint extensions
appear dimmed and their tooltip says **endpoint hold**; the movement span ends at
the last key (including its explicit hold duration). A single key with a hold is a
playable stationary shot.

```text
mirv_campath path list
mirv_campath path new "Second shot"
mirv_campath path duplicate "Alternate shot"
mirv_campath path use 0
mirv_campath path rename "Opening"
mirv_campath path mute 0
mirv_campath path solo 1
mirv_campath path visible 0
mirv_campath path remove "Alternate shot"
mirv_campath insert
mirv_campath insert 12.5
mirv_campath keyHold 1
mirv_campath keyHold 1 14.0
mirv_campath keyHold 1 clear
mirv_campath retime 10
mirv_campath distanceTiming 1 position
mirv_campath distanceTiming 1 pose
mirv_campath distanceTiming 0
mirv_campath save "opening.xml"
mirv_campath save "sequence.xml" all
mirv_campath load "sequence.xml" all
```

`insert` without a time uses the active preview cursor. `keyHold` without a release
captures the current demo time; an explicit release uses path seconds. Path
selection/removal accepts a zero-based path index or a quoted name. Undo restores
path creation, removal, renaming and camera edits across the document.

## Preview shortcuts and seeking

The shortcuts below work in native preview with the overlay closed, and in the
open editor when a text field is not being edited:

| Key | Action |
| --- | --- |
| E | Capture pose and current demo time |
| Ctrl+E | Insert the interpolated pose at the preview cursor (cached cursor during cache preview) |
| Shift+E | Replace the one selected key's pose, keeping its time and hold; changed rotation handles rebuild |
| Y | Capture the selected keys' hold release at current demo time |
| Shift+Y | Clear selected holds |
| Delete | Delete selected keys, preserving path offset |
| Ctrl+A | Select all keys |
| Ctrl+Shift+A | Anchor the selection from its first selected key to the current camera, using HLAE's existing anchor command |
| Right Ctrl alone | Seek to the preview cursor on release |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Up / Down | Native preview: next / previous key |
| Home / End | Native preview: first / last key |
| Left / Right | Native preview: scrub, or step keys if interpolation is unavailable |

Using Right Ctrl as part of another chord does not also seek. Shift remains
available for Source's Shift+F2 demo UI. The Campath tab also provides capture,
replace, select-all and anchor buttons.

**After seeking** chooses Timescale, Pause or Continue (1x), with **Timescale 0**
as the default. This applies to preview seek, Goto Start and timeline scrubbing.
The seek temporarily resumes at 1x, asks Source to pause at the target, and applies
the chosen behavior after arrival. Setting timescale before resuming at the target
keeps zero-timescale models and particles frozen. Seeking does not move the
independent preview cursor.

```text
mirv_campath preview afterSeek timescale 0
mirv_campath preview afterSeek timescale 0.25
mirv_campath preview afterSeek pause
mirv_campath preview afterSeek continue
```

The mode and timescale persist in the existing ImGui settings file. Source's
console and menus retain input priority.

## TF2 player movement interpolation

Open **Demo fixes** in the Campath tab and enable **Smooth player movement**.
The suggested window is 0.1 seconds; increase or decrease it while comparing the
same portion of the demo. Console equivalents are:

```text
mirv_fix playerInterpolation 0.1
mirv_fix playerInterpolation 0.15
mirv_fix playerInterpolation status
mirv_fix playerInterpolation default
```

This controls TF2's existing `cl_interpolate`, `cl_interp` and `cl_interp_ratio`
settings. It targets uneven movement caused by disabled or insufficient
interpolation. It adds presentation delay and cannot reconstruct missing demo
snapshots, remove genuine teleports, or repair limb-animation state.

The first enable captures the original settings. Changing the window keeps those
restore values. **default**, disabling the checkbox, or ending demo playback
restores settings still owned by the control; later manual cvar edits are kept.
Rejected/clamped engine settings roll back. The control is available only for a
TF2 demo with the required engine variables; it does not bypass engine restrictions.
It is off by default and does not alter seeking or set a persistent config preset.
Its visual effect still needs validation against the user's stuttering demo.

The control remains active while TF2 reports a loading screen, including the
temporary playback gap during a backward-seek reload. It checks for demo ending
and manual cvar overrides after loading finishes; it does not force settings back
over a changed config. New window edits are rejected during loading, while
**default** still restores the owned settings. A rejected restoration remains
active so it can be retried. Manual edits made immediately before changing the
window become the restore values for those fields.

Use **Interpolation status** in Demo fixes, or the `status` command, to print
the requested setting, actual `cl_interpolate`/`cl_interp`/`cl_interp_ratio`
values, matching/overridden/unavailable state, and loading/STV/POV context.
These are cvar readings, not measured visual delay: updaterate constraints,
prediction and per-entity rules can alter interpolation. The SDK takes different
paths for locally recorded POV and non-local demos; increasing `cl_interp` does
not guarantee the same result for both or for the locally predicted player.

For a visual check, replay the same section at the same timescale using default,
0.1 and 0.15 seconds, checking status after each change and after a backward seek.
Repeat with STV and POV demos. Compare movement smoothness and presentation delay;
this control cannot replace snapshots missing from the recording.

## TF2 / TF2C POV death/freeze camera suppression

In **Demo fixes**, enable **Suppress POV deathcam**, or use:

```text
mirv_fix povDeathcam 1
mirv_fix povDeathcam status
mirv_fix povDeathcam 0
```

Enable it before the recorder dies. The control resolves `DT_BasePlayer`'s
`m_iObserverMode` receive proxy from TF2's VClient017 interface, then forwards
local POV deathcam/freezecam updates as roaming mode through the original proxy.
The SDK normally selects `FreezeCam_Only` on entering freezecam and subsequently
requests a frozen render frame. Suppressing that transition avoids both entry
effects while HLAE's free camera or campath continues overriding the view. Without
an HLAE camera override, the native roaming camera is the fallback.

Only the local recorder during non-HLTV TF2 or TF2C/Classified demo playback is affected. Other
players, live play, STV, ordinary observer modes and other Source games forward
their original data. The demo file and audio-mixer cvars are left intact. Unsupported
client interfaces, missing receive metadata and failed hook installation reject
enabling. It is off by default; disabling leaves the hook forwarding normally.
The enabled preference persists across demo seeks/reloads and remains inert
outside POV playback.

This is preventative: it affects incoming observer-mode updates. Enabling after
the screenshot has already frozen does not immediately clear that existing
frame; restart playback or seek before the death to test it. Disabling similarly
takes effect on subsequent incoming observer updates. Check `status` after the
death: its blocked-update count should increase. The hook/controller and ABI
are tested with engine stubs and Windows cross-compilation; visual/audio behavior
still needs validation in a complete TF2 installation.

## Overlay render isolation

The overlay draws into the swapchain backbuffer immediately before Present,
after Source's world and menu scenes. Offscreen game textures never receive
overlay geometry. If necessary, drawing temporarily selects the backbuffer and
restores the previous render target, depth surface and viewport afterward.
Device/target-query failures skip drawing. A window-thread frame request keeps
the overlay interactive in menus where normal client render updates stop.

Rendering explicitly captures the DX9 state block and backs up/restores world,
view and projection transforms separately, matching ImGui's DX9 backend's driver
workarounds. Failed capture/transform queries skip the draw. Regression tests
model offscreen targets, omitted transforms, non-default shader/texture/sampler
states, failure paths and surface-reference ownership.

Relaunch the game after installing this fix; closing the overlay cannot undo
pixels an older build already wrote into a cached game texture. Full in-game
validation is still needed for the affected TF2C build.

## Rotation authoring and classic export

Camera captures (`mirv_campath add` and **Add point**) align their physical
orientation with the existing Euler curve, including after tangent edits or
rotation-backend switches. Accumulated free-camera angles are not interpreted as
new revolutions. Pitch-fold crossings use equivalent Euler representations, and
yaw/roll at a pitch pole keep the nearest continuous combination. Explicit turns
authored in the graph or loaded from XML remain intact. This does not clamp
intentional Free tangents or overshoot from the chosen interpolation.

Custom Euler interpolation and numeric editing can express complete turns: for example, yaw 0 to yaw 720 gives two turns. Editing another
angle component retains the untouched winding; rotation and anchor transforms
retain an equivalent Euler representation near the authored angles.

Rotate and Anchor also transform effective position velocities and accelerations,
baking automatic positional tangents when needed to preserve the shaped curve.
For a rotation that mixes XYZ axes, varying axes must have compatible handle time
weights. Otherwise the transform is rejected without changing the path; align
those weights first. Axis-permutation rotations can preserve differing weights.
This preserves positional geometry; Custom Euler angle curves still interpolate
between the transformed key orientations and do not promise an identical rotated
orientation at every intermediate time. Quaternion
Linear/Classic modes continue to use quaternion orientation, which does not encode
a turn count; use Custom rotation for deliberate spins.

**Export classic XML** exports the selected shot, including a muted alternate,
as sampled ordinary HLAE linear-position/FOV and spherical-linear rotation keys.
Choose **Sample FPS** (default 120). Original key and hold-release times are
included, and extra rotation samples preserve fast complete turns. Enabled noise
ranges receive additional samples at boundaries/fades and at least eight samples
per noise lattice interval, subject to the export limits. The file has
no custom tangents, per-key hold or distance-timing extensions. The editing path,
its indices, native save target and savepoint remain unchanged. Exports are bounded
to 100,000 samples and the native reader's 64 MiB file limit; an excessive request
fails without replacing the destination.

```text
mirv_campath exportClassic "opening-classic.xml" 120
```

This is a sampled approximation, not an editable copy of the original curve.
Increase sample FPS for fast or sharply curved movement. The exported file has
additional keyframes and can be used by older HLAE versions. Save path / Save all
remain the formats for retaining the editable project. Export is for one selected
shot; collections retain their own native format and playback rules.

## Gizmos and observing

Right-click a keyframe marker and choose **Edit** to open its viewport gizmo.
Right-clicking empty timeline space instead opens the tick-seeking menu.
**Show Gizmo** also toggles the gizmo manually. **G** selects
translation and **R** rotation when a text field is not active. Local/world modes
and Ctrl snapping are available. **C** toggles free-camera control. Camera Control
also provides FOV, roll and keyboard speed sliders.

The **Observing** tab is a player list with a text filter. Clicking a player
only changes the selected row; it does not create an HLAE camera, detach an
existing one, or switch the spectator view. In STV demos, the explicit
**Spectate selected (STV)** action targets the player's `#userid` and switches
to TF2's in-eye spectator mode. POV demos cannot change their recorded native
spectator target. A separate POV spectator view override (without a persistent
camera rig) is not yet implemented.

The selected player still exposes named bones and forward/left/up offsets below
the list. Choose a **Bone** (or **Entity origin**) and use **Attach selected** for
the existing TF2 attachment system, including in POV demos. Fly to a composition
and use **Capture relative framing** to follow the target from that composition.
**Detach** clears the attachment.

**Trace entity** selects the entity under the camera crosshair, including non-player
entities. Aim the camera before opening the menu. Walls and static map geometry
block the trace. The panel reports hits, misses and unavailable engine hooks.
This uses TF2's `EngineTraceClient003`; the original console command
`mirv_traceentity` is CS:GO-specific in this HLAE base.

### Cached camera availability and missing targets

**The currently active camera is evaluated only against the sampled scene at
the current preview time.** An attached camera becomes unavailable when its
recorded target or required bone data is absent and becomes available again
when scrubbing back to a timestamp containing usable target data. Do not
substitute a pose from an earlier or later scene frame to report the current
camera as available.

The Scene Cache records time-stamped entities by stable identity; it only
interpolates across adjacent frames with a matching identity and within the
configured gap and travel limits. Player filters, off-screen/first-person
visibility, missing captured bone matrices, and recording gaps can make a
camera unavailable in cached preview even if it worked in live playback.
For bone-attached cameras, capture player model poses as needed. Aiming
cameras may also have independent, recorded camera samples.

**Transition test revision:** The outgoing-camera fade fallback is
reapplied in the transition evaluators only. When an outgoing camera disappears,
live evaluation can hold its last pose while the configured fade advances.
Cached preview can re-evaluate the outgoing camera using earlier recorded
entity frames with the current editable camera settings. This affects only
the outgoing blend contribution, not camera availability or Scene Cache data.
If no outgoing pose can be recovered, the incoming source continues directly.

**Capture note:** Avoid fast-forwarding with scene-cache recording enabled:
discontinuous capture intervals can leave missing preview coverage. Stop
recording, seek, clear the history, then begin recording at the desired start
point. This is a workaround, not an automatic seek-handling fix.

## Recording, files and settings

The Recording tab now reads the live HLAE recording settings registry, including
user-created profiles, and shows the current screen profile even when configured
from the console. Encoder presets are no longer hard-coded in the UI. Capture FPS
remains an editable text field with no preset buttons; it sets both
`host_framerate` and HLAE's input FPS override, not a sampler's output FPS.

### Recording profile editor (PR #1)

Expand **Recording profile details / editor** after choosing a screen profile.
Built-in `afx*` profiles are protected and read-only. Use **Duplicate as editable
profile** with a unique name to copy a built-in sampler or FFmpeg preset into
the same live settings registry. Custom sampler profiles expose **output FPS**,
sampling method (rectangle/trapezoid), exposure, cross-frame strength, and the
underlying output encoder/settings profile. Custom FFmpeg and FFmpegEx profiles
expose their raw HLAE FFmpeg options. **Apply profile changes** explicitly
commits a validated edit to the actual profile; **Revert unsaved edits**
re-reads its last committed settings. Recording must be stopped to switch,
edit, duplicate, or delete profiles.

Custom recording profiles, the `afxDefault` output route and the selected screen
profile now restore across sessions from `%APPDATA%\HLAE\RecordingProfiles.txt`.
Console and overlay edits save the same registry automatically. FFmpeg, FFmpegEx,
sampler, multi-output and interleave dependencies are preserved. Invalid files are
left intact and automatic overwriting is suppressed; move the file aside and restart
to reset preferences. Pass selections remain session settings.

For example, Capture FPS **480** with a sampler configured for Output FPS **60**
gives a nominal **8:1 capture-to-output frame ratio**. The sampler's exposure
and strength determine temporal blending; the output encoder is independent
of the input capture rate. A sampler may reference another profile for output,
but indirect dependency cycles are rejected. A selected custom preset can
be deleted once switched back to `afxDefault`, provided no other profiles
still reference it.

**Custom recording profiles are session-only**, matching the existing
`mirv_streams settings add/edit` registry behavior. Save their creation/edit
commands in a startup configuration such as `autoexec.cfg` to recreate them
across launches. The overlay's recording hotkey and behavior preferences,
unlike profile definitions, persist in its ImGui settings. The editor does
not change the HLAE sampler implementation or force a Source game to render
more frames than the engine can produce.

Start/Stop and the configurable keyboard recording hotkey use the same
recording routine. Click the hotkey field, press a key (optionally with
Ctrl/Shift/Alt), use Escape to cancel or Backspace to clear. F8 stays reserved
for opening the overlay; unsupported system shortcuts are not captured.
The hotkey is disabled by default; it works when the overlay is closed,
but is ignored while entering text or using the game console.
Options include automatically resuming the demo on start, pausing on stop,
closing the overlay on start, and stopping the recording when opening the overlay.
These options and the hotkey persist in the overlay's ImGui settings.

TF2 x64 multi-stream separation (world, HUD, viewmodels, players and depth)
is **not implemented** in this change. The legacy HLAE render-view stream
classes are behind 32-bit build guards and need a validated TF2 x64 hook port.

The Recording tab uses Source 1 screen recording through `mirv_streams`. Choose a
record directory, FPS and screen profile. TGA, HLAE's built-in FFmpeg profiles,
ProRes and x264/x265 profiles are available. Start Recording closes the overlay;
reopen it with F8 to stop. Recording uses the chosen screen profile directly.
FFmpeg profiles require HLAE's normal FFmpeg installation.

**Save path** / **Save path as** write ordinary native XML for the selected path;
**Save all** / **Save all as** write a named-path collection. Open the corresponding
format with **Open path** or **Open collection**. Files retain custom tangents,
weights, holds, Euler winding and distance timing. Old classic files still load.
Save appends `.xml` when needed. Undoing authoring changes after Save As retains
the current save destination. Removing the final shot creates a clean placeholder
with playback/visibility flags reset. Classic whole-path hold remains available
before its start even when an unused second path exists. Failed loads preserve current work and saves use
an adjacent temporary file before replacing the target. Collection XML requires
this branch; older HLAE versions do not understand its additional features.

Native and collection saves reject invalid path data, including nonfinite offsets.
Loading an invalid offset leaves the current path intact; console offset changes
also reject malformed or nonfinite numbers.

An asterisk marks unsaved edits. Idle dirty checks reuse immutable snapshot
comparisons; selection and playback enable do not count as authored changes.
Unchanged documents do not repeatedly rewrite recovery backups, and clean idle
frames do not touch the recovery filesystem. Periodic recovery waits until a
mouse drag finishes. Actual changed-document saves still serialize synchronously;
very large documents or slow storage can delay a save.

Periodic recovery checks run every 30 seconds by default. Changed work is written
under `%LOCALAPPDATA%/advancedfx/camera-recovery` (or HLAE's folder when that
variable is unavailable). Closing the overlay or loading another file also flushes
changed work. Settings controls the periodic backup and interval. Backups do not overwrite
source files or mark changes saved. The Campath tab offers **Recover latest
backup** and **Discard latest backup** when older backups exist. Recovery forces
Save As; older backups remain until discarded. Undo history itself is not persisted.

Settings include a demo browser, UI scale, fonts and docking. Availability of the
extra fonts depends on Windows. UI preferences, panel layout and last-used
folders are stored in HLAE's existing `AfxHookSource_imgui.ini`. Key label size and label visibility are
adjustable in the Campath tab using the classic world-space index renderer.

[Port inventory and source attribution](../shared/overlay/PORTING.md)

## Validation

```text
cmake -S tests/cinematic -B build/camera-tests
cmake --build build/camera-tests --config Debug
ctest --test-dir build/camera-tests -C Debug --output-on-failure
```

On Windows select `-A x64` or `-A Win32` with separate build directories. On Linux,
the headless GUI integration test also runs the actual editor and bundled widgets:

```text
python tests/cinematic/test_overlay.py --imgui-dir <path-to-imgui-v1.89.9-docking>
```

Mouse editing records its owning path and revision. Capture, hold, selection,
undo, path switching and external edits invalidate an older drag, so a subsequent
mouse update or Escape cannot restore stale camera data. Incoming/outgoing
handles use one validated native edit, and tangent lookups use cached key indices.
World-path drawing still uses the classic renderer and simplifier, with adaptive
sampling; FOV-only and offset-only changes can reuse positional samples.

These checks support review; the complete MSVC build, in-game input handoffs,
gizmo axes/projection, device resets and recorded output still require testing in TF2.

Selection changes keep trajectory and distance caches intact; the drawer updates
selection colors without resampling geometry. Minimum-jerk curves use a cached
banded solve rather than solving again on each evaluation. GPU performance and
high-frequency world-path drawing still require measurement in TF2.

## POV recorder sound mute

In the Campath tab, open **Demo fixes** and enable **Mute recorder sounds**.
Console equivalents:

```text
mirv_fix recorderSounds 0
mirv_fix recorderSounds status
mirv_fix recorderSounds 1
```

`0` mutes and `1` restores normal recorder audio. This is off by default and
requires a playing TF2/TF2C POV demo. It mutes active sounds attributed to the
recorder, the SDK's local-player sound source, and entities whose ownership chain
leads to the recorder (such as weapons). Other players, world sounds and UI audio
with other emitters retain their volumes. It does not reposition sounds, change
the global mixer, mute microphone voice chat, or modify the demo file. Sounds
recorded as world/UI emitters cannot reliably be attributed to the recorder.

New sounds are rejected at the engine's common sound-start function before a
channel can be queued or mixed. This covers both client-generated and recorded
network sounds. Stop requests, microphone voice channels and anonymous static
ambient audio pass through. The hook is resolved and checked against the
versioned sound interface; if this engine layout is unrecognized, enabling the
mute fails with a status message. There is no per-frame-only fallback.

Already-playing recorder sounds are silenced by GUID. Disabling restores the
previous volumes of surviving sounds still owned by the mute. Sounds prevented
from starting have no channel to restore: rewind/replay to restart any such
loops after disabling. Native volume changes observed while muted become the
new restore values. Finished sounds are discarded. Ending playback or entering
STV playback disables the mute; loading during a backward seek temporarily
suspends active-channel checks. It is not saved as a persistent overlay preference.
`status` reports both active muted channels and the number of blocked starts.

## Sequencer file and command controls

Automatic event indexing accepts a name relative to the game directory (for
example `example.dem`) or prefixed with that directory (`tf/example.dem`), as
well as an absolute path. A missing file produces a useful status message and
can still be opened with **Open demo...**.

Click a command marker on the **Commands** timeline row to select it and open
its command editor. Drag it horizontally to change its tick without moving
camera keys. Right-click a marker to seek to its tick or use the lead-in.
Editing or dragging does not execute the command immediately.

In the Campath tab, **Choose path folder...** selects a directory for path and
collection file dialogs. The folder is displayed and remembered across sessions.
**Save path as** / **Save all as** choose a new destination; **Save path** /
**Save all** continue to update their current files. The folder browser also
supports creating directories, editing the full directory path, and Enter to
confirm the current directory.

## Scene Cache visual preview

Scene caching is part of **HLAE Sequencer**. Open its **Scene Cache** details tab
and press **Start recording**, then play forward through the shot. Capture
continues with the overlay closed. Pause/freeze actual demo playback, enable
**Cached preview** above the timeline, and scrub the sequencer playhead or use
**Go to tick**. These actions preview cached history without seeking the demo.
**Seek actual demo here** explicitly seeks the real demo to the selected tick.
The Scene Cache track shows recorded coverage and gaps; its gray marker shows
actual demo position independently of the cached playhead.

Enable **Show path** and keep the active path visible for cyan player boxes and
yellow heading lines drawn by the classic renderer. Cached wireframe
poses use the engine's studio renderer and do not require **Show path**. Live
models remain visible.
When **Preview path** is active, cached navigation also evaluates the camera path
using the recorded client timestamp. The separate camera-preview slider is
available only outside cached mode. Capture actions and `mirv_campath add` use
cached client time minus path offset; missing coverage rejects capture instead
of silently using live demo time. Replacing a selected key retains its time.

Recording, clearing, payload budget and usage statistics live in the sequencer's
Scene Cache details tab. The default budget is 64 MiB; oldest frames are evicted.
The payload estimate excludes allocator bookkeeping. The cache stores player snapshots and optionally visible projectile snapshots per
observed demo tick. Dormant entities are omitted. Native respawn boundaries are
not yet classified; do not assume every historical pose is a valid live state.
Sparse compression and trajectory guides remain pending.
Boxes use model bounds rather than animated silhouettes; opt-in body poses are
described below. Samples separated by
more than 0.1 seconds or 256 units are not bridged. Opening/restarting a demo
clears history; backward capture begins a new interval. Historical audio and
simulation state are not restored.

### Cached navigation commands and binds

These commands use the sequencer playhead and work with the overlay closed:

```text
mirv_cache record 1
mirv_cache preview 1
mirv_cache goto 90000
mirv_cache step -1
mirv_cache step 1
mirv_cache status
mirv_cache preview 0
```

`goto` requires a tick inside the retained bounds; gaps can still have no scene
coverage. `step` clamps to the retained ends. Repeated `preview 1` keeps the
current cached playhead. These commands do not pause, resume or seek the actual
demo. Freeze actual playback separately before inspecting history.

With cached preview active, hold **Right Ctrl** for cache navigation:
**Left/Right** step ticks (including keyboard repeat), **Up/Down** step one demo
second, and **Home/End** select the retained ends. With the overlay closed,
arrows step immediately, then repeat from the preview frame clock after a
300 ms delay (about 30 steps/second), independently of OS key-repeat messages.
Releasing either key stops repetition; focus/input handoff and Source menus
cancel it. Long frame stalls have a bounded catch-up. These chords yield to
menus/text input. Releasing the chord does not seek the actual demo.

Unmodified keys retain classic path-preview behavior: **Up/Down** jump to and
select camera keyframes, **Home/End** select the first/last camera key, and
**Left/Right** travel along the camera path. Camera navigation also updates the
cached scene playhead using recorded timestamps. Jumps preserve exact keyframe
times, including sub-tick times; outside retained coverage no cached entities
are shown.

Equivalent single-step Source bindings, when HLAE does not own keyboard input:

```text
bind LEFTARROW "mirv_cache step -1"
bind RIGHTARROW "mirv_cache step 1"
```

For continuous scrubbing, first enable cached preview, then use ordinary Source
press/release aliases. Here 66 ticks/second is the scrub speed, independent of
actual demo timescale:

```text
alias +cache_back "mirv_cache rate -66"
alias -cache_back "mirv_cache rate 0"
alias +cache_forward "mirv_cache rate 66"
alias -cache_forward "mirv_cache rate 0"
bind F6 +cache_back
bind F7 +cache_forward
```

`rate 0` stops scrubbing. Disabling cached preview, clearing history or restarting
a demo stops it as well. Frame stalls do not cause an unbounded cursor jump.
Bindings are examples and replace whatever actions those keys currently have.


The built-in Right Ctrl navigation chords remain reserved. Use unreserved keys
for custom rate bindings; normal preview/editor input ownership still applies.
Input release or focus handoff stops an active scrub rate.

### Player, projectile and attached model poses

In the sequencer's **Scene Cache → Capture** section, enable **Player models**
(default scope: all players). **Player filter (optional)** can narrow player
bodies and held gear to any set of players. Enable **Projectiles**, **Weapons**,
**Cosmetics** and the other categories as wanted, then **Start recording** and play
forward through the shot. Freeze actual playback, enable **Cached preview**,
and scrub backward through the covered interval. **Draw cached models** shows
the recorded wireframe poses with the overlay closed and **Show path** disabled.
Live models remain visible, so at the latest tick the wireframes can overlap
them. Players are cyan, projectiles red, weapons green, cosmetics purple,
ragdolls gray, and gibs orange.
On engines without the verified
`VMaterialSystem082` interface, model drawing falls back to the default studio
wireframe; proxy boxes still use the category colors.

Console equivalents:

```text
mirv_cache players all
mirv_cache projectiles 1
mirv_cache weapons 1
mirv_cache cosmetics 1
mirv_cache record 1
// Play forward, then freeze actual demo playback.
mirv_cache record 0
mirv_cache preview 1
mirv_cache step -66
mirv_cache status
```

`mirv_cache players <indices...>` enables body capture for one or more player
entity indices, for example `mirv_cache players 1 4 7`.
`mirv_cache players none` (or `players 0`) disables player poses while
retaining player boxes.
`projectiles 0` excludes projectiles from future captures. `weapons 0|1` and
`cosmetics 0|1` capture visible third-person gear for the selected player, or all
players in **All players** mode. Loose weapons/cosmetics capture independently
of that player selection, including when **Player models** is off. Held gear
uses the player filter even with body capture off. Both options default off.
Corpse-attached gear requires its ragdoll to be captured. Gear options affect
future frames and increase memory use. Hidden inventory weapons, first-person
viewmodels/arms and particle-only cosmetic effects are outside this pass.
`mirv_cache models 0|1` controls model drawing independently of capture.
Changing capture options affects future frames only; record the interval again
for missing poses. All-player and category preferences persist across demo
changes. Opening/restarting a demo clears history and any individual player
target. Individual selection pins the user's ID, and projectile identity includes
its entity-handle serial number, preventing a recycled slot from connecting two
projectiles. Missing entities are not interpolated across spawn/despawn gaps.
Gear records its stable owning-player ID as well as its own serial-aware entity
handle. Ownership changes interrupt interpolation. Gear whose player snapshot
cannot be bridged is omitted from the interpolated preview rather than left
floating. Native `SetupBones` captures the observed bone-merged world pose;
no live owner/bone pointers are retained or rebound while scrubbing.

More models consume **Scene budget MiB** faster. The UI shows retained span and
payload usage; raise the budget for a longer interval if needed. The default
remains 64 MiB. This milestone captures visible networked studio projectiles;
particles, trails and explosions are not historical projectile models.

The cache stores immutable model names/checksums, bone hierarchy metadata, body
groups, recorded bone names and observed transforms. It reuses the game asset through
`VStudioRender025` / `MDLCache004`; it does not store a new mesh in every frame.
AGR's existing transform helpers and model/bone conventions are reused without
starting/stopping AGR or changing recording mode. Bone capture runs for
the enabled categories, once per newly observed demo tick, and requests a
`SetupBones` evaluation. Hidden/dormant players, unsupported headers, invalid
matrices or missing render interfaces produce an explanatory status and keep
proxy history available. Source can report its first-person local player as hidden;
choose a visible player for the initial test.

**Smooth cached animation** is enabled by default. Between observed ticks it
interpolates parent-relative bone rotations using Source's normalized quaternion
blend for animation frames (shortest-sign alignment, linear blend, normalization),
with linear translation and scale, then rebuilds the hierarchy. This preserves
limb lengths through rotations instead of blending world-space matrices.
Gear shares the owning player's interpolated merge-enabled bones by recorded
bone name, so matching hands, weapons and wearables follow the same motion.
All transforms and relationships come from the cache, including during reverse
scrubbing; live animation state is not consulted.

Disable **Smooth cached animation**, or use these commands, to compare against
nearest recorded poses without recording again:

```text
mirv_cache animation nearest
mirv_cache animation smooth
```

`cache rate` retains fractional ticks for smooth playback forwards/backwards.
Right Ctrl + Left/Right taps still step by one tick; Up/Down taps step by a
second. Holding an arrow after the initial 300 ms delay advances continuously
at the previous repeat speed, including fractional ticks. Opposite arrows
cancel, and release/menu/focus handoff stops held scrubbing. This works with
the overlay open or closed. Playhead dragging in cached mode also retains
fractional ticks and draws the pointer at that position; release preserves the
fractional time. Exact tick entry and camera-key selection remain discrete.
Normal demo seeking keeps its existing behavior; authored keyframe times are
unchanged.
Stepping directly between recorded ticks still displays the original samples
in both animation modes.

Capture prepares compact local transforms once per observed pose. Unsupported
local shear, missing parents or singular parent transforms preserve only the
affected bones. The supported bones continue blending. Parent-relative
exceptions follow the interpolated parent; a bone without a usable parent
keeps its recorded world transform, retargeted with the entity's root. Scale
handedness changes also preserve only that bone. Body-group values switch
at the nearer sample without disabling skeleton interpolation. Model changes, missing samples, excessive gaps/travel and owner
changes retain their existing safeguards. Smoothing does not repair bad demo
samples or synthesize missing simulation, particles, audio or facial flexes.
Use **Scene Cache → Diagnostics → Animation diagnostics** or `mirv_cache status`
while slow scrubbing to inspect each model's actual blend weight, blended and
preserved bone counts, and exception bone names/reasons. A recorded/nearest
pose at a whole tick is expected. A world-only pose requires a fresh capture.
Exceptions are limited to their affected bones; the diagnostics do not claim
that every model has the same amount of supported interpolation.

Native life-state/respawn boundaries are still not classified. This remains a
visual preview, not restoration of engine simulation.

Pose payload and shared model metadata count toward **Scene budget MiB**.
Each model descriptor and each unique pose allocation is charged once while
referenced by retained frames; its charge is released when the last such frame
is evicted or history is cleared. Exactly unchanged poses from the same stable
entity, category and owner share storage across adjacent recorded ticks. Changed
roots, bones, body groups and per-bone exceptions remain separate. The counter excludes engine asset memory, allocator
bookkeeping and temporary published/queued snapshots. Oldest frames are evicted
as before. Status shows cumulative pose captures/unavailable attempts since the
last clear, not the count currently retained. A single over-budget frame is
rejected rather than exceeding the history budget.

Model submission runs in the existing engine-tools 3D render hook on the engine
thread. Source's studio-rendering front end then queues its own immutable draw
state. Calling that front end from a render-worker callback can race normal
rendering and leak wireframe state onto unrelated entities. HLAE's material
queue only signals when a draw has finished (or its callback was discarded).
The engine thread then releases model/material references and locked studio
headers. At most 512 cached model draws can await retirement; if Source's queue
falls behind, new cached submissions are deferred. Bone storage is allocated
through Source's render context; scene history owns no engine pointers.

Clearing history, disabling models, leaving cached preview or changing demos
prevents further submissions. Already submitted Source draws can finish their
frame safely before their resources are released. A current engine material
override defers model submission rather than replacing that override.
Category colors use the shader's `$color2` model tint: Source's studio renderer
replaces `$color` with its per-draw modulation, so `$color` alone renders white.

Validation covers capture failures, cached matrix ownership, model mismatches,
render-resource cleanup, producer/worker thread ownership and payload eviction with fixtures,
plus x86/x64 compilation. Live TF2/TF2C rendering and performance still require
in-game testing.

### Bounded capture and render lifecycle

**Sequencer → Scene Cache → Capture tick range** enables inclusive start/end
ticks. Set them directly or from the playhead, press **Start recording**, then play
the actual demo from before the start. Capture waits for the start and stops
at/past the end. Seeking past the end stops capture without inventing skipped
samples. It does not start playback or seek for you. A newly opened demo clears
its old range and stops bounded capture; ordinary unbounded recording remains
available.

```text
mirv_cache range 1000 2000
mirv_cache record 1
// Play the actual demo through the range.
mirv_cache range off
```

The Scene Cache tab reports capture last/mean/peak milliseconds, unique retained
poses and cumulative unchanged-pose reuse. Record a short representative sample
to see observed payload growth, estimated duration within the memory budget and
an estimate for the chosen range. These are estimates of retained CPU payload,
not total game memory. More active players/projectiles or changed capture
categories can alter the result. Oldest frames still evict at the memory limit;
a selected range is not a promise to retain everything within it.

While the cached cursor, history revision and animation mode are unchanged, the
published CPU sample is reused. Engine-thread retirement polling continues even
when preview or model drawing is disabled. Cached model submission suspends on
window focus loss, failed presents, device reset and shutdown, and waits for a
successful present plus two stable engine frames before resuming. Cached history
is preserved. Failed resets no longer recreate the overlay's device objects.
`mirv_cache status` includes pending/peak cached draws and focus/device
readiness to help diagnose delayed failures. The safeguards have fixture coverage;
live TF2 windowed idle/alt-tab testing is still required to verify the reported
crash is resolved.

### Ragdolls and gibs

In **Sequencer → Scene Cache**, enable **Ragdolls** and/or
**Gibs** before recording. They are separate, opt-in categories
and do not require a selected player. Record forward through a death/explosion,
stop recording, then enable **Cached preview** and scrub the retained interval.
Existing smooth animation, memory budget and capture-range controls apply.

```text
mirv_cache ragdolls 1
mirv_cache gibs 1
mirv_cache record 1
// Play forward through the event, then:
mirv_cache record 0
mirv_cache preview 1
```

Ragdolls draw gray; gibs draw orange. `cache status` reports their capture
settings and observed pose counts. This records visible model poses produced by
the game, including client-only objects whose public entity index is -1.
Transient identity uses the entity handle's slot **and serial**, so different
gibs sharing that public index, or objects reusing a slot, stay distinct. The
collector preserves existing AGR tools attachments and never stores its live
capture pointers in history. Existing gap/travel/model/category safeguards and
per-bone transform fallbacks remain in effect.

Gib recognition uses known gib class names and the model's `/gibs/` directory,
including Windows-style paths. Unrecognized custom debris may need another
classifier rule. Blood particles, dissolve effects, physics constraints and
simulation are not cached. Hidden/dormant networked entities are skipped; models
that cannot supply a supported studio pose retain proxy bounds. Live models
remain visible alongside cached preview. TF2 in-game coverage still needs testing,
especially client-only gibs and unusual/custom death effects.

Player history now excludes dead/dying, spectator and `EF_NODRAW` bodies using
read-only receive-table metadata, in addition to the normal render checks.
Attached player gear is omitted when its owning player is not retained, rather
than leaving it suspended at the death pose. Ragdolls and gibs keep their own
serial-bearing identities and observed lifetimes; a respawn does not delete an
older corpse. This does not infer ownership from corpse proximity or suppress
an alive player just because a corpse appears (important for feigned deaths).
Clear history and record the interval again to replace previously captured
floating bodies; the visibility checks do not rewrite old cached frames.

### Dropped weapons and cosmetics

**Weapons** and **Cosmetics** also capture visible
loose items, including recognized client-only physics props using weapon or
player-item model paths. A dropped object can retain gameplay ownership; the
read-only `moveparent` receive property identifies whether it is actually still
attached. Detached items use independent cached poses. Items parented to a
captured ragdoll use that corpse's recorded bone hierarchy, and disappear from
preview if the required attachment host is unavailable. Hidden gear remaining
attached to a dead player is still excluded.

Loose items are captured for the enabled category regardless of player selection;
this does not infer which player originally dropped a newly spawned object.
Custom models outside recognized class/model paths may need another classifier
rule. Clear and recapture existing history to include previously omitted drops.

### Buildings, props and objectives

Enable **Buildings** and **Props / objectives** in
**Sequencer → Scene Cache**, then record forward through the interval again.
Both default off and operate independently of player selection.

```text
mirv_cache buildings 1
mirv_cache props 1
```

Buildings draw yellow: sentries, dispensers, teleporters, sappers and recognized
building variants. Props/objectives draw blue: visible studio-model entities
such as model-based doors, payload carts, intelligence, pickups and dynamic or
physics props. Construction/body-group changes and animated parts use the same
observed pose capture as players; destruction/removal ends that entity's history.
Building gibs remain in the separate orange gib category. Object identity still
includes the handle serial, so destroyed/rebuilt objects cannot blend together.
Parented gear/objectives can use retained player, corpse, building or prop hosts.
Missing required hosts remain subject to the orphan filter.

**Brush doors/moving BSP entities use recorded surface wireframes** when their
map's BSP geometry can be read. Mesh metadata is shared across frames; recorded
origin and angles place the geometry at the cached tick. Unsupported geometry
keeps its recorded bounds, which can be enabled separately.

More entities increase capture cost and memory use. The existing capture range,
payload budget, pose reuse and profiling controls apply. `cache status` reports
the new category settings and pose counts; unsupported brush/studio models appear as
proxy fallbacks rather than successful studio poses. In-game TF2/TF2C testing is
still required, particularly for custom objectives and brush doors.

Fallback scene proxies render with **Cached preview** even when **Show path** is
off or the editing path is hidden. This uses the existing campath line renderer
with an empty path snapshot: no path labels, keys or camera glyphs are enabled.
When the visible active path already submits scene bounds, no second fallback
pass is queued. A model-only scene needs no independent proxy pass.

### Scene Cache menu and console command

The menu has four sections: **Capture** (categories, optional player filter,
tick range, Start/Stop and Clear), **Preview** (smoothing and model display),
**Memory** (budget and estimates), and **Diagnostics** (capture cost, model
status and animation details). Memory/Diagnostics start collapsed. Checked
category marks use their preview colors; color customization is deferred.

Player filtering remains an optional memory-saving scope for player bodies and
held gear. It does not select the whole scene. Enabling Players defaults to all
players; with player-body capture off, weapon/cosmetic capture stays available
and keeps the selected-player scope. Corpses, loose items, projectiles, buildings and props remain
independent categories.

Scene caching now has its own **`mirv_cache`** command. Replace the old
`mirv_campath cache` prefix in binds/scripts with `mirv_cache`, and use
`players` instead of the old `pose` subcommand. No compatibility alias remains
in the campath tree. `mirv_cache` / `mirv_cache help` prints concise syntax;
`mirv_cache status` prints detailed diagnostics. Other successful commands print
one summary rather than dumping all diagnostics.


### Multiple-player filtering

Expand **Player filter (optional)** and enable **Only selected players**. Check
as many players as wanted; **Find player** filters the list by name. **Select
shown** and **Invert shown** affect matching rows, while **Clear selection**
clears the entire set. An empty selected-player set captures no player bodies
or held gear. Disable **Only selected players** to return to all players.
The filter affects future recordings, not previously cached frames.

```text
mirv_cache players 1 4 7
mirv_cache filter add 2 3
mirv_cache filter remove 4
mirv_cache filter set 1 7
mirv_cache filter none
mirv_cache filter all
```

`filter` edits body/held-gear scope independently of body capture. `players none`
disables body capture and restores all-player scope. Adding/removing from all
players first snapshots the current player list. Selections pin demo-local user
IDs and clear on a different/restarted demo; filtered body capture then turns off
so another player cannot inherit an old selection.

### Saved overlay preferences

The existing **AfxHookSource_imgui.ini** in the HLAE directory now also saves:

- Scene Cache capture categories, animation smoothing, model visibility and memory budget.
- Sequencer, camera control, graph editor and gizmo window visibility, alongside ImGui layout.
- Demo fixes: recorder muting, POV deathcam suppression, smooth player movement and interpolation delay.

Preferences save through ImGui's normal timer and immediately when closing the
overlay. Demo fixes remain selected between demos and apply when compatible
TF2/TF2C playback becomes available. Recorder muting prepares the existing
sound-start filter before activating the per-frame mute. Native interpolation
settings still restore when demo playback ends.

Recording, cached preview, tick ranges, player identities, repair samples and
other running operations are not saved globally. Demo-specific path and repair
files continue using their existing save/load workflow.


### Cached bounds, brush meshes and movement trails

In **Sequencer → Scene Cache → Preview**, **Draw cached bounds** is separate
from **Draw cached models** and **Show path**. Bounds default off, including
when a campath is drawn. Enabling bounds adds them to cached entities even if
those entities already have a wireframe mesh. The old yellow heading strokes
are no longer drawn.

Brush entities, including moving BSP doors, use their map submodel's actual
triangulated surface edges through HLAE's existing line renderer. Record the
interval again to add geometry to previously captured bounds-only history.
Meshes follow each cached origin/rotation, and their shared vertex/edge storage
counts toward the cache budget. Reading uses the game's versioned filesystem
interface, with a loose-map-file fallback. This pass supports uncompressed
Source BSP versions 19–21; compressed geometry, external lump overrides and
unsupported layouts are not reconstructed. Capture diagnostics report missing
or unsupported mesh data; **Draw cached bounds** remains an optional fallback.

Enable **Movement trails** to see retained movement leading up to the cached
cursor. **Trail duration (seconds)** accepts 0.05–30 seconds; **Trail categories**
lets players, projectiles, held gear, ragdolls, gibs, buildings and props be
shown independently. Trails fade toward the older end, stop at the fractional
cached cursor, and break on missing updates, identity/owner changes and large
teleports. They use existing retained samples and require no new recording.
Previously uncaptured entities naturally have no trail.

```text
mirv_cache bounds 1
mirv_cache bounds 0
mirv_cache trails 1
mirv_cache trails seconds 2
mirv_cache trails all 0
mirv_cache trails players 1
mirv_cache trails projectiles 1
mirv_cache trails props 1
mirv_cache trails 0
```

Bounds, trail enablement, duration and category preferences persist in the
existing overlay INI. Mesh visibility follows **Draw cached models**. Bounds
and trails can be drawn with both model display and campath display disabled.
Drawing uses a bounded, immutable line batch shared with the campath renderer;
idle cursors reuse it. Limits of 65,536 total lines and 16,384 trail segments
bound work on dense/long captures, so exceptionally dense previews may truncate
some lines. Color customization remains deferred.


### Bounding box filters

Enable **Draw cached bounds**, then expand **Bounding box filters**. Category
checkboxes control which types receive boxes. **Only entities without cached
meshes** is useful for unsupported custom-map geometry.

For a single door, enable **Only selected cached entities**, find it in the
cached-cursor list and check its row. The list shows category, entity slot and
model name where available; hover for the stable identity and world-space box
center. Category and missing-mesh restrictions still apply. This changes drawing
only, so existing history does not need re-recording.

```text
mirv_cache bounds all 0
mirv_cache bounds props 1
mirv_cache bounds missing 1
mirv_cache bounds 1
// Optional exact scope, using stable identities shown by the picker tooltip:
mirv_cache bounds entities <identity...>
mirv_cache bounds entities none
mirv_cache bounds entities all
```

Category/missing-mesh preferences persist. Individual identities are tied to
this cache and clear with history. While an individual filter is active, bounds
save disabled for the next session, preventing the missing selection from
accidentally drawing boxes on every entity.


### Preview performance cleanup

Bounds and trail category filters use a compact three-column layout. Additional
preview explanations are available on hover.

The existing line renderer batches independent segments into triangle strips
and uses dynamic discard buffers to avoid waiting on previous submissions. A
renderer fixture covering 16,384 segments submits 49 draw calls rather than
16,384 while preserving the original line quads. This measures submission count,
not TF2 frame rate. Trail preparation also merges sorted entity identities once
per snapshot pair instead of repeating a binary search for each entity.


### Attaching to client-only gibs

The existing **mirv_drawentities**, **mirv_listentities**, **mirv_listbones** and
**mirv_cam attach** commands now share numeric addresses for client-only
entities. Networked indices keep their existing meaning. Client-only IDs start
at 2048 and are assigned by HLAE; they are not decoded engine slot numbers.
Their display category is **Gibs**, independently filterable from **Other**.

While the gib is alive in actual demo playback:

```text
mirv_drawentities filter gibs 1
mirv_drawentities enabled 1
mirv_listentities sort=distance
// Substitute the gib's displayed number:
mirv_cam attach 2050
mirv_listbones 2050
// Existing bone/offset/capture syntax remains available:
mirv_cam attach 2050 0
mirv_cam capture
mirv_cam attach clear
```

Attachment pins the complete entity handle and demo-level generation. Deleting
the gib, reusing its slot, or reloading the demo invalidates that target rather
than following another object. Reattach explicitly after a reload. IDs are
session-local and may differ in another playback; they are not suitable as
permanent identifiers in saved scripts. Cached-only historical entities are not
live attachment targets.

The address resolver avoids depending on legacy/current Source handle bit
layouts. Existing tool attachments (including AGR recording) are preserved when
listing, classifying, or reading a client's pose. Other index-based commands,
such as aim targeting, have not yet been extended to these client-only IDs.
Viewmodel-specific categorization/visibility remains separate work.


### Gib entity labels

Gib labels use the selected bodygroup's actual VVD vertices to calculate a
geometry center, including per-vertex bone weights and bind transforms. This
avoids the unused mesh-center field, model pivot and invisible bodygroups.
VVD checksums, ranges and LOD fixups are validated. Per-model/bodygroup bone
contributions are cached (at most 128 entries); subsequent frames transform
those contributions without reading files or scanning vertices again.
Fallbacks are transformed studio hitboxes, followed by the used leaf bones
of the observed pose. World render bounds remain a fallback when pose data is
unavailable. This avoids relying on pivots and surrounding bounds that may
still describe the original character rather than its detached piece.
Attachment origins and bone transforms are unaffected.

```text
mirv_drawentities filter gibs 0
mirv_drawentities filter gibs 1
mirv_drawentities gibSize 20
```

Gib digits default to 20 pixels (other labels default to 32). `gibSize` accepts
6–200 pixels and is independent of `mirv_drawentities size`. Label stacking uses
the corresponding label's dimensions. The existing category color command also
accepts `gibs`.


Identified cosmetics retain their cosmetic category, even with a debris collision
group. Other generic break-model debris uses the same **gibs** category and `gibSize` setting
as player gibs. TF2 creates projectile fragments as client physics props; their
standard debris collision groups (1–3) are read through validated receive-table
metadata. Recognition also covers gib class names, gib directories and model
names containing `_gib`/`gib_`, even when the original class was categorized as
a weapon or projectile. Live projectiles with their normal collision group and
model keep their existing category.

Reference: Valve SDK `tf_weapon_grenade_pipebomb.cpp::CreatePipebombGibs` and
`physpropclientside.cpp::BreakModelCreateSingle` (the CheapBreakModel path).

For a misplaced live entity label, `mirv_drawentities debugLabel <address>`
prints its model, bodygroup, debris flag, render origin/angles, local and world
bounds, computed gib anchor, and available bone matrices. It does not alter the
entity. Use the numeric label currently shown in the scene; client-only addresses
are level-local.

`debugLabel` reports a specific vertex-loading or validation failure when the
geometry anchor is unavailable. It retries a cached failed load once per explicit
diagnostic request; drawing does not retry files every frame. Model reads query
the app-system registry first, then the loaded filesystem module's public
`VBaseFileSystem011` factory, preserving GAME search paths and VPK access.

Entity labels support `mirv_drawentities filter ragdolls 0|1` and
`mirv_drawentities color ragdolls R G B`. Ragdoll labels default to gray
(170,170,170); gib labels default to orange (255,160,64).

## Procedural cameras and preview

Cameras have stable numeric IDs and can be stored in cinematic projects. Naming them is optional;
unnamed cameras receive a `Camera <ID>` name. IDs do not renumber after deletion.
Camera commands live directly under `mirv_cam`; there is no separate `rig` command group.

```text
mirv_cam add
mirv_cam add "Player shoulder"
mirv_cam list
mirv_cam preview 0
mirv_cam edit 0
mirv_cam capture 0
mirv_cam rename 0 "Wide"
mirv_cam attachMode entity
mirv_cam attach 5 -1
mirv_cam preview cameras
mirv_cam preview keys
mirv_cam preview off
```

`add [name]` captures the current view and leaves the free camera usable.
`preview <ID/name>` views a camera; `preview cameras` resumes the selected camera.
In camera preview, Up/Down cycle cameras by creation ID, Home/End select the first
or last, and Escape exits at the displayed pose. Switching cameras does not seek
the demo. **Tab** switches between Cameras and Keys while preview is active,
including with the overlay closed. Held Tab does not repeat the switch; modifier
chords such as Alt+Tab and typing in overlay text fields do not switch modes.
An empty destination leaves the current view intact. Right Ctrl + arrows retain
their Scene Cache scrubbing behavior.

For a custom single-key bind:

```text
bind "F6" "mirv_cam preview toggle"
```

The console command also enters preview when it was off.

Preview chooses the enabled sequence automatically. With sequence playback off,
it uses cameras or keyframes according to availability and the last chosen domain.
Tab switches between cameras and keys when both exist; it keeps sequence preview
when sequence playback is enabled. A missing camera/keyframe domain falls back to
the available one. Add camera, Edit camera and Recapture camera
provide the same workflow as the console: edit moves the free camera to the
selected camera, and capture saves the adjusted view. Attached cameras retain
their target when recaptured. These controls do not yet provide a rig gizmo.

The **Cameras** tab lists cameras by stable ID, with Preview, Edit, Recapture,
Rename and Remove controls. Position (X/Y/Z), angles (pitch/yaw/roll) and FOV
fields let you edit the selected camera numerically. **Apply camera pose** saves
all fields together; **Reset camera fields** reloads the current saved pose.
Attached cameras retain their target and mode; unavailable targets reject edits
without changing their saved pose. **Manage cameras** in the Campath tab opens it.
Selecting a row leaves the current view alone unless camera preview is already
active; in that case it switches the viewed camera. Removal asks for confirmation
and remains pinned to the chosen ID even if selection changes. Camera and campath edits share chronological session undo/redo ordering (up to each domain's retained history limit). The Undo/Redo buttons
and Ctrl+Z / Ctrl+Shift+Z undo the latest edit across cameras and campaths, regardless of panel or preview selection. Keyframe
editing shortcuts remain disabled while managing cameras.

**Show camera guides** optionally draws small camera icons independently of
Show path and Scene Cache drawing. **Camera frustums** adds a short viewing-angle
guide using the camera FOV and current viewport aspect ratio. The selected camera
is gold, other cameras are purple, and an unavailable attachment uses its last
observed pose in gray. The camera being viewed is omitted from its own image.
Guides evaluate live or cached subjects through the same target provider as
camera preview, and are hidden automatically during HLAE recording. Guide
visibility and frustum preferences persist in the overlay INI; camera definitions
are stored through explicit cinematic project saves. The console uses the same saved settings:

```text
mirv_cam draw enabled 1
mirv_cam draw enabled 0
mirv_cam draw frustums 1
mirv_cam draw frustums 0
```

`mirv_cam draw` lists the drawing options; `mirv_cam draw enabled` and
`mirv_cam draw frustums` show usage and the current value. `mirv_cam` lists each operation
on its own line, using `[...]` for operations with further options and `-` before
the description, following the existing HLAE command conventions. `edit` has one
help entry; `mirv_cam edit` explains its optional numeric pose values.

Drawing uses immutable engine-thread pose/line snapshots and the existing line
renderer. It does not add D3D resources or evaluate entity targets on the drawing
thread. Guides default to off, and disabling frustums leaves the smaller icons.

A compact, noninteractive indicator at the bottom left shows preview mode and
the selected camera or keyframe (including the interval when between keys).
It remains visible with the main overlay closed and is hidden during HLAE
recording. Toggle it with the persisted Preview indicator preference or
`mirv_campath preview hud 0|1`.

`mirv_cam attach <entity address/player name> [bone name/index] [offsets...]`
is the single attachment workflow. It also registers a numbered camera for later
Cameras preview; no separate add or naming step is needed. Only after an explicit `mirv_cam edit <ID/name>` does the next attach retarget
that camera instead of creating another. Adding or recapturing a camera does not
arm it for later retargeting. Otherwise each successful attach creates a new camera.
The command enters the live attachment view, ending free-camera control.
It preserves the current `mirv_cam attachMode` and existing optional offsets.

`mirv_cam capture` updates the registered camera as well as the live
attachment. `mirv_cam attach clear` detaches the selected/edited camera, or the
current live attachment, at its evaluated pose. A live attachment detaches into
the free camera at the displayed pose. `remove <ID/name>` deletes a camera;
deleting the viewed camera exits preview and releases its live attachment.

TF2 and TF2C entity addresses, including client-only gibs, use the existing
resolver. Bindings pin the full entity handle and level so a recycled slot
cannot silently change the subject. Invalid/unavailable targets retain the
previous source, camera definition and offsets.

`mirv_cam edit <ID/name>` starts free-camera editing; `capture <ID/name>` saves
the result. The same edit command accepts a numeric world pose as
`mirv_cam edit <ID/name> <x> <y> <z> <pitch> <yaw> <roll> <fov>`.
Values describe the desired world pose, including when attached. Names are
case-sensitive, unique, nonnumeric and limited to 128 bytes; there is a maximum
of 128 cameras. The redundant `rig`, `fixed` and `attach camera` routes have been
removed. Use the normal keyframe preview controls for campaths.

The shared `CameraPose`/`CameraRig` evaluation code has no engine or UI dependency.
Procedural cameras output that pose; campaths retain their existing evaluator.
A missing attachment target holds the last valid controller pose and reports the
failure once instead of silently switching sources. Reloading the same demo
automatically reconnects attached cameras when their subjects become available.
Camera IDs, names, local framing, bone selection and captured attachment offsets
remain unchanged. Players reconnect by their recorded user ID; other entities
require the same full handle and model. An unrelated entity occupying the old
slot is never accepted. Different demo files do not reconnect automatically.
Client-only gibs may receive different identities on replay; those still require
explicit retargeting with `edit <ID>` followed by `attach`, preserving the camera.
Fixed camera definitions remain available across level changes.

Saved cameras support per-camera follow and orbit, described below. Gizmos,
modifiers and collision for saved cameras remain future work. The live legacy
`mirv_cam attach` view retains its existing collision/follow evaluator; saved-camera
motion settings apply when viewing that camera or using it in a sequence.
Fixed cameras can be used during cache preview, and campaths use its client-time cursor.

Attached cameras also evaluate against Scene Cache preview. Create the binding
on the live demo, record its target, then enable cache preview and choose that
camera. All four attachment modes use the sampled entity/bone pose at the cache
cursor, including fractional and backward scrubbing. Cached bone attachments
use bone names and the same reconstructed matrices as cached meshes, including
bone-merged gear. Player bindings use the recorded user ID; other entities use
the full entity handle, so client-only objects can remain addressable after
vanishing from the live scene.

The cache must contain the target and, for a bone attachment, its model pose.
Missing coverage, a filtered target or a missing bone holds the last valid camera
pose and shows `[unavailable]`; it never substitutes the live entity. A new
attachment must still be created in live playback. Existing cameras can be
recaptured against cached poses without changing the demo or native animation
state. Cached follow reconstructs damping from recorded history; collision against
cached geometry remains a later milestone.

### Next milestone: camera mixing and existing aim integration

Independent cameras must support the existing `mirv_aim` targeting and smoothing
features. A stationary camera can keep its position while aiming at a moving
subject; position attachment and aiming remain independent choices. Reuse the
existing aiming implementation and command conventions rather than introducing
a competing tracking system.

The current legacy aim override executes before the saved-camera pose override,
which replaces its angles. Integration therefore needs to evaluate the camera's
position first, apply its aiming configuration, then pass the completed pose to
the mixer. A transition can release its outgoing pose at the boundary, while the incoming
camera continues evaluating; legacy live-outgoing sequence clips remain
supported. Each source's aim configuration and smoothing history remain
isolated: evaluating an
inactive camera must not advance the active camera's global aiming state. The
existing `Aiming` object owns a reference-counted target and mutable smoothing
history, so an implicit object copy is not a suitable configuration snapshot.

Cached targeting must resolve the recorded subject at the cache cursor through
the existing cached target provider. It must not substitute a live target when
coverage is missing. Stateful aiming needs deterministic reconstruction for
backward and out-of-order scrubbing before it can be advertised as cache-ready.

Camera guides should also display each camera's name above its icon, using the
existing world-label renderer in the same way as path names. Names should follow
the camera's evaluated position, work without Show path, and share the existing
recording suppression and safe engine-thread drawing snapshots.

Camera-name labels, per-camera aiming, playback mixing and sequencer clips are
implemented below. Deterministic cached smoothing remains pending.


### Camera transitions

`mirv_cam transition <ID/name|campath> <seconds>` starts a transition when the
command is issued. Use `0` for an immediate cut. Transition progress follows
live demo or cached preview time, so pausing pauses the blend. Manual
transitions now capture the displayed outgoing camera pose once at command time
when that view is available, rather than allowing an attached/aimed source to
continue rotating or snapping throughout the fade. The incoming camera or path
continues evaluating. From the free camera or when interrupting an unfinished
transition, the same displayed-pose release is used. If the displayed pose
cannot be captured but the outgoing camera remains valid, the previous live
outgoing evaluator remains as a fallback.

`campath` captures the active path's stable ID at command time. It evaluates that
path regardless of the normal playback-enabled flag or collection mute/solo
settings. Before its time range it holds the first evaluated pose; after the
range it holds the last. Inside the range it follows the normal evaluator,
including offsets and modifiers. It does not alter the path's enabled or Hold
settings, and switching the active editing path does not retarget the transition.
The path must have enough keys for its interpolation method to evaluate.

In the Cameras tab, choose **Transition destination**, set **Transition duration
(s)**, then press **Transition**. The destination can be Active campath or any
saved camera. Selecting/previewing a camera remains separate from transitioning.
A new transition can interrupt an unfinished one from the displayed pose.

```text
mirv_cam transition campath 2
mirv_cam transition Hero 2
mirv_cam transition Hero 0
```

`mirv_cam preview` remains camera/keyframe inspection and navigation; it no longer
accepts blend durations or the `playback` destination. `mirv_cam preview off`
exits the controller at the displayed pose. Transitions are session operations,
not saved sequencer clips. Camera names draw above their guides independently
of path drawing and key-index label size, and hide during HLAE recording.

Camera history covers add, remove, rename, recapture, numeric pose edits and
attachment changes. `mirv_cam undo` and `mirv_cam redo` operate on that history
explicitly. Preview navigation and drawing preferences do not create history
entries. History retains up to 64 edits and is session-only. Restored attachments
retain their recorded identity checks; undo cannot bind to a recycled entity slot.


### Mixer milestone: per-camera aiming and campath playback

Independent cameras reuse the existing aim engine and its options. Scope an
existing `mirv_aim` command to a camera using `mirv_aim camera <ID/name> [...]`:

```text
mirv_cam add Hero
mirv_aim camera Hero entityIndex 5
mirv_aim camera Hero active 1
mirv_aim camera Hero snapTo 0
mirv_cam preview Hero
```

Position stays fixed unless the camera itself is attached. Aiming controls only
orientation. `point abs`, `point cam`, `point last`, `offset`, `up`, `velLimit`,
`accelLimit`, `softDeactivate`, and custom `calcVecAng` sources use the existing
implementation. `mirv_aim camera Hero` lists scoped usage; ordinary `mirv_aim`
continues to configure legacy aiming. The Cameras tab's **Aiming** section
exposes target, active, snap/smooth, offset and smoothing-limit controls. Enter
commits target fields. Aim settings participate in camera undo/redo and survive
retargeting the camera's positional attachment.

Each camera owns its settings and smoothing state. Evaluating guides or both
ends of a blend cannot advance another camera's filter, or advance the same
filter twice in the same engine frame. Inactive guide previews do not advance
the stored filter. Live smoothing uses the engine frame step and keeps
converging while demo time is paused. Discontinuous live time resets the filter.
Entity aiming pins recorded identity and reconnects through the same provider
as attachments; choose a target that exists in live playback when configuring
it. Snap aiming can then resolve that subject from Scene Cache, without using
its live pose. Fixed world-point targets also work in cache preview. Cached
smoothed aiming and custom calc sources can use recorded camera samples. Without
them, built-in aiming is unsmoothed and custom sources hold the last valid view
instead of silently evaluating against live state.

Use the root `mirv_cam transition` command described above to mix procedural
cameras and the active campath. This is explicit source selection, not the normal
collection playback selector: disabled paths and endpoint holds no longer block
the transition. Legacy global aiming/collision are not applied to this explicit
campath source. Missing camera subjects, invalid paths and unsupported cached
aim settings still report a specific failure and hold the last valid mixed pose.


### Sequencer camera clips and live rig evaluation

Aimed independent cameras now update once per engine frame even while viewing
another camera or using the free camera. Their guides show the current aimed
orientation, including smooth aiming. Drawing remains a read-only sample;
turning guides on or off does not change the camera's aiming behavior.

The sequencer's **Camera sources** track shows camera clips and their blend
intervals. Click a clip to select it. Open the **Camera clips** tab beside Commands and Events:

1. Enter the desired **Start tick** (choose **New clip** first if editing an existing clip).
2. Set its exact demo tick, source camera or campath, and blend duration.
3. Press **Add camera clip**, or **Save changes** when editing an existing clip.
4. Enable **Enable camera sequence**, then play or scrub the demo/cache.

Zero blend makes a cut at the transition start, which may be offset from the edit
point. A positive blend applies the selected timing curves to position, rotation
and FOV independently; position can follow an authored cubic Bezier trajectory.
The outgoing handle and incoming handle are **world-space XYZ offsets** from
the straight-line control points at one-third and two-thirds distance between
the evaluated endpoint poses. Zero handles reproduce the straight-line route.
The trajectory editor uses a small interactive 2D canvas with two draggable
control points and switchable **XY/XZ/YZ** projection planes. The graph edits
the chosen two world-offset axes; switch planes for the remaining axis. An
optional collapsed panel retains exact numeric XYZ values.
Clip blending follows demo/cache time and is sampled by timestamp. Cached preview first uses observed tracking-camera samples recorded with the
scene. Without matching camera samples, it evaluates built-in aiming without
smoothing on a private settings copy, preserving live smoothing history. Custom aiming calculations
still require live playback. Cached attachments and aim targets require coverage.
The first clip is already active before its start tick. Its camera/path is
evaluated during pre-roll, and its own initial blend duration is ignored; later clips blend from the previously composited result, which may include an
unfinished earlier transition. No editor/freecam pose seeds the shot.

Camera source blocks still begin at their editorial **Start tick**, and the last
source continues indefinitely. The **transition offset** separately places an
incoming envelope before (negative), on (zero), or after (positive) that edit
point. Before/Center/After buttons set the offset to -duration,
-half-duration or zero; exact offset seconds remain editable. Two camera edit
points cannot coincide, and transition envelope start times must retain the
same chronological order as the clips' edit points.

**Overlapping envelopes are supported.** Rather than choosing a single
incoming/preceding pair, the mixer composes each active transition in order.
The next transition begins from the output of any earlier transitions (for
example, an unfinished A/B composite can become the outgoing side of B/C).
A fully completed later transition supersedes previous transitions. Blue
transition overlays are drawn on the same Camera sources strip as the camera
clips, not on separate rows. They can extend across later edit points and overlap other envelopes.
**Normal drag moves the camera clip**; hold **Shift while dragging** a blue
transition to move its timing independently, or Shift-drag its right edge to
resize its duration. A visual overlay that is not being Shift-edited cannot
capture the clip's ordinary mouse drag.

With **Show selected transition path in 3D** enabled, the existing DX9
camera-guide renderer shows only the selected transition as an amber dashed
line (not a solid campath). This is editor-only and suppressed during HLAE
recording. Attached and aimed camera guides use historical Scene Cache samples
at the transition's timestamps, regardless of whether cached preview is
currently enabled. If historical coverage is absent or incomplete, no guide
is drawn for those moving sources: a guess from the live entity position at
another timestamp could place the guide far away from the shot. Fixed cameras
and ordinary campaths can still be evaluated directly without a cache.
The draw-only sampler restores live camera and aiming state and never changes
the active transition.

New clips authored in the UI enable **Freeze outgoing camera at transition
start** by default. This releases the outgoing attached/aimed/path camera from
further influencing the blend; the incoming camera continues evaluating at
each timestamp. Disable the freeze checkbox to keep both sides live. For
live demos, freeze captures the last displayed composite at the first crossed
boundary, or falls back to the evaluated outgoing pose when playback begins
inside an envelope. For Scene Cache preview and classic baking, it samples the
outgoing composite at the envelope's exact start timestamp. If that sample
has no available camera/target, the valid incoming source takes over rather
than resurrecting an unrelated historical pose. Cached preview therefore
requires coverage of the transition boundary for a frozen release.

Saved cinematic projects now use **HLAE_CINEMATIC 9** and preserve all transition
timing, rotation/FOV curves, freeze state and Bezier handles. Versions 1-8
remain loadable and retain their original live-outgoing/zero-offset semantics.
Retiming and source changes apply together through Save; rejected edits
leave the existing clips intact. Clip edits use camera undo/redo. Removing
a referenced camera leaves its clips visibly addressable for correction and
produces an unavailable source until restored or retargeted. Deleting the last
clip or clearing the sequence stops sequence preview.

`mirv_cam sequence 1` enables the configured sequence; `0` stops it at the current
view. `mirv_cam sequence clear` clears the clips with undo support. Manual preview or transition commands temporarily override the sequence view;
exiting preview restores enabled sequence playback. Sequence clips are bound to
their demo/session; clear them before authoring for a different demo. Cameras and
clips can be saved in cinematic projects; ordinary campath files still contain paths only.

Camera clips use start markers and faint continuation spans. Translucent blue
transition spans sit directly over the camera source clips, marking the
actual offset start/end without introducing a lane per transition. For cached
transitions, enable Cached preview and Enable camera sequence;
there is no need to preview an individual camera or campath first.

Sequence playback is enabled independently of preview. Entering preview while it
is enabled automatically previews the sequence with an independent cursor. Explicit
individual camera commands can still override the view without changing the enabled flag. Exiting
preview returns to enabled playback. Freecam can also temporarily override it;
leaving freecam returns to the sequence. Only the sequence enable switch (or
clearing its clips/loading a project) disables playback.

During sequence or independent-camera preview, cached scrubbing never enters the
native keyframe preview controller. Campath endpoints only clamp the sampled
source pose; they cannot clamp the sequence clock. Cache gaps retain historical
time mapping rather than falling back to the live demo clock. Blue regions on
the Camera sources track denote blends; hover them for the duration.

Console preview enter/toggle/exit uses the same controller handoff as the preview
key. Rejected camera selections retain sequence resume state. Cached clip editing
and timeline placement use historical cursor time even when entity coverage is
missing. Leaving preview immediately after enabling it retains the current view.

### Cinematic projects and classic camera export

The Cameras and Camera clips tabs provide **Save cinematic project...** and
**Open cinematic project...**. Projects use the versioned `.hlae` format and
contain the full path collection, independent cameras, built-in aiming settings,
attachment framing/identity, sequence clips and command markers. Scene Cache
payloads, demo files, overlay preferences and undo history are not embedded.

```text
mirv_cam save "shot.hlae"
mirv_cam load "shot.hlae"
mirv_cam bake sequence "shot.xml" 10 20 0.015151515
mirv_cam bake 3 "camera.xml" 10 20 0.015151515
```

Loading replaces the project only after all sections pass validation, starts
fresh edit histories, stops camera/path preview, and leaves path playback and
command execution disabled. Open the matching demo before loading attachments.
Absent subjects remain unavailable and reconnect by recorded identity when
possible. Client-only gibs may still require explicit retargeting after replay.
Custom `mirv_calcs` aim sources cannot currently be serialized: saving a project
with one fails instead of silently dropping its behavior. Independently saved
command tracks remain available through Commands → Save/Load command track.

In Cameras, open **Project and export**, then **Export sequence as classic campath**
and choose **Bake sequence...** to export all clips and transitions. No camera selection is required. The same export
is available in Sequencer > Camera clips. **Export selected camera as classic campath**
exports only that camera and does not include sequence transitions. Each source keeps
its own start/end **demo ticks** and sampling step in seconds. The default sequence
range follows changes to the sequence until you edit its start/end ticks.
**Use sequence range** restores that automatic range. Constant sequence bakes
report clip start times and sample counts to help diagnose timing mismatches. Camera pose, aiming,
name/removal settings, transitions and guide display controls are grouped into
collapsible sections; the camera list and preview/edit/recapture actions stay visible.
The console range is expressed in **client seconds**, matching campath timestamps, rather than demo ticks. Sampling is
bounded to 50,000 regular intervals. Missing target/bone coverage aborts export;
fixed cameras can export without cached entities. Attached and aimed cameras
require a cache. A live recording-pass exporter is not included yet.

Baking reads recorded poses without seeking the demo, moving the preview cursor,
or changing the authored cameras or live aim history. Recorded camera samples preserve
observed tracking; fallback cached targeting is unsmoothed.
The baked classic XML contains position, quaternion orientation and FOV, without
attachments, aim settings or commands. Sequences keep both sources evaluating
during blends. Instant cuts use keys one microsecond apart: classic campaths
approximate a cut with a very short transition, rather than encoding an exact
step. Export may require denser sampling for rapid motion. Ordinary path Save
and Save all remain path-only; use Save cinematic project to retain the full shot.

### Sequence playback and dense baked paths

`mirv_cam sequence 1` enables automatic sequence playback. It exits the current
preview/freecam view so playback starts immediately. Preview navigation and
freecam editing can then be used temporarily without turning the sequence off.
`mirv_cam sequence 0` disables it. Both the top-level **Enable sequence** checkbox
and the Camera clips tab control the same enabled flag. Pause and cache scrubbing
still control its evaluation clock.

Clip creation does not require leaving freecam. Enter a **Start tick**, select the source and press **Add camera clip**.
Select a timeline block to edit it with **Save changes**; **New clip** returns
to creation mode without copying the playhead tick. Duplicate
edit points or envelopes that start out of order are rejected inside the clip
tab. Overlapping transition envelopes are allowed. New clips do not move the free camera.

Dense classic bakes exposed an out-of-bounds write in trajectory polyline drawing:
the helper writes four vertex slots while the caller reserved only two. The
renderer now flushes with room for all four, including at batch boundaries.

### Recorded tracking cameras and sequence pre-roll

Create/configure tracked cameras before recording Scene Cache, then replay the
shot with scene recording enabled and cached preview off. Active tracked cameras
are sampled even when not selected. Their observed position, orientation and FOV
are retained within the cache memory budget. This preserves live smooth aiming
and custom aim results for cached scrubbing and baking. Existing caches cannot
recover unrecorded live filter history; built-in targeting falls back to the
previous unsmoothed evaluation when no camera sample is available.

Camera setting/pose edits invalidate that camera's older samples. Rename and
sequence clip edits preserve them; undo can restore the original camera samples.
Cache gaps and definition changes are never bridged with recorded camera poses.
Scene Cache diagnostics show how many recorded camera poses exist at the cursor.
These samples are transient, alongside the entity cache, rather than project data.

The first clip is active during pre-roll; switching off preview or leaving freecam
returns to its pose before the first transition. `mirv_input camera` and the
overlay freecam controls explicitly leave camera/keyframe preview without disabling
the sequence. Export controls identify selected-camera versus whole-sequence
sources, offer the sequence range, and display the bake result in the camera tab.
The result includes the client-time range, number of sampled poses and changed
samples. Failed exports report the unavailable source/time and retain the old file.

Bake tick ranges are converted through recorded demo/client-time pairs, including
nonuniform clock offsets. Selected-camera and whole-sequence exports have distinct
labels and filenames. The visible bake report identifies the source, sampled
client-time range, pose count, and changed-sample count; an all-identical result
is called out explicitly. This makes it possible to distinguish a static export
from a failed export which retained a previous file.

Camera source clips draw as solid labelled blocks. A hatched region inside the
leading edge indicates the blend into that source; the selected block has a gold
outline. Clicking a source block or its translucent blue transition opens the same clip
editor. Drag normally to move the source clip; **Shift+drag** the blue region
to move the envelope or **Shift+drag its right edge** to resize the duration.
The edit point remains independent. Edit points, offsets and durations also
remain editable numerically.

A full-sequence bake requires coverage for every active tracked source, including
its incoming blend. If a camera has neither a recorded camera sample nor its
cached aim target, export aborts at that source/time and keeps the existing file.
A retained XML may therefore be an older static camera export. The main file
status and camera-tab status both show the specific bake report.

In the Cameras list, click the selected camera again to deselect it. This ends
individual camera preview while leaving sequence playback enabled.

Sequence preview uses the existing Preview toggle or `mirv_campath preview enter`.
Enable sequence playback first, then enter preview; the HUD identifies Sequence,
the current source or blend, and the independent client-time cursor. Enabling a
sequence while preview is already open switches to sequence preview. Disabling
it returns to available camera/keyframe preview. Exiting preview leaves playback
enabled and returns its clock to the actual demo/cache time.

Without Cached preview, Left/Right scrub the sequence while the demo stays frozen;
Up/Down jump to the next/previous clip, and Home/End go to the sequence bounds.
The sequencer playhead, Go to tick, and preview slider use the same cursor without
seeking the demo. **Seek actual demo here** and RCtrl remain explicit seek actions.
No command markers execute when only the preview cursor moves. With Cached preview,
the cache clock also supplies historical entity poses and recorded tracking.
Without cached motion, entity attachment and aiming use the current live poses;
aiming is evaluated without advancing the live smoothing history. Individual
`mirv_cam preview <ID/name>` inspection remains an explicit override.

### Per-camera attachment, follow and orbit

Select a camera and expand **Attachment** to edit its entity address, bone index/name
(`-1` means no bone), and existing Bone/Entity/Aligned/World mode. **Apply attachment**
retargets that same camera and preserves its offset and orientation relative to the target.
It moves to the new entity, including when that entity is across the map. Camera IDs,
sequence references and undo/redo remain intact. Return to live playback before
changing bindings; attachment selection uses the existing entity/bone resolver.

```text
mirv_cam edit 0 attach 5 head entity
mirv_cam edit 0 attach 8 -1 world
```

**Motion** uses the same attachment follow evaluator as `mirv_cam attach follow`,
with state/settings owned by each saved camera. There is no separate position or
rotation smoothing algorithm. Root and bone damping, orbit/composition response,
lag limits, teleport resets and framing correction use the existing controller.
Live attached-camera views use the existing collision controller when enabled.
Cached and frozen sequence preview do not trace against the current live world.
The attached collision switch, soft zone and recovery controls are in Cameras;
Campath retains freecam/path collision. Clearance and obstacle options are shared.

**Follow half-life** controls root damping; **Follow bone half-life** independently
controls bone movement. **Follow orbit half-life** and **Follow turn-rate limit**
control the existing turn/composition response while root follow is enabled.
The latter is the legacy `orbitSpeed` follow option, not an automatic orbit rate.
**Maximum follow lag** and **Teleport reset distance** use world units. Zero disables
the respective filter/limit. World attachment mode keeps its authored heading;
existing per-camera aiming remains available for independent target tracking.

```text
mirv_cam edit 0 follow 0.15 0.08
mirv_cam edit 0 follow boneHalfLife 0.05
mirv_cam edit 0 follow orbitSpeed 180
mirv_cam edit 0 follow maxDistance 128
mirv_cam edit 0 follow teleportDistance 1024
```

**Orbit speed** supplies a signed automatic orbit rate around the physical entity/bone
pivot on the world up axis. The full captured boom is rotated, including offsets
stored inside an attachment calculator. Its maximum magnitude is 3600 degrees/second.
Capture framing away from the pivot to provide a radius; zero radius cannot circle.
Changing speed preserves the current pose; recapture saves framing at the current phase.

```text
mirv_cam edit 0 orbit 30
mirv_cam edit 0 orbit 0
```

Motion settings support undo/redo and project persistence. New projects use format
version 3, while version-1 and version-2 projects still load. Orbit follows the
preview/demo clock. Uncached sequence preview uses frozen target poses; cached
follow replays recorded history through the same attachment controller using small
checkpoints, giving the same result in either scrub direction. Its first available
observation seeds the state, so record lead-in before shots that need settled follow.

### Camera list and project files

Camera indices are zero-based positions in the creation-ordered camera list.
After removal, visible indices and unnamed `Camera N` labels close the gap.
Custom names remain unchanged. Console camera references use these current
indices or names. Internal camera identities remain stable, so renumbering never
redirects attachments, aiming, cached samples or sequence clips to another camera.
Clips referring to a removed camera remain unavailable until it is restored.

Click a row to select it; Ctrl-click toggles additional rows, and Shift-click
selects a range. Select all / Deselect controls are below the list. Removal is
immediate and undoable. Batch pose, attachment, motion edits and removals use one undo step.
Edits apply only fields changed from the primary camera; other values are retained
per camera, so a batch FOV change preserves each camera’s position. Recapture
applies the full current world view to all selected cameras. Rename is available for a single selection.

`mirv_cam capture` can repeatedly recapture the selected attached camera,
including after leaving camera editing or switching away from the legacy live
attachment controller. No new attachment is required between captures.

The **Project** tab contains cinematic-project save/open, campath XML/collection
files, classic export/baking, and standalone command-track file import/export.
Camera clips and Cameras contain editing controls rather than duplicate save
panels. Cinematic projects already include command markers.

### Shared behavior audit

The `mirv_cam capture` command and overlay recapture action use `MirvCam_EditCamera` / `EditCameraPose`.
The pose is stored once relative to the attachment frame. Recapture no longer
mutates the legacy attachment calculator and mirrors another offset into the rig.
An active legacy attachment hands evaluation to its saved camera; a running
free camera remains editable until `mirv_input end`.

Explicit transitions and camera-source clips share `BlendCameraPoses`; sequence
preview and export use the same sequence evaluator. Attachment restoration and
retargeting share the calculator-copy implementation, retaining calibration and
captured data from older projects.

The audit also checked the existing shared paths: saved cameras use `Aiming`
for aiming, `MirvCameraAttachedEvaluate` for follow/collision, and `CameraRig`
for attachment/orbit transforms. Scene-cache and entity labels share the entity
classifier. Keyframe replacement and gizmo edits use `CamPath::ReplacePose`.
Live target lookup, cached-pose replay and the independent preview clock remain
different data/state adapters; they do not introduce another follow, aiming or
blend algorithm. Classic baking's previously reported problems remain a separate,
deferred task; this consolidation does not claim to resolve them.

### Shared modifiers and clip timing

Open **Cameras → Camera modifiers**, choose **New modifier**, and select Noise/shake or
Oscillation. Set strength, frequency, seed and position/FOV/angle amplitudes, then
**Apply shared modifier**. Names commit when leaving the name field; applying the
other settings is not required to rename an effect. Default names reuse available
numbers after deletion.

Select a source block in the sequencer, then open **Camera clips → Clip modifiers →
Link modifier**. The definition is shared; each clip owns its instance's enabled
state, influence, order, range and fades. Start/end are demo ticks; fade lengths
are ticks. Moving the source clip carries its modifier timing with it. Reapplying
an existing definition enables its existing instance instead of duplicating it.
Unlink retains the definition. Deleting the definition removes all its instances;
undo restores them.

Only the selected source clip displays its modifier lanes. Drag a block to move
its range, its boundaries to resize, or its fade markers to adjust the envelope.
A drag commits as one undoable edit on release. Numeric tick fields provide precise
editing. The source's own duration is still defined by the next source clip; a
modifier range does not change that duration.

Effects use the existing campath additive evaluator after attachment, follow,
orbit and aiming, before blending sources. Local clip time makes forward/backward
scrubbing deterministic. Old camera-owned links remain functional under **Legacy
camera modifier stack** when opening existing projects; new instances are authored
on clips. Free-camera editing continues to display the base pose.

Cinematic project format 5 saves clip instances and envelopes. Versions 1–4 still
load, retaining their existing camera-linked effects. Classic baking remains a
separate deferred task. Only existing additive noise and oscillation are exposed.

### Sequencer visibility and navigation

Empty tracks hide automatically. Demo events are opt-in. **Tracks** provides
Auto (hide empty), Hidden and Shown overrides for each track; **Show all** reveals
all tracks and paths, and **Reset tracks** restores the compact defaults. These
preferences are saved in the overlay's INI settings. The Commands, Events and
other detail tabs remain accessible even when their timeline rows are hidden.

The path selector offers **Active path**, **Selected paths** (active plus paths
with selected keys), and **All paths**. Modifier lanes appear only for the selected
camera-source clip. **Frame selection** fits selected keys and the selected clip;
**Frame authored content** fits path keys, holds, source clips and modifier ranges,
without including the full demo or its event stream. **Fit demo** restores the full
range. Framing never changes authored timing.

Wheel up/down zoom around the pointer, Shift+wheel or horizontal wheel pans, and
middle-button drag pans. The sequencer claims wheel input while hovered, including
its labels and zoom limits, so either direction stays out of the parent menu.

### Unified recapture command

`mirv_cam capture [index/name]` recaptures stationary or attached cameras through
the same pose editor used by the overlay. With no argument it uses the selected
camera; an explicit reference captures another camera. `mirv_cam attach capture`
has been removed. Fly the free camera to the desired framing, capture, and use
`mirv_input end` to return to playback.

### Camera preview, modifier names and grouped tracks

Camera clip modifiers also evaluate when previewing an individual camera with
sequence playback disabled. Their envelopes still follow the current demo/cache
clock. A camera referenced more than once uses its latest camera clip at or before
that time (or its first clip before any begin). This reuses the sequence's effect
evaluator; it does not run the sequence or switch the view to another source.
Guide poses include the effects, while numeric editing and recapture retain the
base pose. Default Shake/Oscillation names follow type changes and reuse available
suffixes; custom names are retained.

All sequencer tracks use subtle horizontal separators. Groups add a border,
accent gutter, disclosure control and indented child arrows. When multiple paths
are visible they share a Paths group; a lone path stays directly accessible.
Camera sources can collapse modifier children without hiding their source blocks.
Group expansion state is saved with the overlay preferences. Modifier blocks show
their effect names rather than repeated drag instructions.

Scene capture now runs after the engine's FRAME_RENDER_END notification, before
frame retirement. Previously it requested bone poses at FRAME_RENDER_START,
which could preempt the live renderer's normal interpolated bone setup. The
capture-stage fixture checks that the engine finishes rendering before any scene
pose capture. Visual improvement still requires testing in the game.

### Follow and smooth aiming share the camera clock

Independent cameras evaluate attachment/follow (including collision) first,
then aiming from that final position, then camera modifiers. Source transitions
blend the completed poses. The existing follow and Aiming controllers are reused.

Smooth aiming uses elapsed demo/client time while the scene advances, matching
attachment follow rather than the render frame duration. With the scene paused,
render time still lets aiming settle; follow remains stationary. Repeated calls
at the same render frame and camera time reuse the aim result. Distinct camera
times within one render frame evaluate separately, so subframes and capture do
not pair a fresh follow position with a stale heading. Follow and aim share the
same large-gap reset threshold. Backward time changes still reset live aim;
recorded cached-camera samples and frozen inspection keep their existing behavior.

Follow lag and aim velocity/acceleration remain separate artistic controls.
A tracking limit can still let a fast target leave the frame; this synchronization
change does not force snap-to or change those limits.

The shared acceleration-limited smoother also preserves displacement across all
phases of an update and ignores roundoff-sized leftover time. Previously a later
phase could overwrite earlier motion, including after very small timing changes.
Zero/nonpositive limits stop smoothing motion instead of entering zero-duration
phases. The same corrected routine is used by legacy aiming and calculator callers.

### Attachment framing, bone controls and camera presets

In **Cameras → Motion**, **Preserve subject framing** uses the existing attachment
framing solver after follow and collision to retain the physical attachment
anchor's captured screen direction, including off-centre composition. It does
not need `mirv_aim`. Positional lag yields as the anchor approaches the safe margin
of the authored view. Margin is a fraction of each frame edge (default 0.1),
used when the anchor is initially inside that region. This protects an anchor
point; it does not guarantee the full player's silhouette or visibility through
obstacles. Capture an offset from the anchor: a coincident head-mounted camera
cannot show its own attachment point. World attachment keeps its fixed heading.

When explicit aiming is active it owns the final yaw/pitch. Attachment still
moves the boom, but its angular framing/history stage is bypassed. The existing
aiming up setting controls roll, and soft deactivation returns toward the
attachment's framed orientation. Changing follow settings retains active aiming
history rather than resetting it to the bone heading.

**Follow bone half-life** now filters bone movement in entity-root coordinates.
With root follow at zero, translation and turning of the entity remain direct;
only relative bone animation is damped. **Bone influence and framing limits**
contains position/rotation influence (0..1), displacement/angle soft limits
(0 disables), and the framing margin. Influence scales deviation from a captured
bone reference, so zero position influence retains the captured head height.
Soft limits bound that deviation without a hard stop. Recapture and retarget
update the reference through the existing camera editing pipeline.

Motion presets reuse those settings: **Rigid attachment**, **Stable head mount**
(no root lag; damped/reduced bone motion), and **Soft follow**. Applying
a preset, numeric edits, console edits and undo/redo share the same motion API.
The shared modifier library also offers **Camera modifier preset**: Handheld,
Impact shake and Oscillation. Handheld/Impact reuse `CamPath::MakeModifierPreset`;
link them to source clips as usual. Preset fades initialize editable clip fades.

```text
mirv_cam edit 0 preset stable-head
mirv_cam edit 0 follow preserveFraming 1
mirv_cam edit 0 follow framingMargin 0.1
mirv_cam edit 0 follow bonePositionInfluence 0.25
mirv_cam edit 0 follow boneRotationInfluence 0.25
mirv_cam edit 0 follow bonePositionLimit 4
mirv_cam edit 0 follow boneRotationLimit 10
```

Cinematic project format 6 saves these settings and the root-relative bone
reference. Older formats still load; their previous framing option remains off
until enabled or a motion preset is applied. Cached follow replays the same
controller and reference through its existing checkpoints, without live collision
queries. Camera guides, normal viewing, saved projects and recapture use the same
attachment calculations. Recorded aimed-camera samples retain their existing
re-record-after-edit requirements. Full-subject bounds framing remains deferred.

### Classic bake verification

Camera baking uses the same individual-camera evaluator as preview, including
timed clip modifiers. Sequence baking uses the shared sequence evaluator. Poses
are assembled in one batch and preserve quaternion orientation directly. Classic
export reloads its generated XML and checks every exported pose before replacing
the destination file. Successful camera bake reports include
`XML reload verified for every exported pose.`

This check validates serialized position, orientation and FOV; it does not confirm
the in-game view or renderer. Loading a bake into an existing source path replaces
that path's keys. Sequence playback can still override ordinary path playback;
the baked file itself contains no camera bindings or commands.

### Camera effect presets

Cameras > Camera modifiers offers Subtle handheld, Heavy handheld, Gentle drift,
FOV punch, Recoil, Dolly, Slide and Roll / bank, alongside the existing presets.
They are shared editable noise/oscillation definitions, using the same evaluator
as campath modifiers. Link them in Camera clips; time their ranges and fades in
the sequencer. Impact and recoil are manually timed, without event detection.

Dolly moves forward and back, Slide moves sideways and back, and Roll / bank
tilts and returns. Their strength envelope controls the gesture, including a
short plateau at maximum displacement. Negative amplitudes reverse direction.
These gestures and FOV punch use frequency zero with phase 0.25 so their envelope
scales with the clip range. FOV punch narrows the FOV briefly; recoil moves back
and pitches upward. Frequency remains in Hz for handheld and drift.

The camera library exposes Phase, Octaves, Roughness and the existing Strength
envelope editor. Changes require Apply shared modifier and affect every link.
Console campath preset IDs are subtle-handheld, heavy-handheld, camera-drift,
zoom-punch, recoil, dolly, slide and bank. Existing fov-punch retains its original
oscillation behavior. Preview, undo, projects and classic baking use the same
settings and require no additional effect system.

### Camera clips and export audit

Clip source selectors use the same displayed names as the Cameras list. Drag
a source block to move its start tick; the whole move is one existing clip edit,
including undo and relative modifier timing. Escape cancels; invalid overlapping
blends are rejected. New camera clip at playhead fills the start tick for a new
clip; explicit tick entry remains available.

Blend interpolation offers Smooth (the existing smoothstep), Linear, and Smoother
(smootherstep). Blend duration zero remains a cut. Project format 7 stores the
curve per clip; older projects retain Smooth. All preview, playback and baking
use the same mixer.

Camera modifiers also support Value limits and Value remap through the existing
campath filter calculations. Filter channel amplitudes are weights from 0 to 1;
Camera space is unavailable for filters. Smooth, spring and timing modifiers still
require authored curve/history input and are not exposed as per-pose filters.
All curve presets now have descriptions, including the earlier effect presets.

Classic export entry points for paths, cameras and sequences now route through
MirvCam_Bake and the existing classic XML writer. Their sources still need different
evaluators; the sampling, quaternion output and XML verification are shared.
Path export uses the selected path and its offset, without requiring scene cache.
The stock-HLAE compatibility regression compiles the unmodified loader and
interpolators from commit 7c52c93f (HLAE 2.189.6) with compiler portability
adapters. It checks every XML key and interval midpoint, including orientation.
This verifies file/interpolation compatibility, not the game session clock or
paused-time settings; those remain relevant when a bake plays at the wrong tick.

Baked cameras/sequences carry the classic endpoint hold flag, matching sequence
pre-roll/post-roll. Path baking preserves that path's hold setting. Cut guards
are at least one float clock step apart, rather than collapsing into the same
representable playback time in stock HLAE. This does not replace testing a newly
reported file: the earlier supplied XML matches stock 2.189.6 at every key and
midpoint; the reported missing-motion case still needs its current XML to isolate.


### Experimental TF2 separate recording passes

The Recording tab's **Separate recording passes** section enables a complementary
Base / world output plus selected Players, Viewmodel, HUD and Depth outputs.
Selecting an isolate enables Base by default. Base contains the remaining scene;
normal recording remains unchanged until separate outputs are configured.
**Record full scene reference** optionally adds an untouched scene recording,
disabled by default. Disable Base for isolate-only recording, or use
**Reset separate outputs** to restore ordinary recording. These use the
existing `mirv_streams` capture nodes, recording clock and selected screen profile.
Choose an FFmpeg profile for video files; image-sequence profiles still write images.
Each enabled pass writes inside its own named subfolder of the same take.

```text
mirv_streams record pass base 1
mirv_streams record pass players 1
mirv_streams record pass viewmodel 1
mirv_streams record pass hud 1
mirv_streams record pass depth 1
```

Use `mirv_streams record pass reference 0|1` for the optional reference, or
`mirv_streams record pass reset` to restore ordinary recording. The legacy
`world` name aliases `base`. Base files use the `base` subfolder; optional
reference files use `reference`.

Use `0` to disable an output. Configure passes before recording, then use the normal
record start/stop controls. They initially share the screen recording profile,
including any sampler output FPS. Extra passes render extra views at the same demo
frame; they do not advance the demo for each output. Unsupported hooks or missing
INTZ depth support reject recording rather than silently treating depth as color.

Base omits players, weapons, cosmetics, ragdolls and gibs only when Players is
selected. Likewise, it omits HUD or viewmodels only when their outputs are selected.
Removed models submit no depth or stencil, so they cannot leave invisible
silhouettes blocking particles or translucent map geometry. Depth is auxiliary
and subtracts nothing. The players
pass includes those models (including dropped/debris models and corpse-attached
gear) and retains world depth occlusion. Viewmodels remain a separate pass;
decals and ordinary props remain in the world pass. The render passes share the
scene-cache class/model classification fallback. Player, viewmodel and HUD
outputs default to chroma green. **Transparent isolated passes** enables embedded
alpha for these three outputs; Base, Depth and full scene reference keep
their existing format. **Isolated pass background**
offers Green, Blue and Black; custom RGB is available using
`mirv_streams record pass background <red 0..255> <green 0..255> <blue 0..255>`.
Transparency reuses HLAE's existing black/white matte transform. The world is
rendered once per isolated pass; only selected models or HUD paint are replayed
into a second color target, sharing scene depth and preserving the viewport and
viewmodel depth range. The two samples recover coverage, and the opt-in
straight-color conversion removes
coverage from the black render without selecting/removing a key color. This keeps
opaque greens and partially covered edges in the output. Capture failures retain
pair boundaries, so one failed sample cannot shift later frames. The mode adds
extra selected-object draws, a second readback, and matte processing; it does not
advance the demo or render the full scene a second time. Transform workers sleep
until shared tasks finish instead of busy-waiting. Capture surfaces use the
existing bounded readback pool; creation references are released when ownership
passes to the wrapper, avoiding a per-take COM surface leak.

Select an alpha-capable recording profile, such as `afxFfmpegProresAlpha` (ProRes
4444 MOV), before recording; ordinary H.264/H.265 profiles can discard alpha.
Console equivalent:

```text
mirv_streams record pass alpha 1
mirv_streams record screen settings afxFfmpegProresAlpha
```

For NVIDIA hardware, `afxFfmpegNvencColorAlpha` uses H.264 NVENC to write
`color.mp4` plus a synchronized grayscale `alpha.mp4` for each transparent isolate.
Use the mask as a **luma matte** (white opaque, black transparent) for the straight
color footage in your editor. Enable transparent isolated passes first:

```text
mirv_streams record pass alpha 1
mirv_streams record screen settings afxFfmpegNvencColorAlpha
```

Base, Depth and full scene reference write only `color.mp4`. This preset
requires a GPU/driver supporting NVENC H.264 4:4:4 lossless encoding. Color uses
NVENC; masks use single-threaded, lossless x264 ultrafast to avoid consuming a
second GPU session for each isolate. All five passes request five GPU sessions;
the optional full scene reference adds a sixth. Older drivers can expose a lower limit;
update the driver or enable fewer outputs if session allocation still fails.
It accelerates color encoding, while matte rendering/readbacks
still have their existing cost. The split graph and frame/mask integrity have been
tested with a CPU encoder substitute; hardware performance needs in-game testing.
Duplicating this preset preserves its separate RGB-only options in the editor.
Existing custom copies retain their own options and must be edited or copied again
to pick up this change. The built-in name uses the new settings after restarting.

The Recording tab contains a scrollable **FFmpeg log** with a **Clear log**
button. It shows each output's expanded command, encoder stderr and exit code,
labelled with the output path so simultaneous pass failures can be distinguished.
New messages follow the bottom while you are already there; scrolling up preserves
your position. The shared log is bounded to 256 KiB / 2,048 chunks and is session-only.
No FFmpeg log files are written to the recording folders. Console diagnostics remain
available, and failed encoder exits direct you to the overlay.
`OpenEncodeSessionEx failed: incompatible client key (21)` with multiple outputs
indicates an NVENC session-allocation failure; the subsequent AFX image-write
errors are a consequence of the encoder exiting.

`alpha 0` restores chroma backgrounds. The option cannot change during a take.
Matte reconstruction assumes ordinary alpha compositing; additive/refractive
materials and screen effects still need an in-game check.

World shadows are not separated. Color passes preserve the game's MSAA sample count;
only depth extraction uses a matched non-MSAA color/INTZ pair. The capture callback
explicitly binds the capture surface even if Source left an intermediate target
bound, and restores the previous color/depth surfaces and viewport afterward.
Depth uses the existing logarithmic axial-distance shader over 0–4096 world units
(near is dark; far is light), with the rendered frame's actual projection planes.
It is not a floating-point depth file. The HUD color mask brackets native in-game
`IVRenderView::VGui_Paint`, including its offscreen capture binding, rather than
the tools-only paint callbacks. Use lossless encoding when preserving
its values matters. Pass selections are session settings. This initial adapter still
needs an in-game check for TF2/TF2C render effects and per-pass output synchronization.

### Native POV-demo spectating

Click a player name in **Observing** to spectate immediately, or use:

```text
mirv_pov <player entity index>
mirv_pov off
mirv_pov mode first
mirv_pov mode third
```

The click/console action stops HLAE
camera playback/preview and releases the live view source without detaching or
replacing saved camera definitions. It temporarily sets the recorder's native
observer mode and target during rendering, then restores the original observer values
before scene capture and subsequent network updates. No camera rig is created. **POV view** selects native first-person or third-person
chase. For another player, chase reads their interpolated eye angles at the native
engine angle query, rather than using the recorder's look direction. The adapter
covers distinct `VEngineClient013/014/015` interfaces: TF2's client requests `014`,
while HLAE can select `015`; hooking only HLAE's interface leaves the native chase
query unchanged. Aliased interface vtables are hooked once. The recorder's
engine angles are never overwritten. The recorded player's own third-person view
uses Source's existing `thirdperson` behavior. Native chase positioning and collision
remain handled by Source. While a remote observer scope is active, Source's tools
third-person-camera visibility flag displays the recorder's world model and hides
the recorder's first-person viewmodels; it does not move the spectator camera.
The flag is released when the observer scope ends. In first-person observation,
the shared render classification excludes the observed player's own world mesh
and attached gear, so enabling the recorder's body does not put the target's
head or world weapon in front of the camera. Chase view retains the target model.

While observing another player, native input third-person offsets are cleared
before view calculation. The existing `tf_taunt_first_person` and
`cl_first_person_uses_world_model` options are enabled, `r_drawviewmodel` and
`cl_demoviewoverride` disabled, while native chase position and orientation are preserved as a pair.
An angle-only final override is not applied. These guards prevent recorder camera/viewmodel
state from overwriting remote spectating. The previous
values are restored on exit; taunt conditions and animations are not cleared. Native
view setup also uses the target's available recoil, FOV and eye-offset inputs;
those temporary substitutions on the recorder are restored immediately after
view calculation, before simulation, tools processing and scene-cache capture.
Unrecorded recoil is neutral, and missing eye-height data uses the standard fallback.

Returning to the recorded player, changing demos, or shutting down the level restores
normal observation. Targets are tracked by user ID and validated entity handles, so
a reused entity slot cannot silently select a different player.

A POV demo only contains entities and local-player data recorded by its author.
Spectating cannot reconstruct missing players, remote viewmodel animation, or HUD
information that the demo did not record. STV continues to use its native spectator
command separately.

Spectating diagnostics: `mirv_pov debug` prints native angle-query count, the
last native/target orientation and active camera guards. If the view still drifts,
include this output along with first/third-person mode and the demo tick.

### Compositing scope and depth diagnostics

These outputs are object isolates. Excluded models retain native depth writes,
but refractive cloak shaders and interleaved/additive particles still need a
depth-aware effects workflow for accurate reconstruction. Keep normal screen
recording as the beauty reference. See [TF2 pass compositing](TF2_PASS_COMPOSITING.md)
for the proposed capture contract and current unsupported cases.

After recording finishes, `mirv_streams record pass stats` reports GPU attempts,
complete captured frames and failures per output. Compare these along with media
frame counts/FPS before attributing depth stutter to capture or the codec.

### Attached collision reliability and diagnostics

The attachment controller now keeps return damping active when the camera-sized
hull overlaps geometry at the attachment point (common for gibs near a floor).
It searches a bounded distance on the existing boom for a clear sweep origin,
using a point trace to prevent searching through an intervening wall. Moving
obstacles still require immediate safety corrections; unobstructed return uses
the existing return half-life and speed cap. Pause freezes that return.

Embedded-start recovery probes upward before away-from-goal escape, preventing
a goal above a thin floor from first sending an embedded camera underneath it. First-frame results are checked against world geometry;
deeply embedded starts with no verified clear result report unavailable. A subject
can occupy all remaining space between camera and wall; world safety wins and
the diagnostic reports missing subject clearance rather than crossing the wall.

Cameras > Attached camera collision uses the existing shared collision controls:

- Minimum subject distance: optional distance from the physical attachment point.
  Useful for non-solid gibs that engine traces do not hit; zero keeps engine-only
  subject checks. This is an explicit exclusion distance, not a visible-mesh fit.
- Attached near-plane depth: zero keeps radius-only collision. A positive depth
  expands the existing clearance hull using a roll-safe bound for the attachment
  pose FOV and viewport aspect. Leave margin for later modifiers that widen FOV.
  Set it to the depth you want protected; it does not change the game near clip.
- Draw collision diagnostics: requires camera guides enabled. Draws the requested
  to corrected displacement, boom, corrected hull and contact normal. Labels
  identify embedded anchors and impossible subject gaps. Disabled in recording.

Console equivalents are `mirv_cam collision subjectClearance <0..256>`,
`mirv_cam collision nearPlane <0..32>` and `mirv_cam collision debug 0|1`.
These are shared live settings, like the existing radius/return settings.
Return speed zero is unlimited; half-life zero disables exponential easing.
The existing blocked-follow recovery delay can intentionally cut to a verified
clear shot after a closed-door obstruction; set it to zero for strict collision.
Historical world collision remains deferred: cached scrubbing never traces
against the unrelated live positions of doors or entities.

### Camera interaction and collision overrides

Global collision settings remain the defaults. In Cameras → Attached camera collision,
**Use per-camera collision override** lets selected cameras use their own clearance,
return, subject-distance and recovery settings. Disable it to inherit the globals.
Overrides participate in undo/redo and project format 8; older projects still load.
Freecam/campath collision remains global.

`mirv_cam edit <index/name> collision <field> <value>` edits an override; `collision
inherit` clears it and `collision status` reports effective settings and controller
state. Motion edits retain live return damping. Preserve framing now respects the
filtered subject/orbit frame and explicit aiming ownership; it does not lock every
raw bone sample to the same pixel. Camera and clip modifier outputs receive a final
shared collision safety check. See [camera interaction audit](CAMERA_INTERACTION_AUDIT.md)
for findings, test cases and live retest guidance.

### Proposed POV visibility continuity fix

See [TF2 POV visibility research](TF2_POV_VISIBILITY_RESEARCH.md) for the intended
map/prop continuity behavior, dormant-entity retention options, source evidence,
validation gates and proposed milestones. This is research, not an implemented fix.
