# Campath improvements from testing

Open backlog from the user's first Windows/in-game review, November 4, 2026.
These are future changes, not completed features. Add or revise items as testing
continues. Existing implementation/validation documents describe current behavior.

## Required direction for all future changes

- [ ] Follow HLAE's existing architecture, command structure, naming, formatting,
  help style, and visual conventions. Treat possible upstream integration as a
  design constraint, rather than a formatting pass at the end.
- [ ] Before introducing or replacing a subsystem, identify the existing HLAE
  implementation, explain any missing capability, and reuse or extend it where
  practical. New functionality must have a concrete reason to be separate.
- [ ] Audit the current implementation's duplicate vector/quaternion operations,
  XML reader/writer, drawing/text code, input handling, and command/help parsing.
  Compare against `shared/AfxMath.*`, the existing RapidXML persistence,
  `CampathDrawer.*`, `EntityIndexDrawer.*`, `shared/MirvInput.*`, and the existing
  command argument/help infrastructure. Preserve coordinate conventions, files,
  deterministic timing and meaningful tests during refactoring.
- [ ] Keep the genuinely new geometry/timing distinction, independent animation
  tracks and placement capabilities without assuming that every supporting
  primitive needs a new implementation. Reuse must not re-couple geometry to
  demo keyframe times or break classic behavior.
- [ ] Restrict future commits/pushes to `feature/cinematic-campath`. Do not modify
  any other branch or its contents.

## Keyboard ownership and menu access

- [ ] Fix Source binds firing while the editor owns keyboard input. This is a
  user-observed in-game defect/behavior gap, not a passing validation result.
- [ ] Route camera movement and editor actions through the appropriate existing
  HLAE input path before suppressing game input; blocking game binds must not
  disable free-camera movement.
- [ ] Account for key-down, key-up, repeat, character/system events, focus changes,
  and any additional Source input paths established by investigation. Prevent
  duplicate actions, missed releases and stuck keys.
- [ ] Preserve deliberate access to the console, game menu, demo UI and other
  menus. Visible UI must retain its intended input ownership; do not indiscriminately
  swallow the keys required to open/close or operate it.
- [ ] Test the user's reported leakage with bound editor keys and other bound
  keys, in both freecam editing and preview. Include held keys across menu opening,
  UI cursor handoff, alt-tab, editor exit and system switching.

Evidence: `MirvCinematic_KeyEvent` currently consumes only a whitelist of reserved
editor keys, passing other keys onward. That confirms incomplete keyboard
ownership. The exact paths responsible for any reserved-key leakage still need
in-game diagnosis; do not claim a specific root cause without that evidence.

## One `mirv_campath` command tree and explicit system selection

- [ ] Provide `mirv_campath system classic` and `mirv_campath system new`.
  Use `new` as the system name; do not require an additional branded name.
- [ ] Dispatch the `mirv_campath` command tree, subcommands and help according to
  the chosen system. Users should not have to insert `cinematic` into routine
  commands when outside edit mode.
- [ ] Separate chosen system, editor activation, preview, and playback state.
  Disabling the editor must not implicitly select classic or make loaded new paths
  inaccessible. Define camera handoffs explicitly and avoid unexpected stops.
- [ ] Retain each system's data when switching and preserve classic command/file
  behavior. Shared subcommand names must resolve predictably for the selected
  system, including classic `edit start/...`.
- [ ] Resolve the earlier playback workflow confusion with a consistent enable
  control and clear status/help, following classic conventions where appropriate.
  Whether timing capture should automatically enable playback remains a design
  decision; do not silently assume it was requested.
- [ ] Update docs/examples and handle any compatibility alias for the current
  `cinematic` prefix deliberately; its retention/removal has not been decided.

## Readable, contextual help

- [ ] Replace the single dense command dump shown in the user's screenshot with
  HLAE-style hierarchical help.
- [ ] Print one complete command/subcommand usage per line, with concise wording
  and indentation/spacing consistent with existing HLAE output.
- [ ] Root help should expose immediate subcommands; subcommand help should expand
  only the relevant branch. Include current values where HLAE normally does.
- [ ] Avoid pipe-separated lists of unrelated commands, unexplained shorthand and
  unnecessarily long lines. Ensure the output is understandable at ordinary game
  console widths/resolutions.
- [ ] Keep keyboard help discoverable without repeating the entire command tree.

## More keyboard editor operations

- [ ] Add convenient keyboard access to movement timing begin/end capture and
  selected-point hold begin/end capture.
- [ ] Add keyboard access to selecting/capturing incoming and outgoing curve
  handles with freecam, with clear feedback about what is being edited.
- [ ] Review point/path selection, insertion/replacement, and other frequently
  repeated operations for keyboard coverage.
- [ ] Choose and document the keymap with the user; specific additional bindings
  are not agreed yet. Reuse existing HLAE binding/input facilities where possible
  rather than creating an unnecessary parallel binding system.
- [ ] Keep console equivalents available. Minimize command entry for ordinary
  editing without making advanced controls or state changes undiscoverable.

## Larger and adjustable in-world labels

- [ ] Increase default text size for the new system's in-world point indices/path
  names, checking readability at common resolutions.
- [ ] Expose size adjustment through the selected system's `mirv_campath draw`
  subtree, following existing HLAE naming and value/help conventions.
- [ ] Reuse/extend existing rendering and text support where it meets the need;
  preserve device-reset handling, render-state restoration and thread safety.
- [ ] Check scaling, spacing, label placement and clipping. Decide whether point
  indices and path names need independent settings as part of that review.

Clarification: classic campath already provides
`mirv_campath draw keyIndex <fHeight>` for its index-label height. The new renderer
currently uses fixed 8-by-16-pixel glyph quads and does not expose a size control.
This is separate from the Source console's own font size: restructuring HLAE help
and scaling in-world labels do not automatically change the console font.

## Completion criteria for the next revision

- [ ] Root/subcommand help is readable and follows the selected system.
- [ ] Both command trees remain usable without data loss or ambiguous routing.
- [ ] Ordinary editing can use the expanded keyboard controls without firing
  gameplay binds; menus and demo UI remain accessible.
- [ ] In-world labels are readable by default and adjustable through commands.
- [ ] Reuse decisions and remaining new infrastructure are justified against
  existing HLAE code; style and integration have been reviewed.
- [ ] Re-run affected core checks, then validate the changed integration in actual
  Windows/Source sessions. Portable or cross-compiler checks alone cannot establish
  keyboard/menu/graphics behavior.
