#include "charsprites.h"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE 16
#define MIN_FREE_LINEAR (6u * 1024u * 1024u)

typedef struct { char name[48]; int w, h, single, obj; } Entry;
typedef struct { char name[48]; C2D_SpriteSheet sheet; bool failed; u32 used; const Entry *info; } Slot;

static char s_dir[128];
static Entry *s_idx;
static int s_n;
static Slot s_cache[CACHE];
static u32 s_clock;

bool charspritesInit(const char *dir) {
    snprintf(s_dir, sizeof(s_dir), "%s", dir);
    char path[192];
    snprintf(path, sizeof(path), "%sindex.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    int cap = 128;
    s_idx = malloc(sizeof(Entry) * cap);
    Entry e;
    while (s_idx && fscanf(f, "%47s %d %d %d %d", e.name, &e.w, &e.h, &e.single, &e.obj) == 5) {
        if (s_n == cap) {
            cap *= 2;
            Entry *n = realloc(s_idx, sizeof(Entry) * cap);
            if (!n) break;
            s_idx = n;
        }
        s_idx[s_n++] = e;
    }
    fclose(f);
    return s_n > 0;
}

void charspritesExit(void) {
    for (int i = 0; i < CACHE; i++)
        if (s_cache[i].sheet) C2D_SpriteSheetFree(s_cache[i].sheet);
    memset(s_cache, 0, sizeof(s_cache));
    free(s_idx);
    s_idx = NULL;
    s_n = 0;
}

static Slot *get(const char *name) {
    s_clock++;
    for (int i = 0; i < CACHE; i++)
        if (s_cache[i].name[0] && !strcmp(s_cache[i].name, name)) {
            s_cache[i].used = s_clock;
            return s_cache[i].failed ? NULL : &s_cache[i];
        }
    int v = 0;
    for (int i = 1; i < CACHE; i++) if (s_cache[i].used < s_cache[v].used) v = i;
    Slot *sl = &s_cache[v];
    if (sl->sheet) C2D_SpriteSheetFree(sl->sheet);
    memset(sl, 0, sizeof(*sl));
    snprintf(sl->name, sizeof(sl->name), "%s", name);
    sl->used = s_clock;
    for (int i = 0; i < s_n; i++)
        if (!strcmp(s_idx[i].name, name)) { sl->info = &s_idx[i]; break; }
    while (linearSpaceFree() < MIN_FREE_LINEAR) {      // real-3DS memory guard
        int lru = -1;
        for (int i = 0; i < CACHE; i++)
            if (s_cache[i].sheet && &s_cache[i] != sl &&
                (lru < 0 || s_cache[i].used < s_cache[lru].used)) lru = i;
        if (lru < 0) break;
        C2D_SpriteSheetFree(s_cache[lru].sheet);
        memset(&s_cache[lru], 0, sizeof(s_cache[lru]));
    }
    char path[200];
    snprintf(path, sizeof(path), "%.120s%.47s.t3x", s_dir, name);
    sl->sheet = sl->info ? C2D_SpriteSheetLoad(path) : NULL;
    sl->failed = !sl->sheet;
    return sl->failed ? NULL : sl;
}

void charspritesDraw(const char *name, int index, int dir, int pattern,
                     float sx, float sy, int ts) {
    if (!name || !name[0]) return;
    Slot *sl = get(name);
    if (!sl) return;
    const Entry *e = sl->info;
    int fw = e->single ? e->w / 3 : e->w / 12;
    int fh = e->single ? e->h / 4 : e->h / 8;
    int bx = e->single ? 0 : (index % 4) * 3 * fw;
    int by = e->single ? 0 : (index / 4) * 4 * fh;
    int fx = bx + pattern * fw, fy = by + dir * fh;

    C2D_Image img = C2D_SpriteSheetGetImage(sl->sheet, 0);
    float tw = img.tex->width, th = img.tex->height;
    Tex3DS_SubTexture st = { (u16)fw, (u16)fh, fx / tw, 1.0f - fy / th,
                             (fx + fw) / tw, 1.0f - (fy + fh) / th };
    C2D_Image im = { img.tex, &st };
    // Feet on the tile; normal characters sit a few pixels higher (MV shiftY).
    C2D_DrawImageAt(im, (int)(sx + (ts - fw) / 2.0f), (int)(sy + ts - fh - (e->obj ? 0 : 3)),
                    0, NULL, 1, 1);
}
