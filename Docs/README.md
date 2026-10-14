# Half-Life for Wii U: documentation

Developer documentation and project memory. Start here when picking the project back up, whether you're a
person or an AI assistant in a new chat.

| File | What's in it |
|---|---|
| [PROJECT.md](PROJECT.md) | What the project is, its history, the decisions made so far and why, and the owner's working preferences |
| [ARCHITECTURE.md](ARCHITECTURE.md) | How the engine, game code, menu and renderer are combined into one Wii U executable, source layout, the Wii U platform layer |
| [BUILDING.md](BUILDING.md) | Toolchain, build steps, the Windows path-with-spaces workaround, patches and updating the engine, packaging |
| [DEBUGGING.md](DEBUGGING.md) | Testing in Cemu (SD card, logging, interpreter mode, the recompiler crash), real hardware logging, crash and hang reports |
| [STATUS.md](STATUS.md) | What works, test results, known issues and the roadmap |

Helper scripts are in [`../scripts/`](../scripts/):

- `fetch-deps.ps1`: downloads the engine and game source and applies the patches.
- `make-patches.ps1`: saves the changes made in `external/` as patches.
- `run-cemu.ps1`: starts the build in Cemu.
- `capture-cemu.ps1`: saves an image of Cemu's windows.
- `cemu-recompiler-bisect.ps1`: finds code that Cemu's recompiler mis-runs.

Keep these files up to date when something important changes. In particular, record anything learned about Cemu
behaviour or Wii U quirks in DEBUGGING.md, since it's easy to lose an afternoon to the same problem twice.
