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
| 2026-10-14 | same | **Real Wii U** (owner) | **Keyboard shows. Decals and screen shake work.** GamePad rumble not felt. Load Game list: "<missing GameHeader>" for Wii U saves, "<corrupted>" for the Steam PC saves; opening it in c4a1 crashed (DSI in `strcasecmp` from `SV_GetSaveComment`). Loading a quick save from the title screen popped up the keyboard |
| 2026-10-14 | save list fix, keyboard only on purpose, rumble pattern starts "on", `hud_fastswitch 1` | Cemu 2.6, recompiler | Load Game in-game: Wii U save listed ("APPREHENSION", 00:04), PC saves "<PC save, can't be loaded>", no crash. Console keyboard still opens with `toggleconsole`. Save dates show Jan 1 1980 (file time 0, at least in Cemu) |
| 2026-10-14 | same | **Real Wii U** (owner) | **Loading works** (quick saves from the title screen, c2a3 → c0a0 → c4a1). GamePad rumble still not felt |
| 2026-10-14 | rumble v3 (solid runs, no per-frame re-send, logged), `glReadPixels`, missing game files screen | Cemu 2.6, recompiler | `vibrate 500` / `vibrate 1500`: `VPADControlMotor returned 0`. Save picture (356×200) and `screenshot` (1280×720 PNG in `valve/scrshots`) show the scene, right way up (black with `LINEAR_ALIGNED`). Without `valve/` the help text appears on the TV |
| 2026-10-14 | same | **Real Wii U** (owner) | **GamePad rumble "works great"** |
| 2026-10-14 | (row below) | **Real Wii U** (owner) | Save pictures from the menu showed the menu; the last used menu button stayed highlighted; Blue Shift and Opposing Force menus had no title |
| 2026-10-14 | world rendered for the save picture, stale menu focus cleared, animated title for every game | Cemu 2.6, recompiler | Save from the in-game menu: picture shows the game. Blue Shift menu shows the HALF-LIFE title |
| 2026-10-14 | Blue Shift + Opposing Force (`bshift.wuhb`, `opfor.wuhb`), menus trimmed (no Multiplayer / Change game, Console always shown, Controls list GamePad-only), HUD text x2, config version 1 | Cemu 2.6, recompiler | Blue Shift: ba_tram1 with its blue HUD and credits (now readable). Opposing Force: of1a1 with its own entities. Main menu: Console, New game, Hazard course, Configuration, Load game, Quit. Cemu itself crashed on `changelevel` and on the Opposing Force menu; both fine with `--force-interpreter` (Opposing Force Controls: no Communication / Capture the Flag sections, only GamePad buttons) |
| 2026-10-14 | gx2gl: 32-byte vertices written once + indexed draws, fast path for ref_gl's model arrays; `r_speeds 6` frame phase breakdown | Cemu 2.6, recompiler | c1a0 lobby (same view): CPU 5.9 → 4.1 ms, studio model vertices 3.5 → 1.6 ms. c2a5: correct. (Cemu timings, not hardware) |
| 2026-10-14 | polygon offset factor no longer ×16 (GX2 does it) | Cemu 2.6, recompiler | Owner saw faces through other faces; c1a0's door wheels poked through the wall above them (also with the old gx2gl). Fixed; the wheels match `-ref soft` apart from GL's brighter lighting |
| 2026-10-14 | same | **Real Wii U** (owner), `r_speeds 6` | 58–60 FPS everywhere shown. c1a0 tram with the lobby in view: CPU 8.9 ms (game 1.3, client 0.4, sound 1.2, world 1.4, entities 3.0 of which studio models 2.4 / their vertices 2.0, HUD 0.3, gx2gl 0.5), 199 draws, 10,120 verts, 2,976 model polys. Tram in a tunnel: CPU 4.8 ms. c1a0 lobby: CPU 8.8 ms (game 1.6, client 1.3, sound 0.2, world 1.1, studio models 2.1 / vertices 1.8). Hardware takes about twice Cemu's CPU time |
| 2026-10-14 | same | **Real Wii U** (owner), `r_speeds 6` | c1a0 door wheels correct. Surface Tension: canyon with 3 soldiers 60 FPS, CPU 12.4 ms (game 2.6, studio models 3.4). Open walled area with 4–5 soldiers 59 FPS, **CPU 16.5 ms, at the limit**: game 2.5, client 1.4, world 2.4, entities 6.4 of which studio models 5.4 (vertices 3.2, so about 2.2 in the game's bone setup), HUD 0.8, gx2gl 0.8; 322 draws, 13,808 verts, 4,615 model polys |
| 2026-10-14 | fast sin/cos + blending for animation, cheaper model lighting | **Real Wii U** (owner), `r_speeds 6` | Same walled area, 4 soldiers: **CPU 15.2 ms** (was 16.5): game 2.0, client 3.4, sound 0.2, world 2.0, entities 5.1 of which studio models **4.0** (was 5.4; vertices 2.6, was 3.2; bones about 1.4, was 2.2), HUD 0.9, gx2gl 0.7; 273 draws, 13,324 verts, 4,495 model polys. 52 FPS: frame 19.3 ms with 4.0 ms vsync wait, so some frames still miss the 16.7 ms vsync |
| 2026-10-14 | gx2gl stencil buffer; client time split in `r_speeds 6` | Cemu 2.6, recompiler (portable test copy) | c2a2a: water draws (textured, warping). c1a0 lobby with `r_shadows 1`: model shadows under the scientists, no artifacts. Normal rendering unchanged with the new depth/stencil format. Decals on grates not tested (needs shooting one). The c2a5 save shows only 0.2 ms client in Cemu (player dead, no fight), so the 3.4 ms needs the new breakdown on hardware |
| 2026-10-14 | same | **Real Wii U** (owner), `r_speeds 6` | Same walled-area fight, 58 FPS, CPU 14.7 ms: game 2.0, **client 1.2** (packets 0.5, predict 0.1, entities 0.6 of which temp 0.4), sound 0.1, **input 1.4** (Cemu: 0.2), world 2.2, entities 6.5, studio models 5.3 (vertices 3.2), HUD 0.8, gx2gl 0.9; 133 tempents, 172 particles. The earlier 3.4 ms client reading didn't recur; input is now the odd one out, so it was split further (events / pads / move / predict) |
| 2026-10-14 | input split | **Real Wii U** (owner), `r_speeds 6` | Same fight, 54 FPS, CPU 17.8 ms: **client 3.5 of which packets 2.8** (the earlier 3.4 ms spike), input 1.5 but events 0.0, pads 0.1, move 0.0, predict 0.2, so ~1.2 ms is elsewhere in `Host_ClientBegin`; 113 tempents, 212 particles. Next: the "top" line (which message type / TE type / begin part) |
| 2026-10-14 | "top" line | **Real Wii U** (owner), `r_speeds 6` | Same fight, 60 FPS, CPU 13.5 ms: client 1.1 (packets 0.5), input 1.2 of which **`CL_SendCommand` 1.2** (move + predict 0.2 of it); top svc 41 (delta entities) 0.4, te 109 (gunshot decal) 0.0. `CL_SendCommand` split further (begin parts 7–15, send totals) |
| 2026-10-14 | Send parts | **Real Wii U** (owner), `r_speeds 6` | Same fight, CPU 15.2 ms: top begin 13 (`Netchan_TransmitBits`) 0.9, send: cmd 0.4, packet 1.0. Cause: `Netchan_TransmitBits` cleared its whole 192 KB `send_buf` (`NET_MAX_MESSAGE`) on every send, on the client and again on the server. Now clears only the bytes the packet can use; Cemu c1a0 tram ride runs normally, begin 13 gone from "top" |
| 2026-10-14 | Send buffer fix | **Real Wii U** (owner), `r_speeds 6` | Same fight, 58 FPS, **CPU 12.2 ms** (was 13.5–15.2): input 0.4 (was 1.2–1.4), packet 0.1 (was 1.0), game 1.7 (was 2.2), client 0.9. Remaining CPU is mostly rendering: 3D view 7.9 of which studio models 4.6 (vertices 3.0) |
| 2026-10-14 | − = gyro aiming on/off (`joy_gyro_toggle`), "top" timing line | Cemu 2.6 (test copy) | − bound to the toggle; a saved `bind BACK "+gyro_recenter"` loads as the toggle; "top" line shows (c1a0: begin part 5, 0.2 ms) |

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
  default since 2026-10-14 (`-ref soft` for the old one). gx2gl sends only changed state, writes each
  vertex once (32 bytes, indexed draws) and copies model arrays directly since 2026-10-14 (hardware FPS
  not compared yet). `r_speeds 6` shows where the CPU time goes (ARCHITECTURE.md); the next step depends on
  hardware numbers from scenes that drop below 60 FPS. A Surface Tension fight with 4–5 soldiers used
  16.5 of 16.7 ms, 5.4 of them for models; fast sin/cos and blending for model animation and cheaper model
  lighting followed (2026-10-14, ARCHITECTURE.md): 15.2 ms there on hardware, studio models 4.0 ms, but
  still 52 FPS because some frames miss vsync; the owner finds 52–60 FPS fine. The client's 3.4 ms didn't
  recur (1.2 ms in the next reading), but input took 1.4 ms on hardware against 0.2 in Cemu; `r_speeds 6`
  now splits input (events, pads, move, predict) to find out why. The cause was `Netchan_TransmitBits`
  clearing a 192 KB buffer per send (fixed, ARCHITECTURE.md): the same fight is at CPU 12.2 ms on hardware
  since. **Done for release** (owner, 2026-10-14); only revisit for scenes that drop well below 50 FPS
  during playtesting. Candidates then: cheaper model vertex work (3.0 ms of the 4.6 ms for models), VBOs
  for the world.
- **Save dates show Jan 1 1980** in Cemu (file time 0); correct on hardware (owner, 2026-10-14).
- **gx2gl gaps:** stencil added 2026-10-14 (decals on grates and fences, `r_shadows`); decals on grates
  and water checked on hardware (owner, 2026-10-14). The `GL_EQUAL` → `LEQUAL` depth workaround stays
  (harmless for ref_gl's use, ARCHITECTURE.md).
- **Decals in Cemu:** invisible with the recompiler (Cemu bug, DEBUGGING.md); use `--force-interpreter`.
- **Settings hidden on Wii U:** video modes, touch controls, the update check, Previews and the mouse-only
  options (mouse sensitivity, invert mouse, mouse look, look strafe, mouse filter, raw input, input
  devices). Stick look speed is `joy_yaw` / `joy_pitch` in degrees per second (180 / 120); the mouse
  `sensitivity` from PC configs is cancelled out on Wii U.
- **No mouse cursor:** SDL's `CreateSystemCursor is not currently supported` messages are harmless.
- **No intro videos:** no ffmpeg; the menu logs `couldn't open "gamestartup"`, which is harmless.
- **No HTTPS:** mbedTLS isn't built. Only affects downloads from servers, not single player.
- **Cemu's recompiler crashes more since the expansions were linked in** (2026-10-14): a level change
  (`changelevel`, e.g. the c0a0 tram) and the Opposing Force main menu killed Cemu itself, no guest
  exception, and the same runs work with `--force-interpreter`. Same class as the `zone.c` and decal bugs
  (DEBUGGING.md). Level changes work on hardware (owner, 2026-10-14), so this is Cemu only.
- **Counter-Strike (`cstrike`) and other mods:** not supported (closed-source or separate game code).
- **IPv4 only:** the Wii U network stack has no IPv6.

## Roadmap

1. Hardware renderer (`ref_gl` on gx2gl, ARCHITECTURE.md), **required before release**: started
   2026-10-14; the Hazard Course plays at 30–60 FPS on hardware, HOME → Close works, and it's the default
   renderer. Sprites, studio models, sky, decals and particles checked in Cemu. Performance is done for
   now (owner, 2026-10-14: 52–60 FPS in heavy fights is fine). Stencil added (2026-10-14); decals on
   grates and water checked on hardware. Screenshots and save pictures work since 2026-10-14
   (`glReadPixels`).
2. Hardware checks done (owner, 2026-10-14): in-game Quit, on-screen keyboard (console and save names),
   level changes, decals and water, save dates, Blue Shift and Opposing Force start and play.
3. Play through the campaign (and the expansions) on hardware; check whether the `zone.c` workaround is
   needed on hardware.
4. Release: public README written 2026-10-14 (features, install, credits, licenses, build at the bottom);
   `LICENSE.md` (GPLv3) added by the owner. See BUILDING.md "Releasing".
5. Polish: icons and boot screens (done 2026-10-14, owner's art, work on hardware; the boot sound is in
   the bundles but Aroma doesn't play homebrew boot sounds, BUILDING.md), a first-run message with clear instructions when `sd:/wiiu/apps/halflife/valve` is
   missing (done 2026-10-14; other fatal errors still just close the app, as SDL has no message box on Wii
   U), GamePad screen use (HUD, console or map).
