# Status, known issues and roadmap

## Test results

| Date | Build | Where | Result |
|---|---|---|---|
| 2026-10-13 | first link | Cemu 2.6 | Starts, logs, reports the missing game folder correctly |
| 2026-10-13 | + game data | Cemu 2.6, recompiler | Finds the game files and mounts all WADs, detects the GamePad, loads the server game code. Cemu crashes in `Delta_Init` |
| 2026-10-13 | same | Cemu 2.6, `--force-interpreter` | **Reaches the main menu** (the owner saw the title screen) |
| 2026-10-13 | zone.c `-O0` workaround, 8 MB stack, extras.pk3 in the .wuhb | Cemu 2.6, recompiler | **Main menu at ~36 FPS** (software renderer). extras.pk3 loads from `fs:/vol/content` |
| — | — | Real Wii U | **Not tested yet** |

## Test checklist for new builds

1. Cemu, recompiler: reaches the main menu (`run-cemu.ps1`). The log shows `Adding ZIP: fs:/vol/content/valve/extras.pk3`.
2. Menu navigation with the GamePad (D-pad / sticks, A / B).
3. New Game → Hazard Course and New Game → Easy (c1a0): the map loads, you can move, look and shoot.
4. Save and load (quick save / load, menu save / load).
5. Level transitions (c1a0 → c1a0a ...).
6. Sound: music (mp3 from `valve/media`), sound effects, voices.
7. Uplink (25th Anniversary content) starts from the menu.
8. HOME button: the menu opens and returning resumes the game.
9. Real hardware: the same tests; check frame rate with `cl_showfps 1`.

## Known issues

- **Menu image is zoomed and cropped** in Cemu: the logo runs off the right edge and the menu off the bottom.
  It's probably a mismatch between the engine's render size (1280x720, `DEFAULT_MODE_*`) and the SDL Wii U
  window or texture. SDL also logs `Unknown pixel format` once at video init. Look at `vid_sdl2.c` (the
  `SW_*` buffer functions).
- **Cemu recompiler crash** in `_Mem_Realloc` with `-O2`, worked around by building `zone.c` at `-O0` (see
  DEBUGGING.md). Unknown whether real hardware is affected.
- **Performance:** the software renderer will be slow on the real 1.24 GHz CPU. Cemu runs much faster than
  the hardware, so don't judge speed there. A GX2 renderer is the fix (roadmap).
- **No mouse cursor:** SDL's `CreateSystemCursor is not currently supported` messages are harmless.
- **No intro videos:** no ffmpeg; the menu logs `couldn't open "gamestartup"`, which is harmless.
- **No HTTPS:** mbedTLS isn't built. Only affects downloads from servers, not single player.
- **Controls aren't tuned for the GamePad yet:** they come from SDL GameController defaults and the game's
  controller config.
- **Only Half-Life (`valve`):** Blue Shift and Opposing Force need their own game code (not built yet), and a
  way to choose the game.
- **IPv4 only:** the Wii U network stack has no IPv6.

## Roadmap

1. Fix the menu scaling (above), then play through Hazard Course and the first chapters in Cemu.
2. GamePad controls: sensible default bindings, aim sensitivity, gyro aiming, on-screen keyboard for the
   console (SDL's Wii U swkbd).
3. Test on real hardware. Measure ref_soft performance and check whether the `zone.c` workaround is needed.
4. `ref_gx2`: a hardware renderer using GX2 (see ARCHITECTURE.md). The big one for performance.
5. Game selection plus Blue Shift (`bshift`) and Opposing Force (`gearbox`) game code. Both are in the Steam
   install, and hlsdk-portable has branches for them.
6. Polish: icon and splash art, a first-run message with clear instructions when `sd:/halflife/valve` is
   missing (currently only in the log), GamePad screen use (HUD, console or map).
