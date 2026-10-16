# Architecture

## Repository layout

| Path | Purpose |
|---|---|
| `CMakeLists.txt` | Top-level build: engine executable, the five "libraries", `.wuhb` packaging |
| `cmake/XashStaticLib.cmake` | `xash_static_library()`: turns an object library into one relocatable `.o` with a hidden symbol table (see below). `xash_write_library_tables()` writes `generated_library_tables.h` |
| `cmake/xash_link_relocatable.cmake` | The script that function runs at build time (`ld -r`, `link_helper.c` generation, `objcopy`) |
| `cmake/ThirdParty.cmake` | Static libraries from `xash3d-fwgs/3rdparty` (bzip2, mpg123, ogg, vorbis, opus, opusfile, miniz, MultiEmulator, library_suffix) and `public/` |
| `cmake/HLSDK.cmake` | `hlsdk_game()`: server and client of one hlsdk-portable checkout (+ FreeVGUI); called for Half-Life (`server`/`client`), Blue Shift (`*_bshift`) and Opposing Force (`*_opfor`). Source lists are read from each checkout's own CMakeLists |
| `exports/*.txt` | Export lists for the engine's own libraries (filesystem, renderer, menu) |
| `src/platform/wiiu/` | Wii U platform layer: `sys_wiiu.c` (startup, paths, logging, crash handler, watchdog), `net_wiiu.h` (socket compatibility) |
| `src/gx2gl/` | OpenGL 1.x subset on GX2 for `ref_gl` (see "Hardware rendering" below); `shaders/` holds the GPU assembly and the generated header |
| `tools/` | Downloaded build tools, not in git (`latte-assembler`, see BUILDING.md) |
| `meta/` | `.wuhb` icons, boot screens and boot sound, built from `meta/source` by `meta/art/make_art.ps1` (BUILDING.md) |
| `patches/` | The port's changes to the upstream sources (see BUILDING.md) |
| `external/` | Upstream sources, not in git: `xash3d-fwgs/`, `hlsdk-portable/`, `hlsdk-bshift/`, `hlsdk-opfor/` (fetched by `scripts/fetch-deps.ps1`) |
| `scripts/` | Helper scripts (fetching, patches, Cemu, `wuhb-add-file.ps1` for the boot sound) |

## One executable instead of shared libraries

On PC the engine loads five shared libraries at runtime: `filesystem_stdio`, a renderer (`ref_soft`, `ref_gl`,
...), the menu (`menu`, from mainui_cpp), and the game's `server` (`hl.dll`) and `client`. The Wii U has no
`dlopen`, so they're all linked into `halflife.rpx`.

The engine supports this with `XASH_STATIC_LIBS` (`common/defaults.h` → `XASH_LIB == LIB_STATIC`,
`XASH_INTERNAL_GAMELIBS`). `engine/platform/misc/lib_static.c` replaces the library loader:

- `COM_LoadLibrary(name)` looks `name` up in `libs[]` from `generated_library_tables.h`.
  - Names are `"filesystem_stdio"`, `"ref_soft"`, `"menu"`, `"server"` and `"client"`.
- `COM_GetProcAddress(lib, sym)` looks `sym` up in that library's `lib_<name>_exports[]` table.

### How each library is built (`xash_static_library`)

1. Compile the sources as a CMake OBJECT library.
2. Find the exports:
   - **Engine libraries:** read `exports/<name>.txt`.
   - **Game libraries:** partially link (`ld -r`), then list every global, default-visibility function with a
     C name using `readelf`. hlsdk builds with `-fvisibility=hidden` and marks its exports visible, so this
     catches `GetEntityAPI`, `GiveFnptrsToDll` and all ~250 `LINK_ENTITY_TO_CLASS` entity names. The engine
     looks each entity class up by name with `COM_GetProcAddress`.
3. Generate `link_helper.c` containing `lib_<name>_exports[] = { {"name", &func}, ... }`, then compile it.
4. Run `ld -r` on all objects plus the helper, producing `<name>.unstripped.o`.
5. Run `objcopy -R .group -G lib_<name>_exports`, producing `<name>.o`:
   - `-G` makes every symbol except the table local, so libraries can't clash. Client and server both contain
     the weapon classes and `pm_shared`; every renderer has `GetRefAPI`.
   - `-R .group` removes the COMDAT groups (inline functions, vtables). Without it, the final link would merge,
     for example, the client's and server's `CHandGrenade` vtables into one. The discarded copy's
     now-local references would then fail ("defined in discarded section").
6. Link the `.o` files into the executable as external objects.

The per-library outputs, including the generated `exports.txt`, end up in `build/staticlibs/<name>/`.

Save games store function pointers as offsets from the game DLL's `GameInit`
(`XASH_ALLOW_SAVERESTORE_OFFSETS`). Saves therefore only load in the same build of the executable.

## Engine build (what `CMakeLists.txt` compiles)

It mirrors upstream's `engine/wscript` for a client build:

- **Engine sources:**
  - `engine/common/**`, `engine/server/*`, `engine/client/**`
  - `engine/platform/posix/*`, `engine/platform/sdl2/*`, `engine/platform/misc/lib_static.c`
  - `src/platform/wiiu/sys_wiiu.c`
- **Key defines:**
  - `XASH_SDL=2`, `XASH_STATIC_LIBS=1`, `XASH_REF_SOFT_ENABLED=1`, `XASH_ENABLE_MAIN=1`, `XASH_GAMEDIR="valve"`
  - `_GNU_SOURCE` plus the `HAVE_*` libc checks that waf's configure step would do
- **Endianness:** the devkitPPC compiler defines `__BIG_ENDIAN__`, so `build.h` sets `XASH_BIG_ENDIAN=1`.
  Upstream byte-swaps all on-disk formats with `public/swaplib.h` and the `Little*` macros.
- **Not built:**
  - mbedTLS (no HTTPS downloads)
  - ffmpeg (no video playback)
  - the GLES renderer variants (`ref_gl` itself is built, on gx2gl, see below)
  - libbacktrace
  - the dedicated server

## Wii U platform layer

The engine detects the platform from `__WIIU__`, which wut's toolchain defines. That gives `XASH_WIIU=1`
(`build.h`), `PLATFORM_WIIU`, and `Q_buildos() == "wiiu"`. Wii U counts as POSIX: newlib + wut provide files,
threads, time and BSD sockets.

`src/platform/wiiu/sys_wiiu.c`:

- **`WiiU_GetArgv`** (called from `main` in `launcher.c`):
  - Sets up logging.
  - Adds the arguments from `sd:/wiiu/apps/halflife/cmdline.txt`.
  - Sets `XASH3D_RODIR=fs:/vol/content`, the `.wuhb`'s own files.
- **Logging:** wut doesn't connect stdout/stderr to anything. A devoptab sends them, line-buffered, to
  `WHBLogPrint`, which writes to OSReport (Cemu log) and UDP port 4405 (`udplogserver` on a PC).
- **`WiiU_GetBasePath`:** returns `fs:/vol/external01/wiiu/apps/halflife`, i.e. `sd:/wiiu/apps/halflife`
  (next to the .wuhb; it was `sd:/halflife` until 2026-10-14).
- **`WiiU_Init`** (from `Platform_Init`):
  - Installs the crash handler (DSI/ISI/program/alignment). It logs PC, LR, the data address, registers and
    stack return addresses, then calls `OSFatal`.
  - Starts the optional hang watchdog (`-watchdog <seconds>`).
- **`Platform_ShellExecute`:** a stub.

Video, audio, input, timers and threads come from Xash's SDL2 backend (`engine/platform/sdl2`) on top of
devkitPro's `wiiu-sdl2`. That covers the GamePad, Pro Controller and Wiimote+Nunchuk, HOME menu handling
(ProcUI) and AX audio. The engine only uses SDL's GameController API, and SDL ships a mapping for the
GamePad only; the Pro Controller's is added on Wii U (below).

**Video output:** SDL's Wii U window always has the TV scan mode's size (1920x1080, 1280x720 or 854x480).
`ref_gl` (see "Hardware rendering") and the software renderer draw at `vid_wiiu_render_height` lines instead (default 720, width follows the TV's
aspect ratio; saved in `video.cfg`, takes effect after a restart). `vid_sdl2.c` always uses SDL_Renderer
(the GX2 backend) on Wii U: ref_soft writes into the top-left corner of a TV-sized RGBX streaming
texture, and that corner is stretched to the screen (`ref_soft` only; DEBUGGING.md explains why it's done
this way).

Engine changes for the port are guarded by `#if XASH_WIIU`. They include:

- platform hooks, the base path, IPv4-only networking and stubs for `fork`/`exec`/`getpwuid`/`sigaction`;
- a case-insensitive filesystem (FAT32);
- `whereami.c` (no executable path);
- the video output above (`vid_sdl2.c`, `vid_common.c`);
- stick look in degrees per second without the mouse `sensitivity` multiplier (`input.c`, `in_joy.c`);
- the GamePad layout (`Key_SetWiiUGamepadDefaults` in `in_keys.c`, re-applied after `unbindall`) and a
  `joy_centerview` command (`in_joy.c`);
- GamePad gyro: SDL doesn't report it, and a second `VPADRead` caller would steal samples from SDL (each
  sample is returned once; SDL detects button presses from the samples' trigger/release bits). So the
  link wraps `VPADRead` (`-Wl,--wrap=VPADRead`, `__wrap_VPADRead` in `joy_sdl2.c`) and collects the gyro
  from SDL's own reads; `SDLash_WiiUGyroFrame` feeds the average to the engine's normal gyro calibration
  and aiming every frame. Calibration runs on the raw GamePad axes; `SDLash_WiiUGyroToView` then converts
  them to pitch (−x) and yaw around gravity (from the accelerometer, so any holding angle works).
  Since 2026-10-15 a calibration (startup, `joy_calibrate_gyro`, turning gyro on with −) only takes a
  1 s window in which every sample stays within 0.1 rad/s of the window's average; any movement starts the
  window over, and gyro events are blocked meanwhile. Before, the first 5 s were averaged whatever
  happened, so moving the GamePad during startup left a constant drift until the next start (owner,
  hardware). A recalibration that finds no still second within 4 s keeps the previous offsets;
- Pro Controller (2026-10-15): SDL's driver already reads it (KPAD) with the GamePad's button order, so
  `Platform_JoyInit` sets `SDL_HINT_GAMECONTROLLERCONFIG` to a copy of the GamePad's mapping under SDL's
  name-based GUID for "Wii U Pro Controller" (first 11 characters; a mapping with CRC 0 matches any CRC).
  As a hint it's read before SDL detects controllers, so one connected at startup is added too. Same
  bindings (the `K_*` keys are shared), no gyro (− says "This controller has no gyro"), no rumble. The
  controller last used is the active one; a stick only switches over when pushed past 8000 of 32767, so a
  resting stick's jitter on the other controller can't overwrite the shared axes. Switching back to the
  GamePad keeps its gyro calibration;
- `-log` writes `sd:/wiiu/apps/halflife/engine.log` (`sys_con.c`, absolute default path on Wii U);
- `joy_gyro_toggle` (−, since 2026-10-14): gyro aiming on/off through the saved `joy_gyro_enable` (on by
  default) with a centre-screen message (`in_joy.c`). Turning it on recalibrates (above), the fix for a
  drift left by a bad startup calibration. It replaced `+gyro_recenter` on − (still a command:
  levels the view, gyro paused while held), since the right stick (`joy_centerview`) levels the view
  already. `Key_SetBinding` turns a saved `bind BACK +gyro_recenter` into the toggle, so older
  `config.cfg` files get it too (`in_keys.c`);
- HOME → Close (`SDL_QUIT`) quits immediately instead of opening the menu's quit dialog (`host_sdl2.c`);
- GamePad rumble through `VPADControlMotor` (`Platform_Vibrate2` in `joy_sdl2.c`): SDL's Wii U driver has
  none, and each failed `SDL_GameControllerRumble` logged an SDL error (every frame during `env_shake`).
  The motor is on/off, driven by a bit pattern of 1/120 s steps, up to 120, first bit = most significant.
  The length is in bits: Cemu and the SDK cap it at 120 with a 15-byte buffer (wut's header comment says
  bytes), and Cemu plays it at that rate. Strength sets how many steps of each block of 8 are on (3 to 8),
  a pattern lasts at least 1/10 s, and a new one is only sent when the current one is nearly over (Cemu
  queues up to 5 patterns, so sending every frame would only pile up). The first two versions switched
  the motor every step and sent a new pattern every frame during `env_shake`, and nothing was felt on
  hardware; this version works there (owner, 2026-10-14). The first calls and any error are logged ("GamePad rumble: … VPADControlMotor returned …");
  `vibrate <ms>` in the console tests it;
- a screen explaining where to copy the game files when `sd:/wiiu/apps/halflife/valve` is missing
  (`WiiU_CheckGameData` in `sys_wiiu.c`, OSScreen text through WHB's console, before SDL starts). SDL has no
  message box on Wii U, so without it the app just closed;
- `ref_gl` is the default renderer and the saved `r_refdll` is ignored (`ref_common.c`);
- save list: `SV_GetSaveComment` (`sv_save.c`) reads the game's save fields little-endian (hlsdk-portable
  stores them that way, `CSave::BufferData`) and checks token indices and field sizes; it used to crash or
  show "<missing GameHeader>". Saves copied from a PC have a little-endian engine header and are listed as
  "<PC save, can't be loaded>";
- the console's keyboard opens when the console is opened (`Con_ToggleConsole_f`) or with Y, not whenever
  the console gets focus (`Key_SetKeyDest`, as on Switch and Vita): it used to pop up while loading a save;
- `hud_fastswitch` defaults to 1 (`cl_dll/ammo.cpp`, hlsdk-portable patch);
- `hud_fontscale` defaults to 2 (`cl_main.c`): titles, credits and `game_text` were sized for 480 lines and
  were small at 720p on a TV;
- `con_fontscale` defaults to 2 (2026-10-15, `console.c`): the console font, and with it the notify lines
  (top left with `-dev`), the FPS counter, centre prints (gyro on/off, game messages) and the `r_speeds`
  overlay, which all draw with it, were barely readable on the GamePad's screen;
- notify lines (recent console messages, top left) only with `-dev` (2026-10-15, `Con_DrawNotify`):
  `DEFAULT_ALLOWCONSOLE` is 1 on Wii U so the menu's Console entry works, and that alone showed every
  console message during play. On PC they need `-dev` too. Chat input still draws there;
- `wiiu_config_version` (`Host_WiiUUpgradeConfig`, `host.c`): `config.cfg` saves every archived cvar,
  defaults included, so changed Wii U defaults never reached existing installs. After `config.cfg`, each
  step newer than the saved version is applied once (1: `hud_fastswitch 1`, `hud_fontscale 2`; 2: `con_fontscale 2`). Add a
  step whenever a Wii U default changes;
- `snd_gain` (default 1.5, saved): final audio output gain before clipping (`s_mix.c`), because the
  GamePad's speakers were quiet at the PC level. SDL's Wii U audio already mixes at unity (`0x8000`);
- big-endian fixes in `img_wad.c` (palette entry 255's alpha, `Image_FindBestBlack`);
- menu entries that make no sense on a console hidden in mainui (`mainui.patch`): Multiplayer, Change
  game, Previews, window buttons, video modes, touch, mouse options, "Use VBO", "Mute when inactive",
  the built-in on-screen keyboard option. Console is always shown (developer mode not needed). The
  Controls list (`KbActListModel.h`) leaves out chat, voice, mouse/keyboard look and strafe modifiers,
  scores, spray, quit and Opposing Force's Capture the Flag actions, shows only GamePad buttons, and
  "Use defaults" restores the GamePad layout instead of loading the PC keyboard defaults. Only the item at
  the cursor keeps a focus flag (`CMenuItemsHolder::Think`): showing a menu again reset the cursor without
  clearing the last button's flag, and the virtual mouse parked at the screen centre set mouse focus that
  the D-pad never cleared, so two buttons were lit. The title is always the animated one from
  `resource/logo*.tga`: Blue Shift and Opposing Force use `logo.avi`, which needs a video decoder this
  build doesn't have, so their menus had no title;
- saving from the menu renders the world for the save picture's frame (`V_RenderView`, `cl_view.c`):
  with a menu open the engine skips the world (`ui_renderworld 0`), so the picture was the menu.

The full list is in `patches/xash3d-fwgs.patch`.

## Data on the SD card

```
sd:/wiiu/apps/halflife.wuhb          the app (contains valve/extras.pk3 in its content folder)
sd:/wiiu/apps/bshift.wuhb            the same app for Blue Shift (content folder adds game.txt = "bshift")
sd:/wiiu/apps/opfor.wuhb             the same app for Opposing Force (game.txt = "gearbox")
sd:/wiiu/apps/halflife/valve/        the user's own game files from Steam
sd:/wiiu/apps/halflife/bshift/       Blue Shift's files (only for bshift.wuhb)
sd:/wiiu/apps/halflife/gearbox/      Opposing Force's files (only for opfor.wuhb)
sd:/wiiu/apps/halflife/cmdline.txt   optional extra engine arguments, e.g. "-dev 2 -console"
sd:/wiiu/apps/halflife/engine.log    written with -log
```

The engine writes `config.cfg`, saves (`valve/SAVE/`) and logs inside `sd:/wiiu/apps/halflife/valve/` (the
expansions in their own folder).

### Blue Shift and Opposing Force

All three games' code is linked into the one program: hlsdk-portable's master, `bshift` and `opfor`
branches each become a server and client static library (`cmake/HLSDK.cmake`, `hlsdk_game()`), and the
engine appends `_bshift` / `_opfor` to the library names for the `bshift` / `gearbox` game folders
(`COM_WiiUGameLibrarySuffix`, `lib_common.c`). The engine can't restart into another game on Wii U, so
each game is its own `.wuhb` (owner's choice, 2026-10-14): `CMakeLists.txt` (`game_wuhb()`) packs the same
`.rpx` with a content folder holding `game.txt`, which `sys_wiiu.c` turns into `-game <folder>` unless
`cmdline.txt` has a `-game`. The missing-files screen then also checks that game's folder.

## Hardware rendering: ref_gl on gx2gl

Instead of a new renderer, the engine's own OpenGL renderer (`ref/gl`, unchanged, built as the static
library `ref_gl`) runs on **gx2gl** (`src/gx2gl/`), a small OpenGL 1.3 subset written on top of GX2. This is
how the Wii (OpenGX) and PS Vita (vitaGL) ports of Xash3D work too. `ref_gl` takes its GL functions from the
engine's `GL_GetProcAddress`, which on Wii U is `GX2GL_GetProcAddress`.

- **Engine side (`vid_sdl2.c`, `XASH_WIIU`):** no `SDL_WINDOW_OPENGL` and no SDL GL library. The "context"
  is `GX2GL_CreateContext(render size)`, swap is `GX2GL_SwapBuffers`, vsync is `GX2GL_SetSwapInterval`. GL
  attributes are fixed (RGBA8, 24-bit depth, 8-bit stencil). The render size is `vid_wiiu_render_height` (as for
  `ref_soft`, `VID_WiiURenderSize`). `gl` is the default and `soft` the fallback. On Wii U the saved
  `r_refdll` (`video.cfg`) is ignored (`ref_common.c`, `R_Init`): the menu's renderer choice is hidden, and
  installs from before ref_gl had `soft` saved. Only `-ref soft` in `cmdline.txt` picks the software
  renderer, which is kept for comparison and debugging.
- **What gx2gl reports:** GL 1.3, 2 texture units, `GL_ARB_multitexture`, `GL_ARB_texture_env_combine`,
  `GL_ARB_texture_env_add`, `GL_EXT_texture_edge_clamp`, `GL_ARB_texture_non_power_of_two`. No VBOs, so
  `ref_gl` uses its classic immediate-mode path. Every OpenGL 1.1 function `ref_gl` looks up exists, some as
  stubs (see below).
- **Drawing:** immediate mode and client arrays are converted on the CPU into one vertex format (position,
  RGBA8 colour, two texcoords: 32 bytes, one CPU cache line, zeroed with `dcbz` instead of read before it's
  written) in a vertex ring buffer (2 × 6 MB, one half per frame in flight, guarded by GPU timestamps).
  Every vertex is written once: `glVertex` writes straight into the ring, and only 16-bit indices turn
  polygons, fans, strips and quads into triangles (an index ring next to it, 3 indices per vertex).
  `glDrawElements` / `glDrawRangeElements` with ref_gl's array layout (float xyz, float st, RGBA8; studio
  models use this, `r_studio_drawelements 1`) copy each used vertex once and keep the indices
  (`GX2GL_FastArrays`); other layouts go through the slow generic reader. Consecutive primitives with the
  same state are one draw call; a GL call that really changes state flushes (setting the same value again
  doesn't). The texture matrix is applied on the CPU. Before each draw, `GX2GL_ApplyState` only sends the state groups and
  uniform blocks that differ from what it sent last (`sent`); the cache is cleared whenever the GX2 context
  is set again (after clears, copies, uploads and swaps) or a texture's registers change.
- **Shaders:** one fixed-function pair (`src/gx2gl/shaders/fixed.vsh/.psh`, GPU assembly, see BUILDING.md).
  Texture environments (modulate, replace, decal, add, combine-modulate with `RGB_SCALE`) for both units are
  one formula with per-unit weights in uniforms. Exponential fog is in the shaders; the alpha test is GX2's.
- **Textures:** tiled RGBA8 GX2 textures with a full mip chain allocated up front. Uploads (also
  `glTexSubImage2D`, e.g. lightmaps) are converted to RGBA8, written to a linear staging surface and copied
  by the GPU (`GX2CopySurface(Ex)`). Memory the GPU may still use is freed one frame later.
- **Vsync (`gl_vsync 1`, the default):** after a swap, gx2gl only waits while more than one flip is
  pending. Waiting for every flip made a frame that missed a vblank hold up the next one too, so the frame
  rate jumped between 60 and 30.
- **On-screen keyboard:** SDL draws its keyboard (nn::swkbd) when it presents a frame, which gx2gl does
  itself. So while `SDL_IsScreenKeyboardShown`, `GX2GL_SwapBuffers` gets the window and calls SDL's
  `WIIU_SWKBD_Draw` (a public symbol in `libSDL2.a`) with no context state bound, before copying to the
  screens. SDL also switches the colour buffer to sRGB around the draw; that crashed Cemu, so it's left out
  (the keyboard's gamma may be slightly off on hardware).
- **Performance counters:** `r_speeds 6` (a Wii U case in `ref_gl`'s `gl_backend.c`) shows, averaged over
  half a second (`glGetString( GX2GL_STATS )`): frame time, CPU time (frame time minus the waits), the time
  spent waiting for the GPU and for vsync; then where the CPU time goes: game (server frame), client,
  sound mixing, input (`Host_InputFrame` and `Host_ClientBegin`; split into SDL events with the controller
  reads `VPADRead` / `KPADReadEx` / `WPADProbe` as "pads", wrapped at link time in `joy_sdl2.c` only to time
  them, the movement command, and prediction of the new command), the client's parts (the client.dll's
  `HUD_Frame`, reading the local server's packets, movement prediction, and adding entities with the
  temporary entities as a sub-part), the 3D view with the world and the entities, studio models (all of
  it, including the game's bone setup) and their vertex part (`R_StudioDrawPoints`), HUD/menus, and
  gx2gl's own share (sending draws, uploads); then draw calls, vertices, polygon, temporary entity and
  particle counts. A "top" line names the most expensive server message type (`svc` number, game messages
  included, timed in `CL_ParseServerMessage`), temporary entity type (`TE_*` number, `CL_ParseTempEntity`)
  and part of `Host_ClientBegin` (1 commands, 2 connection state, 3 logo, 4 client data, 6 Steam broker;
  `CL_SendCommand` split into 7 solid entities, 8 solid players, 9 `Platform_PreCreateMove`, 10
  interpolation amount, 11 writing user commands, 12 voice, 13 `Netchan_TransmitBits`, 14 netchan
  progress, 15 packet loss) with their times (`GX2GL_ProfItem`); "send" shows `CL_CreateCmd` and
  `CL_WritePacket` in full (items 20 and 21). Item numbers: `svc_*` in `protocol.h` (41 =
  delta entity updates), `TE_*` in `const.h` (109 = gunshot decal). The phases are timed with `GX2GL_PROF( phase, call )` (`gx2gl.h`) in `host.c`,
  `cl_main.c`, `cl_frame.c`, `gl_rmain.c` and `gl_studio.c`.
- **Network send buffer (2026-10-14):** `Netchan_TransmitBits` (`net_chan.c`) builds each packet in a
  `NET_MAX_MESSAGE` (192 KB) stack buffer and used to clear all of it on every send, about 1 ms per call on
  the Wii U and paid twice per frame (client to server, server to client). It now clears only the header,
  the reliable and unreliable payload, padding and some slack (all platforms).
- **Model animation and lighting on the CPU (2026-10-14):** the Wii U's CPU has no fast sin/cos/acos
  (newlib's take well over 100 cycles), and bone setup calls them about 16 times per bone and animation
  layer. Under `XASH_WIIU`, the engine's `SinCos` (`public/xash3d_mathlib.h`, used by view, movement and
  the server's hitbox bone setup) and the games' `AngleQuaternion` (`cl_dll/studio_util.cpp` in all three
  hlsdk trees) use a polynomial sin/cos (within ~1e-6), and `QuaternionSlerp` uses a normalised linear
  blend when the two rotations are close (cos > 0.95, within 0.06 degrees of the exact result). ref_gl's
  `gl_studio.c` turns each normal's light value into colour bytes once (instead of at every triangle
  corner) and skips the per-vertex dynamic light setup when no dynamic light touches the model. Cemu runs
  the math natively, so these only show on hardware.
- **Polygon offset:** GL's factor goes to `GX2SetPolygonOffset` unchanged, because GX2 itself multiplies
  it by 16 for the register (Cemu's GX2 does the same). Until 2026-10-14 gx2gl multiplied by 16 as well,
  so brush entities (ref_gl draws them with `gl_polyoffset_bmodels`, factor -0.5) were pulled 16 times too
  far toward the camera at steep angles and showed through walls (c1a0's door wheels).
- **Stencil (2026-10-14):** the depth buffer is `GX2_SURFACE_FORMAT_FLOAT_X8_X24` (32-bit float depth plus
  8-bit stencil, so depth precision didn't change), and `glStencilFunc/Op/Mask`, `glClearStencil` and
  `GL_STENCIL_TEST` map to `GX2SetDepthStencilControl` / `GX2SetStencilMask` (same state for both faces).
  ref_gl uses it for decals on see-through `{` surfaces (grates, fences) and for `r_shadows 1` (each pixel
  darkened once). The stencil `GL_EQUAL` is exact; only the depth test has the LEQUAL workaround below.
- **Output:** our own colour (RGBA8) and depth/stencil buffers in MEM2, copied (scaled) to the TV and
  GamePad scan buffers that SDL's video driver owns. Nothing is sent to the GPU in the background, checked
  live with `ProcUIInForeground` before every GPU call or wait. A per-frame flag wasn't enough: after
  HOME → Close the renderer shuts down in the background, and its `GX2DrawDone` never returned (the
  system hung at the loading screen, 2026-10-14). Textures uploaded while in the background are lost.
- **Differences from real OpenGL:** `GL_EQUAL` depth tests are done as `GL_LEQUAL` (lightmap passes
  failed the exact test in Cemu; ref_gl only uses it for blend passes over surfaces that already wrote
  their depth, where both give the same picture); `glReadPixels` only does `GL_RGB`/`GL_RGBA` bytes and stalls until the GPU is
  done (fine for screenshots and save pictures): it copies the colour buffer to a `GX2_TILE_MODE_LINEAR_SPECIAL`
  surface, the mode games use for CPU reads. Cemu only writes the pixels back for that mode, so
  `LINEAR_ALIGNED` came out black there; no `glTexGen`, clip planes,
  polygon modes, `GL_TEXTURE_ENV_COLOR`, 1D/3D/cube textures; fog is always `GL_EXP`-style on eye-space z.
