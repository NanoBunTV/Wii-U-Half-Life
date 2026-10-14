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
| 2026-10-13 | same | **Real Wii U** (owner) | Still no gyro. `joy_debug` unusable: the on-screen keyboard is too slow at this frame rate |
| 2026-10-14 | gyro starts calibrated (no startup calibration), `-log` → `sd:/halflife/engine.log` | Cemu 2.6, recompiler | `engine.log` written; log shows `GamePad gyro: receiving samples` |
| 2026-10-14 | same | **Real Wii U** (owner) | Gyro reacts: up/down inverted, left/right nothing, steady drift down-left (offset ~0.014/0.013/0.024 rev/s in the log). − still toggled the gyro: the old bind was saved in `config.cfg` |
| 2026-10-14 | GamePad axes converted to the view (gravity-based yaw), startup calibration back on raw axes (needs ≥ 3 samples) | Cemu 2.6, recompiler | Calibrates; no motion in Cemu |
| 2026-10-14 | same | **Real Wii U** (owner) | Gyro off: `Calibration failed, got samples 2 < 3`. The menu loads at ~2 frames in the first 5 s. Accelerometer held upright ≈ (−0.01, −0.13, −0.97), as predicted |
| 2026-10-14 | Wii U calibration never fails: keeps collecting until 20 samples | Cemu 2.6, recompiler | Calibration done (117 samples) |
| 2026-10-14 | same | **Real Wii U** (owner) | **Gyro works great**, no drift; only left/right inverted (yaw sign flipped in the next build, not re-tested) |
| 2026-10-14 | **ref_gl on gx2gl** (hardware rendering), `-ref gl` | Cemu 2.6, recompiler | **Menu correct at 60 FPS** (background, fonts, blending, animated logo). **Hazard Course renders**: textures, alpha-tested grates, depth, lightmaps (after the `GL_EQUAL` → `LEQUAL` fix). Looks brighter than `ref_soft` (GL overbright lighting) |
| 2026-10-14 | same | **Real Wii U** (owner) | **Hazard Course at 30–60 FPS, "perfectly playable"**, nothing looks broken. HOME → Close hung at the system loading screen again: the log stops at `Shutting down audio.`, so the renderer's shutdown waited on the GPU in the background |
| 2026-10-14 | gx2gl checks the foreground live before every GPU call | **Real Wii U** (owner) | **HOME → Close works** |
| 2026-10-14 | gl is the default renderer (saved `r_refdll` ignored on Wii U) | Cemu 2.6, recompiler | Starts with `ref_gl` without `-ref`. c1a1, c2a5, c4a1 (Xen sky, glow sprites), c2a3: weapon view models, HUD, studio models and sky correct at 60 FPS. Decals invisible: Cemu recompiler bug (DEBUGGING.md) |
| 2026-10-14 | + big-endian fix in `img_wad.c` (palette index 255 alpha) | Cemu 2.6, `--force-interpreter` | Bullet decals blend correctly, spark particles render |
| 2026-10-14 | data folder moved to `sd:/wiiu/apps/halflife`; gx2gl sends only changed state; GamePad rumble via `VPADControlMotor` | Cemu 2.6, recompiler | Starts from the new folder, `-log` writes there. Menu, c2a5, c4a1 look the same as before. No more `SDL: [Error] That operation is not supported` (was 540 lines in a few seconds of c4a1's `env_shake`) |
| 2026-10-14 | same | **Real Wii U** (owner) | FPS "roughly exactly the same" as before the state caching. Decals and rumble not reachable without a late save (now: `scripts/gxtest.cfg`). Audio a little quiet on the GamePad speakers |
| 2026-10-14 | vsync waits only with 2 flips pending, `r_speeds 6` counters, `snd_gain` 1.5 | Cemu 2.6, recompiler | c2a5: 4.7 ms CPU per frame, 215 draws, 11,400 vertices (Cemu's CPU is much faster than the console's) |
| 2026-10-14 | same | **Real Wii U** (owner) | c1a0 tram: **61 FPS**, frame 17.0 ms = 8.1 ms CPU + 8.9 ms vsync wait, GPU wait 0, 59 draws, 6,072 vertices. The console's on-screen keyboard is invisible with `ref_gl` (key clicks are heard) |
| 2026-10-14 | gx2gl draws SDL's on-screen keyboard (`WIIU_SWKBD_Draw`) | Cemu 2.6, recompiler | `+toggleconsole`: keyboard shown, no crash. With SDL's sRGB switch around the draw Cemu crashed, so that's left out |

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
  `SDL_QUIT` quits directly on Wii U). The owner's hardware logs since then end with a clean
  `Stopped with reason "caught SDL_QUIT"` and no hang was reported. The in-game Quit hasn't been tested
  on hardware. With `-ref gl` it hung again (2026-10-14) for a different reason, gx2gl waiting on the GPU
  in the background; fixed, not re-tested. A log that ends before `Stopped with reason` points to a hang in
  the shutdown code after the last line.
- **Gyro aiming: solved** (owner: "works great", 2026-10-14, with yaw flipped in the final build). The
  path there, for future reference:
  1. A second `VPADRead` caller starved the gyro and could steal button presses from SDL. Each sample is
     returned once on hardware; Cemu throttles to one per 1/60 s, so Cemu hid the problem. Fixed by
     wrapping `VPADRead` at link time (ARCHITECTURE.md).
  2. The engine's startup calibration counts one sample per frame and failed at the menu's ~2 FPS
     load, leaving the gyro off. On Wii U it now keeps collecting until it has 20 samples.
  3. The GamePad gyro has a real zero offset (several °/s), so calibration is needed. It runs on the
     raw GamePad axes.
  4. The axes: pitch = −x; yaw = −(rotation around the smoothed accelerometer's up axis), so turning
     works at any holding angle (`SDLash_WiiUGyroToView`). Cemu's SDL→VPAD conversion got pitch right
     but suggested the wrong yaw sign.

  Diagnosis: run with `-log` (cmdline.txt); `sd:/wiiu/apps/halflife/engine.log` shows
  `GamePad gyro: receiving samples (gyro …, acc …)` and `Calibration done. Result: …`. Players can invert
  or scale it in Configuration → Gamepad → Gyroscope.
- **Changed default bindings don't reach existing installs:** the engine saves all bindings to
  `config.cfg` on exit, and that file is loaded over the defaults. Testers must fix or delete the line
  by hand (this happened with − on 2026-10-14).
- **GamePad layout is re-applied after `unbindall`** (PC configs start with it and don't bind gamepad
  buttons). Rebinding works, but a GamePad button that's deliberately left unbound gets its default
  back on the next start.
- **Cemu recompiler crash** in `_Mem_Realloc` with `-O2`, worked around by building `zone.c` at `-O0` (see
  DEBUGGING.md). Unknown whether real hardware is affected.
- **Performance:** the software renderer is very slow on the real 1.24 GHz CPU (owner's first hardware
  test). Cemu runs much faster than the hardware, so don't judge speed there. The hardware renderer
  (`ref_gl` on gx2gl, ARCHITECTURE.md) is the fix: 30–60 FPS in the Hazard Course on hardware, and the
  default since 2026-10-14 (`-ref soft` for the old one). gx2gl sends only changed state since
  2026-10-14 (hardware FPS not compared yet). Still on the CPU: every vertex goes through immediate mode and
  is converted to gx2gl's format. Next candidate: VBO support, so `ref_gl` can keep the world in GPU
  memory.
- **gx2gl gaps:** screenshots are black (`glReadPixels`), no stencil (`ref_gl`'s stencil shadows off, and
  decals on transparent `{` surfaces such as grates are skipped), decals not yet checked on hardware, water
  not checked yet, the `GL_EQUAL` → `LEQUAL` workaround (ARCHITECTURE.md).
- **Decals in Cemu:** invisible with the recompiler (Cemu bug, DEBUGGING.md); use `--force-interpreter`.
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

1. Hardware renderer (`ref_gl` on gx2gl, ARCHITECTURE.md), **required before release**: started
   2026-10-14; the Hazard Course plays at 30–60 FPS on hardware, HOME → Close works, and it's the default
   renderer. Sprites, studio models, sky, decals and particles checked in Cemu. Next: optimise (state
   re-sending, CPU vertex work), check water and decals on hardware, `glReadPixels` for screenshots.
2. Confirm on hardware: the gyro yaw flip, the in-game Quit.
3. On-screen keyboard: SDL's swkbd opens for the console and is drawn by gx2gl since 2026-10-14 (to
   confirm on hardware). Check save names and the multiplayer name field too.
4. Play through the campaign in Cemu and on hardware; check whether the `zone.c` workaround is needed on
   hardware.
5. Game selection plus Blue Shift (`bshift`) and Opposing Force (`gearbox`) game code. Both are in the Steam
   install, and hlsdk-portable has branches for them.
6. Polish: icon and splash art, a first-run message with clear instructions when `sd:/wiiu/apps/halflife/valve` is
   missing (currently only in the log), GamePad screen use (HUD, console or map).
