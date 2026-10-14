# Playing

First [build and install](building.md). You need your own copy of *Breath of the Wild* (Wii U, US, update v208)
working in Cemu 2.6 with the Vulkan renderer.

## Start

Close Cemu. Then run `implementation/botw-live-bridge/play.cmd` (double-click, or from a terminal):

```powershell
python -B native_route.py play --evidence gameplay
```

What happens:

1. A copy of your saves of the game is made in `lab/save-backups/<date_time>` (see "Your saves" below).
2. Cemu starts with the game, with the bridge loaded. Minecraft starts hidden beside it and opens its own world.
3. The game shows its title screen. **Nothing is pressed for you**: choose Continue, a save, or New Game with your
   controller as always.
4. When the save is loaded, Minecraft's player stands where Link stands and you see through its eyes.

Close Cemu to end the session. The launcher then stops Minecraft and puts Cemu's settings back.

## Controls

You play with **mouse and keyboard, as in Minecraft** (Minecraft's default keys): walk, jump, sneak, sprint, the
hotbar, the inventory, attack and use.

| Key | What |
|---|---|
| F8 | hands control to the game (your controller drives Link's menus and the game) and back to Minecraft |
| F7 | hands control to the game and opens the game's menu |
| G | the game's A button (talk, pick up, open, confirm) |
| Backspace | the game's B button |
| Arrow keys | the game's D-pad |

While a screen or an event of the game is up, these keys also work: Q / E = L / R, Z / C = right stick left / right,
X = X, Y = Y, Tab = Plus, M = Minus.

At the title screen, and whenever the game has control, your controller works as you set it up in Cemu.

The runes are items in Minecraft's hotbar once the game has given them: hold one and use it.

## What a session changes on your computer, and how it is put back

**Cemu's settings.** The bridge needs its own graphic pack switched on and Cemu in a window. At the start of a
session the launcher copies Cemu's `settings.xml` to `lab/cemu-settings-before.xml` and writes a changed one. When
Cemu has ended, the copy is put back: by the launcher, and also by a small watcher process of its own, so it happens
even when the launcher's window was closed first. If something goes wrong (a crash, a power cut), run this once with Cemu
closed:

```powershell
python -B session.py restore
```

Do not start Cemu by itself while `lab/cemu-settings-active.txt` holds `1`: the bridge's pack would be on without
the bridge, and the game does not run like that. `restore` fixes it.

**Your graphic packs.** When you play, your graphic packs stay as you have them in Cemu: the resolution and the
other choices of the Graphics pack, Enhancements, FPS++, cheats, your mods. The bridge only adds its own pack for
the session and takes it out again after it. A pack you switched off stays off.

- None of the community packs for this game changes an instruction the bridge changes. Tried in a run with
  Graphics (1920x1080, more shadow detail), Enhancements, Draw Distance, FPS++ and three cheats on: Minecraft's
  blocks were drawn and lit, the menus worked, 30 frames a second.
- FPS++ does not raise the frame rate here: the bridge replaces the game's wait for the next frame with its own.
- If something looks wrong, `"keep_my_graphic_packs": false` in `local.json` plays with the bridge's pack alone
  (and the Graphics pack's resolution named in `lab/render-size.txt`, or `native` for the game's own 1280x720).
  That is also what automatic test runs use, so that they are the same on every machine; `BRIDGE_PACKS=mine` in
  the environment makes a test run use your packs.

**Frame rate.** The bridge paces the game at exactly 30 frames a second, the game's own rate.

## Your saves

The game saves by itself while you play, into your own save files, as it always does. The mod is young. The
launcher therefore copies your saves before every session to `lab/save-backups/`. To go back, close Cemu and copy a
backup's content over Cemu's `mlc01/usr/save/00050000/101c9400`.

Minecraft's world of the session is kept in `lab/minecraft/` (not your normal Minecraft folder).

**What the player carries goes with the game's saves.** When the game writes a save (by itself or from its menu),
what Minecraft's player carries at that moment is kept with that save: the inventory, what is worn, the ender chest,
experience and hunger. When the game loads a save, the player's things of that save come back, so going back to an
older save goes back in Minecraft too. A New Game starts with nothing. A save made before the mod gets what the
player carries when it is first loaded.

- The files are in `lab/minecraft/saves/BotwCraft/botwcraft-saves/`, one small file for each save of the game's.
- Before anything is put back, what the player carried is written to `before-load-<time>.dat` in that folder, so a
  load never loses things for good.
- Like in the game itself, what you got after the last save is gone after a load. The game saves often by itself.
- Minecraft's blocks are not part of this yet: what you built or dug stays as it is, whichever save is loaded.
- `-Dbotwcraft.saves=false` (Minecraft) or `0` in `lab/save-link.txt` (the bridge) switches it off.

## Hearts

The player's hearts are Link's: as many as Link has, and the game's own row of hearts shows them too. Yellow hearts
of the game (some food, Mipha's Grace) are Minecraft's yellow hearts (absorption), and Minecraft's yellow hearts (a
golden apple) show in the game's row. When the game loads a save, the player has the hearts of that save.

## Falling

The player falls as far without harm as Link does: about 15 blocks (the game's own height for Link, 20 of its
metres), not Minecraft's 3. Beyond that the harm is Minecraft's own, half a heart for each block more.
`lab/safe-fall.txt` may hold another height in blocks.

## Climbing and stamina

The player climbs the game's walls. **Jump into a wall**, facing it, and keep the forward key held: the player
goes up at Minecraft's ladder speed (2.35 blocks a second). Let go of the key and the player slides down; sneak
to hold on; step back to drop off. A wall is every surface of the game's that is too steep to walk on and reaches
more than one block above the ground the jump began on. Walking into a wall does not climb it: that is for
pushing things, such as the game's round boulders (see "Walking, sneaking, sprinting").
Not climbed: walls the game marks as not climbable, and the patterned stone of the game's shrines. The game's
ladders work as before, at Minecraft's ladder speed and without cost.

Going up a wall costs **Link's own stamina**, per block of height: one wheel lasts 25 blocks, at any speed.
Link's own climbing costs 36.5 a second (the game's parameter; a wheel is 1000), about 27 seconds a wheel; 25
blocks assumes he climbs 1.2 metres a second, which is not measured yet. Holding
on, sliding down and ladders cost nothing. With no stamina left the player falls off and can climb again only
when the stamina is full. The stamina fills up by itself on the ground, by the game's own rules.

Minecraft's HUD shows the stamina as a row of green icons over the food, where the air bubbles are (they move
one row up under water): ten icons are one wheel, and each wheel Link gains is one more row. The row shows only
while the stamina is not full. Red icons: used up, wait until full.

Settings: `lab/climb-height.txt` holds the blocks one wheel lasts (default 25; 0: climbing is free).
`lab/climb-speed.txt` holds the speed in blocks a second (default 2.35; 0: walls are not climbed).

## Walking, sneaking, sprinting

The game walks Link along with the player, so it hears and feels him as it would hear and feel Link:

- Walking and running make the game's noise (its noise meter on the HUD moves) and its step sounds, besides
  Minecraft's. The game's creatures hear it.
- Sneaking is the game's crouch: almost no noise.
- **Sprinting is the game's dash and costs Link's stamina**, as for Link: a wheel lasts about three seconds of
  sprinting. When it is used up (red icons) the player cannot sprint until it is full again.
- Link's weight is on what he stands on, and he pushes what the player walks into: a round boulder moves a little.
  The player slides around a round boulder, so a long push like Link's does not work yet.

Settings: `lab/walk.txt` (`0` off, `1` without the dash, `2` the default) and `lab/walk-drive.txt` (`0`: Link's body
is put at the player's place as before, without weight or push).

## The game's HUD

While Minecraft has the player, the bridge hides three parts of the game's HUD, because they stand over
Minecraft's HUD or show the wrong thing with a keyboard: the text of what a button does now ("A Examine"), the
hints of the controls at the start of a new game, and the guide of the D-pad at the top left. No file of the
game is changed for this. `0` in `lab/hud-hide.txt` shows them again.

The game's hearts and its stamina wheel are not hidden by the bridge. If you do not want them twice, Cemu's own
graphic pack "Remove HUD elements" hides them.

## Dying

When the player's hearts run out in Minecraft, Link dies in the game, by the game's own rules:

- What brings Link back in the game (Mipha's Grace, a fairy you carry) brings you back on the spot, with the
  hearts the game gives: Mipha's Grace fills them and adds yellow hearts.
- Otherwise the game shows its own Game Over. **Continue** (the `G` key is the Wii U's A) loads your last save, and
  what the player carries goes back to that save too (see "Your saves").

Nothing is dropped in Minecraft. `0` in `lab/death.txt` switches this off: the player then comes back on the spot
and nothing happens in the game.

Minecraft's death message names the game's creature that killed you, by the game's own name: "BOTWCraft was slain by
Bokoblin" for a blow, "was pummeled by Bokoblin" for a rock it threw, "was shot by" for an arrow. The names are read
from your copy of the game when Minecraft starts.

Link dies the game's way for what killed the player. The bridge gives Link a last hit of the game's own kind,
and the game picks its death by its own rules:

| What killed the player | Link in the game |
|---|---|
| a blow (a creature, another player) | reels and falls forward; knocked to the ground if the blow pushed him |
| fire, lava, a hot floor | staggers, burning, and falls |
| an arrow, a thrown thing, a blast | is knocked to the ground |
| anything else (a fall, drowning, ...) | the plain death (in water, the swimmer's) |

`lab/death-hit.txt`: `0` gives the plain death always.

## Your skin

Put a 64x64 skin of the classic model as `lab/skin/skin.png`. `lab/skin/name.txt` may hold a player name.

## What does not work yet

- Only the Great Plateau was played from start to end. Elsewhere, expect things nobody has seen yet.
- Minecraft's blocks do not go with the game's saves yet (what the player carries does, see "Your saves").
- BOTW's weapons, bows, shields, cooking and clothes are still used through the game's own menu (F7).
- Climbing is a first version: no jump up a wall, rain does not make walls slippery, swimming costs
  no stamina, and some single objects in shrines can be climbed although Link could not (the bridge does not
  know their material yet).
- Deaths by electricity, cold and falls show the game's plain death (see "Dying").
- Pushing a round boulder moves it only a little (the player slides around it). Floor switches and the game's
  creatures hearing the player were not tried yet in play.
- The bridge is made for one exact build of Cemu and one exact version of the game. Another version stops with a
  message.

## If it does not start

| What you see | What to do |
|---|---|
| "Cemu is already running; close it first" | close Cemu, start again |
| "run build.ps1 -Install" | the pack is not in Cemu's data folder: [build and install](building.md) |
| "The bridge is made for Cemu 2.6" | `local.json` names another Cemu build |
| Cemu: "Unable to create a Vulkan instance" | `lab/layer.json` or the DLL is missing: run `./build.ps1 -Install` |
| The game runs but Minecraft's hand and HUD never show | look at `lab/native.txt` and `lab/minecraft/logs/latest.log` |

The launcher writes what it did into a folder of its own for each session (`runs/gameplay/route-N`, or the
`evidence` folder of `local.json`).
