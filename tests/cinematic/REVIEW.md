# Windows review checklist

Use a clean checkout of `feature/cinematic-campath`. Initialize submodules and
build using `BUILDING.md`, testing both `Win32` and `x64` where the games support
them. No game binaries or MSVC build were executed in the Linux implementation
workspace. The core tests can run independently of HLAE and should run in both
Windows build architectures.

1. Load a TF2 demo, enable `mirv_campath edit on`, and capture three points with
   Insert. Check automatic path creation, visible curve, frustums and labels.
2. Toggle preview with Tab. Verify all navigation keys, paused-demo scrubbing,
   retained freecam pose on exit, and Shift+Escape exit. Verify arrows do not
   scrub in freecam editing. Check normal Source binds do not also fire.
3. Open the console, menu, and optional `uiCursor` UI while a scrub key is held.
   Alt-tab out and back. Scrubbing must not stick. Ctrl+Z/Y must reach console
   text editing while the console is open, and editor undo while it is closed.
4. Replace/insert/delete/select points, duplicate/remove paths, undo/redo each
   operation, and verify all inactive paths remain visible.
5. Exercise linear, smooth, Bézier handles captured with freecam, and analytic
   XY/XZ/YZ orbits. Re-anchor, rotate and scale without altering local geometry.
   Enable Z locking/grid, then capture/replace points and constrain existing ones.
6. For a straight equally spaced 3-point path: duration 10, middle hold 2. Check
   arrivals at 0, 4 and 10; departure at 6. Verify stops differ from holds.
   Add explicit segment timing, custom speed and reversing motion tracks.
7. Animate rotation and FOV on a single point. Rotate the anchor and confirm
   orientation keys and fixed targets move with it. Check native `mirv_aim`
   behavior with and without this feature enabled.
8. Capture start/end/hold duration with demo time and wall time. Reject a reversed
   or mismatched capture without losing existing data.
9. Schedule two paths with cuts and blends. Seek directly into each path and
   transition, backward and forward. Overlap selection must be stable. Gaps and
   `finish release` must show the current demo camera, not an old pose.
10. Save one path and a collection. Check extension defaults, spaces/UTF-8 file
    paths, refusal to overwrite without `overwrite`, and protected replacement of
    unsaved work. Invalid/truncated/unsupported XML must preserve the editor.
    Verify append, replace, undo import, classic import and baked legacy export.
11. Toggle every draw layer. Check main view/skybox filtering, aspect ratios,
    console/menu rendering, engine rendering with multicore on/off, and graphics
    device resets/resolution changes. Overlays must not alter subsequent game
    rendering. Disable overlays when recording clean footage.
12. Disable editing and end freecam. Load an existing classic campath and verify
    its commands, exact interpolation and classic preview still work.

See `doc/CINEMATIC_CAMERA.md` for command semantics, numerical approximations,
legacy import/export differences, limits and known validation boundaries.
