# Half-Life for Wii U

<img src="meta/icon.png" alt="Half-Life for Wii U icon" width="96" align="right">

A native port of Half-Life to the Wii U, for the Aroma homebrew environment.

It runs the game on [Xash3D FWGS](https://github.com/FWGS/xash3d-fwgs), an open-source engine compatible with
Valve's GoldSrc, with the game code from [hlsdk-portable](https://github.com/FWGS/hlsdk-portable). Rendering
uses the Wii U's GPU directly. Compatibility is included for playing Blue Shift and Opposing Force.

**You need your own copy of Half-Life from Steam.** No game files are included.

## Status

Playable on a real Wii U. Most of the game runs at 60 FPS; big fights with many
enemies can drop into the 50s. If you find a bug, please open an issue (see [Reporting a problem](#reporting-a-problem)).

## Features

 - Half-Life, Blue Shift and Opposing Force, each with its own icon on the Wii U Menu
 - Hardware rendering on the Wii U's GPU, 1280x720, on the TV and the GamePad at the same time (Off-TV play works)
 - Uses the current Steam version of the game files (25th Anniversary update), including Hazard Course and Uplink
 - Full GamePad controls, with gyro aiming (can be turned off, inverted or made faster/slower)
 - Wii U Pro Controller support, with the same button layout
 - Rebindable buttons and adjustable stick speed in the game's own Configuration menu
 - Quick save / quick load on the D-pad, plus the normal save and load menus with save pictures
 - Music, sound effects and voices, with a sound boost for the GamePad's speakers
 - On-screen keyboard (GamePad keyboard) for the console and for naming saves
 - HOME button support: the game pauses and resumes, and Close quits cleanly
 - On-screen text such as titles, credits, messages, the FPS counter and the console drawn larger, to be
   readable on a TV and on the GamePad
 - Custom icons and boot screens

## Controls (Wii U GamePad and Pro Controller)

| Button | Action |
|---|---|
| Left stick / right stick | Move / look |
| ZR / ZL | Fire / alt-fire |
| R / L | Next / previous weapon |
| A / B | Jump / crouch |
| X / Y | Reload / use |
| D-pad up / down | Flashlight / last weapon |
| D-pad left / right | Quick save / quick load |
| L3 (click left stick) | Walk (hold) |
| R3 (click right stick) | Look straight ahead |
| + | Pause menu |
| − | Gyro aiming on / off |

 - **Gyro aiming** (GamePad only) is on by default and adds to the right stick. It calibrates when the game
   starts and starts working once the GamePad has been held still for a second. To move the GamePad back to
   a comfortable position, turn gyro aiming off with −, move it, and turn it back on. Turning it on also
   recalibrates: hold the GamePad still for a moment, which also fixes a slow drift of the view. Gyro
   settings are in Configuration → Gamepad → Gyroscope.
 - **Pro Controller:** same buttons, without gyro aiming. The controller you last used is the one in
   control; you can switch between it and the GamePad at any time.
 - Buttons can be rebound in Configuration → Controls; stick speed is in Configuration → Gamepad.
 - The console (Main menu → Console) opens the GamePad keyboard; press Y to bring it back after closing it.

## Installing

Requires a Wii U with [Aroma](https://aroma.foryour.cafe/) installed. If you don't know how to mod your Wii U, [use this guide](https://wiiu.hacks.guide/)​.

1. Download the latest release from the [Releases page](https://github.com/NanoBunTV/Wii-U-Half-Life/releases/latest).
2. Copy the `halflife` folder to `sd:/wiiu/apps/`.
3. Copy the `valve` folder from your Steam install (`Steam\steamapps\common\Half-Life\valve`, about 600 MB) to
   `sd:/wiiu/apps/halflife/`.
4. Start Half-Life from the Wii U Menu. If the game files aren't found, it says so on screen.

**Blue Shift and Opposing Force** (if you own them on Steam): also copy the `bshift` (about 300 MB) and/or `gearbox`
(about 290 MB) folder from the same Steam folder to `sd:/wiiu/apps/halflife/`, next to `valve`.
Both need the `valve` folder too.

Saves and settings are stored in the game folders under `sd:/wiiu/apps/halflife/`.

## Settings and tips

 - **Sound** is boosted by default (`snd_gain 1.5`) because the GamePad's speakers are quiet. If loud sounds
   distort, lower it, for example with `+snd_gain 1.2` in `sd:/wiiu/apps/halflife/cmdline.txt`; the value is
   saved.
 - **Engine arguments** can be put on one line in `sd:/wiiu/apps/halflife/cmdline.txt`.
 - **FPS counter:** `cl_showfps 1` in the console.

## Known limitations

 - Single player only: there is no multiplayer.
 - Saves copied from a PC can't be loaded; they're marked as such in Load Game.
 - No intro videos.
 - Counter-Strike and other mods aren't supported.
 - If the game hits a fatal error, it closes without a message. Turn on logging (below) to find out why.

## Reporting a problem

Put `-log` in `sd:/wiiu/apps/halflife/cmdline.txt`. The game then writes its log to
`sd:/wiiu/apps/halflife/engine.log`. Attach that file to your issue, with the map you were on and what
happened. For slowdowns, a screenshot with `r_speeds 6` turned on in the console shows where the time goes.

## Credits

 - [Xash3D FWGS](https://github.com/FWGS/xash3d-fwgs) by Flying With Gauss and contributors - the engine.
 - [hlsdk-portable](https://github.com/FWGS/hlsdk-portable) by Flying With Gauss - the game code for Half-Life,
   Blue Shift and Opposing Force, based on Valve's Half-Life SDK.
 - [devkitPro](https://devkitpro.org/), [wut](https://github.com/devkitPro/wut) and
   [SDL2 for Wii U](https://github.com/devkitPro/SDL) - the toolchain and platform libraries.
 - [Aroma](https://aroma.foryour.cafe/) by Maschell - the homebrew environment.
 - [decaf-emu](https://github.com/decaf-emu/decaf-emu) - `latte-assembler`, used to build the GPU shaders.
 - [Cemu](https://cemu.info/) - used for testing during development.
 - The [Wii port of Xash3D](https://github.com/MintFerret/xash3d-fwgs) - proved that statically linking Xash3D
   on a big-endian Nintendo console works.
 - Claude - AI assistance.

## License

This port's own code is licensed under the GNU General Public License v3 or (at your option) any later
version. Refer to the provided [LICENSE.md](LICENSE.md) file for further information. The port's changes to the
engine and game code are in [`patches/`](patches/), applied to pinned upstream commits by
`scripts/fetch-deps.ps1`.

## Third-party licenses

### [Xash3D FWGS](https://github.com/FWGS/xash3d-fwgs)
by Flying With Gauss and contributors under GNU General Public License v3
### [hlsdk-portable](https://github.com/FWGS/hlsdk-portable)
by Flying With Gauss, based on Valve's Half-Life 1 SDK, under the Half-Life 1 SDK License (non-commercial use only)
### [mainui_cpp](https://github.com/FWGS/mainui_cpp)
by Flying With Gauss under GNU General Public License v2 or later
### [FreeVGUI](https://github.com/FWGS/freevgui)
by Nagist, Valve Software and Alibek Omarov under a BSD-style license
### [wut](https://github.com/devkitPro/wut)
by devkitPro under zlib License
### [SDL2](https://github.com/devkitPro/SDL)
by Sam Lantinga and contributors under zlib License
### [Opus](https://opus-codec.org/), [Ogg and Vorbis](https://xiph.org/)
by Xiph.Org Foundation and contributors under BSD License
### [mpg123](https://www.mpg123.de/)
by the mpg123 project under GNU Lesser General Public License v2.1
### [bzip2](https://sourceware.org/bzip2/)
by Julian Seward under the bzip2 License
### [miniz](https://github.com/richgel999/miniz)
by Rich Geldreich under MIT License

Half-Life, Blue Shift and Opposing Force are trademarks of Valve Corporation. This project is not affiliated
with or endorsed by Valve or Nintendo.

## Building

Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the Wii U packages, Git and PowerShell 7.

1. Install the Wii U packages from devkitPro's MSYS2 shell:
   ```
   pacman -S --needed wiiu-dev wiiu-sdl2 wiiu-pkg-config wiiu-cmake
   ```
2. Fetch the engine and game code (cloned into `external/` at pinned commits, with this port's patches applied):
   ```
   .\scripts\fetch-deps.ps1
   ```
3. Build:
   ```
   .\build.ps1
   ```
   This produces `build\halflife.wuhb`, `build\bshift.wuhb` and `build\opfor.wuhb`.

[Full build instructions and developer notes](Docs/BUILDING.md) · [Developer documentation](Docs/README.md)
