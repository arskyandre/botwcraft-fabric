# Building

Everything is built on Windows 10 or 11, 64-bit. Nothing of the game is needed to build; the game is needed to play.

## What to install

| What | Used for | Notes |
|---|---|---|
| Git | getting the sources | |
| Python 3.13 | the build scripts and the launcher | `pip install pillow` (and `numpy` for the test tools) |
| MinGW-w64 `g++` with C++23 | the bridge's DLL | a recent MSYS2 or WinLibs build |
| `glslangValidator` | the bridge's shaders | in the Vulkan SDK, or MSYS2's `glslang` package |
| JDK 25 | the Minecraft mod | for example Eclipse Temurin 25 |
| Cemu 2.6, official Windows build | running the game | the bridge checks that it is this build |

`ffmpeg` is optional (video of automatic test runs).

## 1. Get the sources

```powershell
git clone <this repository> botw-minecraft
cd botw-minecraft
git clone https://github.com/KhronosGroup/Vulkan-Headers implementation/botw-live-bridge/vendor/Vulkan-Headers
git -C implementation/botw-live-bridge/vendor/Vulkan-Headers checkout 9a0f3099c8a9607a7c0f3127d8abfdc19a93e8c5
```

The Vulkan headers are only used to compile; `build.ps1` checks that revision.

## 2. Say where things are on your machine

No path of any machine is written in the sources. Copy `implementation/botw-live-bridge/local.example.json` to
`local.json` in the same folder and fill it in:

| Key | What |
|---|---|
| `cemu` | your `Cemu.exe` (2.6) |
| `game` | the game's `code/U-King.rpx` (the base game, as Cemu starts it) |
| `jdk` | the JDK 25 folder; leave it out if `java` and `javac` 25 are on the PATH |
| `gxx`, `glslang`, `ffmpeg` | the tools; leave one out to take it from the PATH |
| `keep_my_graphic_packs` | `true` (the default): your graphic packs stay on when you play; see [playing](playing.md) |
| `evidence` | optional: where automatic test runs write their records (default `runs/` in the project) |
| `game_content` | optional: the game's `content` folders, the update's first; else the `content` beside `game`. The launcher reads the game's English names of its creatures there for Minecraft's death messages (`actor_names.py`, written to `lab/actor-names.json`; nothing goes into the project) |

`local.json` is not in Git.

## 3. Build the bridge and install it

In PowerShell, in `implementation/botw-live-bridge`:

```powershell
./build.ps1 -Install
```

This compiles `build/botw_live_bridge.dll` and the shaders, runs the offline checks (`native_check.cpp`,
`finite_patch_check.py`) and installs:

- `lab/botw_live_bridge.dll`, `lab/layer.json` and `lab/fixture/`: the bridge's working folder (`lab` is not in Git).
- the graphic pack `pack/` into Cemu's data folder as `graphicPacks/NativeBridge`.

Cemu must be closed. Nothing else of Cemu's is changed by the build: the pack is left switched off, and the
launcher switches it on only for its own sessions.

## 4. Build the Minecraft mod

In the same folder:

```powershell
python -B setup_mod.py
python -B mod_build.py
```

`setup_mod.py` is run once. It copies the mod's Gradle project to `implementation/botwcraft-fabric/retained` (not in
Git) and runs Gradle there: Gradle downloads Minecraft 26.3, Fabric and the mod's libraries (about 1.2 GB, some
minutes) and writes the launch files of the development client.

`mod_build.py` is run after every change of the mod's Java sources. When something changed, it compiles the mod's
sources again over a copy of that build with `javac` (seconds, no Gradle) and runs the offline collider check. The launcher starts Minecraft from
this copy.

If the mod's dependencies change (`gradle.properties`, `build.gradle`), move the `retained` folder away and run
`setup_mod.py` again.

A plain Gradle build also works and gives the mod's jar (`fabric/build/libs/botwcraft-0.1.2.jar`):

```powershell
cd implementation/botwcraft-fabric/fabric
./gradlew build
```

The launcher does not use that jar today; it starts the development client. A packaged release (a jar for an ordinary
Minecraft launcher, with the DLL and the pack) is not made yet.

## 5. Play

See [playing](playing.md).

## What a build does not touch

- Your game files, your saves and your shader caches.
- Cemu's settings: only a session of the launcher changes them, and puts them back (see [playing](playing.md)).
