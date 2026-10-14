# The bridge's source

Everything here is in `implementation/botw-live-bridge`. Read [how it works](how-it-works.md) first.

The DLL is one translation unit: `probe.cpp` includes the headers below, in an order that matters (a header may
use what an earlier one defined). `build.ps1` compiles it with `g++ -std=c++23`.

## The DLL

### Entry, the game's hooks, the frame

| File | What it does |
|---|---|
| `probe.cpp` | `DllMain`, the registration of every `Bridge_*` function with Cemu, the Vulkan layer's hooks, the camera hooks, the worker threads' start. |
| `pack/rules.txt`, `pack/patch_bridge.asm` | The Cemu graphic pack: the PowerPC patches that call the `Bridge_*` functions, for the game's US v208 program only (`moduleMatches`). |
| `layer.json` | The Vulkan layer's manifest. `VK_LAYER_PATH` points at `lab` for a session. |
| `vendor/vkroots.h` | The Vulkan layer framework (Apache-2.0 OR MIT), unchanged. |
| `trace.h` | The traces the bridge writes into `lab` (the current file and two older parts). |

### Minecraft's side of the shared memory

| File | What it does |
|---|---|
| `live.h` | Opens the shared memory, checks its header, reads the guarded values and the rings. Adapted from SkyCraft's `Link.cpp`. |
| `live_scene.h` | The render ring's reader: keeps Minecraft's geometry (block sections, atlas, textures, entities, the player's model) as snapshots a worker owns. Also the player model's pose in a scene of the game's. |
| `live_gpu.h` | The Vulkan buffers and images made from those snapshots, in generations that live until no frame uses them. |
| `live_target.h` | Places and bins for what is drawn; settings of the drawing read from `lab`. |

### The player

| File | What it does |
|---|---|
| `physical.h` | Keyboard and mouse of Cemu's window, captured by Windows events and sent to Minecraft; the claim (who is in control), F8 and F7, the keys sent to the game as gamepad buttons. |
| `body.h` | Minecraft's newest player state, and Link's body brought there in the game's physics step (a speed before it, or the place). |
| `walk.h` | The game's left stick, crouch and dash from the player's movement, so that the game walks Link (noise, steps). |
| `game_camera.h` | The game's own camera for the frame, kept for scenes and for the game's screens. |
| `player_render.h` | Hiding Link (and what he holds) while Minecraft's player is shown; the Sheikah Slate's exception. |
| `link_bones.h` | Link's skeleton read from the game's memory, for the player model in scenes. |
| `swing.h` | Minecraft's blow as the swing of a weapon of the game's (the weapon's attack sensor). |
| `runes.h`, `rune_items.h` | The runes: which ones the game's player owns, and working them from Minecraft's items. |
| `pouch.h` | Link's inventory, read from the game's own list each half second (`lab/pouch.json`): the first step to show it in Minecraft. |
| `memfind.h` | A lab tool: searches the game's memory for a text (`lab/memfind.txt`), to find where the game keeps something. |
| `save_link.h` | When the game has written or loaded a save, and which: told to the mod, which keeps the player's things with each save. |
| `hud_hide.h` | Hides parts of the game's HUD while Minecraft has the player: finds the game's layout objects in memory by name and clears their panes' "visible" bits. |

### The game's world for Minecraft

| File | What it does |
|---|---|
| `native_shapes.h` | Finds the game's collision bodies near the player and reads their shapes, inside the game's physics lock. The largest file: boxes, hulls, compound shapes, terrain height fields. |
| `native_mesh.h` | Decodes the game's static collision meshes (compressed Havok meshes) into triangles. Pure code: bytes in, triangles out. |
| `native_geometry.h` | Triangles to Minecraft's blocks: which blocks a triangle touches, and the coarsening of steep surfaces. Adapted from SkyCraft's `Collision.cpp`. |
| `native_regions.h` | The rolling regions of 8×8×8 blocks around the player: what was sent, what must be sent again. Holds the rule that ties the two spaces together. |
| `native_movers.h` | Bodies that move (platforms, doors, things held by a rune). |
| `native_stream.h` | The worker that asks for shapes, receives them and writes the collision ring. |
| `water.h` | The game's water heights around the player, for Minecraft's `WaterGrid`. |
| `actors.h` | The game's actors near the player (place, size, life), for Minecraft's `ActorTable`; hits both ways. |

### Minecraft's world for the game

| File | What it does |
|---|---|
| `integrate.h` | Draws Minecraft's geometry inside the game's own passes: G-buffer, shadow maps, the lit picture. |
| `render.h` | The plain draw path and the overlay (Minecraft's HUD over the final picture). |
| `mesh.vert`, `mesh.frag`, `gbuffer.frag`, `shadow.frag`, `hdr.frag`, `overlay.vert`, `overlay.frag` | The shaders. |
| `walls.h` | Minecraft's solid blocks as ground and walls in the game's physics. |
| `spawn.h` | Asks the game for one of its own objects at a place (used by tests). |

### Tools inside the DLL for finding things out

| File | What it does |
|---|---|
| `frame_survey.h` | Writes one frame of the game's drawing down: every pass, its targets, its draws. Heavy; off unless asked. |
| `calibration.h`, `lab_vector.h` | Small bounded measurements used while a feature was worked out (`lab_vector.h`: search the game's memory for a vector or for a text and what points at it, dump what stands around a place). |
| `finite_patch.h` | A small fixed piece of collision from a file, the first collision source of the project; kept for checks. |
| `native_replay.h`, `native_check.cpp` | The same geometry and region code run without any game, on made-up data: the offline check `build.ps1` runs. |

## The launcher and the tools (Python)

| File | What it does |
|---|---|
| `local.py` | Reads `local.json`: where Cemu, the game and the tools are on this machine. |
| `session.py` | Starts and stops Cemu for a bridge session: saves and restores Cemu's settings, switches the packs, loads the DLL. |
| `identity.py`, `launch.py`, `modules.py` | Starting a process, knowing it again later by its exact identity, stopping only that one, listing what it loaded. |
| `client.py` | Starts the Minecraft development client from the mod's build, hidden, with its folders in `lab`. |
| `setup_mod.py`, `mod_build.py`, `retained.py` | The mod's build ([building](building.md)). |
| `native_route.py` | A whole session from start to stop, as a **plan**: `play` for a person, and many plans for automatic tests ([testing](testing.md)). |
| `rig.py`, `os_input.py` | Window placing, pictures, video, and real keyboard and mouse input for the test plans. |
| `live_check.py` | Recognising the game's title and save screens in a picture; a run's record. |
| `planner.py`, `terrain_probe.py` | Path finding on exported collision, to steer the player in tests. |
| `perf_sample.py`, `frame_survey.py` | Reading load and frame surveys. |
| `fixture.py` | Makes the small fixed files the DLL loads at start (a cube, a plain atlas). |
| `finite_patch.py`, `finite_patch_check.py` | The file format of `finite_patch.h` and its offline check. |
| `recycle.py` | Sends files to the Windows recycle bin; the tools never delete directly. |
| `live_proof.py`, `combined.py`, `common_probe.py`, `walking_play.py` | Older test steps that `native_route.py` still uses parts of. |
| `actor_names.py` | At Minecraft's launch: the game's English names of its creatures, read from your copy of the game into `lab/actor-names.json` (for Minecraft's death messages). Nothing of the game's goes into the project. |
| `rpx_dis.py`, `rpx_q.py`, `actor_pack.py` | Read-only research tools for the game's program and actor files **on your own disk** (`local.json` names them). Nothing is copied out. |
| `build.ps1`, `prepare_lab.ps1`, `restore_fixture.ps1`, `play.cmd` | Build and install; the test saves' folder; the play session. |

## Conventions in this code

- **Addresses are for US v208 only.** Every address of the game's is written where it is used, with a comment on
  how it was found. The pack applies to that one program only.
- **Check before trusting.** Reads of the game's memory go through helpers that check the address; structures are
  checked against what they must look like.
- **Threads.** The game's threads only do short, bounded work inside a hook. Decoding, voxelizing and writing
  to Minecraft happen on worker threads with their own copies of the bytes; no pointer into the game's memory is
  kept between frames.
- **Switches are files.** A feature under work reads a small text file in `lab`, so it can be changed while the
  game runs and a test can set it.
- **Names like "Task E", "v11", "lab"** in comments are from the project's history: the code grew through lettered
  assignments, and the protocol had version 11 when the bridge began.
