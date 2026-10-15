# Project overview and decisions

## What it is

A native port of Half-Life (1998) to the Wii U as Aroma homebrew (`.wuhb`). It runs the game on
[Xash3D FWGS](https://github.com/FWGS/xash3d-fwgs), an open-source GoldSrc-compatible engine, with the game
code from [hlsdk-portable](https://github.com/FWGS/hlsdk-portable). Everything is built with devkitPro
(devkitPPC + wut) and statically linked into one executable.

**No game data is included.** Users copy the `valve` folder from their own Steam copy of Half-Life to the
SD card. The target is the current Steam build (the 25th Anniversary update; the owner's copy is build
`15961492`, updated October 2024), since that's what everyone who buys the game today has.

Valve's GoldSrc engine binaries (`hw.dll`, `hl.dll`, `client.dll`) can't be used: they're closed-source x86
code, and the Wii U has a big-endian PowerPC CPU. That's why the engine and game code are open-source
reimplementations.

## History

- **2026-10-13**: feasibility check. Conclusion: possible with Xash3D FWGS + hlsdk-portable; the hard parts
  are a GX2 renderer and big-endian bugs.
- **2026-10-13**: project scaffolded.
  - Toolchain: wut 1.9.1, wiiu-sdl2 2.32.10 and devkitPPC (GCC 16.2), installed through devkitPro pacman.
  - Sources: engine at FWGS master `e0ece3d` (2026-10-12) and hlsdk-portable `6c168fc` (2026-09-28).
  - Engine changes: added a `XASH_WIIU` platform (detection, platform hooks, IPv4-only networking).
  - Build: a CMake build that partially links each engine library and hides its symbols, the same idea as
    upstream's waf `--static-linking`.
  - First boot in Cemu 2.6: the engine starts, finds the game files and loads the game code. With
    `--force-interpreter` it **reaches the main menu**. With Cemu's default recompiler, Cemu crashes during
    `Delta_Init` (see DEBUGGING.md).

## Decisions and why

| Decision | Reason |
|---|---|
| Xash3D FWGS + hlsdk-portable, not a recompilation of Valve's binaries | Valve's engine is closed-source x86. FWGS is GPL, actively maintained, already big-endian aware, and runs the 25th Anniversary data |
| One statically linked executable | The Wii U has no `dlopen`. The engine already supports `XASH_STATIC_LIBS` (`engine/platform/misc/lib_static.c`) |
| Own CMake build instead of upstream's waf | wut ships CMake support (`wut_create_rpx` / `wut_create_wuhb`), and the game code has to be linked into the engine anyway. Waf has no Wii U target and no way to pull hlsdk-portable into the engine link |
| Hide each library's symbols (`ld -r` + `objcopy -G` + strip COMDAT groups) | Client and server share the weapon and player-movement code; every renderer defines `GetRefAPI`. The [Wii port](https://github.com/MintFerret/xash3d-fwgs) renamed clashing symbols by hand, which is fragile across engine updates |
| Engine source not vendored: `external/` + `patches/` | Keeps the repository small and makes it obvious which lines are the port's. `scripts/fetch-deps.ps1` pins exact upstream commits |
| Game files at `sd:/wiiu/apps/halflife/valve` | Next to `halflife.wuhb`, owner's decision (2026-10-14). Was `sd:/halflife/valve` before, to keep game files out of the folder the Aroma launcher scans for apps; it only looks for `.wuhb`/`.rpx` files, so a data folder there is fine |
| One `.wuhb` per game (Half-Life, Blue Shift, Opposing Force), same program | Owner's choice (2026-10-14) over a picker or in-menu switching: each game has its own Wii U Menu icon, and the engine can't restart into another game on Wii U |
| Engine extras (`extras.pk3`) inside the `.wuhb` | Users copy only the Steam `valve` folder. The `.wuhb` content folder is mounted as the engine's read-only root |
| Software renderer (`ref_soft`) first, GX2 renderer later | `ref_soft` needs no GPU work, so the rest of the port could be tested first. Done 2026-10-14: `ref_gl` on gx2gl is the default renderer, `ref_soft` the fallback (`-ref soft`) |
| IPv6 disabled | The Wii U network stack (nsysnet) is IPv4 only |
| Logs go to OSReport and UDP | OSReport shows in Cemu's log; UDP port 4405 works on real hardware with devkitPro's `udplogserver` |

## Owner preferences (for AI assistants)

- **Never run `git commit` or `git push`**, and don't stage changes. The owner handles all git operations.
- Test in **Cemu** first (`E:\ROMs and Emulators\_Emulators\Cemu (Wii U)\Cemu.exe`). It's the owner's
  convenience debugging setup. Cemu's SD card is `E:\ROMs and Emulators\_Emulators\Cemu (Wii U)\sdcard`,
  reached through a junction from `%APPDATA%\Cemu\sdcard`, because the owner's C: drive is short on space.
  Don't put large files on C:.
- The owner's Half-Life install (source for test data) is `E:\Programs\Steam Library\steamapps\common\Half-Life`.
  Never write into it.
- Documentation lives in this `Docs/` folder, written so the next session can continue without the chat history.
