# How it works

## The idea

Two real games run at the same time and each does what it is good at.

- **Minecraft** (a real Java Edition client with a Fabric mod) owns the player: its movement, its camera, its
  inventory, its blocks, its damage rules.
- **Breath of the Wild** (in Cemu) owns the world: the land, the creatures, the story, the weather, the picture.
- **The bridge** joins them every frame, in both directions.

The Minecraft side is an adapted copy of [SkyCraft](https://github.com/chasmlol/SkyCraft), which does this for
Skyrim. SkyCraft's mod and its shared-memory protocol are kept (renamed BotwCraft here); the Skyrim plugin is replaced by a
bridge for BOTW.

## The processes

```
 ┌──────────────────────────── Cemu.exe ────────────────────────────┐        ┌──────── java.exe ────────┐
 │  the game (emulated PowerPC)                                     │        │  Minecraft 26.3 + Fabric │
 │     │  patches of the graphic pack call Bridge_* functions       │        │  the mod (botwcraft)     │
 │     ▼                                                            │        │                          │
 │  botw_live_bridge.dll ── reads and writes the game's memory      │ shared │  player, world, HUD      │
 │     │                                                            │ memory │                          │
 │     ├── Vulkan layer: sees Cemu's drawing, adds Minecraft's      │◄──────►│  exports geometry, takes │
 │     │                                                            │        │  collision and input     │
 │     └── captures the keyboard and the mouse of Cemu's window     │        │  (its window is hidden)  │
 └──────────────────────────────────────────────────────────────────┘        └──────────────────────────┘
                 ▲ started and watched by the launcher (Python: session.py, client.py, native_route.py)
```

You only ever see Cemu's window. Minecraft's own window stays hidden; what Minecraft would draw is sent to the
bridge and drawn inside the game's picture.

## The bridge's three connections

### 1. To the game's code: the graphic pack

`pack/patch_bridge.asm` is a Cemu graphic pack for the US version with update v208. At chosen addresses of the
game's program it puts a jump to a function named `import.coreinit.Bridge_…`. Cemu looks such imports up in a table
of host functions, and the DLL adds its own functions to that table when it is loaded (Cemu's exported
`osLib_registerHLEFunction`). So a patch in the game calls straight into C++ in the DLL, on the game's own thread,
with the emulated processor's registers as its arguments. The DLL reads and writes the game's memory through
`memory_getBase`.

About fifty such entry points exist. Some examples:

| Entry | Where in the game | What the bridge does there |
|---|---|---|
| `Bridge_Input` | the game reads the gamepad | passes the controller through, or sends what Minecraft's player needs |
| `Bridge_Post` | after the player's physics step | puts Link's body where Minecraft's player is |
| `Bridge_Camera`, `Bridge_RenderCamera`, `Bridge_RenderProjection` | the game builds the frame's camera | gives it Minecraft's eye and view |
| `Bridge_ActorOpacity`, `Bridge_WeaponOpacity` | drawing of Link and what he holds | hides Link in first person |
| `Bridge_Shape…`, `Bridge_Terrain…`, `Bridge_Water…` | inside the physics world's lock | reads collision shapes, terrain heights and water safely |
| `Bridge_Health`, `Bridge_AddDamage`, `Bridge_DamageReceive`, `Bridge_Recover`, `Bridge_Eaten` | hearts, hits and food | keeps Link's hearts and Minecraft's health the same |
| `Bridge_FramePace`, `Bridge_SwapPace` | once a frame | holds the game at its 30 frames a second while waiting for Minecraft |

The pack must never be on without the DLL (its imports would not exist). The launcher switches it on only for its
own sessions.

### 2. To Cemu's drawing: the Vulkan layer

The same DLL is a Vulkan layer (`layer.json`, built on `vkroots`). Cemu draws the game with Vulkan, and the layer
sees every call. It recognises the game's passes in a frame (the G-buffer pass, the shadow maps, the lit HDR
picture, the final picture) and adds draw calls of its own in them (`integrate.h`, `render.h`):

- Minecraft's blocks, the player's hand and held item, and Minecraft's entities are written into the game's
  **G-buffer** with the game's own depth. The game then lights them, fogs them and puts its shadows on them like its
  own scenery.
- The same geometry is drawn into the game's **shadow maps**, so Minecraft's things cast shadows on the game's world.
- Minecraft's HUD and screens (hotbar, hearts, inventory, chat) are laid over the final picture.

To know which pass belongs to which camera, a patch (`Bridge_MarkerFloat`, `Bridge_Match`) lets the layer match a
frame it sees in Vulkan to the frame the game is building.

### 3. To Minecraft: shared memory

The DLL and the mod share one block of memory (a named Windows file mapping). Its layout is
`implementation/botwcraft-fabric/protocol/botwcraft_protocol.h`, used by both sides; SkyCraft's layout is kept. The
parts:

| Part | Direction | What |
|---|---|---|
| `BotwState` | game → Minecraft | the game's side of the player: its place, the look, a teleport request, the hour of the day, a change of world |
| `McState` | Minecraft → game | the player: position, eye, look, speed, what it does, its health |
| input ring | game side → Minecraft | keyboard and mouse events, captured from Cemu's window |
| collision ring | game → Minecraft | the game's collision around the player, as triangles and filled blocks |
| `WaterGrid` | game → Minecraft | the water's height around the player |
| `ActorTable` | game → Minecraft | the game's creatures and things near the player |
| event ring | Minecraft → game | what the player did: a swing, a hit, a used item |
| `WorldEntities` | Minecraft → game | Minecraft's entities |
| render ring | Minecraft → game | Minecraft's geometry: sections of blocks, the atlas, the player's model |
| overlay slots | Minecraft → game | the pixels of Minecraft's HUD |

Single values are guarded by a sequence number (a writer never blocks a reader); streams are single-writer rings.
No side ever waits on a lock of the other.

## A frame, step by step

1. **Input.** The DLL captures the keyboard and the mouse while Cemu's window is in front (`physical.h`) and puts the
   events on the input ring. The mod's `InputBridge` feeds them to Minecraft as if they were its own. Minecraft
   is played without ever being the window in front.
2. **Minecraft moves its player.** The player collides with the game's world: the DLL reads the game's own collision
   shapes (static Havok meshes, terrain, moving bodies) around the player in rolling regions (`native_shapes.h`,
   `native_mesh.h`, `native_regions.h`, `native_movers.h`), turns them into Minecraft's space and streams them
   (`native_stream.h`). The mod's `BotwCollision` and `TriCollider` make them solid for Minecraft's physics.
3. **The game follows.** The newest `McState` is read (`body.h`). Before the game's physics step Link's body is given
   the speed that brings it to the player's place in that step, so the game's physics moves it (see "Link walks"
   below); further off than half a unit, or in a state the game moves by itself, it is put there. The frame's camera
   is Minecraft's eye. Link is not drawn.
4. **Minecraft's world goes into the game's frame.** The mod exports what it would draw (`WorldExporter`,
   `AvatarExporter`, `BotwAtlas`, `FrameExporter`); the DLL keeps it as snapshots (`live_scene.h`, `live_gpu.h`) and
   draws it in the game's passes.
5. **The game's world goes into Minecraft.** Creatures become entities Minecraft can hit and be hit by (`actors.h`,
   the mod's `BotwCombat`); water becomes water (`water.h`, `BotwWater`); hearts, damage, food and effects are carried
   both ways.
6. **Minecraft's blocks go into the game's physics.** Solid blocks near the game's creatures and things are given
   to the game as walls and ground (`walls.h`).

### Places and sizes

Minecraft's space and the game's space are tied by one fixed rule (`native_regions.h`): a block is 1.3 of the
game's metres, and a fixed point of Minecraft's world sits on a fixed point of the game's. Minecraft's player is
1.8 blocks tall, so it is a little taller than Link.

### Time

The game runs at 30 frames a second. The bridge holds exactly that pace (`Bridge_FramePace`) and takes the newest
state Minecraft has published; it never makes the game wait for Minecraft longer than a small budget. Nothing in
the code assumes 30: the pace is a setting.

## Who is in control

Minecraft controls the player only while the bridge holds a **claim**. The claim is given up by itself whenever the
game needs Link: a screen of the game (its menu, the map, a shop), a loading screen, a scene of the story. It is
taken again by itself when that is over. F8 switches by hand.

While the game is in control, the controller set up in Cemu drives it, and a few keys of the keyboard are sent as
the gamepad's buttons (`physical.h`).

### Scenes of the game

In a scene (the opening, a shrine, a tower, a terminal) the game moves Link and its own camera. The bridge then
shows the game's camera, reads Link's skeleton from memory every frame (`link_bones.h`) and draws Minecraft's
player model with its six parts following Link's bones, at Link's height (`live_scene.h`, `player_render.h`). The
Sheikah Slate Link holds is shown; Link himself stays hidden.

### Runes

The game's runes are Minecraft items (`rune_items.h`, `runes.h`, the mod's `BotwRunes`). Holding one selects it in
the game; using it presses the game's own buttons for it, with the game's aim taken from Minecraft's look.

### Saves

The game's save data in memory holds named values ("flags"). Two numbers are used (`save_link.h`, found by the same
search of the memory as the runes' flags): `LastSaveTime_Lower`, the time a save was written, which is new after
every save and the same after a load of that save, and `PlayReport_PlayTime`, the seconds played, which goes up by
one a second and is part of the save. If the seconds played go back, or forward by more than the time that passed,
the game has loaded a save; if only the save's time changes, the game has written one. The bridge tells the mod
(`IN_SAVE`), which keeps what the player carries in a file named by the save's time and puts it back on a load
(`BotwSaves`). A number flag in memory is 32 bytes: a fixed word, the value at +20, the CRC-32 of its name at +24.

### Climbing and stamina

The collision triangles the bridge sends carry what the game's own material word says of them where the bridge
knows it (`native_shapes.h`): a ladder, or not to be climbed (the wall codes NoClimb, Slip and
NoDashUpAndNoClimb, and the stone of the shrines). In the mod (`BotwClimb`, `BotwClimbClient`,
`LivingEntityClimbMixin`) every other triangle too steep to walk on is a wall: when the player jumps into one
that reaches more than a block above the ground the jump began on, Minecraft's ladder rules apply to him. While
he goes up, the mod sets a flag in its state (`MC_CLIMBING`), and the bridge takes from the game's own stamina
number at its input point, by the height he rose (`physical.h`, `climbStamina()`; `PlayerInfo+0x68`, the most
at `+0x6C`) and lets the game not raise it. The
bridge tells the mod the number (`IN_STAMINA`), the mod draws it (`StaminaHud`) and drops the player off the wall
when it is used up. Afterwards the game fills the stamina by its own rules.

### Link walks

The game moves Link itself, so that it hears his steps, feels his weight and lets him push (`walk.h`, `body.h`):

- **The stick.** The game's left stick is the player's real movement of the last tick, seen from the player's look
  (`walk.h`); its size is the player's speed over Minecraft's walking speed. The game then runs its own walk and run,
  with their noise for its creatures (the noise meter of its HUD moves) and their step sounds. Sneaking is the game's
  crouch (a press of the stick when the two differ; the player's crouch flag is actor+0x8DC bit 2). Sprinting holds
  B: the game's dash, which pays its own stamina; the mod lets the player sprint only while that stamina is not used
  up (`LocalPlayerSprintMixin`). The movement, not the keys: a player stopped by a wall pushes no stick, so the game
  is not asked to climb it.
- **The speed.** Where the bridge used to set the body's place before the physics step and stop it, the pack's
  probe now takes a second answer of `Bridge_Sample`: the speed in `DriveVector`, which `Body_setVelocity` gives the
  body. Placed as before: further than 0.5 units from the player's place, swimming, in a scene, while held.
- **The push.** The mod sends what collision took from the player's step on the ground (`McState.pushX/Z`, version
  22; `EntityCollideMixin`). The bridge adds it to the speed, so Link keeps pressing on what stopped the player, and
  the game's physics moves it if it can be moved (a push rock moved 1.7 units, route-401).

`lab/walk.txt` (`0` off, `1` stick and crouch, `2` also the dash) and `lab/walk-drive.txt` (`0` places the body as
before; then the leash and `0` for no push).

### The game's HUD

The parts of the game's HUD that are hidden (`hud_hide.h`) are found in the game's memory by name, without any
file of the game's: a built layout keeps a pointer to its name in its layout file's `lyt1` header, with its root
pane beside it; panes form a tree and carry their names. Once, when Minecraft first has the player, a thread
searches the memory for the three layouts (about two seconds), and from then on the bridge clears the "visible"
bit of their panes at the game's input point, after checking that each pane is still the one found.

### Death

Link is never hurt while Minecraft owns the player: every blow goes to Minecraft's player, and Link's life only
shows that player's health. When the player dies, the mod sends an event (`EV_PLAYER_DIED`, with what killed him: `DEATH_*` in its flags, the
game's attacker in its form id) and the bridge ends Link's life, once (`physical.h`, `hearts()`). For a blow, fire,
a shot or a blast it does so with a hit of the game's own: the next call of the damage manager's `addDamage` (the
game makes idle ones every update) gets the codes of such a hit (`deathHitFor`), so the game picks the matching
death (reeling, burning, knocked down). For any other cause the life is set to nothing, and the game's death
follows Link's state. The game does the rest by itself: a fairy or Mipha's Grace
if there is one, else its death scene (shown like every scene, with the player's model in Link's place), its Game
Over screen and the load of the last save, which the save link above turns into Minecraft's things of that save.

Who hit the player: a blow of a weapon passes the game's defence code, where the bridge reads its attacker. A thrown
thing (a Bokoblin's rock, `Rock_Weapon`; a Bokoblin a Moblin throws) or bare hands do not; for those the bridge looks
at the objects near Link when the hit goes to Minecraft (`actors::hitSource`): a loose weapon whose holder
(`weapon+0x1B8`, then `+8`) is an enemy was thrown by it, else the nearest enemy hit him. The mod's stand-in for that
creature carries the game's English name (`actor_names.py`, `BotwNames`), so Minecraft's own death message names it.

When the game writes Link's life from nothing (a load, a revive) the bridge sends it to the mod (`IN_LIFE`): what is
up to his most life as health, what is over it (the game's yellow hearts) as absorption. The other way, the
player's absorption is written as life over Link's most life, so the game's row of hearts shows it.

## Safety rules inside the bridge

- The game's memory is only read after a check that the address is inside the game's memory, and structures are
  checked (expected table addresses, counts within limits) before they are trusted.
- Physics data is only read while the game itself holds its physics lock (`Bridge_WorldLockEntered` /
  `Bridge_WorldLockLeft`), by jobs a worker thread prepared; the game's thread never waits for the worker.
- Everything Minecraft sends is checked (finite numbers, sizes within limits) before it is used.
- If anything is not as expected, the bridge gives the claim up and the game simply goes on as itself.

## The working folder: `lab`

`implementation/botw-live-bridge/lab` (not in Git) is where the DLL lives and works. It holds the installed DLL,
`layer.json`, the compiled shaders (`fixture/`), Minecraft's session folder (`minecraft/`), the traces of the last
session, and small text files that switch things on and off while the game runs (for example `frame-pace.txt`,
`render-size.txt`, `scene-slate.txt`). The launcher and the test plans write those files; the DLL reads them.

## Where to read on

- [The bridge's source, file by file](bridge-source.md)
- [The Minecraft mod](minecraft-mod.md)
- [Automatic test runs](testing.md)

Comments in the code often cite "report NN" or "route-NNN". Those are the project's working notes and test runs from
before it was public. They are not in this repository; the comment itself says what was found.
