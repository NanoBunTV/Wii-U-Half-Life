# Half-Life for Wii U

A native port of Half-Life to the Wii U (Aroma homebrew), running on the open-source
[Xash3D FWGS](https://github.com/FWGS/xash3d-fwgs) engine with [hlsdk-portable](https://github.com/FWGS/hlsdk-portable)
game code.

**Work in progress.** It reaches the main menu in the Cemu emulator, and hasn't been tested on a real Wii U
yet. See [Docs/STATUS.md](Docs/STATUS.md).

**You need your own copy of Half-Life** (Steam). No game files are included.

## Installing

Requires the Aroma environment.

1. Copy `halflife.wuhb` to `sd:/wiiu/apps/`.
2. Copy the `valve` folder from your Steam install (`Steam\steamapps\common\Half-Life\valve`, about 600 MB)
   to `sd:/halflife/valve`.
3. Start Half-Life from the Wii U menu.

## Controls (Wii U GamePad)

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
| − | Gyro recenter: levels the view; gyro aiming is paused while held |

Gyro aiming is on by default and adds to the right stick. Keep the GamePad still for a few seconds after
starting so the gyro can calibrate. To recenter, hold −, move the GamePad back to a comfortable position
and let go. Gyro aiming can be turned off, inverted or made faster/slower in Configuration → Gamepad → Gyroscope. Buttons can be rebound in Configuration → Controls; stick speed is in
Configuration → Gamepad.

## Building

See [Docs/BUILDING.md](Docs/BUILDING.md). In short, with devkitPro installed:

```
.\scripts\fetch-deps.ps1
.\build.ps1
```

Developer documentation is in [Docs/](Docs/README.md).

## Credits and license

- Xash3D FWGS by Flying With Gauss and contributors (GPLv3). This port's engine changes are in `patches/`.
- hlsdk-portable by Flying With Gauss, based on Valve's Half-Life SDK (Half-Life 1 SDK license).
- mainui_cpp, FreeVGUI, and the libraries in `xash3d-fwgs/3rdparty` (opus, vorbis, ogg, mpg123, bzip2, miniz)
  under their own licenses.
- Wii U support through devkitPro, wut and SDL2 for Wii U.
- The idea of statically linking Xash3D on a big-endian Nintendo console was proven by the
  [Wii port](https://github.com/MintFerret/xash3d-fwgs).

Half-Life is a trademark of Valve Corporation. This project is not affiliated with or endorsed by Valve.
