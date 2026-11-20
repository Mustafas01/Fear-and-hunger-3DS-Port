// source/ui.c
#include "ui.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define LOG_LINES 4
#define LOG_LEN   52
#define INV_MAX   32
#define INV_ROWS  9

#define RGBA8(r,g,b) ((u32)(((r)&0xFF) | (((g)&0xFF)<<8) | (((b)&0xFF)<<16) | ((u32)0xFF<<24)))

#define COL_BG     RGBA8( 12, 10, 14)
#define COL_FRAME  RGBA8( 62, 56, 68)
#define COL_BARBG  RGBA8( 32, 28, 36)
#define COL_TEXT   RGBA8(216,208,192)
#define COL_DIM    RGBA8(118,112,104)
#define COL_AMBER  RGBA8(204,168, 64)
#define COL_SELBG  RGBA8( 58, 42, 24)
#define COL_HP     RGBA8(150, 34, 34)
#define COL_EHP    RGBA8(118, 18, 28)
#define COL_FOOD   RGBA8(178,122, 42)
#define COL_MIND   RGBA8( 88, 98,178)
#define COL_ENEMY  RGBA8(196, 64, 56)

// ---------------------------------------------------------------
// Built-in 5x7 bitmap font (ASCII 32..126).
// 7 bytes per glyph, one per row. Bit 4 = leftmost column.
// Drawn with C2D_DrawRectSolid: no system font needed, so this
// works on emulators without dumped system archives.
// ---------------------------------------------------------------
static const u8 FONT5X7[95][7] = {
 {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 ' '
 {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, // 33 '!'
 {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00}, // 34 '"'
 {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}, // 35 '#'
 {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // 36 '$'
 {0x19,0x19,0x02,0x04,0x08,0x13,0x13}, // 37 '%'
 {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // 38 '&'
 {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, // 39 '''
 {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // 40 '('
 {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // 41 ')'
 {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // 42 '*'
 {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // 43 '+'
 {0x00,0x00,0x00,0x00,0x06,0x04,0x08}, // 44 ','
 {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // 45 '-'
 {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // 46 '.'
 {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, // 47 '/'
 {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 48 '0'
 {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 49 '1'
 {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 50 '2'
 {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // 51 '3'
 {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 52 '4'
 {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 53 '5'
 {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 54 '6'
 {0x1F,0x01,0x02,0x04,0x04,0x04,0x04}, // 55 '7'
 {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 56 '8'
 {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 57 '9'
 {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, // 58 ':'
 {0x00,0x0C,0x0C,0x00,0x0C,0x08,0x10}, // 59 ';'
 {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // 60 '<'
 {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // 61 '='
 {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // 62 '>'
 {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // 63 '?'
 {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // 64 '@'
 {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // 65 'A'
 {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // 66 'B'
 {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // 67 'C'
 {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // 68 'D'
 {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // 69 'E'
 {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // 70 'F'
 {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}, // 71 'G'
 {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // 72 'H'
 {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // 73 'I'
 {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // 74 'J'
 {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // 75 'K'
 {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // 76 'L'
 {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // 77 'M'
 {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, // 78 'N'
 {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // 79 'O'
 {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // 80 'P'
 {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // 81 'Q'
 {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // 82 'R'
 {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // 83 'S'
 {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // 84 'T'
 {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // 85 'U'
 {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // 86 'V'
 {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, // 87 'W'
 {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // 88 'X'
 {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // 89 'Y'
 {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // 90 'Z'
 {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // 91 '['
 {0x10,0x10,0x08,0x04,0x02,0x01,0x01}, // 92 '\'
 {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // 93 ']'
 {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // 94 '^'
 {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // 95 '_'
 {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, // 96 '`'
 {0x00,0x00,0x0E,0x01,0x0F,0x13,0x0D}, // 97 'a'
 {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, // 98 'b'
 {0x00,0x00,0x0E,0x10,0x10,0x10,0x0E}, // 99 'c'
 {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, // 100 'd'
 {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, // 101 'e'
 {0x06,0x08,0x08,0x1C,0x08,0x08,0x08}, // 102 'f'
 {0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E}, // 103 'g'
 {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, // 104 'h'
 {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // 105 'i'
 {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, // 106 'j'
 {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, // 107 'k'
 {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // 108 'l'
 {0x00,0x00,0x1A,0x15,0x15,0x15,0x15}, // 109 'm'
 {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, // 110 'n'
 {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // 111 'o'
 {0x00,0x00,0x1E,0x11,0x11,0x1E,0x10}, // 112 'p'
 {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01}, // 113 'q'
 {0x00,0x00,0x16,0x18,0x10,0x10,0x10}, // 114 'r'
 {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, // 115 's'
 {0x08,0x08,0x1C,0x08,0x08,0x08,0x06}, // 116 't'
 {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, // 117 'u'
 {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, // 118 'v'
 {0x00,0x00,0x11,0x15,0x15,0x15,0x0A}, // 119 'w'
 {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // 120 'x'
 {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, // 121 'y'
 {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // 122 'z'
 {0x02,0x04,0x04,0x08,0x04,0x04,0x02}, // 123 '{'
 {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, // 124 '|'
 {0x08,0x04,0x04,0x02,0x04,0x04,0x08}, // 125 '}'
 {0x00,0x00,0x0D,0x12,0x00,0x00,0x00}, // 126 '~'
};

static C3D_RenderTarget* s_bottom = NULL;
static UIMode s_mode = UI_MODE_FIELD;

static char s_log[LOG_LINES][LOG_LEN];

static int  s_hunger, s_mind;
// Party leader's real HP / MP, labelled with the game's own term names.
static char s_hpLabel[8] = "HP", s_mpLabel[8] = "MP", s_leader[24];
static int  s_hp, s_mhp = 1, s_mp, s_mmp = 1;

void uiSetVitals(const char* leader, const char* hpLabel, int hp, int mhp,
                 const char* mpLabel, int mp, int mmp) {
    snprintf(s_leader, sizeof(s_leader), "%s", leader ? leader : "");
    snprintf(s_hpLabel, sizeof(s_hpLabel), "%s", hpLabel && hpLabel[0] ? hpLabel : "HP");
    snprintf(s_mpLabel, sizeof(s_mpLabel), "%s", mpLabel && mpLabel[0] ? mpLabel : "MP");
    s_hp = hp; s_mhp = mhp > 0 ? mhp : 1;
    s_mp = mp; s_mmp = mmp > 0 ? mmp : 1;
}
static char s_hStage[16], s_mStage[16];
static int  s_px, s_py;

static char s_enemy[24];
static int  s_ehp, s_emax = 1;
static char s_parts[8][12];
static int  s_partCount, s_partCursor;
static char s_weapon[24];
static int  s_php, s_pmax = 1;

#define MSG_MAX_LINES 16
#define MSG_MAX_CHOICES 8
static char s_msgSpeaker[52];
static char s_msgLines[MSG_MAX_LINES][52];
static int  s_msgN;
static char s_msgChoices[MSG_MAX_CHOICES][52];
static int  s_msgChoiceN, s_msgCursor;

static char s_title[52];
static int  s_titleCursor;
static bool s_titleContinue;
static bool s_titleHasArt;

static char s_invNames[INV_MAX][20];
static int  s_invCounts[INV_MAX];
static int  s_invN, s_invCursor;

// --- text renderer: each glyph pixel-run becomes one rect ---

static void drawGlyph(int x, int y, int scale, u32 col, u8 gi) {
    const u8* rows = FONT5X7[gi];
    for (int r = 0; r < 7; r++) {
        u8 m = rows[r];
        if (!m) continue;
        int c = 0;
        while (c < 5) {
            if (m & (0x10 >> c)) {
                int start = c;
                while (c < 5 && (m & (0x10 >> c))) c++;
                C2D_DrawRectSolid(
                    x + start * scale,
                    y + r * scale,
                    0.5f,
                    (c - start) * scale,
                    scale,
                    col
                );
            } else {
                c++;
            }
        }
    }
}

// scale 1 = 6px per char, 7px tall. scale 2 = 12px per char, 14px tall.
static void text(int x, int y, int scale, u32 col, const char* str) {
    for (const char* p = str; *p; p++) {
        char c = *p;
        if (c < 32 || c > 126) c = '?';
        drawGlyph(x, y, scale, col, (u8)(c - 32));
        x += 6 * scale;
    }
}

static void textf(int x, int y, int scale, u32 col, const char* fmt, ...) {
    char tmp[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    text(x, y, scale, col, tmp);
}

static void bar(float x, float y, float w, float h, int val, int max, u32 fill) {
    if (max < 1) max = 1;
    if (val < 0) val = 0;
    if (val > max) val = max;
    float f = (float)val / (float)max;
    C2D_DrawRectSolid(x - 1, y - 1, 0.5f, w + 2, h + 2, COL_FRAME);
    C2D_DrawRectSolid(x, y, 0.5f, w, h, COL_BARBG);
    if (f > 0.0f)
        C2D_DrawRectSolid(x, y, 0.5f, w * f, h, fill);
}

static void hline(float y) {
    C2D_DrawRectSolid(8, y, 0.5f, 304, 1, COL_FRAME);
}

// --- public API (unchanged) ---

bool uiInit(void) {
    s_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    for (int i = 0; i < LOG_LINES; i++)
        s_log[i][0] = 0;
    return s_bottom != NULL;
}

void uiExit(void) {
    s_bottom = NULL; // freed by C2D_Fini()
}

void uiSetMode(UIMode mode) { s_mode = mode; }

void uiLog(const char* fmt, ...) {
    for (int i = 0; i < LOG_LINES - 1; i++)
        memcpy(s_log[i], s_log[i + 1], LOG_LEN);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_log[LOG_LINES - 1], LOG_LEN, fmt, ap);
    va_end(ap);
}

void uiSetSurvival(int hunger, const char* hungerStage,
                   int mind, const char* mindStage) {
    s_hunger = hunger;
    s_mind = mind;
    snprintf(s_hStage, sizeof(s_hStage), "%s", hungerStage);
    snprintf(s_mStage, sizeof(s_mStage), "%s", mindStage);
}

void uiSetPos(int x, int y) { s_px = x; s_py = y; }

static char s_status[52];
void uiSetStatus(const char* status) {
    snprintf(s_status, sizeof(s_status), "%s", status ? status : "");
}

void uiSetCombat(const char* enemyName, int ehp, int emax,
                 const char* const* partNames, int partCount, int partCursor,
                 const char* weaponName, int playerHp, int playerMax) {
    snprintf(s_enemy, sizeof(s_enemy), "%s", enemyName);
    s_ehp = ehp; s_emax = emax;
    if (partCount > 8) partCount = 8;
    s_partCount = partCount;
    s_partCursor = partCursor;
    for (int i = 0; i < partCount; i++)
        snprintf(s_parts[i], sizeof(s_parts[i]), "%s", partNames[i]);
    snprintf(s_weapon, sizeof(s_weapon), "%s", weaponName);
    s_php = playerHp; s_pmax = playerMax;
}

void uiSetInventory(const char* const* names, const int* counts,
                    int n, int cursor) {
    if (n > INV_MAX) n = INV_MAX;
    s_invN = n;
    s_invCursor = cursor;
    for (int i = 0; i < n; i++) {
        snprintf(s_invNames[i], sizeof(s_invNames[i]), "%s", names[i]);
        s_invCounts[i] = counts[i];
    }
}

void uiSetMessage(const char *speaker, const char *const *lines, int lineCount,
                  const char *const *choices, int choiceCount, int cursor) {
    snprintf(s_msgSpeaker, sizeof(s_msgSpeaker), "%s", speaker ? speaker : "");
    if (lineCount > MSG_MAX_LINES) lineCount = MSG_MAX_LINES;
    if (choiceCount > MSG_MAX_CHOICES) choiceCount = MSG_MAX_CHOICES;
    s_msgN = lineCount;
    for (int i = 0; i < lineCount; i++)
        snprintf(s_msgLines[i], sizeof(s_msgLines[i]), "%s", lines[i]);
    s_msgChoiceN = choiceCount;
    for (int i = 0; i < choiceCount; i++)
        snprintf(s_msgChoices[i], sizeof(s_msgChoices[i]), "%s", choices[i]);
    s_msgCursor = cursor;
}

// --- screens ---

static void drawField(void) {
    // Message log, newest brightest at the bottom.
    for (int i = 0; i < LOG_LINES; i++) {
        u32 col = (i == LOG_LINES - 1) ? COL_TEXT
                : (i == LOG_LINES - 2) ? COL_DIM : COL_FRAME;
        text(8, 6 + i * 12, 1, col, s_log[i]);
    }


    hline(56);

    text(8, 62, 1, COL_AMBER, s_leader);
    text(8, 76, 2, COL_TEXT, s_hpLabel);
    bar(64, 76, 170, 12, s_hp, s_mhp, COL_HP);
    textf(240, 78, 1, COL_DIM, "%d/%d", s_hp, s_mhp);

    text(8, 98, 2, COL_TEXT, s_mpLabel);
    bar(64, 98, 170, 12, s_mp, s_mmp, COL_MIND);
    textf(240, 100, 1, COL_DIM, "%d/%d", s_mp, s_mmp);

    if (s_status[0])
        text(8, 172, 1, COL_AMBER, s_status);
    textf(8, 184, 1, COL_FRAME, "x%d y%d", s_px, s_py);

    hline(196);
    text(8, 206, 1, COL_DIM, "D-pad:move B:run A:check");
    text(8, 218, 1, COL_DIM, "L+R:test fight SEL:items L+B:stop event");
}

static void drawCombat(void) {
    textf(8, 6, 2, COL_ENEMY, "%s", s_enemy);
    textf(268, 8, 1, COL_DIM, "%d/%d", s_ehp, s_emax);
    bar(8, 24, 304, 10, s_ehp, s_emax, COL_EHP);

    hline(40);
    text(8, 48, 1, COL_DIM,  s_log[LOG_LINES - 2]);
    text(8, 60, 1, COL_TEXT, s_log[LOG_LINES - 1]);

    // Body part grid: 3 per row, cursor highlighted.
    for (int i = 0; i < s_partCount; i++) {
        int cx = 8 + (i % 3) * 104;
        int cy = 76 + (i / 3) * 14;
        if (i == s_partCursor) {
            C2D_DrawRectSolid(cx - 2, cy - 2, 0.5f, 96, 11, COL_SELBG);
            text(cx, cy, 1, COL_AMBER, s_parts[i]);
        } else {
            text(cx, cy, 1, COL_TEXT, s_parts[i]);
        }
    }

    textf(8, 110, 1, COL_DIM, "Weapon: %s (UP:switch)", s_weapon);

    text(8, 130, 2, COL_TEXT, "YOU");
    bar(64, 130, 180, 12, s_php, s_pmax, COL_HP);
    textf(252, 132, 1, COL_DIM, "%d/%d", s_php, s_pmax);

    textf(8, 150, 1, COL_DIM, "Mind %d (%s)", s_mind, s_mStage);

    hline(196);
    text(8, 208, 1, COL_DIM, "< >:target A:attack B:flee");
}

static void drawInventory(void) {
    text(8, 6, 2, COL_TEXT, "INVENTORY");
    textf(200, 8, 1, COL_DIM, "%s %d %s %d", s_hpLabel, s_hp, s_mpLabel, s_mp);
    hline(24);

    if (s_invN == 0) {
        text(18, 40, 1, COL_DIM, "(empty - go loot crates!)");
    } else {
        int first = 0;
        if (s_invN > INV_ROWS) {
            first = s_invCursor - 4;
            if (first < 0) first = 0;
            if (first > s_invN - INV_ROWS) first = s_invN - INV_ROWS;
        }
        for (int r = 0; r < INV_ROWS && first + r < s_invN; r++) {
            int i = first + r;
            int y = 32 + r * 14;
            bool sel = (i == s_invCursor);
            if (sel) {
                C2D_DrawRectSolid(6, y - 2, 0.5f, 308, 11, COL_SELBG);
                text(8, y, 1, COL_AMBER, ">");
            }
            text(18, y, 1, sel ? COL_AMBER : COL_TEXT, s_invNames[i]);
            textf(292, y, 1, COL_DIM, "x%d", s_invCounts[i]);
        }
    }

    hline(176);
    text(8, 184, 1, COL_TEXT, s_log[LOG_LINES - 1]);
    hline(196);
    text(8, 208, 1, COL_DIM, "UP/DOWN:choose A:use/equip B:close");
}

void uiSetTitle(const char* gameTitle, int cursor, bool canContinue, bool hasArt) {
    s_titleHasArt = hasArt;
    snprintf(s_title, sizeof(s_title), "%s", gameTitle ? gameTitle : "");
    s_titleCursor = cursor;
    s_titleContinue = canContinue;
}

static void drawTitle(void) {
    // Game name, centred; scale 2 if it fits (12px per char), else scale 1.
    int len = (int)strlen(s_title);
    int scale = (len * 12 <= 304) ? 2 : 1;
    int w = len * 6 * scale;
    text((320 - w) / 2, 40, scale, COL_TEXT, s_title);

    static const char* items[3] = { "New Game", "Continue", "Quit" };
    for (int i = 0; i < 3; i++) {
        int y = 100 + i * 26;
        bool sel = (i == s_titleCursor);
        bool enabled = (i != 1) || s_titleContinue;
        int tw = (int)strlen(items[i]) * 12;
        int x = (320 - tw) / 2;
        if (sel)
            C2D_DrawRectSolid(x - 10, y - 4, 0.5f, tw + 20, 22, COL_SELBG);
        text(x, y, 2, !enabled ? COL_FRAME : sel ? COL_AMBER : COL_TEXT, items[i]);
    }

    if (!s_titleHasArt)
        text(8, 190, 1, COL_DIM, "(title.t3x not found on SD)");

    hline(206);
    text(8, 214, 1, COL_DIM, "UP/DOWN:choose A:select");
}

static char s_goCause[64], s_goName[24];
static int s_goHp, s_goMaxHp, s_goMp, s_goMaxMp;

void uiSetGameOver(const char* cause, const char* leaderName,
                   int hp, int maxHp, int mp, int maxMp) {
    snprintf(s_goCause, sizeof(s_goCause), "%s", cause ? cause : "");
    snprintf(s_goName, sizeof(s_goName), "%s", leaderName ? leaderName : "");
    s_goHp = hp; s_goMaxHp = maxHp; s_goMp = mp; s_goMaxMp = maxMp;
}

static void drawGameOver(void) {
    const char* t = "GAME OVER";
    text((320 - (int)strlen(t) * 12) / 2, 40, 2, COL_ENEMY, t);

    hline(80);
    text(8, 90, 1, COL_DIM, "Triggered by:");
    text(8, 102, 1, COL_TEXT, s_goCause[0] ? s_goCause : "(unknown)");
    if (s_goName[0]) {
        textf(8, 124, 1, COL_DIM, "%s  HP %d/%d  MP %d/%d",
              s_goName, s_goHp, s_goMaxHp, s_goMp, s_goMaxMp);
    }
    // Last log lines, so the reason the game gave stays visible.
    text(8, 146, 1, COL_DIM, s_log[LOG_LINES - 2]);
    text(8, 158, 1, COL_DIM, s_log[LOG_LINES - 1]);

    hline(206);
    text(8, 214, 1, COL_DIM, "A: back to title");
}

static const BattleView* s_bview;
void uiSetBattle(const BattleView* view) { s_bview = view; }

static void drawBattle(void) {
    const BattleView* v = s_bview;
    if (!v) return;

    // Battle log, newest at the bottom.
    for (int i = 0; i < BT_LOG_LINES; i++)
        text(8, 6 + i * 11, 1, i == BT_LOG_LINES - 1 ? COL_TEXT : COL_DIM, v->log[i]);
    hline(52);

    // Party: name, HP bar, MP.
    for (int i = 0; i < v->actorCount; i++) {
        int y = 58 + i * 14;
        u32 c = v->actorDead[i] ? COL_FRAME : (i == v->activeActor ? COL_AMBER : COL_TEXT);
        text(8, y, 1, c, v->actorName[i]);
        bar(110, y, 100, 6, v->hp[i], v->mhp[i], COL_HP);
        textf(216, y, 1, COL_DIM, "%d/%d", v->hp[i], v->mhp[i]);
        textf(274, y, 1, COL_MIND, "%d", v->mp[i]);
    }
    hline(116);

    // Command / skill / item / target menu.
    if (v->menuCount > 0) {
        text(8, 122, 1, COL_DIM, v->menuTitle);
        int rows = 5, first = 0;
        if (v->menuCount > rows) {
            first = v->menuCursor - 2;
            if (first < 0) first = 0;
            if (first > v->menuCount - rows) first = v->menuCount - rows;
        }
        for (int r = 0; r < rows && first + r < v->menuCount; r++) {
            int i = first + r;
            int y = 136 + r * 12;
            bool sel = (i == v->menuCursor);
            if (sel) {
                C2D_DrawRectSolid(6, y - 2, 0.5f, 308, 11, COL_SELBG);
                text(8, y, 1, COL_AMBER, ">");
            }
            u32 c = !v->menuEnabled[i] ? COL_FRAME : sel ? COL_AMBER : COL_TEXT;
            text(18, y, 1, c, v->menuItems[i]);
        }
    }

    hline(206);
    text(8, 214, 1, COL_DIM, v->hint);
}

static void drawMessage(void) {
    int y = 6;
    if (s_msgSpeaker[0]) {
        text(8, y, 1, COL_AMBER, s_msgSpeaker);
        y += 12;
        hline(y);
        y += 5;
    }
    // Keep room for the choices and the hint line.
    int maxY = 196 - s_msgChoiceN * 13 - (s_msgChoiceN ? 6 : 0);
    for (int i = 0; i < s_msgN && y + 7 <= maxY; i++) {
        text(8, y, 1, COL_TEXT, s_msgLines[i]);
        y += 11;
    }
    if (s_msgChoiceN) {
        y += 6;
        for (int i = 0; i < s_msgChoiceN; i++) {
            bool sel = (i == s_msgCursor);
            if (sel) {
                C2D_DrawRectSolid(6, y - 2, 0.5f, 308, 11, COL_SELBG);
                text(8, y, 1, COL_AMBER, ">");
            }
            text(18, y, 1, sel ? COL_AMBER : COL_TEXT, s_msgChoices[i]);
            y += 13;
        }
    }
    hline(206);
    text(8, 214, 1, COL_DIM, s_msgChoiceN ? "UP/DOWN:choose A:select B:cancel"
                                          : "A:continue");
}

void uiRender(void) {
    if (!s_bottom) return;
    C2D_TargetClear(s_bottom, COL_BG);
    C2D_SceneBegin(s_bottom);
    text(8, 222, 2, RGBA8(255, 255, 255), "HELLO 3DS");

    switch (s_mode) {
        case UI_MODE_COMBAT:    drawCombat();    break;
        case UI_MODE_INVENTORY: drawInventory(); break;
        case UI_MODE_MESSAGE:   drawMessage();   break;
        case UI_MODE_TITLE:     drawTitle();     break;
        case UI_MODE_GAMEOVER:  drawGameOver();  break;
        case UI_MODE_BATTLE:    drawBattle();    break;
        default:                drawField();     break;
    }
}