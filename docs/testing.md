# Automatic test runs

For people changing the code. To play, none of this is needed ([playing](playing.md)).

A change of the bridge is checked by a **run**: `native_route.py` starts Cemu and Minecraft, loads a known save,
does a **plan** (a list of steps with real keyboard and mouse input), takes pictures, stops everything it started
and leaves a folder with what happened. Then a person looks at the pictures. Numbers alone have missed things that
were plain on the screen.

All commands are run in `implementation/botw-live-bridge`.

## Checks without the game

```powershell
./build.ps1                # compiles, then runs native_check.cpp and finite_patch_check.py
python -B mod_build.py     # compiles the mod, then runs the collider check
```

`native_check.cpp` runs the bridge's geometry, region and lock bookkeeping on made-up data. It needs no game and no
Cemu.

## Setting up runs with the game (once)

A run must never write into your own saves. It therefore starts Cemu with a save folder of its own (Cemu's `--mlc`
option, `session.py start --private-saves`).

**1. The test saves.** Make a save of the game you want runs to start from, and copy Cemu's
`mlc01/usr/save` (the folder holding `00050000/101c9400/...`) somewhere. Then:

```powershell
./prepare_lab.ps1 -CheckpointSource <that copy>
```

This makes `lab/mlc01`: links to the installed game inside Cemu's own `mlc01` (nothing of the game is copied) and
a copy of your save, plus `lab/pristine-save`, from which the save is put back before every run.

More starting points go in `lab/saves/<name>` (same form) and are chosen with `BRIDGE_CHECKPOINT=<name>`.

**2. The reference pictures.** A run finds its way from the title screen into the save by recognising three
screens. Put your own captures of them in `lab/references/`: `title.png` (the title screen with Continue),
`list.png` (the list of saves), `confirmation.png` (the question before loading). They are pictures of the game and
therefore are not in this repository; take them once with the window size the runs use.

**3. Optional.** `ffmpeg` in `local.json` for video of a run; `"evidence"` in `local.json` for where runs are kept
(default: `runs/` in the project, which Git ignores).

## A run

```powershell
$env:BRIDGE_KEYS = 'wait8 shot where cmd:/tp_@s_-75.81_89.45_70.66 wait3 shot'
python -B native_route.py keys --evidence gameplay
```

`keys` is the plan most used: it does the words of `BRIDGE_KEYS` one after the other. Some words:

| Word | What |
|---|---|
| `waitN` | wait N seconds |
| `shot` | a picture of the game's window |
| `where` | the player's place, written to the record |
| `cmd:TEXT` | a Minecraft command (`_` for a space) |
| `hold:KEY:SECONDS` | hold a key (W A S D SPACE SHIFT CTRL ...) |
| `look:DX:DY`, `aim:YAW:PITCH` | the mouse |
| `mouse:left:SECONDS`, `mouse:right:SECONDS`, `wheel:N`, `1`..`9` | attack, use, the hotbar |
| `F7`, `G`, `BACK`, `UP` `DOWN` `LEFT` `RIGHT`, `Q` `E` `Z` `C` `X` `Y` `TAB` `M` | keys for the game's screens (`G*3` = three taps) |
| `set:NAME=VALUE` | writes `lab/NAME.txt` for the run (a switch of the bridge) |
| `seek:NAME:UP`, `find:NAME`, `spawn:NAME:AHEAD:UP` | the game's objects: look at one, list them, ask for one |
| `health`, `survival`, `creative`, `hurtN` | the player's state in Minecraft |
| `rec`, `recstop` | start and stop a video of the window |

The full list is in the comment of `keys_plan` in `native_route.py`. `BRIDGE_START=title` stops at the title screen
(the words go on from there); `BRIDGE_START=newgame` starts a new game.

In a command, `_` is a space and `__` is one `_` (`cmd:/give_@s_minecraft:gold__ingot_4`).

The game's menu, for tests of saving and loading: `F7 wait2 E wait2 G wait2 UP wait1 G` writes a save ("Save
progress?" starts on Cancel), and `F7 wait2 E wait2 DOWN wait1 G wait3 G wait3 UP wait1 G wait25` loads the newest
one. The mod's log then has lines beginning `BotwCraft: save`.

Other plans (`python -B native_route.py -h`) are fixed tests of one thing each: `perf`, `swim`, `hits`, `water`,
`blocks`, `fall`, and so on.

## What a run leaves

A folder `route-N` with, among others:

| File | What |
|---|---|
| `NN-shot.png` | the pictures. **Look at them.** |
| `keys.json`, `actions.json` | what the plan did and what it read |
| `control.jsonl` | the bridge's events (claims, releases, the game's screens, gamepad buttons) |
| `frames.jsonl.gz`, `poses.csv.gz` | per-frame traces |
| `native.txt`, `log.txt`, `latest.log`, `console.log` | the logs of the DLL, of Cemu and of Minecraft |
| `route-summary.json` | numbers of the run: collision stream, time per frame, what was sent |
| `process.json`, `cleanup.json`, `settings-restored.json` | what was started, that it was stopped, that Cemu's settings are back |

## Rules of a run

- **One session at a time.** A run refuses to start while a Cemu is running, and it only ever stops the processes
  it started itself (it knows them by their exact identity, not by their name).
- **Your Cemu is left as it was.** Settings are saved before and put back after, also when a run fails.
- **Nothing is deleted.** Test tools send what they replace to the Windows recycle bin (`recycle.py`).
- **Where the window goes.** `lab/background.txt` `1` puts a run's window on a second screen without sound, so the
  machine can be used meanwhile.

## Finding things in the game's program

`rpx_dis.py`, `rpx_q.py` and `actor_pack.py` read the game's program and actor files on your own disk
(`game_update_rpx` and `game_content` in `local.json`; `rpx_dis.py` needs the Python package `capstone`). They only print; what they print about the game's code
must not be committed, apart from the addresses and facts a patch needs.
