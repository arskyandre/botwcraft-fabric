# Contributing

Thank you for wanting to help. This page is the project's rules. They are short and they are meant.

## The goal

Play the whole story of *Breath of the Wild* as Minecraft's player. The work goes area by area: the Great Plateau
first, from the Shrine of Resurrection to the paraglider, then the rest of the game.

The decisions so far:

- **Minecraft owns the player; BOTW owns the world.** Movement, camera, inventory and blocks are Minecraft's.
  Land, creatures, story, weather and the picture are BOTW's.
- **SkyCraft is the basis.** The Minecraft mod (named BotwCraft here) and the protocol stay close to SkyCraft. A change for BOTW is fine
  when a measured problem needs it, and its comment says why.
- **One picture.** Minecraft's things are drawn inside the game's own frame, so the game lights and shadows them.
  A change of the drawing must keep that path.
- **30 frames a second by default, but never assumed.** The bridge paces at the game's rate; no code may depend on
  the number 30, so higher rates stay possible.
- **Scenes of the game** are shown with BOTW's camera and Minecraft's player model moving as Link.
- **BOTW's items stay BOTW's** for now (weapons, food, rupees); the runes are Minecraft items.

## No game files. Ever.

- Nothing from the game goes into this repository: no part of its program, no textures, models, sounds, text or
  maps, no saves, no shader caches, and no screenshots or videos of it.
- Nothing of Minecraft's either: no jar, no assets. Gradle fetches Minecraft from Mojang.
- What may be written down about the game: addresses and offsets in its program, names of its actors and bones,
  the meaning of its data, and patches we wrote ourselves.
- Everything the mod needs from the game is read from the **running** game in the player's own Cemu.
- Pictures you draw for the mod must be your own work.

## No paths of your machine

Nothing in the tracked files may name a folder of one computer or a person. Paths come from `local.json` (ignored
by Git) through `local.py`. Before a commit, search your change for your user name and your drive letters.

## Never delete directly

Tools of this project do not delete files. What must go is moved to the Windows recycle bin (`recycle.py`), so a
mistake can be undone. This holds for test tools, build scripts and anything a contribution adds.

## Leave the player's Cemu as it was

The bridge runs in the player's own Cemu. Whatever a session changes there (settings, packs) it saves first and
puts back afterwards, also when it fails. A contribution that touches the launcher must keep that true, and must
never write into the player's saves in an automatic run (`--private-saves`).

## How a change is checked

1. It builds: `./build.ps1`, `python -B mod_build.py`. The offline checks pass.
2. An automatic run shows it works ([docs/testing.md](docs/testing.md)), and **you looked at the run's pictures**.
3. Feel (how movement, the camera or a fight feels) is judged by a person playing, not by numbers. Do not tune in
   long loops against a number; make the change, check it shortly, and let it be played.
4. Report what you saw honestly: what works, what does not, what was not tested.

## The code

- Match the code around your change: its naming, its density, its kind of comments.
- A comment says **why**: what was measured or seen that made the code so. Addresses of the game's program carry a
  note on how they were found.
- Read the game's memory only through the checked helpers, and check a structure before trusting it. If something
  is not as expected, give control back to the game; never guess.
- Short, bounded work on the game's threads; everything heavy on a worker with its own copy of the bytes.
- Code taken from another project keeps its licence and is named in
  [NOTICE](implementation/botw-live-bridge/NOTICE). Do not copy from GPL projects into the bridge.

## Licence of contributions

The project is under the MIT licence ([LICENSE](LICENSE)). What you contribute is under the same licence.

## Sources of knowledge

The game's inner workings were learnt from [BetterVR](https://github.com/Crementif/BotW-BetterVR), the
[BOTW decompilation](https://github.com/zeldaret/botw) (a Switch build: names and structures help, addresses do
not), [Cemu](https://github.com/cemu-project/Cemu)'s source, and by reading the game's program in memory.
