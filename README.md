# BOTW × Minecraft bridge

Play *The Legend of Zelda: Breath of the Wild* as Minecraft's player.

A real Minecraft: Java Edition client runs beside the game in Cemu. Minecraft owns the player: movement, the
camera, the inventory, placing and breaking blocks, its own damage rules. Breath of the Wild supplies the world, its
creatures, its story and its picture. The bridge joins the two every frame, in both directions.

This is a work in progress. It is playable on the Great Plateau; the rest of the game is not tested yet.

**No game files are in this repository.** You need your own copy of the game, of Cemu and of Minecraft.

## What works today

- First person in BOTW's world, with Minecraft's movement, jumping, sneaking, swimming, climbing of ladders and
  gliding.
- BOTW's ground, walls and moving platforms are solid for Minecraft's player: the game's own collision shapes are
  read and streamed to Minecraft as the player travels.
- Minecraft's blocks, hand, items and HUD are drawn inside BOTW's own frame, so the game lights, shadows and fogs
  them like its own scenery.
- Blocks placed in Minecraft are ground and walls for BOTW's creatures and things.
- BOTW's enemies can hurt the player and the player can hurt them; the player's hearts are Link's; BOTW's food heals.
- BOTW's water is water for Minecraft (swimming, boats).
- The runes (Magnesis, Stasis, Cryonis, the two bombs) are Minecraft items; holding one makes it active.
- Scenes of the game (the opening, shrines, towers, terminals) play with BOTW's camera and Minecraft's player model
  moving as Link does.

## What you need

- Windows 10 or 11, 64-bit, a graphics card with Vulkan.
- **Cemu 2.6** (the official Windows build) using its **Vulkan** renderer.
- **Breath of the Wild, Wii U, US version** (title `00050000-101C9400`) with **update v208**, dumped from your own
  console and working in Cemu.
- Minecraft: Java Edition 26.3 is fetched by the mod's Gradle build; you need a JDK 25.
- To build: Python 3.13 with Pillow, MinGW-w64 `g++` (C++23), `glslangValidator`, Git.

## Getting started

1. [Build the bridge and the mod](docs/building.md).
2. [Set it up and play](docs/playing.md).

## How it is made

- [How it works](docs/how-it-works.md): the processes, what crosses between them each frame, and who owns what.
- [The bridge's source](docs/bridge-source.md): what each file of the native side does.
- [The Minecraft mod](docs/minecraft-mod.md): the Fabric mod and the shared-memory protocol.
- [Automatic test runs](docs/testing.md): for people changing the code.
- [Contributing](CONTRIBUTING.md): the project's rules.

```
implementation/botw-live-bridge   the native side: a Vulkan layer and DLL for Cemu, the game's patches, the launcher
implementation/botwcraft-fabric    the Minecraft side: a Fabric mod, and the protocol both sides share
docs                              how it works, how to build, how to play
```

## Credits

- The Minecraft side is an adapted copy of **[SkyCraft](https://github.com/chasmlol/SkyCraft)** by chasmlol (MIT),
  which does the same for Skyrim. Its mod, its protocol and its ideas are the basis of this project.
- Much of what is known here about the game's code was learnt from **BetterVR** by Crementif (MIT), from the
  **Breath of the Wild decompilation** project, and from **WiiXLaunch-BotW**.
- `vkroots` (Apache-2.0 OR MIT) is used for the Vulkan layer.

See [NOTICE](implementation/botw-live-bridge/NOTICE) and the licence files beside it.

## Licence

This project is under the [MIT licence](LICENSE). The Minecraft mod is SkyCraft's work, changed; SkyCraft's own MIT
licence and copyright notice are kept in [implementation/botwcraft-fabric/LICENSE](implementation/botwcraft-fabric/LICENSE).
The licences of the other projects whose work is used are beside the [NOTICE](implementation/botw-live-bridge/NOTICE).

The licence covers this project's code and documents only. It gives no right to the game, to Cemu or to Minecraft.

This project is not affiliated with Nintendo, Mojang or Microsoft. *The Legend of Zelda* and *Minecraft* are
trademarks of their owners.
