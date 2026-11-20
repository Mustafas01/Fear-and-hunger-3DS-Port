#include "pictures.h"
#include "events.h"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Same scale as the map (48px MV tiles -> 24px). RPG Maker's 816x624
// screen becomes 408x312, slightly larger than the 400x240 top screen.
#define SCALE 0.5f
#define MV_TILES_X 17.0f   // 816 / 48
#define MV_TILES_Y 13.0f   // 624 / 48

#define CACHE_SIZE 10
// Keep this much GPU (linear) memory free; a real 3DS has far less than
// an emulator, so old pictures are dropped before loading a new one.
#define MIN_FREE_LINEAR (6u * 1024u * 1024u)

typedef struct { char name[64]; int w, h; } IndexEntry;

typedef struct {
    char name[64];
    C2D_SpriteSheet sheet;   // NULL + failed = file missing
    bool failed;
    int origW, origH;
    u32 lastUse;
} CacheSlot;

static char s_dir[128];
static IndexEntry *s_index;
static int s_indexCount;
static CacheSlot s_cache[CACHE_SIZE];
static u32 s_clock;
static int s_missing;

static void sanitize(const char *in, char *out, size_t n) {
    size_t i = 0;
    for (; *in && i + 1 < n; in++) {
        char c = *in;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_';
        out[i++] = ok ? c : '_';
    }
    out[i] = '\0';
}

bool picturesInit(const char *dir) {
    snprintf(s_dir, sizeof(s_dir), "%s", dir);
    char path[192];
    snprintf(path, sizeof(path), "%sindex.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) return false;

    int cap = 256;
    s_index = malloc(sizeof(IndexEntry) * cap);
    char name[64];
    int w, h;
    while (s_index && fscanf(f, "%63s %d %d", name, &w, &h) == 3) {
        if (s_indexCount == cap) {
            cap *= 2;
            IndexEntry *n = realloc(s_index, sizeof(IndexEntry) * cap);
            if (!n) break;
            s_index = n;
        }
        snprintf(s_index[s_indexCount].name, 64, "%s", name);
        s_index[s_indexCount].w = w;
        s_index[s_indexCount].h = h;
        s_indexCount++;
    }
    fclose(f);
    return s_indexCount > 0;
}

void picturesExit(void) {
    for (int i = 0; i < CACHE_SIZE; i++)
        if (s_cache[i].sheet) C2D_SpriteSheetFree(s_cache[i].sheet);
    memset(s_cache, 0, sizeof(s_cache));
    free(s_index);
    s_index = NULL;
    s_indexCount = 0;
}

int picturesMissingCount(void) { return s_missing; }

static CacheSlot *lookup(const char *rawName) {
    char name[64];
    sanitize(rawName, name, sizeof(name));
    s_clock++;

    for (int i = 0; i < CACHE_SIZE; i++)
        if (s_cache[i].name[0] && !strcmp(s_cache[i].name, name)) {
            s_cache[i].lastUse = s_clock;
            return &s_cache[i];
        }

    // Evict the least recently used slot.
    int victim = 0;
    for (int i = 1; i < CACHE_SIZE; i++)
        if (s_cache[i].lastUse < s_cache[victim].lastUse) victim = i;
    CacheSlot *slot = &s_cache[victim];
    if (slot->sheet) C2D_SpriteSheetFree(slot->sheet);
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    slot->lastUse = s_clock;

    // Make room in GPU memory: free least-recently-used pictures.
    while (linearSpaceFree() < MIN_FREE_LINEAR) {
        int lru = -1;
        for (int i = 0; i < CACHE_SIZE; i++)
            if (s_cache[i].sheet && &s_cache[i] != slot &&
                (lru < 0 || s_cache[i].lastUse < s_cache[lru].lastUse)) lru = i;
        if (lru < 0) break;
        C2D_SpriteSheetFree(s_cache[lru].sheet);
        memset(&s_cache[lru], 0, sizeof(s_cache[lru]));
    }
    char path[256];
    snprintf(path, sizeof(path), "%.120s%.63s.t3x", s_dir, name);
    slot->sheet = C2D_SpriteSheetLoad(path);
    slot->failed = (slot->sheet == NULL);
    if (slot->failed) s_missing++;

    for (int i = 0; i < s_indexCount; i++)
        if (!strcmp(s_index[i].name, name)) {
            slot->origW = s_index[i].w;
            slot->origH = s_index[i].h;
            break;
        }
    return slot;
}

// Where RPG Maker's screen origin sits on the map (Game_Map display x/y):
// centred on the player, clamped to the map edges.
static float mvDisplay(int player, int mapSize, float screenTiles) {
    float d = player - (screenTiles - 1.0f) / 2.0f;
    float maxD = mapSize - screenTiles;
    if (maxD < 0) return maxD / 2.0f;        // map smaller than screen: centred
    if (d > maxD) d = maxD;
    if (d < 0) d = 0;
    return d;
}

void picturesDraw(int camX, int camY, int px, int py, int mapW, int mapH) {
    // MV screen pixel (sx, sy) -> 3DS top-screen pixel.
    float originX = mvDisplay(px, mapW, MV_TILES_X) * 24.0f - camX;
    float originY = mvDisplay(py, mapH, MV_TILES_Y) * 24.0f - camY;

    for (int id = 1; id <= FH_MAX_PICTURES; id++) {
        const EventPicture *p = eventsGetPicture(id);
        if (!p || p->opacity <= 0.0f) continue;
        CacheSlot *slot = lookup(p->name);
        if (!slot->sheet) continue;

        C2D_Image img = C2D_SpriteSheetGetImage(slot->sheet, 0);
        float texW = img.subtex->width, texH = img.subtex->height;
        float ow = slot->origW ? (float)slot->origW : texW / SCALE;
        float oh = slot->origH ? (float)slot->origH : texH / SCALE;

        // Size on the 3DS screen.
        float dispW = ow * SCALE * p->scaleX / 100.0f;
        float dispH = oh * SCALE * p->scaleY / 100.0f;
        float x = originX + p->x * SCALE;
        float y = originY + p->y * SCALE;
        if (p->origin == 1) { x -= dispW / 2.0f; y -= dispH / 2.0f; }

        // Opacity, plus picture tone approximated as fade to black/white.
        float alpha = p->opacity / 255.0f;
        if (alpha > 1.0f) alpha = 1.0f;
        float avg = (p->tone[0] + p->tone[1] + p->tone[2]) / 3.0f;
        C2D_ImageTint tint;
        u8 a8 = (u8)(alpha * 255.0f);
        if (avg < -1.0f) {
            float b = -avg / 255.0f; if (b > 1.0f) b = 1.0f;
            C2D_PlainImageTint(&tint, C2D_Color32(0, 0, 0, a8), b);
        } else if (avg > 1.0f) {
            float b = avg / 255.0f; if (b > 1.0f) b = 1.0f;
            C2D_PlainImageTint(&tint, C2D_Color32(255, 255, 255, a8), b);
        } else {
            C2D_AlphaImageTint(&tint, alpha);
        }

        C2D_DrawImageAt(img, x, y, 0.5f, &tint, dispW / texW, dispH / texH);
    }
}
