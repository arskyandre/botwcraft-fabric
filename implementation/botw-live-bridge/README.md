# The native side

The bridge's DLL for Cemu (a Vulkan layer and the game's hooks in one), the Cemu graphic pack with the game's
patches, and the Python launcher.

| To do | Command (in this folder) |
|---|---|
| Build and install | `./build.ps1 -Install` |
| Build the Minecraft mod | `python -B setup_mod.py` once, then `python -B mod_build.py` |
| Play | `play.cmd` |
| Put Cemu's settings back after a crash | `python -B session.py restore` |

Before the first build, copy `local.example.json` to `local.json` and fill it in.

- [Building](../../docs/building.md)
- [Playing](../../docs/playing.md)
- [How it works](../../docs/how-it-works.md)
- [What each file does](../../docs/bridge-source.md)
- [Automatic test runs](../../docs/testing.md)

`lab/` (made by the build, not in Git) is the bridge's working folder. Credits and licences: [NOTICE](NOTICE).
