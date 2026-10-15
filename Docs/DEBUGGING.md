# Testing and debugging

## Cemu (PC)

The owner uses Cemu 2.6 (the latest release as of 2026-10; checked on GitHub) at
`E:\ROMs and Emulators\_Emulators\Cemu (Wii U)\Cemu.exe`.

```
.\scripts\run-cemu.ps1                       # run build\halflife.wuhb, print engine log lines live
.\scripts\run-cemu.ps1 -EngineArgs '-dev 2'  # set sd:/wiiu/apps/halflife/cmdline.txt first
.\scripts\run-cemu.ps1 -Interpreter          # Cemu's exact (slow) single-core interpreter
```

Or start it by hand: `Cemu.exe -g "F:\Wii U Homebrew\Half-Life\build\halflife.wuhb"`.

### Setup (already done on the owner's PC)

- **SD card:**
  - Cemu mounts `%APPDATA%\Cemu\sdcard` as the SD card (`fs:/vol/external01`). On the owner's PC that folder
    is a **junction** to `E:\ROMs and Emulators\_Emulators\Cemu (Wii U)\sdcard`, because C: is short on space.
  - The game files are a copy of the Steam `valve` folder at `sdcard\halflife\valve`.
  - Copy, never link, so the engine's writes (`config.cfg`, saves) can't touch the Steam install.
- **Engine log in Cemu's log:**
  - Cemu's "Coreinit Logging" option must be on (Debug > Logging in the GUI). In
    `%APPDATA%\Cemu\settings.xml` it is `<logflag>131072</logflag>`, i.e. bit 17, `LogType::CoreinitLogging`.
  - The original settings are backed up as `settings.xml.bak-before-halflife`.
  - Engine output appears in `%APPDATA%\Cemu\log.txt` as `[OSConsole]` lines.
- **Only one Cemu log:** `log.txt` is shared by every Cemu instance. On 2026-10-13 another project (an SCP:CB
  Wii U port) ran Cemu at the same time and overwrote it. If the log shows another title, rerun.
- **Scripts that wait for a log line:** Cemu recreates `log.txt` about 1–2 s after starting, so the previous
  run's lines are still there at first. Wait a few seconds before searching it (`run-cemu.ps1` does).

### Cemu command-line options worth knowing

| Option | Use |
|---|---|
| `-g <file>` | Start a `.wuhb` / `.rpx` |
| `--force-interpreter` | Single-core interpreter: slow but exact. Use this when the recompiler crashes or behaves differently from hardware |
| `--force-multicore-interpreter` | Multi-core interpreter |
| `--enable-gdbstub` | GDB server on port 1337 (`powerpc-eabi-gdb`, `target remote localhost:1337`). Breakpoints did **not** fire in our tests; see below |
| `--cos-argstr "halflife.rpx -dev 2"` | Override program arguments (an alternative to `cmdline.txt`) |

### When Cemu itself crashes

Cemu doesn't deliver DSI/ISI exceptions to the program. When emulated code touches unmapped memory, **Cemu
exits** with code `0xC0000005` (access violation, "Faulting module: unknown" in the Windows event log)
instead of running our crash handler. What to do:

1. Run with `--force-interpreter`. If the problem goes away, it's timing- or codegen-sensitive (see the
   history below). If it still crashes, that's even better: in interpreter mode Cemu writes a **crash
   log into `log.txt`** with the PPC registers (`IP`, `LR`) and a "PPC stack trace" of return addresses.
   Decode those with `addr2line -i` (see "Real hardware" below). This is how the FreeVGUI font crash
   was found on 2026-10-13.
2. Narrow it down with temporary `Con_Reportf( "WIIU TRACE: ..." )` lines. OSReport output reaches the log
   right up to the crash. Remove them afterwards; `git checkout -- <file>` in `external/` is easiest if the
   file has no other changes.
3. `scripts/capture-cemu.ps1` saves an image of Cemu's windows (only Cemu's) to `build/cemu-capture/`.
   Before 2026-10-13 the script wasn't DPI-aware, so with Windows display scaling it saved only the
   top-left part of the window. That looked like a zoomed and cropped game image, but the game was fine.

Things that **did not** work, so don't spend time on them again:

- **`--ppcrec-lower-addr` / `--ppcrec-upper-addr`** (Cemu's hidden recompiler address-range options):
  `scripts/cemu-recompiler-bisect.ps1` showed Cemu still crashed with a 32-byte range, so they don't isolate
  code the way their name suggests.
- **GDB breakpoints through `--enable-gdbstub`:** gdb connects and sets the breakpoint, but it never stopped,
  even unconditionally on `Delta_Init`. Possibly because the engine runs on a thread created after attach.

### History: crash in `_Mem_Realloc` with Cemu's recompiler (2026-10-13)

**Symptom:** with Cemu's default recompiler and with `--force-multicore-interpreter`, Cemu crashed during
`Delta_Init`, in `Z_Realloc` (`engine/common/zone.c`, `_Mem_Realloc`). With `--force-interpreter`
(single-core) the same binary reached the main menu.

**What was ruled out:**

| Suspect | How it was ruled out |
|---|---|
| Heap corruption | `Mem_Check()` passed right before the call |
| newlib `realloc` itself | Shrinking and grow self-tests worked |
| Heap trimming (negative `sbrk`) | Tested and worked |
| The 64 KB main stack | Not the cause, but the engine now runs on an 8 MB stack anyway (see ARCHITECTURE.md) |
| A different realloc implementation | Swapping in malloc + copy + free still crashed |

Adding `printf` calls inside `_Mem_Realloc` made the crash disappear. That's a heisenbug: either Cemu
mis-executes the -O2 code for this function on its multi-core paths, or (less likely) GCC 16.2 miscompiles
it. The interpreter result argues for Cemu.

**Workaround:** `zone.c` is compiled with `-O0` (see `CMakeLists.txt`). Since then the main menu runs with
the recompiler at about 36 FPS.

**To do:** test the `-O2` build on real hardware. If it works there, report to Cemu and consider making the
workaround Cemu-test-builds only.

### History: invisible decals with Cemu's recompiler (2026-10-14)

**Symptom:** bullet holes and other decals were invisible with `ref_gl` and mostly missing (a quarter of the
hole) with `ref_soft`, but only with the recompiler. With `--force-interpreter` the same binary draws them
correctly.

**What logging showed (`ref/gl/gl_decals.c`):** the clipped decal vertices in `g_DecalClipVerts` were
correct, but right after `R_DecalCreatePoly` copied them into a new `Mem_Calloc` block, the block read back
as all zeros. Every decal then drew as a 0-size quad at the world origin. gx2gl itself was fine: with the
interpreter the bullet holes blend correctly.

**Takeaway:** a second case of Cemu's recompiler mis-executing this engine, again around zone memory
(`Mem_Calloc` → `_Mem_Alloc`). Check decals with `--force-interpreter` or on hardware, not with the
recompiler. Scripted test: `scripts/gxtest.cfg` (copy it to `valve/` on the SD card) defines `gxfire`
(impulse 101, MP5, hold fire), e.g. `+sv_cheats 1 +exec gxtest.cfg +map c2a3 +w100 +w100 +gxfire`; the
decal wall is straight ahead at the c2a3 start. The interpreter needs about 3 minutes to get there. The
same line works on hardware, and `+map c4a1` shakes the ground right away (rumble).

### Cemu and reading GPU results on the CPU

Cemu keeps textures on the host GPU and only writes them back to emulated memory in a few cases. A
`GX2CopySurface` to a `LINEAR_ALIGNED` surface left the memory untouched (black save pictures); copying to
`GX2_TILE_MODE_LINEAR_SPECIAL` is read back at once (`LatteSurfaceCopy.cpp`). Test screenshots with
`+exec gxtest.cfg +map c2a5 +w100 +w100 +w100 +screenshot` (→ `valve/scrshots/`) or `+save name`
(→ `valve/SAVE/name.bmp`). Rumble can't be felt in Cemu; the engine logs what `VPADControlMotor` returned.

### SDL Wii U renderer quirks (found 2026-10-13)

The software renderer's frame goes through SDL's GX2 renderer (`vid_sdl2.c`, `SW_*` functions, Wii U
branches). Three traps, all handled now:

- **Alpha test:** SDL's GX2 renderer enables `GX2SetAlphaTest(GREATER, 0)`. ref_soft leaves alpha at 0, so
  an RGBA texture draws nothing (black screen). We use `SDL_PIXELFORMAT_RGBX8888`.
- **Texture smaller than the TV stops updating:** with a 1280x720 streaming texture on a 1920x1080 TV, the
  screen kept showing the first frame forever while the game ran (owner confirmed by eye in Cemu; the
  log showed the scene being drawn and presented). Uploading with `SDL_UpdateTexture` and dropping
  `SDL_RenderSetLogicalSize` didn't help. Fix: the texture is TV-sized, ref_soft draws into its top-left
  corner, and only that corner is stretched to the screen. Not checked on hardware whether it's a Cemu
  or SDL issue.
- **The render size changes during startup** unless `VID_SaveWindowSize` applies `vid_wiiu_render_height`
  from the first call (before the SDL renderer exists).

Window captures (`capture-cemu.ps1`) of the Vulkan window are reliable; the frozen picture was real.

## Real hardware

- **Engine log over the network:** the app sends every log line as UDP broadcast on port 4405 (WHBLogUdp).
  On a PC in the same network run `F:\Programs\devkitPro\tools\bin\udplogserver.exe`.
- **Crashes:**
  - The crash handler in `sys_wiiu.c` logs, then shows an `OSFatal` screen. It logs the exception type, PC,
    LR, the faulting data address (DAR), registers and stack return addresses, in this form:
    ```
    *** DSI exception: pc 020b8930 lr 020b8a10 dar 00000004 dsisr 40000000
    *** stack: 020c1234 020c5678 ...
    ```
  - Decode the addresses with:
    ```
    F:\Programs\devkitPro\devkitPPC\bin\powerpc-eabi-addr2line.exe -f -C -e build\halflife.elf 020b8930 020b8a10 020c1234
    ```
    In Cemu `.text` is loaded at `0x02000000`, like in the ELF, so Cemu addresses match directly. On the
    owner's console (Aroma) it's loaded `0x0A000000` higher: subtract that first, e.g. a hardware
    `pc 0c46a810` is `0246a810` in the ELF. If the result decodes to nonsense, the load address changed;
    check that pc and lr land in functions that call each other.
- **Hangs:** put `-watchdog 10` in `sd:/wiiu/apps/halflife/cmdline.txt`. Every 10 s the watchdog suspends the engine
  thread and logs its PC, LR and stack, which you decode the same way. Under Cemu the values are garbage
  (Cemu doesn't keep the saved thread context up to date), so this only helps on hardware.

## Useful engine arguments (cmdline.txt)

| Argument | Effect |
|---|---|
| `-dev 2` | Verbose developer messages (file loading, DLL setup...) |
| `-console` | Enables the console (it's already allowed by default on Wii U) |
| `-log` | Also write `sd:/wiiu/apps/halflife/engine.log` (on Wii U the default path is absolute, `sys_con.c`: the working directory isn't set yet when the log opens). The easiest way to get a log from real hardware |
| `+r_speeds 6` | Wii U performance page (top right): frame time, CPU time, GPU and vsync waits, draw calls, vertices, world CPU time. CPU above 16.7 ms means the CPU limits the frame rate; a large GPU wait means the GPU does |
| `+map c1a0` | Start a map directly, skipping the menu |
| `-watchdog <s>` | Hang watchdog (see above) |
