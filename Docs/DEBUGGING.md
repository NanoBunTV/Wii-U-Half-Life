# Testing and debugging

## Cemu (PC)

The owner uses Cemu 2.6 (the latest release as of 2026-10; checked on GitHub) at
`E:\ROMs and Emulators\_Emulators\Cemu (Wii U)\Cemu.exe`.

```
.\scripts\run-cemu.ps1                       # run build\halflife.wuhb, print engine log lines live
.\scripts\run-cemu.ps1 -EngineArgs '-dev 2'  # set sd:/halflife/cmdline.txt first
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
   history below).
2. Narrow it down with temporary `Con_Reportf( "WIIU TRACE: ..." )` lines. OSReport output reaches the log
   right up to the crash. Remove them afterwards; `git checkout -- <file>` in `external/` is easiest if the
   file has no other changes.
3. `scripts/capture-cemu.ps1` saves an image of Cemu's windows (only Cemu's) to `build/cemu-capture/`.

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
    `.text` starts at `0x02000000` in both the ELF and the loaded RPX, so the addresses match directly.
- **Hangs:** put `-watchdog 10` in `sd:/halflife/cmdline.txt`. Every 10 s the watchdog suspends the engine
  thread and logs its PC, LR and stack, which you decode the same way. Under Cemu the values are garbage
  (Cemu doesn't keep the saved thread context up to date), so this only helps on hardware.

## Useful engine arguments (cmdline.txt)

| Argument | Effect |
|---|---|
| `-dev 2` | Verbose developer messages (file loading, DLL setup...) |
| `-console` | Enables the console (it's already allowed by default on Wii U) |
| `-log` | Also write `engine.log` into `sd:/halflife/` |
| `+map c1a0` | Start a map directly, skipping the menu |
| `-watchdog <s>` | Hang watchdog (see above) |
