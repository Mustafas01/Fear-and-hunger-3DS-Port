#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rng.h"
#include "events.h"
#include "runtime.h"
#include "survival.h"
#include "loot.h"
#include "combat.h"
#include "ui.h"
#include "gamedata.h"
#include "pictures.h"
#include "msgtext.h"
#include "actors.h"
#include "battle.h"
#include "battlegfx.h"
#include "charsprites.h"

#define TS 24
#define Q  (TS / 2)
#define LAYERS 4
#define DIR "sdmc:/3ds/fearmap/"
#define NSHEETS 10
#define SLOT_PLAYER 9

typedef struct {
    u16 startX, startY, idx, single, obj, fw, fh;
} PlayerInfo;

static u16 mapW, mapH;
static u16 *mapData;
static u16 flags[8192];
static PlayerInfo pl;
static int anim = 0;

static C2D_SpriteSheet sheets[NSHEETS];

// Where the current map's files live: DIR (single-map setup) or
// DIR "maps/MapNNN/" (made by build_world.py).
static char mapBase[96] = DIR;
static int curMapId = 0;

// Game state is static because it is too large for the normal stack.
static GameState gameState;


static const signed char FLOOR[48][4][2] = {
 {{2,4},{1,4},{2,3},{1,3}},{{2,0},{1,4},{2,3},{1,3}},{{2,4},{3,0},{2,3},{1,3}},{{2,0},{3,0},{2,3},{1,3}},
 {{2,4},{1,4},{2,3},{3,1}},{{2,0},{1,4},{2,3},{3,1}},{{2,4},{3,0},{2,3},{3,1}},{{2,0},{3,0},{2,3},{3,1}},
 {{2,4},{1,4},{2,1},{1,3}},{{2,0},{1,4},{2,1},{1,3}},{{2,4},{3,0},{2,1},{1,3}},{{2,0},{3,0},{2,1},{1,3}},
 {{2,4},{1,4},{2,1},{3,1}},{{2,0},{1,4},{2,1},{3,1}},{{2,4},{3,0},{2,1},{3,1}},{{2,0},{3,0},{2,1},{3,1}},
 {{0,4},{1,4},{0,3},{1,3}},{{0,4},{3,0},{0,3},{1,3}},{{0,4},{1,4},{0,3},{3,1}},{{0,4},{3,0},{0,3},{3,1}},
 {{2,2},{1,2},{2,3},{1,3}},{{2,2},{1,2},{2,3},{3,1}},{{2,2},{1,2},{2,1},{1,3}},{{2,2},{1,2},{2,1},{3,1}},
 {{2,4},{3,4},{2,3},{3,3}},{{2,4},{3,4},{2,1},{3,3}},{{2,0},{3,4},{2,3},{3,3}},{{2,0},{3,4},{2,1},{3,3}},
 {{2,4},{1,4},{2,5},{1,5}},{{2,0},{1,4},{2,5},{1,5}},{{2,4},{3,0},{2,5},{1,5}},{{2,0},{3,0},{2,5},{1,5}},
 {{0,4},{3,4},{0,3},{3,3}},{{2,2},{1,2},{2,5},{1,5}},{{0,2},{1,2},{0,3},{1,3}},{{0,2},{1,2},{0,3},{3,1}},
 {{2,2},{3,2},{2,3},{3,3}},{{2,2},{3,2},{2,1},{3,3}},{{2,4},{3,4},{2,5},{3,5}},{{2,0},{3,4},{2,5},{3,5}},
 {{0,4},{1,4},{0,5},{1,5}},{{0,4},{3,0},{0,5},{1,5}},{{0,2},{3,2},{0,3},{3,3}},{{0,2},{1,2},{0,5},{1,5}},
 {{0,2},{3,2},{0,5},{3,5}},{{0,0},{1,0},{0,1},{1,1}}
};

static const signed char WALL[16][4][2] = {
 {{2,2},{1,2},{2,1},{1,1}},{{0,2},{1,2},{0,1},{1,1}},{{2,0},{1,0},{2,1},{1,1}},{{0,0},{1,0},{0,1},{1,1}},
 {{2,2},{3,2},{2,1},{3,1}},{{0,2},{3,2},{0,1},{3,1}},{{2,0},{3,0},{2,1},{3,1}},{{0,0},{3,0},{0,1},{3,1}},
 {{2,2},{1,2},{2,3},{1,3}},{{0,2},{1,2},{0,3},{1,3}},{{2,0},{1,0},{2,3},{1,3}},{{0,0},{1,0},{0,3},{1,3}},
 {{2,2},{3,2},{2,3},{3,3}},{{0,2},{3,2},{0,3},{3,3}},{{2,0},{3,0},{2,3},{3,3}},{{0,0},{3,0},{0,3},{3,3}}
};

static const signed char WATERFALL[4][4][2] = {
 {{2,0},{1,0},{2,1},{1,1}},{{0,0},{1,0},{0,1},{1,1}},
 {{2,0},{3,0},{2,1},{3,1}},{{0,0},{3,0},{0,1},{3,1}}
};

static const int WS[4] = { 0, 1, 2, 1 };

// directions: 0=down 1=left 2=right 3=up
static const int DX[4] = { 0, -1, 1, 0 };
static const int DY[4] = { 1, 0, 0, -1 };
static const int BIT[4] = { 0x01, 0x02, 0x04, 0x08 };
static const int REV[4] = { 3, 2, 1, 0 };

static bool loadMap(void) {
    char path[128];
    snprintf(path, sizeof(path), "%smap.bin", mapBase);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    free(mapData);
    mapData = NULL;

    if (fread(&mapW, 2, 1, f) != 1 ||
        fread(&mapH, 2, 1, f) != 1) {
        fclose(f);
        return false;
    }

    size_t n = (size_t)mapW * mapH * LAYERS;
    mapData = malloc(n * 2);

    if (!mapData || fread(mapData, 2, n, f) != n) {
        free(mapData);
        mapData = NULL;
        fclose(f);
        return false;
    }

    fclose(f);
    return true;
}

static bool loadFlags(void) {
    char path[128];
    snprintf(path, sizeof(path), "%sflags.bin", mapBase);
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    size_t n = fread(flags, 2, 8192, f);
    fclose(f);

    return n == 8192;
}

static bool loadPlayer(void) {
    FILE *f = fopen(DIR "player.bin", "rb");
    if (!f) return false;

    size_t n = fread(&pl, 2, 7, f);
    fclose(f);

    return n == 7;
}

// Loads tileset sheets 0..8 for the current map. World mode reads the sheet
// names from sheets.txt and the textures from DIR "tilesets/"; the old
// single-map setup uses the fixed files next to map.bin. Returns count.
static int loadTileSheets(bool world) {
    static const char *legacy[NSHEETS - 1] = {
        "a5.t3x", "b.t3x", "c.t3x", "d.t3x", "e.t3x",
        "a2.t3x", "a4.t3x", "a1.t3x", "a3.t3x"
    };
    char names[NSHEETS - 1][64];
    for (int i = 0; i < NSHEETS - 1; i++) names[i][0] = '\0';

    if (world) {
        char path[128];
        snprintf(path, sizeof(path), "%ssheets.txt", mapBase);
        FILE *f = fopen(path, "r");
        for (int i = 0; f && i < NSHEETS - 1; i++)
            if (fscanf(f, "%63s", names[i]) != 1) break;
        if (f) fclose(f);
    }

    int loaded = 0;
    for (int i = 0; i < NSHEETS - 1; i++) {
        if (sheets[i]) {
            C2D_SpriteSheetFree(sheets[i]);
            sheets[i] = NULL;
        }
        char path[200];
        if (world) {
            if (!names[i][0] || !strcmp(names[i], "-")) continue;
            snprintf(path, sizeof(path), DIR "tilesets/%.60s.t3x", names[i]);
        } else {
            snprintf(path, sizeof(path), DIR "%s", legacy[i]);
        }
        sheets[i] = C2D_SpriteSheetLoad(path);
        if (sheets[i]) loaded++;
    }
    return loaded;
}

// World mode: switch to maps/MapNNN. Returns false if that map isn't on SD.
static bool loadWorldMap(int id) {
    char base[96];
    snprintf(base, sizeof(base), DIR "maps/Map%03d/", id);
    char path[128];
    snprintf(path, sizeof(path), "%smap.bin", base);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);

    snprintf(mapBase, sizeof(mapBase), "%s", base);
    if (!loadMap() || !loadFlags()) return false;
    loadTileSheets(true);
    snprintf(path, sizeof(path), "%sevents_compiled.bin", base);
    eventsLoadMap(path);
    curMapId = id;

    // Battle backgrounds for fights on this map (build_battle.py).
    char b1[64] = "", b2[64] = "";
    snprintf(path, sizeof(path), "%sbattleback.txt", base);
    FILE *bf = fopen(path, "r");
    if (bf) {
        if (fscanf(bf, "%63s %63s", b1, b2) < 1) b1[0] = '\0';
        fclose(bf);
    }
    battleSetBacks(b1, b2);
    return true;
}

// same rule as RPG Maker: check tiles top layer first
static bool checkPass(int x, int y, int bit) {
    if (x < 0 || y < 0 || x >= mapW || y >= mapH)
        return false;

    for (int z = LAYERS - 1; z >= 0; z--) {
        u16 f = flags[mapData[(z * mapH + y) * mapW + x]];

        if (f & 0x10) continue;
        if ((f & bit) == 0) return true;
        if ((f & bit) == bit) return false;
    }

    return false;
}

static bool canMove(int x, int y, int d) {
    int nx = x + DX[d];
    int ny = y + DY[d];

    if (nx < 0 || ny < 0 || nx >= mapW || ny >= mapH)
        return false;

    return checkPass(x, y, BIT[d]) &&
           checkPass(nx, ny, BIT[REV[d]]);
}

static void drawSub(
    int slot,
    int sx,
    int sy,
    int w,
    int h,
    float dx,
    float dy
) {
    C2D_Image img = C2D_SpriteSheetGetImage(sheets[slot], 0);

    float tw = img.tex->width;
    float th = img.tex->height;

    Tex3DS_SubTexture st = {
        w, h,
        sx / tw,
        1.0f - sy / th,
        (sx + w) / tw,
        1.0f - (sy + h) / th
    };

    C2D_Image im = { img.tex, &st };

    C2D_DrawImageAt(im, (int)dx, (int)dy, 0, NULL, 1, 1);
}

static void drawAutotile(u16 id, float x, float y) {
    int a = id - 2048;
    int kind = a / 48;
    int shape = a % 48;

    int tx = kind % 8;
    int ty = kind / 8;

    int slot, bx = 0, by = 0, nshapes = 48;

    const signed char (*table)[4][2] = FLOOR;

    if (ty < 2) {
        slot = 7;

        int ws = WS[anim % 4];

        if (kind == 0) {
            bx = ws * 2;
            by = 0;
        } else if (kind == 1) {
            bx = ws * 2;
            by = 3;
        } else if (kind == 2) {
            bx = 6;
            by = 0;
        } else if (kind == 3) {
            bx = 6;
            by = 3;
        } else {
            bx = (tx / 4) * 8;
            by = ty * 6 + (tx / 2) % 2 * 3;

            if (kind % 2 == 0) {
                bx += ws * 2;
            } else {
                bx += 6;
                table = WATERFALL;
                nshapes = 4;
                by += anim % 3;
            }
        }
    } else if (ty < 6) {
        slot = 5;
        bx = tx * 2;
        by = (ty - 2) * 3;
    } else if (ty < 10) {
        slot = 8;
        bx = tx * 2;
        by = (ty - 6) * 2;
        table = WALL;
        nshapes = 16;
    } else {
        slot = 6;
        bx = tx * 2;
        by = (int)((ty - 10) * 2.5f +
                   ((ty % 2 == 1) ? 0.5f : 0.0f));

        if (ty % 2 == 1) {
            table = WALL;
            nshapes = 16;
        }
    }

    // DIAGNOSTIC: missing sheet = dark magenta square.
    if (!sheets[slot]) {
        C2D_DrawRectSolid((int)x, (int)y, 0, TS, TS,
            C2D_Color32(90, 0, 90, 255));
        return;
    }

    if (shape >= nshapes)
        return;

    for (int i = 0; i < 4; i++) {
        int qx = table[shape][i][0];
        int qy = table[shape][i][1];

        drawSub(
            slot,
            (bx * 2 + qx) * Q,
            (by * 2 + qy) * Q,
            Q,
            Q,
            x + (i % 2) * Q,
            y + (i / 2) * Q
        );
    }
}

// pass 0 = normal tiles, pass 1 = tiles drawn above the player
static void drawTile(u16 id, float x, float y, int pass) {
    if (id == 0) return;

    if (((flags[id] & 0x10) != 0) != pass)
        return;

    if (id >= 2048) {
        drawAutotile(id, x, y);
        return;
    }

    int slot;

    if (id >= 1536)
        slot = 0;
    else if (id < 1024)
        slot = 1 + id / 256;
    else
        return;

    // DIAGNOSTIC: missing sheet = bright magenta square.
    if (!sheets[slot]) {
        C2D_DrawRectSolid((int)x, (int)y, 0, TS, TS,
            C2D_Color32(160, 0, 160, 255));
        return;
    }

    int sx = ((id / 128) % 2 * 8 + id % 8) * TS;
    int sy = (((id % 256) / 8) % 16) * TS;

    drawSub(slot, sx, sy, TS, TS, x, y);
}

static void drawLayers(int camX, int camY, int pass) {
    int tx0 = camX / TS;
    int ty0 = camY / TS;

    for (int z = 0; z < LAYERS; z++) {
        for (int ty = ty0; ty <= ty0 + 10 && ty < mapH; ty++) {
            for (int tx = tx0; tx <= tx0 + 16 && tx < mapW; tx++) {
                drawTile(
                    mapData[(z * mapH + ty) * mapW + tx],
                    tx * TS - camX,
                    ty * TS - camY,
                    pass
                );
            }
        }
    }
}

static void drawPlayer(float sx, float sy, int dir, int pattern) {
    if (!sheets[SLOT_PLAYER]) {
        C2D_DrawRectSolid(
            sx + 4,
            sy + 4,
            0,
            TS - 8,
            TS - 8,
            C2D_Color32(220, 40, 40, 255)
        );
        return;
    }

    int bx = 0;
    int by = 0;

    if (!pl.single) {
        bx = (pl.idx % 4) * 3 * pl.fw;
        by = (pl.idx / 4) * 4 * pl.fh;
    }

    drawSub(
        SLOT_PLAYER,
        bx + pattern * pl.fw,
        by + dir * pl.fh,
        pl.fw,
        pl.fh,
        sx + (TS - pl.fw) / 2.0f,
        sy + TS - pl.fh - (pl.obj ? 0 : 3)
    );
}

// Gab window lines (YEP_GabWindow) go to the bottom-screen log.
static void onGab(const char *text) {
    // Don't flood the log with the same line over and over.
    static char last[256];
    if (!strcmp(last, text)) return;
    snprintf(last, sizeof(last), "%s", text);

    char lines[4][MSG_COLS + 1];
    int n = msgWrap(text, lines, 4);
    for (int i = 0; i < n; i++)
        uiLog("%s", lines[i]);
}


// Tile passability for moving events (same rule the player uses).
static bool eventPass(int x, int y, int dir03) {
    return canMove(x, y, dir03);
}

// The party leader, drawn with their own (current) sprite; falls back to
// the build-time player.t3x if that sheet isn't available.
static GameState *s_gsPtr;
static void drawHero(float sx, float sy, int dir, int pattern) {
    int leader = runtimePartyLeader(s_gsPtr), index = 0;
    const char *sheet = leader ? actorSprite(s_gsPtr, leader, &index) : "";
    if (sheet && sheet[0]) charspritesDraw(sheet, index, dir, pattern, sx, sy, TS);
    else drawPlayer(sx, sy, dir, pattern);
}

// Draws map events of one priority (0 below, 1 same level, 2 above).
// For priority 1 the player is drawn in y order among them.
static void drawEvents(const EventSprite *sp, int n, int priority, int camX, int camY,
                       bool withPlayer, int ppx, int ppy, int pdir, int ppat) {
    bool playerDone = !withPlayer;
    for (int i = 0; i < n; i++) {
        const EventSprite *e = &sp[i];
        if (e->priority != priority) continue;
        float sx = e->x * TS - camX, sy = e->y * TS - camY;
        if (sx < -2 * TS || sy < -3 * TS || sx > 400 + TS || sy > 240 + 2 * TS) continue;
        // Player goes before the first event standing lower on screen.
        if (!playerDone && sy > ppy - camY) {
            drawHero(ppx - camX, ppy - camY, pdir, ppat);
            playerDone = true;
        }
        if (e->tileId > 0) drawTile((u16)e->tileId, sx, sy, (flags[e->tileId] & 0x10) != 0);
        else charspritesDraw(e->img, e->index, e->dir, e->pattern, sx, sy, TS);
    }
    if (!playerDone) drawHero(ppx - camX, ppy - camY, pdir, ppat);
}

int main(void) {
    gfxInitDefault();
    osSetSpeedupEnable(true);   // New 3DS: 804 MHz + extra cache (no-op on old 3DS)

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(4096);
    C2D_Prepare();


    C3D_RenderTarget *top =
        C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);

    if (!uiInit())
        printf("ui init FAILED (font? target?)\n");

    // Initialize game state.
    runtimeInit(&gameState);
    rngInit();
    survivalInit();
    if (survivalSelfTest())
        printf("survival self-test: PASSED\n");
    else
        printf("survival self-test: FAILED\n");
    lootInit();
    if (lootSelfTest())
        printf("loot self-test: PASSED\n");
    else
        printf("loot self-test: FAILED\n");

    // Load map data. build_world.py's start.txt means "world mode": every
    // map in its own folder. Without it, the old single-map files are used.
    bool okPlayer = loadPlayer();
    int startMap = 0, startX = 0, startY = 0;
    bool world = false;
    {
        FILE *sf = fopen(DIR "start.txt", "r");
        if (sf) {
            world = fscanf(sf, "%d %d %d", &startMap, &startX, &startY) == 3;
            fclose(sf);
        }
    }
    bool ok, okFlags;
    if (world && loadWorldMap(startMap)) {
        ok = okFlags = true;
    } else {
        world = false;
        ok = loadMap();
        okFlags = loadFlags();
    }

    // Event data built by build_events.py from your copy of the game.
    bool okEvents = world ? (eventsMapId() == startMap)
                          : eventsLoadMap(DIR "events_compiled.bin");
    bool okCommon = eventsLoadCommon(DIR "common_compiled.bin");
    bool okNames = gamedataLoad(DIR);
    gamedataInitParty(DIR, &gameState);
    gamedataLoadActorChars(DIR);
    s_gsPtr = &gameState;
    if (!actorsLoad(DIR))
        uiLog("actors.bin missing (re-run build_world.py)");
    if (!battleLoadData(DIR "battle/"))
        uiLog("battle/ data missing (run build_battle.py)");
    battlegfxInit(DIR "battle/");
    eventsLoadTroopEvents(DIR "battle/troop_events.bin");
    if (!charspritesInit(DIR "characters/"))
        uiLog("characters/ missing (re-run build_world.py)");
    eventsSetPassability(eventPass);
    eventsSetGabHandler(onGab);

    bool okPics = picturesInit(DIR "pictures/");
    if (!okPics)
        uiLog("pictures/index.txt missing (run build_pictures.py)");

    int stPass = 0, stTotal = 0;
    eventsRunSelfTests(&stPass, &stTotal);
    uiLog("events %s (%lu)  common %s  names %s",
        okEvents ? "ok" : "MISSING", (unsigned long)eventsCount(),
        okCommon ? "ok" : "MISSING", okNames ? "ok" : "MISSING");
    uiLog("event self-tests: %d/%d", stPass, stTotal);
    uiLog("free memory: %lu KB GPU, %lu KB main",
        (unsigned long)(linearSpaceFree() / 1024), (unsigned long)(osGetMemRegionFree(MEMREGION_APPLICATION) / 1024));
    if (!ok || !okFlags || !okPlayer)
        uiLog("map %s flags %s player %s", ok ? "ok" : "MISSING",
            okFlags ? "ok" : "MISSING", okPlayer ? "ok" : "MISSING");

    // Load tileset sprite sheets (+ the player sprite in the last slot).
    if (!world) {
        int sheetsOk = loadTileSheets(false);
        if (sheetsOk == 0) uiLog("no tileset sheets found!");
    }
    sheets[SLOT_PLAYER] = C2D_SpriteSheetLoad(DIR "player.t3x");

    // Surface load problems in the game UI itself.
    if (!ok)
        uiLog("MAP FAILED TO LOAD!");
    if (!sheets[SLOT_PLAYER])
        uiLog("player.t3x missing!");
    if (world)
        uiLog("world mode: start map %d", startMap);

    if (ok) {
        int cnt[LAYERS] = {0};

        for (int z = 0; z < LAYERS; z++) {
            for (int i = 0; i < mapW * mapH; i++) {
                if (mapData[z * mapW * mapH + i])
                    cnt[z]++;
            }
        }

        printf("tiles/layer: %d %d %d %d\n",
            cnt[0], cnt[1], cnt[2], cnt[3]
        );
    }

    // Start where convert_map.py says: the game's own start tile when
    // this is the start map, else an open tile near the middle.
    int px = world ? startX : okPlayer ? pl.startX : mapW / 2;
    int py = world ? startY : okPlayer ? pl.startY : mapH / 2;

    int dir = 0;   // 0 = facing down, like RPG Maker's default

    int moving = 0;
    int mdir = 0;
    int mcount = 0;
    int mtotal = 16;

    int walkAnim = 0;
    int tick = 0;
    int uiMode = 0;       // 0 = field, 1 = inventory
    int invCursor = 0;
    bool inCombat = false;
    bool eventBattle = false;          // current fight came from Battle Processing
    bool battleCanLose = false;
    bool gameOver = false;
    char gameOverCause[64] = "";

    // --- title screen assets (built by build_title.py) ---
    C2D_SpriteSheet titleSheet = C2D_SpriteSheetLoad(DIR "title.t3x");
    char gameTitle[64] = "";
    {
        FILE *tf = fopen(DIR "title.txt", "r");
        if (tf) {
            if (fgets(gameTitle, sizeof(gameTitle), tf)) {
                char *nl = strchr(gameTitle, '\n');
                if (nl) *nl = '\0';
            }
            fclose(tf);
        }
    }
    bool onTitle = true;
    int titleCursor = 0;
    const bool canContinue = false;   // no save system yet

    while (aptMainLoop()) {
        hidScanInput();
        u32 held = hidKeysHeld();
        u32 down = hidKeysDown();

        // Treat the Circle Pad like the D-pad. Emulators (Azahar/Citra)
        // bind the keyboard arrow keys to the Circle Pad by default.
        if (held & KEY_CPAD_UP)    held |= KEY_UP;
        if (held & KEY_CPAD_DOWN)  held |= KEY_DOWN;
        if (held & KEY_CPAD_LEFT)  held |= KEY_LEFT;
        if (held & KEY_CPAD_RIGHT) held |= KEY_RIGHT;
        if (down & KEY_CPAD_UP)    down |= KEY_UP;
        if (down & KEY_CPAD_DOWN)  down |= KEY_DOWN;
        if (down & KEY_CPAD_LEFT)  down |= KEY_LEFT;
        if (down & KEY_CPAD_RIGHT) down |= KEY_RIGHT;

        // ---------------- title screen ----------------
        if (onTitle) {
            if (down & KEY_UP) {
                titleCursor = (titleCursor + 2) % 3;
                if (titleCursor == 1 && !canContinue) titleCursor = 0;
            }
            if (down & KEY_DOWN) {
                titleCursor = (titleCursor + 1) % 3;
                if (titleCursor == 1 && !canContinue) titleCursor = 2;
            }
            if (down & KEY_A) {
                if (titleCursor == 0) {
                    onTitle = false;          // New Game: fresh state
                    runtimeInit(&gameState);
                    gamedataInitParty(DIR, &gameState);
                    survivalInit();
                    eventsReset();
                    if (world) loadWorldMap(startMap);
                    px = world ? startX : okPlayer ? pl.startX : mapW / 2;
                    py = world ? startY : okPlayer ? pl.startY : mapH / 2;
                    dir = 0;
                    moving = 0;
                    inCombat = false;
                    eventBattle = false;
                    uiMode = 0;
                    gameOver = false;
                } else if (titleCursor == 2) {
                    break;                    // Quit
                }
            }

            uiSetMode(UI_MODE_TITLE);
            uiSetTitle(gameTitle, titleCursor, canContinue, titleSheet != NULL);

            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
            C2D_SceneBegin(top);
            if (titleSheet)
                C2D_DrawImageAt(C2D_SpriteSheetGetImage(titleSheet, 0),
                                0, 0, 0, NULL, 1, 1);
            uiRender();
            C3D_FrameEnd(0);
            continue;
        }


        if (down & KEY_START)
            break;

        if (++tick >= 30) {
            tick = 0;
            anim++;
        }

        // Field input only works while walking around and no event is
        // running. Sampled BEFORE events update, so the A press that
        // closes a message can't re-trigger the same event this frame.
        bool fieldActive = (uiMode == 0 && !inCombat && !eventsBusy() && !gameOver);

        // Debug: hold L and press B to force-stop a stuck event.
        if ((held & KEY_L) && (down & KEY_B) && eventsBusy()) {
            eventsSuspendCurrent();
            uiLog("event stopped (L+B)");
        }

        // Message window input.
        if (eventsMessageOpen())
            eventsMessageInput(down & KEY_UP, down & KEY_DOWN,
                               down & KEY_A, down & KEY_B);

        // --- movement ---
        if (ok && fieldActive) {
            if (!moving) {
                int d = -1;

                if (held & KEY_DOWN) d = 0;
                else if (held & KEY_LEFT) d = 1;
                else if (held & KEY_RIGHT) d = 2;
                else if (held & KEY_UP) d = 3;

                if (d >= 0) {
                    dir = d;

                    if (eventsBlocking(px + DX[d], py + DY[d], &gameState)) {
                        // Bumping a same-level event: touch triggers.
                        eventsTriggerTouchThere(px, py, d, &gameState);
                    } else if (canMove(px, py, d)) {
                        moving = 1;
                        mcount = 0;
                        mdir = d;
                        mtotal = (held & KEY_B) ? 8 : 16;
                    }
                }
            }

            if (moving) {
                mcount++;
                walkAnim++;

                if (mcount >= mtotal) {
                    px += DX[mdir];
                    py += DY[mdir];
                    moving = 0;
                    eventsTriggerTouchHere(px, py, &gameState);
                }
            } else {
                walkAnim = 0;
            }
        }

        // A: action-button events here / in front (RPG Maker rules).
        if (fieldActive && !moving && (down & KEY_A))
            eventsTriggerAction(px, py, dir, &gameState);

        // L+R: start a random test fight (R alone is a game button now)
        if (fieldActive && (held & KEY_L) && (down & KEY_R) &&
            battleStart(&gameState, 1, true, true)) {
            inCombat = true;
            eventBattle = false;
            moving = 0;
            battlegfxPrepare();
        }

        // SELECT: open/close inventory (not during combat)
        if (!inCombat && !eventsBusy() && (down & KEY_SELECT)) {
            if (uiMode == 0) {
                uiMode = 1;
                invCursor = 0;
            } else {
                uiMode = 0;
            }
        }

        // INVENTORY MODE: the real party inventory (database items,
        // weapons, armors gained through events). Drawing is in ui.c.
        if (uiMode == 1) {
            if (down & KEY_B) {
                uiMode = 0;
            } else {
                InvEntry inv[32];
                int n = gamedataInventory(&gameState, inv, 32);

                if (n > 0) {
                    if (down & KEY_UP) {
                        invCursor--;
                        if (invCursor < 0) invCursor = n - 1;
                    }
                    if (down & KEY_DOWN) {
                        invCursor++;
                        if (invCursor >= n) invCursor = 0;
                    }
                    if (down & KEY_A && invCursor < n) {
                        uiLog("%s", battleFieldUse(&gameState, inv[invCursor].kind,
                                                   inv[invCursor].id));
                        // a book or tool may start its own event: show it
                        if (eventsBusy()) uiMode = 0;
                        n = gamedataInventory(&gameState, inv, 32);
                    }
                }
                if (invCursor >= n) invCursor = n > 0 ? n - 1 : 0;

                const char *names[32];
                int counts[32];
                for (int i = 0; i < n; i++) {
                    names[i] = gamedataName(inv[i].kind, inv[i].id);
                    counts[i] = inv[i].count;
                }
                uiSetInventory(names, counts, n, invCursor);
            }
        }

        // --- events: run scripts, then service their requests ---
        {
            u32 evb = 0;
            if (held & KEY_A)      evb |= EVB_OK;
            if (held & KEY_B)      evb |= EVB_CANCEL;
            if (held & KEY_Y)      evb |= EVB_SHIFT;
            if (held & KEY_DOWN)   evb |= EVB_DOWN;
            if (held & KEY_LEFT)   evb |= EVB_LEFT;
            if (held & KEY_RIGHT)  evb |= EVB_RIGHT;
            if (held & KEY_UP)     evb |= EVB_UP;
            if (held & KEY_L)      evb |= EVB_PAGEUP;
            if (held & KEY_R)      evb |= EVB_PAGEDOWN;
            eventsSetButtons(evb);
            eventsSetPlayer(px, py, dir);
        }
        eventsUpdate(&gameState);
        if (!inCombat && uiMode == 0 && !gameOver)
            eventsCheckAutorun(&gameState);

        {
            int troop; bool canEsc, canLose;
            if (!inCombat && eventsPendingBattle(&troop, &canEsc, &canLose)) {
                if (battleStart(&gameState, troop, canEsc, canLose)) {
                    inCombat = true;
                    eventBattle = true;
                    battleCanLose = canLose;
                    moving = 0;
                    battlegfxPrepare();
                } else {
                    // No battle data on the SD card: count it as a win so
                    // the story can continue, and say so.
                    uiLog("Troop %d not found: battle skipped", troop);
                    eventsResolveBattle(0);
                }
            }

            int nameActor, nameMax;
            if (eventsPendingNameInput(&nameActor, &nameMax)) {
                // 3DS system keyboard (works in Azahar too).
                char buf[32];
                SwkbdState kb;
                if (nameMax > 16) nameMax = 16;
                swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, nameMax);
                swkbdSetInitialText(&kb, actorName(&gameState, nameActor));
                swkbdSetHintText(&kb, "Name");
                swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
                SwkbdButton b = swkbdInputText(&kb, buf, sizeof(buf));
                if (b == SWKBD_BUTTON_CONFIRM) {
                    // keep it printable ASCII for the bitmap font
                    for (char *q = buf; *q; q++)
                        if (*q < 32 || *q > 126) *q = '?';
                    eventsNameInputDone(&gameState, buf);
                } else {
                    eventsNameInputDone(&gameState, NULL);
                }
            }

            int tm, tx, ty, td;
            if (eventsPendingTransfer(&tm, &tx, &ty, &td)) {
                bool arrived = (tm == eventsMapId());
                if (!arrived && world && loadWorldMap(tm)) {
                    arrived = true;
                    // Survival meters and pictures carry over, like MV.
                }
                if (arrived) {
                    px = tx;
                    py = ty;
                    moving = 0;
                    // RPG Maker dirs: 2 down 4 left 6 right 8 up, 0 keep
                    if (td == 2) dir = 0;
                    else if (td == 4) dir = 1;
                    else if (td == 6) dir = 2;
                    else if (td == 8) dir = 3;
                    eventsTransferDone();
                } else {
                    // In the real game this event ends because the map
                    // changes. We can't change maps yet, so stop it and
                    // keep it from re-running (autoruns would loop).
                    uiLog("Map %d isn't converted yet", tm);
                    eventsSuspendCurrent();
                }
            }

            if (!gameOver && eventsGameOver()) {
                if (eventsReturnToTitle()) {
                    onTitle = true;           // 354: straight to the title
                } else {
                    gameOver = true;
                    snprintf(gameOverCause, sizeof(gameOverCause), "%s",
                             eventsGameOverCause());
                }
            }
        }

        // BATTLE: the game's own troops, skills and states (battle.c).
        if (inCombat) {
            bool msg = eventsMessageOpen();     // skill common events may talk
            battleInput(!msg && (down & KEY_UP), !msg && (down & KEY_DOWN),
                        !msg && (down & KEY_LEFT), !msg && (down & KEY_RIGHT),
                        !msg && (down & KEY_A), !msg && (down & KEY_B));
            battleUpdate(&gameState);

            int r = battleResult();
            if (r >= 0) {
                battleFinish();
                battlegfxRelease();
                inCombat = false;
                if (r == 2 && !(eventBattle && battleCanLose)) {
                    gameOver = true;
                    snprintf(gameOverCause, sizeof(gameOverCause), "the party fell in battle");
                } else if (eventBattle) {
                    eventsResolveBattle(r);
                }
                eventBattle = false;
            }
        }

        // --- feed the UI ---
        // Game Over screen: A goes back to the title.
        if (gameOver) {
            int leader = runtimePartyLeader(&gameState);
            ActorState *ld = leader ? actorGet(&gameState, leader) : NULL;
            uiSetGameOver(gameOverCause,
                ld ? actorName(&gameState, leader) : "",
                ld ? ld->hp : 0, ld ? actorParam(&gameState, leader, 0) : 0,
                ld ? ld->mp : 0, ld ? actorParam(&gameState, leader, 1) : 0);
            if (down & KEY_A) onTitle = true;
        }

        uiSetMode(gameOver ? UI_MODE_GAMEOVER
                : eventsMessageOpen() ? UI_MODE_MESSAGE
                : inCombat ? UI_MODE_BATTLE
                : (uiMode == 1) ? UI_MODE_INVENTORY
                : UI_MODE_FIELD);

        if (eventsMessageOpen()) {
            const char *lines[16];
            const char *choices[8];
            int nl = eventsMessageLines(lines, 16);
            int nc = eventsChoiceCount();
            if (nc > 8) nc = 8;
            for (int i = 0; i < nc; i++)
                choices[i] = eventsChoiceText(i);
            uiSetMessage(eventsMessageSpeaker(), lines, nl,
                         choices, nc, eventsChoiceCursor());
        }
        {
            int leader = runtimePartyLeader(&gameState);
            ActorState *ld = leader ? actorGet(&gameState, leader) : NULL;
            uiSetVitals(leader ? actorName(&gameState, leader) : "",
                        battleTerm("termHp", "HP"), ld ? ld->hp : 0,
                        ld ? actorParam(&gameState, leader, 0) : 1,
                        battleTerm("termMp", "MP"), ld ? ld->mp : 0,
                        ld ? actorParam(&gameState, leader, 1) : 1);
        }
        uiSetPos(px, py);
        uiSetStatus(eventsDebugStatus());

        if (inCombat)
            uiSetBattle(battleView());

        // --- camera ---
        float ox = moving
            ? DX[mdir] * TS * mcount / (float)mtotal
            : 0;

        float oy = moving
            ? DY[mdir] * TS * mcount / (float)mtotal
            : 0;

        int ppx = (int)(px * TS + ox);
        int ppy = (int)(py * TS + oy);

        int maxX = mapW * TS - 400;
        if (maxX < 0) maxX = 0;

        int maxY = mapH * TS - 240;
        if (maxY < 0) maxY = 0;

        int camX = ppx + TS / 2 - 200;
        int camY = ppy + TS / 2 - 120;

        if (camX < 0) camX = 0;
        if (camX > maxX) camX = maxX;

        if (camY < 0) camY = 0;
        if (camY > maxY) camY = maxY;

        // --- render ---
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(top);

        if (inCombat && !gameOver) {
            battlegfxDraw();          // battle background + enemies
            picturesDraw(camX, camY, px, py, mapW, mapH);
        } else if (ok && !gameOver) {
            drawLayers(camX, camY, 0);

            int pattern = moving
                ? WS[(walkAnim / 6) % 4]
                : 1;

            // Map events: sorted top-to-bottom so lower ones overlap.
            static EventSprite sprites[400];
            int ns = eventsSprites(sprites, 400);
            for (int i = 1; i < ns; i++)
                for (int j = i; j > 0 && sprites[j].y < sprites[j - 1].y; j--) {
                    EventSprite t = sprites[j];
                    sprites[j] = sprites[j - 1];
                    sprites[j - 1] = t;
                }
            drawEvents(sprites, ns, 0, camX, camY, false, 0, 0, 0, 0);
            drawEvents(sprites, ns, 1, camX, camY, true, ppx, ppy, dir, pattern);

            drawLayers(camX, camY, 1);
            drawEvents(sprites, ns, 2, camX, camY, false, 0, 0, 0, 0);

            // Event pictures sit above the map, below the screen tint.
            picturesDraw(camX, camY, px, py, mapW, mapH);

            // Tint Screen (223): approximate with a translucent overlay.
            int tr, tg, tb, tgray;
            eventsGetTint(&tr, &tg, &tb, &tgray);
            int avg = (tr + tg + tb) / 3;
            if (avg < 0) {
                int a = -avg > 255 ? 255 : -avg;
                C2D_DrawRectSolid(0, 0, 0.9f, 400, 240, C2D_Color32(0, 0, 0, a));
            } else if (avg > 0) {
                int a = avg > 255 ? 255 : avg;
                C2D_DrawRectSolid(0, 0, 0.9f, 400, 240, C2D_Color32(255, 255, 255, a / 2));
            }
        }

        uiRender();

        C3D_FrameEnd(0);
    }

    // Cleanup.
    picturesExit();
    charspritesExit();
    if (titleSheet)
        C2D_SpriteSheetFree(titleSheet);
    for (int i = 0; i < NSHEETS; i++) {
        if (sheets[i])
            C2D_SpriteSheetFree(sheets[i]);
    }

    free(mapData);

    uiExit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();

    return 0;
}