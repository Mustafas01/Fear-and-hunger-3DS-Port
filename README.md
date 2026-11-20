# F&H 3DS Port

An unofficial, fan-made engine that runs **Fear & Hunger** on the Nintendo 3DS, written in C with devkitPro (libctru + citro2d).

It works like an RPG Maker MV runtime: a set of PC scripts converts the data from **your own copy** of the game into 3DS-friendly files, and the engine on the 3DS plays them. Maps, events, cutscenes, pictures, enemies, skills and battles all come from the game's own data.

> **This repository contains no game content.** No maps, text, images or audio from the game are included or distributed. You need a legitimately purchased copy (e.g. on Steam) to generate the data yourself. Please don't share converted game data or builds bundled with it.

---

## Features

**World**
- Every map with all 4 tile layers, autotiles (floors, walls, waterfalls), animated water and upper-layer tiles
- Map transfers, with each map stored in its own folder on the SD card
- Map events drawn with their own sprites, in the correct layer and depth order
- Event movement following RPG Maker MV: random wander, approach the player, custom routes and Set Move Route. Speed, frequency and step animation all work.
- Enemies that chase you and force the encounter when they touch you

**Events**
- A resumable interpreter with one interpreter per parallel event, like MV
- Conditional branches, loops, labels and common events
- Switches, variables and self switches, with self switches tracked per map
- Show Text and Show Choices, including the YEP message codes and speaker name boxes
- The gab window and the name input, which uses the 3DS system keyboard
- Pictures: show, move, tint and erase, plus screen tint
- Party changes, actor names and sprites, items, gold, HP/MP/EXP/level/states
- Game Over and Return to Title

**Battles**
- Front-view battles built from the game's own troops, enemies, skills, items and states
- Body-part targeting: each part is its own troop member, as in the original
- Damage formula evaluator, hit/evade, variance, guard and state turn counts
- Enemy AI that uses each enemy's own action patterns and ratings
- Troop battle events with turn, HP%, switch and turn-end conditions
- Victory EXP, gold and drops, plus escape and defeat handling

**Menus**
- Title screen with the game's own art
- Inventory: use items with their real effects, or equip weapons and armor
- Field HUD showing the leader's real HP/MP, using the game's own term names

**3DS**
- Built-in 5×7 bitmap font, which works on emulators without system font dumps
- Memory guards that free textures on real hardware; New 3DS speed-up
- Runs as `.3dsx` (Homebrew Launcher) or `.cia` (installable through FBI)

## Status

This is a work in progress. The early game is playable, and many systems are still missing. See [Known issues](#known-issues).

---

## Requirements

| What | Why |
|---|---|
| Fear & Hunger (your own copy) | the source of all game data |
| [devkitPro](https://devkitpro.org) with devkitARM, libctru and citro2d | building the 3DS binary |
| Python 3 + Pillow (`py -m pip install pillow`) | the conversion scripts |
| `tex3ds` (comes with devkitPro) | converting images to 3DS textures |
| *(CIA only)* `makerom` and `bannertool` placed in `cia/` | packaging the `.cia` |
| *(QR install only)* `py -m pip install qrcode[pil]` | `serve_cia.py` |

A 3DS with custom firmware (Luma3DS) and the Homebrew Launcher or FBI. Azahar or Citra work for testing on PC.

---

## 1. Convert your game data

Run these from the repository folder, with `GAME` set to your install. For example:
`C:\Program Files (x86)\Steam\steamapps\common\Fear & Hunger`

```bat
py build_world.py    "GAME" out    :: maps, tilesets, events, sprites, actors, start position
py build_battle.py   "GAME" out    :: enemies, troops, skills, states, battle images & scripts
py build_pictures.py "GAME" out    :: event pictures (cutscenes, menus)
py build_title.py    "GAME" out    :: title screen art
```

Order matters: run `build_world.py` before `build_battle.py`, which adds battle backgrounds to each map folder. `build_events.py` has to sit next to these scripts because they import it.

Copy everything in `out\` to the SD card at **`/3ds/fearmap/`**:

```
sdmc:/3ds/fearmap/
├── maps/MapNNN/        map.bin, flags.bin, events_compiled.bin, sheets.txt, battleback.txt
├── tilesets/  characters/  pictures/  battle/
├── common_compiled.bin  actors.bin  actorchars.txt  start.txt
├── actors.txt items.txt weapons.txt armors.txt party.txt
└── player.t3x player.bin title.t3x title.txt
```

## 2. Build

In the devkitPro MSYS shell:

```sh
make clean && make            # -> fear_map.3dsx
sh cia/build_cia.sh           # -> fear_map.cia (optional)
```

## 3. Install

- **Homebrew Launcher:** copy `fear_map.3dsx` to `sdmc:/3ds/`.
- **FBI over Wi-Fi:** run `py cia\serve_cia.py fear_map.cia`, then in FBI go to *Remote Install → Scan QR Code*. The PC and 3DS must be on the same network.
- **Getting the data onto the SD card without removing it:** use [ftpd](https://github.com/mtheall/ftpd) with an FTP client such as FileZilla (port 5000). On a New 3DS you can also use *microSD Management*, which needs the Windows SMB1 client turned on temporarily.
- **Emulator:** put the data in Azahar's `sdmc/3ds/fearmap/` folder and open the `.3dsx`.

---

## Controls

| Field | |
|---|---|
| D-pad / Circle Pad | move |
| hold **B** | run |
| **A** | check / talk / open |
| **SELECT** | inventory (A: use / equip) |
| **L + B** | force-stop a stuck event (debug) |
| **L + R** | test battle (debug) |
| **START** | quit |

| Battle | |
|---|---|
| D-pad | choose command / target |
| **A** / **B** | confirm / back |

---

## How it works

```
your copy of the game ──(PC scripts)──► converted data on SD ──► 3DS engine
     www/data/*.json                    compact .bin / .txt         C, citro2d
     www/img/** (decrypted)             .t3x textures
```

| Module | Role |
|---|---|
| `main.c` | main loop, map rendering, input, map loading, screen flow |
| `events.c` | event file loader, page conditions, interpreters, characters and movement, pictures |
| `battle.c` / `battlegfx.c` | battle rules and flow / top-screen battle drawing |
| `actors.c` | levels, EXP curves, parameters, states, names, sprites |
| `runtime.c` | switches, variables, self switches, party, inventory |
| `msgtext.c` | message escape codes, word wrap, reflow |
| `pictures.c`, `charsprites.c` | texture caches for pictures and character sheets |
| `ui.c` | everything on the bottom screen, with its own bitmap font |
| `gamedata.c` | name tables and actor sprite table |

The engine follows RPG Maker MV's own rules: page selection, interpreter flow, move routes, battle formulas and troop event spans. Converted data comes out the same as the original, so changes should stay faithful to MV behaviour.

## Known issues

- Some enemies don't die when their head is destroyed, so certain mechanics in troop events don't work yet (under investigation)
- Event script calls (`Script` command) and most plugin commands aren't supported. Lighting, YEP extras and similar plugin features are missing.
- Player move routes in cutscenes are skipped
- No saving yet
- No audio yet
- No equipment screen; equipping always goes to the party leader
- Startup can be slow on an Old 3DS, because the common events are parsed at boot

## Credits

- The original game: **Fear & Hunger** by Miro Haverinen. Support the original!
- devkitPro, libctru, citro2d / citro3d and tex3ds
- makerom (Project_CTR), bannertool, FBI and ftpd by their respective authors

## License

The engine code in this repository is yours to license (add a `LICENSE` file, e.g. MIT). That license does **not** cover Fear & Hunger or any of its content, which belongs to its creator.
