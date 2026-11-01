# Half-Life Advanced Effects - advancedfx

## About

Half-Life Advanced Effects (HLAE) is a tool for creating gaming videos and working with demos.

This repository contains an unofficial modification of [HLAE](https://github.com/advancedfx/advancedfx), with additional features for Team Fortress 2 (TF2). It is not maintained by the official HLAE team.

## Changes

* **Entity tools:** `mirv_listentities`, `mirv_listplayers` and `mirv_listbones` for inspecting entities and bones.
* **Entity indices:** `mirv_drawentities` displays entity indices in-game, with category filters, custom colors and label stacking.
* **Camera tools:** TF2 entity and bone support for `mirv_aim`, `mirv_calcs` and `mirv_cam attach`, including offsets and capturing camera framing relative to a target.
* **Campath preview:** `mirv_campath preview` allows navigation along camera paths, jumping between keyframes and seeking the demo. Paths with 1–3 keyframes can also be previewed. Home / End jump to the first / last keyframe.
* **Free camera:** Optional movement independent of demo timescale, changed Escape handling and `mirv_input camera uiCursor 1` for interacting with the Demo UI without leaving freecam.

Use the corresponding console commands without arguments for help (for example, `mirv_campath preview` or `mirv_drawentities`).

## Download

No releases of this modified build are available yet. For official HLAE releases, see [advancedfx releases](https://github.com/advancedfx/advancedfx/releases).

**VAC warning:** HLAE modifies game behavior. Use it for demo viewing and video creation only; do not join VAC-protected servers while using it.

**Epilepsy warning:** This software may display rapidly changing images and colors.

## System requirements

* Microsoft Windows 10 or newer
* Microsoft .NET Framework 4.6.2 or newer
* Genuine Steam version
* A lot of patience

## Support

This is an unofficial build. Issues related to its modifications should be reported to this repository, not to the official HLAE project.

For general HLAE documentation, see the [official wiki](https://github.com/advancedfx/advancedfx/wiki).

## Credits

HLAE is developed by the [AdvancedFX team](https://github.com/advancedfx/advancedfx). See [CREDITS.md](CREDITS.md) for the original credits.

## License

The MIT license applies to the corresponding parts of this repository. Submodules may have different licenses. See [LICENSE](LICENSE).

## How to build

[BUILDING.md](BUILDING.md)
