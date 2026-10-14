# Status, known issues and roadmap

## Test results

| Date | Build | Where | Result |
|---|---|---|---|
| 2026-10-13 | first link | Cemu 2.6 | Starts, logs, reports the missing game folder correctly |
| 2026-10-13 | + game data | Cemu 2.6, recompiler | Finds the game files and mounts all WADs, detects the GamePad, loads the server game code. Cemu crashes in `Delta_Init` |
| 2026-10-13 | same | Cemu 2.6, `--force-interpreter` | **Reaches the main menu** (the owner saw the title screen) |
| 2026-10-13 | zone.c `-O0` workaround, 8 MB stack, extras.pk3 in the .wuhb | Cemu 2.6, recompiler | **Main menu at ~36 FPS** (software renderer). extras.pk3 loads from `fs:/vol/content` |
| 2026-10-13 | same | Cemu 2.6, recompiler, `+map t0a0` | Cemu crashed in the client's VGUI setup (`vgui::Font::getTall`, null font): FreeVGUI read TGA headers in host byte order |
| 2026-10-13 | + `freevgui.patch` (big-endian fixes) | Cemu 2.6, recompiler, `+map t0a0` | **Hazard Course loads and renders** (tram start, ~15 FPS in Cemu). Controls and sound not checked yet |
| 2026-10-13 | same | Cemu 2.6, recompiler, played by the owner | **Hazard Course plays well**: GamePad controls and sound work. Look sensitivity is too high |
| 2026-10-13 | same | **Real Wii U** (owner) | **Runs**, but the frame rate is very poor (software renderer at 1920x1080). Exiting to the HOME Menu hung at "booting Wii U Menu" until a forced power-off (see Known issues) |
| 2026-10-13 | 1280x720 render size (`vid_wiiu_render_height`), stick look fix, menu cleanup | Cemu 2.6, recompiler | Menu at 60 FPS (was ~35); Hazard Course renders correctly, scaled to the TV |
| 2026-10-13 | GamePad layout, gyro (own `VPADRead`), HOME → Close fix | **Real Wii U** (owner) | Gyro does nothing (see Known issues) |
| 2026-10-13 | gyro via `__wrap_VPADRead`, − = gyro recenter | Cemu 2.6, recompiler | Binds correct, gyro calibrates (125 samples / 5 s); motion itself not testable in Cemu |

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

- **Other big-endian bugs are likely:** anything in the game code or FreeVGUI that reads binary files into
  structs without converting byte order breaks on the Wii U (see the FreeVGUI fix). The engine handles
  this itself; the game code (`cl_dll`, `dlls`) and FreeVGUI were written for little-endian PCs. Suspect
  this first when something works on PC and fails here.

- **HOME → Close software hung** on real hardware ("booting Wii U Menu" never finished). Cause found:
  SDL turns the close request into `SDL_QUIT`, and the engine passed it to the menu's "quit?" dialog,
  which nobody can see or answer while the app is in the background. Fixed (2026-10-13, `host_sdl2.c`:
  `SDL_QUIT` quits directly on Wii U); **not yet confirmed on hardware**. The in-game Quit hasn't been
  tested on hardware either.
- **Gyro didn't work on hardware** (owner, 2026-10-13). Likely cause: the gyro code called `VPADRead`
  itself, after SDL had already taken the frame's sample (each sample is returned once on hardware; Cemu
  throttles to one per 1/60 s, so Cemu still delivered some samples). This could also drop button
  presses. Fixed by wrapping `VPADRead` at link time so SDL stays the only reader (ARCHITECTURE.md);
  **not yet confirmed on hardware**. Cemu can't verify gyro motion. Still unverified: direction and speed
  (assumed revolutions per second, axes x/y/z = pitch/yaw/roll). If it's wrong, fix the axes/sign/scale
  in `SDLash_WiiUGyroFrame` (`joy_sdl2.c`); meanwhile `joy_gyro_pitch` / `joy_gyro_yaw` (negative =
  inverted) adjust it. `joy_debug 1` shows live gyro bars; the log says `Calibration done` or
  `Calibration failed` (fewer than 25 samples in 5 s, i.e. under 5 FPS at startup; `joy_calibrate_gyro`
  retries).
- **GamePad layout is re-applied after `unbindall`** (PC configs start with it and don't bind gamepad
  buttons). Rebinding works, but a GamePad button that's deliberately left unbound gets its default
  back on the next start.
- **Cemu recompiler crash** in `_Mem_Realloc` with `-O2`, worked around by building `zone.c` at `-O0` (see
  DEBUGGING.md). Unknown whether real hardware is affected.
- **Performance:** the software renderer is very slow on the real 1.24 GHz CPU (owner's first hardware
  test). Cemu runs much faster than the hardware, so don't judge speed there. The engine now renders at
  1280x720 (`vid_wiiu_render_height`, default 720, try 540 or 480) and the GPU scales it up; a GX2
  renderer is the real fix and is needed before a release (roadmap).
- **Settings hidden on Wii U:** video modes, touch controls, the update check, Previews and the mouse-only
  options (mouse sensitivity, invert mouse, mouse look, look strafe, mouse filter, raw input, input
  devices). Stick look speed is `joy_yaw` / `joy_pitch` in degrees per second (180 / 120); the mouse
  `sensitivity` from PC configs is cancelled out on Wii U.
- **No mouse cursor:** SDL's `CreateSystemCursor is not currently supported` messages are harmless.
- **No intro videos:** no ffmpeg; the menu logs `couldn't open "gamestartup"`, which is harmless.
- **No HTTPS:** mbedTLS isn't built. Only affects downloads from servers, not single player.
- **Only Half-Life (`valve`):** Blue Shift and Opposing Force need their own game code (not built yet), and a
  way to choose the game.
- **IPv4 only:** the Wii U network stack has no IPv6.

## Roadmap

1. Confirm on hardware: HOME → Close exits cleanly, gyro direction/speed, the GamePad layout (README).
2. On-screen keyboard for the console and save names (SDL's Wii U swkbd).
3. `ref_gx2`: a hardware renderer using GX2 (see ARCHITECTURE.md). **Required before release**: the
   software renderer is far too slow on hardware.
4. Play through the campaign in Cemu and on hardware; check whether the `zone.c` workaround is needed on
   hardware.
5. Game selection plus Blue Shift (`bshift`) and Opposing Force (`gearbox`) game code. Both are in the Steam
   install, and hlsdk-portable has branches for them.
6. Polish: icon and splash art, a first-run message with clear instructions when `sd:/halflife/valve` is
   missing (currently only in the log), GamePad screen use (HUD, console or map).
