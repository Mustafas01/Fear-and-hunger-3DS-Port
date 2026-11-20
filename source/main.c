#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>

#include "events.h"
#include "runtime.h"

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

// Game state is static because it is too large for the normal stack.
static GameState gameState;

// 0=A5 1..4=B..E 5=A2 6=A4 7=A1 8=A3 9=player
static const char *sheetFiles[NSHEETS] = {
    "a5.t3x", "b.t3x", "c.t3x", "d.t3x", "e.t3x",
    "a2.t3x", "a4.t3x", "a1.t3x", "a3.t3x", "player.t3x"
};

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
    FILE *f = fopen(DIR "map.bin", "rb");
    if (!f) return false;

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
    FILE *f = fopen(DIR "flags.bin", "rb");
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

    if (!sheets[slot] || shape >= nshapes)
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

    if (!sheets[slot]) return;

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

int main(void) {
    gfxInitDefault();

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    consoleInit(GFX_BOTTOM, NULL);

    C3D_RenderTarget *top =
        C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);

    // Initialize game state.
    runtimeInit(&gameState);

    // Load map data.
    bool ok = loadMap();
    bool okFlags = loadFlags();
    bool okPlayer = loadPlayer();

    // Validate compiled event data.
    EventFileInfo eventInfo;
    bool okEvents = eventsValidate(
        DIR "events_compiled.bin",
        &eventInfo
    );

    // Run actual event execution test.
    bool okRuntime = false;
    if (okEvents) {
        int activePage = eventsGetActivePage(
            DIR "events_compiled.bin", 1, &gameState
        );
        printf("event 1 active page: %d\n", activePage);

        if (eventsExecutePage(DIR "events_compiled.bin", 1, 0, &gameState)) {
            int32_t v9 = runtimeGetVariable(&gameState, 9);
            printf("page 0 executed, var 9 = %ld\n", (long)v9);
            if (v9 == 1) {
                okRuntime = true;
            }
        } else {
            printf("page 0 execution FAILED\n");
        }
    }

    printf("map: %s", ok ? "ok" : "FAILED");

    if (ok)
        printf(" (%dx%d)", mapW, mapH);

    printf("\nflags: %s  player: %s\n",
        okFlags ? "ok" : "MISSING",
        okPlayer ? "ok" : "MISSING"
    );

    printf("events: %s\n", okEvents ? "ok" : "FAILED");

    if (okEvents) {
        printf("event map: %u | events: %lu\n",
            eventInfo.mapId,
            (unsigned long)eventInfo.eventCount
        );

        printf("pages: %lu | commands: %lu\n",
            (unsigned long)eventInfo.pageCount,
            (unsigned long)eventInfo.commandCount
        );

        eventsDumpEventPositions(DIR "events_compiled.bin");
    }

    printf("runtime test: %s\n",
        okRuntime ? "PASSED" : "FAILED"
    );

    // Interpreter self-tests: branch logic (111/411/412, command 0).
    eventsRunBranchSelfTests();

    // Trace event execution so A-button triggers are visible.
    eventsSetTrace(true);

    // Load tileset sprite sheets.
    for (int i = 0; i < NSHEETS; i++) {
        char path[128];

        snprintf(path, sizeof(path), DIR "%s", sheetFiles[i]);

        sheets[i] = C2D_SpriteSheetLoad(path);

        if (!sheets[i])
            printf("%s: missing\n", sheetFiles[i]);
    }

    printf("\nD-pad/circle: walk  B: run  START: exit\n");
    printf("X: teleport to event  A: interact\n");

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

    int px = okPlayer ? pl.startX : 0;
    int py = okPlayer ? pl.startY : 0;

    int dir = 0;
    int moving = 0;
    int mdir = 0;
    int mcount = 0;
    int mtotal = 16;

    int walkAnim = 0;
    int tick = 0;

    while (aptMainLoop()) {
        hidScanInput();

        u32 held = hidKeysHeld();

        if (hidKeysDown() & KEY_START)
            break;

        if (++tick >= 30) {
            tick = 0;
            anim++;
        }

        if (ok) {
            if (!moving) {
                int d = -1;

                if (held & KEY_DOWN) d = 0;
                else if (held & KEY_LEFT) d = 1;
                else if (held & KEY_RIGHT) d = 2;
                else if (held & KEY_UP) d = 3;

                if (d >= 0) {
                    dir = d;

                    if (canMove(px, py, d)) {
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
                }
            } else {
                walkAnim = 0;
            }
        }

        // DEBUG: X teleports to event 1's tile (bypasses walls).
        if (okEvents && (hidKeysDown() & KEY_X)) {
            uint16_t dummyId;
            if (eventsEventAt(DIR "events_compiled.bin", 7, 7, &dummyId)) {
                px = 7;
                py = 8;
                moving = 0;
                printf("\x1b[25;1Hteleported to 7,8\n");
            }
        }

        // A button: trigger the event we are facing, else the one
        // we are standing on. MV also checks one tile beyond a
        // counter-type tile in front; counters come later.
        if (okEvents && (hidKeysDown() & KEY_A)) {
            int tx = px + DX[dir];
            int ty = py + DY[dir];

            uint16_t eventId = 0;

            bool found = eventsEventAt(
                DIR "events_compiled.bin", tx, ty, &eventId
            );

            if (!found) {
                tx = px;
                ty = py;

                found = eventsEventAt(
                    DIR "events_compiled.bin", tx, ty, &eventId
                );
            }

            if (found) {
                printf("\x1b[24;1HA: event %u at %d,%d      \n",
                    eventId, tx, ty
                );

                bool ran = eventsExecuteActivePage(
                    DIR "events_compiled.bin", eventId, &gameState
                );

                if (!ran)
                    printf("  no active page\n");
            } else {
                printf("\x1b[24;1HA: no event here        \n");
            }
        }

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

        printf("\x1b[20;1Hpos: %3d,%3d  ", px, py);

        if (ok) {
            u16 t0 = mapData[(0 * mapH + py) * mapW + px];
            u16 t1 = mapData[(1 * mapH + py) * mapW + px];
            u16 t2 = mapData[(2 * mapH + py) * mapW + px];
            u16 t3 = mapData[(3 * mapH + py) * mapW + px];

            printf("\x1b[21;1Htiles z0-3: %d %d %d %d     ",
                t0, t1, t2, t3
            );

            printf("\x1b[22;1Hflags: %x %x %x %x     ",
                flags[t0], flags[t1], flags[t2], flags[t3]
            );
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(top);

        if (ok) {
            drawLayers(camX, camY, 0);

            int pattern = moving
                ? WS[(walkAnim / 6) % 4]
                : 1;

            drawPlayer(
                ppx - camX,
                ppy - camY,
                dir,
                pattern
            );

            drawLayers(camX, camY, 1);
        }

        C3D_FrameEnd(0);
    }

    // Cleanup.
    for (int i = 0; i < NSHEETS; i++) {
        if (sheets[i])
            C2D_SpriteSheetFree(sheets[i]);
    }

    free(mapData);

    C2D_Fini();
    C3D_Fini();
    gfxExit();

    return 0;
}