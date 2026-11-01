# Additional TF2 commands

This page covers the main features added in this modified build. Enter commands in the game console while viewing a demo. For general HLAE commands, see the [official wiki](https://github.com/advancedfx/advancedfx/wiki).

## Entity information

```text
mirv_listplayers
mirv_listentities sort=distance
mirv_listbones 5
```

`mirv_listplayers` lists player names and entity indices. `mirv_listentities` lists active entities, optionally sorted by distance. Replace `5` in `mirv_listbones 5` with the index of an entity to list its bones.

To display entity indices in the game view:

```text
mirv_drawentities enabled 1
mirv_drawentities filter projectiles 0
mirv_drawentities size 32
mirv_drawentities enabled 0
```

The `filter` command enables (`1`) or disables (`0`) a category. Categories include `players`, `projectiles`, `weapons`, `cosmetics` and `other`. Use `mirv_drawentities` without arguments for the remaining settings, including colors and label stacking.

**Note:** Entity labels are part of the rendered picture and will appear in recordings. Disable them before recording if they are not wanted.

## Camera attachment

First find an entity index using `mirv_listplayers` or `mirv_listentities`.

```text
mirv_cam attach 5
mirv_cam attach 5 bip_head
mirv_cam attachMode entity
mirv_cam attach clear
```

`mirv_cam attach` uses an entity or a bone as the camera source. Replace `5` with an entity index and `bip_head` with a bone name or part of its name (or use a bone index). `attachMode` accepts `bone`, `entity` or `aligned` to control orientation when using a bone.

To capture a camera position relative to the attached target:

```text
mirv_cam attach 5
mirv_input camera
mirv_cam attach capture
mirv_input end
```

Position the free camera as desired before running `mirv_cam attach capture`. The camera will then follow the target with the captured framing.

## Campath preview

Save a camera keyframe using the usual campath command:

```text
mirv_campath add
mirv_campath preview enter
```

While preview is active and the console is closed:

| Key | Action |
| --- | --- |
| Up / Down | Next / previous keyframe |
| Home / End | First / last keyframe |
| Left / Right | Travel along an interpolated path, or step between bookmarks |

A path with even one keyframe can be previewed.

```text
mirv_campath preview seek
mirv_campath preview 0
mirv_campath preview exit
mirv_campath preview toggleKey f8
```

`seek` moves the demo to the previewed camera's time; navigating the cameras alone does not seek. `preview 0` selects the first keyframe (indices start at zero). `exit` leaves the free camera at its current preview pose. `toggleKey f8` assigns F8 to toggle preview without a Source key bind.

For more commands, including `interp` and `toggle`, enter `mirv_campath preview`.

## Free-camera controls

```text
mirv_input camera ignoreTimescale 1
mirv_input camera uiCursor 1
```

`ignoreTimescale 1` makes free-camera movement independent of demo playback speed (enabled by default). `uiCursor 1` allows interaction with visible game UI, such as the Demo UI, without leaving freecam. Use `0` to disable either option.

While in freecam, **Escape** opens the game menu and **Shift+Escape** exits freecam.
