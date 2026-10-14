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
  - the GL renderers (no OpenGL on Wii U)
  - libbacktrace
  - the dedicated server

## Wii U platform layer

The engine detects the platform from `__WIIU__`, which wut's toolchain defines. That gives `XASH_WIIU=1`
(`build.h`), `PLATFORM_WIIU`, and `Q_buildos() == "wiiu"`. Wii U counts as POSIX: newlib + wut provide files,
threads, time and BSD sockets.

`src/platform/wiiu/sys_wiiu.c`:

- **`WiiU_GetArgv`** (called from `main` in `launcher.c`):
  - Sets up logging.
  - Adds the arguments from `sd:/halflife/cmdline.txt`.
  - Sets `XASH3D_RODIR=fs:/vol/content`, the `.wuhb`'s own files.
- **Logging:** wut doesn't connect stdout/stderr to anything. A devoptab sends them, line-buffered, to
  `WHBLogPrint`, which writes to OSReport (Cemu log) and UDP port 4405 (`udplogserver` on a PC).
- **`WiiU_GetBasePath`:** returns `fs:/vol/external01/halflife`, i.e. `sd:/halflife`.
- **`WiiU_Init`** (from `Platform_Init`):
  - Installs the crash handler (DSI/ISI/program/alignment). It logs PC, LR, the data address, registers and
    stack return addresses, then calls `OSFatal`.
  - Starts the optional hang watchdog (`-watchdog <seconds>`).
- **`Platform_ShellExecute`:** a stub.

Video, audio, input, timers and threads come from Xash's SDL2 backend (`engine/platform/sdl2`) on top of
devkitPro's `wiiu-sdl2`. That covers the GamePad, Pro Controller and Wiimote+Nunchuk, HOME menu handling
(ProcUI) and AX audio.

**Video output:** SDL's Wii U window always has the TV scan mode's size (1920x1080, 1280x720 or 854x480).
The software renderer draws at `vid_wiiu_render_height` lines instead (default 720, width follows the TV's
aspect ratio; saved in `video.cfg`, takes effect after a restart). `vid_sdl2.c` always uses SDL_Renderer
(the GX2 backend) on Wii U: ref_soft writes into the top-left corner of a TV-sized RGBX streaming
texture, and that corner is stretched to the screen. DEBUGGING.md explains why it's done this way.

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
  and aiming every frame;
- `+gyro_recenter` (−): levels the view and pauses gyro aiming while held (`in_joy.c`);
- HOME → Close (`SDL_QUIT`) quits immediately instead of opening the menu's quit dialog (`host_sdl2.c`);
- menu entries that make no sense on a console hidden in mainui (`mainui.patch`).

The full list is in `patches/xash3d-fwgs.patch`.

## Data on the SD card

```
sd:/wiiu/apps/halflife.wuhb     the app (contains valve/extras.pk3 in its content folder)
sd:/halflife/valve/             the user's own game files from Steam
sd:/halflife/cmdline.txt        optional extra engine arguments, e.g. "-dev 2 -console"
```

The engine writes `config.cfg`, saves (`valve/SAVE/`) and logs inside `sd:/halflife/valve/`.

## Planned: ref_gx2

A hardware renderer is planned as a new `ref/gx2` library, exported as `"ref_gx2"`. It would implement
`ref_interface_t` (`engine/ref_api.h`, `REF_API_VERSION 20`), probably starting from a copy of `ref/gl`.
Shaders would be precompiled for GX2, or compiled at runtime with CafeGLSL. The engine would also need a
`REF_GX2` graphics API value, or the renderer would create its own GX2 context the way `ref_soft` does through
the `SW_*` callbacks.
