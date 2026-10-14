# Architecture

## Repository layout

| Path | Purpose |
|---|---|
| `CMakeLists.txt` | Top-level build: engine executable, the five "libraries", `.wuhb` packaging |
| `cmake/XashStaticLib.cmake` | `xash_static_library()`: turns an object library into one relocatable `.o` with a hidden symbol table (see below). `xash_write_library_tables()` writes `generated_library_tables.h` |
| `cmake/xash_link_relocatable.cmake` | The script that function runs at build time (`ld -r`, `link_helper.c` generation, `objcopy`) |
| `cmake/ThirdParty.cmake` | Static libraries from `xash3d-fwgs/3rdparty` (bzip2, mpg123, ogg, vorbis, opus, opusfile, miniz, MultiEmulator, library_suffix) and `public/` |
| `cmake/HLSDK.cmake` | hlsdk-portable server and client (+ FreeVGUI). Source lists are read from hlsdk-portable's own CMakeLists |
| `exports/*.txt` | Export lists for the engine's own libraries (filesystem, renderer, menu) |
| `src/platform/wiiu/` | Wii U platform layer: `sys_wiiu.c` (startup, paths, logging, crash handler, watchdog), `net_wiiu.h` (socket compatibility) |
| `src/gx2gl/` | OpenGL 1.x subset on GX2 for `ref_gl` (see "Hardware rendering" below); `shaders/` holds the GPU assembly and the generated header |
| `tools/` | Downloaded build tools, not in git (`latte-assembler`, see BUILDING.md) |
| `meta/` | `.wuhb` icon and splash screens; `meta/art/make_art.ps1` regenerates them |
| `patches/` | The port's changes to the upstream sources (see BUILDING.md) |
| `external/` | Upstream sources, not in git: `xash3d-fwgs/`, `hlsdk-portable/` (fetched by `scripts/fetch-deps.ps1`) |
| `scripts/` | Helper scripts (fetching, patches, Cemu) |

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
(ProcUI) and AX audio.

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
  them to pitch (−x) and yaw around gravity (from the accelerometer, so any holding angle works);
- `-log` writes `sd:/wiiu/apps/halflife/engine.log` (`sys_con.c`, absolute default path on Wii U);
- `+gyro_recenter` (−): levels the view and pauses gyro aiming while held (`in_joy.c`);
- HOME → Close (`SDL_QUIT`) quits immediately instead of opening the menu's quit dialog (`host_sdl2.c`);
- GamePad rumble through `VPADControlMotor` (`Platform_Vibrate2` in `joy_sdl2.c`): SDL's Wii U driver has
  none, and each failed `SDL_GameControllerRumble` logged an SDL error (every frame during `env_shake`).
  The motor is on/off, so strength becomes the share of "on" bits, one bit per 1/120 s, up to 120 bits.
  That's how Cemu reads the pattern; wut's header says the length is in bytes. Not yet felt on hardware;
- `ref_gl` is the default renderer and the saved `r_refdll` is ignored (`ref_common.c`);
- `snd_gain` (default 1.5, saved): final audio output gain before clipping (`s_mix.c`), because the
  GamePad's speakers were quiet at the PC level. SDL's Wii U audio already mixes at unity (`0x8000`);
- big-endian fixes in `img_wad.c` (palette entry 255's alpha, `Image_FindBestBlack`);
- menu entries that make no sense on a console hidden in mainui (`mainui.patch`).

The full list is in `patches/xash3d-fwgs.patch`.

## Data on the SD card

```
sd:/wiiu/apps/halflife.wuhb          the app (contains valve/extras.pk3 in its content folder)
sd:/wiiu/apps/halflife/valve/        the user's own game files from Steam
sd:/wiiu/apps/halflife/cmdline.txt   optional extra engine arguments, e.g. "-dev 2 -console"
sd:/wiiu/apps/halflife/engine.log    written with -log
```

The engine writes `config.cfg`, saves (`valve/SAVE/`) and logs inside `sd:/wiiu/apps/halflife/valve/`.

## Hardware rendering: ref_gl on gx2gl

Instead of a new renderer, the engine's own OpenGL renderer (`ref/gl`, unchanged, built as the static
library `ref_gl`) runs on **gx2gl** (`src/gx2gl/`), a small OpenGL 1.3 subset written on top of GX2. This is
how the Wii (OpenGX) and PS Vita (vitaGL) ports of Xash3D work too. `ref_gl` takes its GL functions from the
engine's `GL_GetProcAddress`, which on Wii U is `GX2GL_GetProcAddress`.

- **Engine side (`vid_sdl2.c`, `XASH_WIIU`):** no `SDL_WINDOW_OPENGL` and no SDL GL library. The "context"
  is `GX2GL_CreateContext(render size)`, swap is `GX2GL_SwapBuffers`, vsync is `GX2GL_SetSwapInterval`. GL
  attributes are fixed (RGBA8, 24-bit depth, no stencil). The render size is `vid_wiiu_render_height` (as for
  `ref_soft`, `VID_WiiURenderSize`). `gl` is the default and `soft` the fallback. On Wii U the saved
  `r_refdll` (`video.cfg`) is ignored (`ref_common.c`, `R_Init`): the menu's renderer choice is hidden, and
  installs from before ref_gl had `soft` saved. Only `-ref soft` in `cmdline.txt` picks the software
  renderer, which is kept for comparison and debugging.
- **What gx2gl reports:** GL 1.3, 2 texture units, `GL_ARB_multitexture`, `GL_ARB_texture_env_combine`,
  `GL_ARB_texture_env_add`, `GL_EXT_texture_edge_clamp`, `GL_ARB_texture_non_power_of_two`. No VBOs, so
  `ref_gl` uses its classic immediate-mode path. Every OpenGL 1.1 function `ref_gl` looks up exists, some as
  stubs (see below).
- **Drawing:** immediate mode and client arrays are converted on the CPU into one vertex format (position,
  colour, two texcoords, 48 bytes) and triangle/line/point lists in a vertex ring buffer (2 × 6 MB, one half
  per frame in flight, guarded by GPU timestamps). Consecutive primitives with the same state are one draw
  call; a GL call that really changes state flushes (setting the same value again doesn't). The texture
  matrix is applied on the CPU. Before each draw, `GX2GL_ApplyState` only sends the state groups and
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
- **Performance counters:** `r_speeds 6` (a Wii U case in `ref_gl`'s `gl_backend.c`) shows gx2gl's frame
  time, CPU time (frame time minus the waits), the time spent waiting for the GPU and for vsync, draw calls
  and vertices per frame (averaged over half a second, `glGetString( GX2GL_STATS )`), plus the world's CPU
  time and polygon counts.
- **Output:** our own colour (RGBA8) and depth (float 32) buffers in MEM2, copied (scaled) to the TV and
  GamePad scan buffers that SDL's video driver owns. Nothing is sent to the GPU in the background, checked
  live with `ProcUIInForeground` before every GPU call or wait. A per-frame flag wasn't enough: after
  HOME → Close the renderer shuts down in the background, and its `GX2DrawDone` never returned (the
  system hung at the loading screen, 2026-10-14). Textures uploaded while in the background are lost.
- **Differences from real OpenGL:** `GL_EQUAL` depth tests are done as `GL_LEQUAL` (lightmap passes
  failed the exact test); `glReadPixels` returns black (screenshots); no stencil, `glTexGen`, clip planes,
  polygon modes, `GL_TEXTURE_ENV_COLOR`, 1D/3D/cube textures; fog is always `GL_EXP`-style on eye-space z.
