#include "battlegfx.h"
#include "battle.h"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>

#define S (240.0f / 624.0f)
#define OFF_X ((400.0f - 816.0f * S) / 2.0f)
#define MAX_TEX (BT_MAX_ENEMIES + 2)

typedef struct { char name[64]; C2D_SpriteSheet sheet; int ow, oh; } Tex;

static char s_dir[128];
static Tex s_tex[MAX_TEX];
static int s_nTex;
static u32 s_frame;

void battlegfxInit(const char *dir) { snprintf(s_dir, sizeof(s_dir), "%s", dir); }

// Original image size from <sub>/index.txt (0 if unknown).
static void origSize(const char *sub, const char *name, int *w, int *h) {
    *w = *h = 0;
    char path[256], n[64];
    snprintf(path, sizeof(path), "%s%s/index.txt", s_dir, sub);
    FILE *f = fopen(path, "r");
    if (!f) return;
    int a, b;
    while (fscanf(f, "%63s %d %d", n, &a, &b) == 3)
        if (!strcmp(n, name)) { *w = a; *h = b; break; }
    fclose(f);
}

static Tex *load(const char *sub, const char *name) {
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < s_nTex; i++)
        if (!strcmp(s_tex[i].name, name)) return s_tex[i].sheet ? &s_tex[i] : NULL;
    if (s_nTex >= MAX_TEX) return NULL;
    Tex *t = &s_tex[s_nTex++];
    snprintf(t->name, sizeof(t->name), "%s", name);
    char path[256];
    snprintf(path, sizeof(path), "%s%s/%.63s.t3x", s_dir, sub, name);
    t->sheet = C2D_SpriteSheetLoad(path);
    origSize(sub, name, &t->ow, &t->oh);
    return t->sheet ? t : NULL;
}

void battlegfxRelease(void) {
    for (int i = 0; i < s_nTex; i++)
        if (s_tex[i].sheet) C2D_SpriteSheetFree(s_tex[i].sheet);
    memset(s_tex, 0, sizeof(s_tex));
    s_nTex = 0;
}

void battlegfxPrepare(void) {
    battlegfxRelease();
    const char *b1, *b2;
    battleBackNames(&b1, &b2);
    load("battlebacks", b1);
    load("battlebacks", b2);
    for (int i = 0; i < battleEnemyCount(); i++) {
        const char *bat; int x, y; bool blink;
        if (battleEnemySprite(i, &bat, &x, &y, &blink)) load("battlers", bat);
    }
}

// Draws texture t with its MV-pixel size, at 3DS position (x, y).
static void drawTex(Tex *t, float x, float y, float alpha) {
    C2D_Image img = C2D_SpriteSheetGetImage(t->sheet, 0);
    float tw = img.subtex->width, th = img.subtex->height;
    float w = t->ow ? t->ow * S : tw, h = t->oh ? t->oh * S : th;
    C2D_ImageTint tint;
    C2D_AlphaImageTint(&tint, alpha);
    C2D_DrawImageAt(img, x, y, 0.5f, &tint, w / tw, h / th);
}

static void drawBack(const char *name) {
    Tex *t = load("battlebacks", name);
    if (!t) return;
    C2D_Image img = C2D_SpriteSheetGetImage(t->sheet, 0);
    float w = t->ow ? t->ow * S : img.subtex->width;
    float h = t->oh ? t->oh * S : img.subtex->height;
    // MV centres the battle background on its 816x624 screen.
    drawTex(t, OFF_X + (816.0f * S - w) / 2.0f, (240.0f - h) / 2.0f, 1.0f);
}

void battlegfxDraw(void) {
    s_frame++;
    const char *b1, *b2;
    battleBackNames(&b1, &b2);
    drawBack(b1);
    drawBack(b2);
    for (int i = 0; i < battleEnemyCount(); i++) {
        const char *bat; int x, y; bool blink;
        if (!battleEnemySprite(i, &bat, &x, &y, &blink)) continue;
        Tex *t = load("battlers", bat);
        if (!t) continue;
        C2D_Image img = C2D_SpriteSheetGetImage(t->sheet, 0);
        float w = t->ow ? t->ow * S : img.subtex->width;
        float h = t->oh ? t->oh * S : img.subtex->height;
        // Enemy sprites stand on their troop position (bottom centre).
        float alpha = (blink && (s_frame / 8) % 2) ? 0.45f : 1.0f;
        drawTex(t, OFF_X + x * S - w / 2.0f, y * S - h, alpha);
    }
}
