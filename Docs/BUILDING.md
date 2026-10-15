# Building

## Requirements

- devkitPro, with these packages: `wiiu-dev` (wut, wut-tools, devkitPPC), `wiiu-sdl2`, `wiiu-pkg-config` and
  `wiiu-cmake`. Install them from devkitPro's MSYS2 shell:
  ```
  pacman -S --needed wiiu-dev wiiu-sdl2 wiiu-pkg-config wiiu-cmake
  ```
  Tested with wut 1.9.1, wiiu-sdl2 2.32.10, devkitPPC GCC 16.2 and CMake 4.0.2.
  - The owner's install is at `F:\Programs\devkitPro`. `build.ps1` also checks `C:\devkitPro`, the
    `DEVKITPRO_WIN` environment variable, and the folder of `make.exe` on PATH.
- Git and PowerShell 7.
- Python is not needed.

## Getting the sources

```
.\scripts\fetch-deps.ps1
```

This clones [Xash3D FWGS](https://github.com/FWGS/xash3d-fwgs) and
[hlsdk-portable](https://github.com/FWGS/hlsdk-portable) into `external/` at pinned commits (with
submodules), then applies `patches/*.patch`. Running it again is safe: patches that are already applied are
skipped. `-Force` discards local changes in `external/` first.

## Build

```
.\build.ps1
```

This produces `build\halflife.rpx`, `build\halflife.wuhb` and the same program as `build\bshift.wuhb` and
`build\opfor.wuhb`, plus `build\halflife.elf` with debug symbols
for decoding crash addresses.

- **First run:** configures CMake (`Unix Makefiles`, `$DEVKITPRO/cmake/WiiU.cmake`, `RelWithDebInfo`). Later
  runs only build.
- **Other commands:**
  - `.\build.ps1 clean` deletes `build\`.
  - `.\build.ps1 reconfigure` reruns CMake. Do this after adding source files to `external/`, because the
    source lists use `file(GLOB)`.
  - `.\build.ps1 -DebugBuild` builds `-O0` into `build-debug\`.
  - `-Jobs N` limits parallel jobs.
- **Paths with spaces:** GNU make can't handle the space in `F:\Wii U Homebrew\...`, so `build.ps1` maps the
  project folder to a free drive letter with `subst` (W: first), builds there, and removes the mapping.
  That's why paths in compiler messages start with `W:/`.

A full build takes about 2 minutes on the owner's PC. The log is quiet: third-party libraries are built with
`-w`.

## GX2 shaders

The renderer's shaders (`src/gx2gl/shaders/*.vsh` + `*.psh`) are GPU assembly. `scripts/build-shaders.ps1`
assembles them into `src/gx2gl/shaders/gx2gl_shaders.h`, which is kept in the repository, so a normal build
doesn't need the assembler. Only run the script after changing a shader.

It needs `latte-assembler.exe` from decaf-emu in `tools/latte-assembler/` (gitignored). The owner got it
from decaf-emu's CI build `decaf-emu-windows-2019.zip` (github.com/decaf-emu/decaf-emu/releases, tag
`release`, 2023-11-09): copy `latte-assembler.exe` and `zlib1.dll` out of it. Its `compile` command
(GLSL) needs AMD's shader analyzer and isn't used; `assemble` is. It checks clause addresses and
instruction counts (`ADDR(..)`, `CNT(..)`) and says what it expected, but not the hardware's register
read rules; see the comments at the top of the shaders.

## Changing the engine or game code (patches)

`external/` is not in git. The port's changes to upstream code live in `patches/`:

| Patch | Repository |
|---|---|
| `xash3d-fwgs.patch` | `external/xash3d-fwgs` (engine) |
| `library_suffix.patch` | `external/xash3d-fwgs/3rdparty/library_suffix` (`build.h`, platform names) |
| `mainui.patch` | `external/xash3d-fwgs/3rdparty/mainui` (its copy of `build.h`) |
| `hlsdk-portable.patch` | `external/hlsdk-portable` (its copy of `build.h`, `hud_fastswitch` default) |
| `hlsdk-bshift.patch`, `hlsdk-opfor.patch` | `external/hlsdk-bshift`, `external/hlsdk-opfor`: hlsdk-portable's `bshift` and `opfor` branches (Blue Shift, Opposing Force game code), same changes |
| `freevgui.patch` | `external/hlsdk-portable/freevgui` (submodule): big-endian fixes for reading TGA headers and VGUI data streams |

Workflow:

1. Edit files in `external/` directly. Guard Wii U changes with `#if XASH_WIIU`.
2. Build and test.
3. Run `.\scripts\make-patches.ps1` to regenerate `patches/`, then commit `patches/` along with everything
   else.

Code that only concerns the Wii U, like the platform layer, goes in `src/` rather than `external/`. The
engine includes `src/platform/wiiu/net_wiiu.h` through the `src/` include path.

## Updating the engine or game code

1. In `external/xash3d-fwgs` (or `hlsdk-portable`): `git stash`, `git fetch`, `git checkout <new commit>`,
   `git submodule update --init --recursive`, `git stash pop`. Fix any conflicts.
2. Run `.\build.ps1 reconfigure`. Upstream may have added files or changed `wscript` source lists or defines;
   compare `engine/wscript` and the `3rdparty/*/wscript` files with `cmake/ThirdParty.cmake` and
   `CMakeLists.txt`.
3. Test in Cemu (see DEBUGGING.md) and on hardware.
4. Update the pinned commits in `scripts/fetch-deps.ps1` and run `.\scripts\make-patches.ps1`.
5. Record the update in PROJECT.md's history.

hlsdk-portable source lists are read from its own `dlls/CMakeLists.txt` and `cl_dll/CMakeLists.txt`, so
added or removed game files are picked up automatically.

## Installing on a Wii U

Requires the Aroma environment.

1. Copy `build\halflife.wuhb` to `sd:/wiiu/apps/`, and for the expansions `build\bshift.wuhb` (Blue Shift) and
   `build\opfor.wuhb` (Opposing Force). Each shows up as its own icon on the Wii U Menu.
2. Copy the `valve` folder from the Steam install, e.g.
   `E:\Programs\Steam Library\steamapps\common\Half-Life\valve`, to `sd:/wiiu/apps/halflife/valve`. The folder is about
   600 MB. For the expansions also copy `bshift` (300 MB) and `gearbox` (290 MB) next to it. The other folders
   (`valve_hd`, `valve_downloads`, `cstrike`) aren't used.
3. Optional: put engine arguments in `sd:/wiiu/apps/halflife/cmdline.txt` on one line, e.g. `-dev 2 -console`.

## Icon and splash screens

The owner's art is in `meta/source` (2026-10-14): `intro.png` (boot screens), `intro.wav` (boot sound) and
`hl.png` / `bs.png` / `of.png` (icons for Half-Life, Blue Shift, Opposing Force). `make_art.ps1` turns them
into what the bundles use and must be run again after changing a source file:

```
powershell -ExecutionPolicy Bypass -File meta\art\make_art.ps1
```

| Output | Used as |
|---|---|
| `meta/icon.png`, `icon-bshift.png`, `icon-opfor.png` (128x128) | Wii U Menu icon of `halflife` / `bshift` / `opfor.wuhb` |
| `meta/tv-splash.png` (1280x720), `meta/drc-splash.png` (854x480) | boot screens on the TV and GamePad, all three bundles |
| `meta/bootSound.btsnd.gz` | boot sound, all three bundles |

The boot sound needs 48 kHz 16-bit stereo WAV, at most 20 s. A `.btsnd` is an 8-byte big-endian header
(0 = play once, loop start 0) followed by big-endian PCM. wuhbtool can't add it, so `build.ps1` puts it into
each bundle's `meta/` folder after the build with `scripts/wuhb-add-file.ps1`, which rewrites the bundle
(a RomFS) exactly as wuhbtool lays it out (checked: a round trip gives a byte-identical file). Aroma's
Wii U Menu plugin serves `meta/*.btsnd` from the bundle, and the bundle reader opens `x.gz` for `x`. A
plain `cmake --build` without `build.ps1` gives bundles without the boot sound. `-List` shows a bundle's
files: `.\scripts\wuhb-add-file.ps1 -Wuhb build\halflife.wuhb -List`.

## Releasing

Not set up yet. When it is: build on the owner's PC and attach `halflife.wuhb` to a GitHub release. The
owner does all commits, tags and pushes. The repository is https://github.com/NanoBunTV/Wii-U-Half-Life. The
engine is GPLv3, so the release must link to the full source, including the pinned upstream commits and
`patches/`.
