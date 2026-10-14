# The Minecraft mod

`implementation/botwcraft-fabric` is a copy of [SkyCraft](https://github.com/chasmlol/SkyCraft) by chasmlol (MIT),
revision `bfcaf178524b92c2cdeb88e4ce0f13ef9ded6f32`, adapted for BOTW where the game needed it. It is a Fabric mod
for Minecraft: Java Edition 26.3 (Java 25, Fabric Loader 0.19.5, Fabric API).

Here the mod is called **BotwCraft**: its id in Minecraft is `botwcraft`, its packages are `dev.botwcraft`, and
what SkyCraft names `Sky…` or `Skyrim…` is named `Botw…` (`SkyLink` is `BotwLink`, `SkyrimActorEntity` is
`BotwActorEntity`, and so on). The rename was done by rule over the whole mod, so a few comments that were written
about Skyrim now say BOTW where they describe something only Skyrim has (form ids, skills). To compare a file with
SkyCraft's, undo those name rules first.

```
implementation/botwcraft-fabric
  protocol/botwcraft_protocol.h   the shared memory's layout, used by the mod (as Proto.java) and by the bridge's DLL
  fabric/                        the Gradle project of the mod
    src/main/java/dev/botwcraft/          both sides: the link, collision, combat, runes, mixins of the server
    src/client/java/dev/botwcraft/client/ the client: exporters, input, the per-frame glue, mixins of the client
    src/main/resources/                  fabric.mod.json, the mixin lists, the rune items' models and pictures
  retained/                      the Gradle build made by setup_mod.py (not in Git)
```

## What the mod does

It makes a normal Minecraft client into the player's half of the bridge.

| Part | Classes | What |
|---|---|---|
| The link | `link/BotwLink`, `link/Proto` | Opens the shared memory when the other game appears, reads and writes its parts, notices when the other game is gone. `Proto` is the layout of `botwcraft_protocol.h` in Java. |
| The frame | `client/BotwClient`, `client/BotwCraftClient` | The per-frame glue on the render thread: publish the player's state, take the other game's state, drive the exporters. |
| The world | `client/MirrorWorld` | Opens (or makes) an empty "mirror" world by itself. The player stands in it; the other game's land comes in as collision only. |
| Input | `client/InputBridge` | Takes keyboard and mouse events from the input ring and feeds them to Minecraft, so Minecraft is played while its window is hidden. |
| Collision | `world/BotwCollision`, `world/TriCollider`, `world/BotwTri`, `world/BotwRay`, `world/BotwEdge`, `world/BotwClip`, `client/BotwCollider` | The other game's triangles and filled blocks as solid ground for Minecraft's player, entities and rays. |
| Water | `world/BotwWater` | The other game's water as Minecraft's water. |
| Drawing out | `client/render/WorldExporter`, `AvatarExporter`, `BotwAtlas`, `BlockLightColors`, `client/FrameExporter` | What Minecraft would draw (block sections, the player's model and hand, the textures, the HUD's pixels), written to the render ring and the overlay slots instead of the screen. |
| Combat | `combat/BotwCombat`, `combat/BotwActorEntity`, `client/ProxySync`, `client/BotwSwing` | The other game's creatures as unseen stand-in entities at their exact places: Minecraft's weapons hit them, and their hits come back as Minecraft damage. |
| Runes | `rune/BotwRunes` | BOTW only: the rune items. The rune in the main hand is told to the game every frame. |
| Saves | `save/BotwSaves` | BOTW only: what the player carries is written to a file with each save of the game's and put back when the game loads that save. |
| Names of the game's creatures | `combat/BotwNames` | BOTW only: a creature's stand-in carries the game's English name (from `-Dbotwcraft.actorNames`), so Minecraft's death message names who killed the player. |
| Climbing and stamina | `world/BotwClimb`, `client/BotwClimbClient`, `client/StaminaHud`, `mixin/LivingEntityClimbMixin` | BOTW only: the player climbs the game's walls by Minecraft's ladder rules; going up costs the game's stamina, which a row of the HUD shows. |
| Walking with Link | `client/BotwClient` (`McState.pushX/Z`), `client/mixin/EntityCollideMixin`, `client/mixin/LocalPlayerSprintMixin` | BOTW only: what collision took from the player's step goes to the bridge as a push; no sprint while the game's stamina is used up (sprinting is the game's dash). |
| Digging | `world/BotwDig`, `world/BotwDigBlast`, `client/BotwDigClient`, `client/render/DigWalls` | SkyCraft's digging into the other game's ground. Not used for BOTW yet. |
| Multiplayer | `net/BotwNet` | SkyCraft's multiplayer. Not tried with BOTW. |

The mixins (`mixin/`, `client/mixin/`) are the places where Minecraft's own code is changed: block collisions,
swimming and climbing, projectiles, the window (hidden), the frame limiter, the world-opening flow.

## What was changed for BOTW

- The mod's name, id, packages and class names (see above). The shared memory has its own name
  (`Local\BotwCraft_v1`), so this bridge and SkyCraft never meet.
- The collider: BOTW's ground has other slopes and edges than Skyrim's; the walkable slope is a setting
  (`-Dbotwcraft.walkableDegrees`).
- The rune items (`rune/BotwRunes`, the five item models and pictures) and what the mod tells the game about the held
  rune.
- Effects of the game's food as Minecraft effects; hearts tied to Link's hearts.
- What the player carries goes with the game's saves (`save/BotwSaves`).
- Climbing the game's walls after a jump, at Minecraft's ladder speed unless set (`-Dbotwcraft.climbSpeed`, from `lab/climb-speed.txt`), and the game's stamina in the HUD.
- Small changes for scenes of the game's (the player's model is exported while the game's camera is shown).

Each change says in a comment why it was made. `git log` on this folder has the rest.

## How it is started

Not from a Minecraft launcher: `client.py` starts the Fabric **development client** from the Gradle build, with

- `-Dbotwcraft.link=Local\BotwCraft_v1`: the shared memory's name,
- `-Dbotwcraft.startHidden=true`: no window,
- `-Dbotwcraft.quitWithBotw=true`: save and quit when the other game is gone,
- an offline player name, and `lab/minecraft` as the game folder (not your own `.minecraft`).

Minecraft itself is downloaded by Gradle from Mojang's servers when the mod is built. It is not in this repository.

## Changing the mod

Edit the Java sources, then:

```powershell
python -B mod_build.py
```

and start a session. `mod_build.py` compiles only what changed and runs the collider's offline check
(`TriColliderCheck`). The unit tests of the Gradle project (`./gradlew test`) cover the collider and the rays.

## Licence

SkyCraft is MIT (`implementation/botwcraft-fabric/LICENSE`). `THIRD-PARTY-NOTICES.md` is SkyCraft's own list.
