# TF2 camera overlay — Paper port

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

Retiming keys in the sequencer, inspector or graph preserves the shaped editable
intervals. Effective Auto tangents are frozen to Free and incoming/outgoing
velocities are scaled by their own interval duration ratios; accelerations use
the squared ratios. This avoids stretching a handle into an overshoot or Euler
revolution when making a shot longer. Hold durations and explicit angle winding
are retained. Crossing key order, collisions and invalid hold timings reject the
whole edit. With editable Euler rotation, Classic position/FOV curves convert
through the existing exact editable conversion when necessary to retain their
shape. Reset tangents to Auto explicitly if you want the interpolation solver to
recalculate a different curve after retiming. Unequal interval duration changes
can change speed continuity even though the spatial curves are preserved.

The sequencer keeps classic zero-based key indices when zooming or panning past
hidden keys. Zoom changes visibility, not numbering.

The overlay uses ImGui's own drawn cursor, including the I-beam over text fields.
Windows cursor changes and Source UI queries are avoided in the busy cursor hooks.
Menus, focus loss and free-camera mouse-look restore the game's cursor; they also
end an active editing gesture. The renderer consumes the newest queued UI frame
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
mirv_cam collision radius 6
mirv_cam collision players 1
mirv_cam collision response slide
mirv_cam collision status
mirv_cam collision reset
```

Use `free 0`, `campath 0` or `attached 0` to disable each independently. Radius is the
half-width of a swept axis-aligned camera cube, from 1 to 64 world units. Clearance and player filtering are shared. Stop/slide applies to free cameras
and campaths; attached cameras use the boom response described below.
`response stop` stops the requested movement at a solid; `slide` retains movement
along obstacle surfaces. Angular and FOV animation are not changed. Free-camera
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

### Attached camera retraction and positional following

`mirv_cam collision attached 1` traces from the current attachment bone/entity
pivot toward the final offset camera position, excluding the attached subject.
Walls shorten the offset immediately; clearing an obstacle extends it gradually
using the return settings. Only boom length is damped: its direction responds
immediately to entity/bone rotation. The extension speed cap does not cap the
subject's movement. This is boom retraction, not the free/path slide
response. If the pivot itself lies inside world geometry, the previous clear
camera position is retained when available. If retraction would put the camera
inside its subject, a bounded search of up to 64 units finds a nearby clear
endpoint without crossing walls. A valid framing is not always possible when
the subject and surrounding geometry leave no space for the camera hull.

```text
mirv_cam attach follow 0.15
mirv_cam attach follow boneHalfLife 0.08
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

For a stationary target correction starting at rest, a half-life is the time to
close half the positional gap. The exact spring update preserves velocity across
small corrections instead of inferring a new velocity from each positional jump.
It does not reconstruct missing snapshots. Angles and rotated camera offsets
remain immediate in the existing attachment modes; the aiming override order is
unchanged. Use `world` mode below to keep orientation independent of animation.

`maxDistance` bounds the total position lag from both filters. `teleportDistance`
resets both filters when the entity moves farther than that many units between
frames; 1024 is the default and 0 disables this distance reset. Small corrections
are smoothed. Explicit editor seeks, backward time, attachment/entity changes,
loading and time gaps greater than 0.25 demo seconds reset the filters. Missing
attachment poses also invalidate the history. Filters advance with demo time and
freeze during pause or timescale 0. Use `reset` to reframe while paused. Set both
`follow 0` and `follow boneHalfLife 0` to disable positional smoothing entirely.
For repeatable recording, begin from the same earlier point and allow preroll.
Collision remains independent and takes priority over smoothing immediately.

`attachMode entity` uses entity-facing angles at the bone position; `bone` uses
bone angles; `aligned` initially aligns with the entity and then follows bone
rotation. The new `world` mode follows the entity/bone position but keeps the
camera heading it had when attached. Its offset stays in that fixed orientation
rather than orbiting around the subject as the subject turns. Head bob and other
bone translation still affect the camera and can be softened with `boneHalfLife`.
A separate `mirv_aim` override can still change the final angles.

Set the orientation mode before attaching; changing it does not rebuild an
existing attachment. For example, replace the player and bone with your target:

```text
mirv_cam attachMode world
mirv_cam attach "Player name" "head" -60 0 0
mirv_cam attach follow 0.15
mirv_cam attach follow boneHalfLife 0.08
```

`mirv_cam attach capture` also works in `world` mode: the captured camera heading
and world-space offset stay fixed while the target translates. Later manual
camera rotation/offset controls remain available. The Observing tab exposes the
orientation choice; the Camera collision section exposes the smoothing controls.

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
| Shift+E | Replace the one selected key's pose, keeping its time, hold and tangents |
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

The overlay draws only when render target zero is the swapchain backbuffer.
Offscreen EndScene calls (for example while updating a game texture) are skipped
without consuming the queued UI snapshot or the frame's overlay draw opportunity.
A later backbuffer scene can draw it. This prevents overlay geometry from being
written into reusable game textures, where corruption can persist after closing
an overlay. Device/target-query failures skip drawing.

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

Choose **Edit** on a timeline key, then enable **Show Gizmo**. **G** selects
translation and **R** rotation when a text field is not active. Local/world modes
and Ctrl snapping are available. **C** toggles free-camera control. Camera Control
also provides FOV, roll and keyboard speed sliders.

The **Observing** tab is a player list with a text filter. Click a player name to
select and spectate them in an STV demo. The command uses the player's explicit
`#userid`, which is different from their entity slot. Selecting a player clears
path preview, free-camera and attachment overrides, disables automatic directing,
and switches to TF2's in-eye spectator camera. There are no buttons inside the
player rows. Native spectator targeting follows living players; POV demos cannot
switch their recorded spectator target, and the panel reports that restriction.

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

## Recording, files and settings

The Recording tab uses TF2 screen recording through `mirv_streams`. Choose a
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
