# CLAUDE.md — project guide for AI coding assistants

## What this is
A Nintendo 3DS engine (C, devkitPro: libctru + citro2d/citro3d) that plays Fear & Hunger, an
RPG Maker MV game, from data that PC-side Python scripts convert out of the user's own copy of
the game. **The engine must stay faithful to RPG Maker MV's behaviour.** When in doubt, mirror
what MV's `rpg_objects.js` / `rpg_managers.js` do (Game_Interpreter, Game_Event,
Game_Character, Game_Action, BattleManager, Game_Troop).

## Hard rules
1. **Never commit or embed game content.** That means no maps, dialogue, images, audio, or
   database text from the game, and no hard-coded per-game data in C. Everything game-specific
   is read at runtime from `sdmc:/3ds/fearmap/`. Test data must be invented (e.g. "TestHead").
   `out/`, `output/`, `sdmc/`, `*.t3x` and the converted `.bin`/`.txt` data are git-ignored.
   Keep it that way.
2. **Text rendering uses the built-in 5×7 bitmap font in `ui.c`.** Do not use
   `C2D_FontLoadSystem` or `C2D_TextBuf`: they render blank on emulators without system font
   dumps. Display text is ASCII-transliterated by `build_events.py`.
3. **No printf console.** `consoleInit` conflicts with the bottom-screen framebuffer.
   Diagnostics go through `uiLog()` or the amber status line (`eventsDebugStatus()`).
4. **Real-hardware limits.** An Old 3DS has 64 MB for the app and roughly 30 MB of linear
   (GPU) memory, at 268 MHz. Large structs must be static, never on the stack (`GameState` is
   static). Texture caches must respect the `linearSpaceFree()` guards. Avoid per-frame work
   that is O(events × pages) without need.
5. **Don't break the converter formats silently.** Event files are `FHEC` v1:
   `magic[4] u16 version u16 mapId u32 eventCount`, then per event
   `u16 id,x,y,pageCount`, then per page `u16 index, u16 cmdCount, u32 condLen,
   cond JSON`, then per command `u16 code, i16 indent, u32 len, params JSON`. New page fields
   go into the conditions JSON, because the C parser ignores unknown keys. If you change a
   format, update both sides and say in the PR that the data must be rebuilt.

## Layout
- `source/main.c`: main loop, map renderer (4 layers, autotiles), input, map/world loading,
  screen flow (title, field, battle, game over), name-input keyboard, inventory.
- `source/events.c`: loader, page conditions, interpreters (one main, one per parallel map
  event and parallel common event, plus a battle interpreter), characters and move routes,
  pictures, triggers, message window state. Self-tests: `eventsRunSelfTests`.
- `source/battle.c`: database loading (`battle/*.txt`), formula evaluator, actions, enemy AI,
  troop event checks, flow, field item use and equipping. `battleSelfTest`.
- `source/actors.c`, `runtime.c`, `gamedata.c`, `msgtext.c`, `pictures.c`,
  `charsprites.c`, `battlegfx.c`, `ui.c`. Legacy and unused: `combat.c`, `loot.c`,
  `survival.c`.
- PC scripts: `build_world.py` (imports `build_events.py`), `build_battle.py`,
  `build_pictures.py`, `build_title.py`, `fh_scope.py` (feature-usage report),
  `cia/build_cia.sh`, `cia/serve_cia.py`.

## Conventions
- MV direction numbers (2/4/6/8) inside `events.c`; the port's own 0..3 (down, left, right,
  up) in `main.c` and drawing code.
- Player position: `px`, `py`. `down`/`held` are read once per frame. Circle Pad is merged
  into the D-pad.
- `fieldActive` is computed before events update, so the A press that closes a message
  doesn't re-trigger the event.
- Interpreter waits: `W_FRAMES`, `W_MESSAGE`, `W_BATTLE`, `W_TRANSFER`, `W_ROUTE`,
  `W_NAME`. Shared resources (message window, battle, transfer, name input) have one owner
  at a time, and other interpreters retry next frame.

## How to verify without hardware
The 3DS-independent modules compile with host gcc, so test them there:

```sh
cd source
gcc -Wall -Wextra -g -fsanitize=address,undefined -I. \
    test_host.c events.c battle.c actors.c runtime.c gamedata.c msgtext.c rng.c -lm -o /tmp/t
```

Write small host test programs (keep them in `tests/`) that load converted data from a local
`out/` folder **on the developer's machine** (never committed), simulate frames, and assert on
behaviour. 3DS-only files (`main.c`, `ui.c`, `pictures.c`, `charsprites.c`, `battlegfx.c`)
can be syntax-checked against stub headers (`tests/stub/3ds.h`, `tests/stub/citro2d.h`) with
`gcc -fsyntax-only`. Final check: `make clean && make` in the devkitPro shell, with no
warnings.
