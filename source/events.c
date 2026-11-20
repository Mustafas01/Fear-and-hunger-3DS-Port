// ---------------------------------------------------------------------------
// Fear & Hunger 3DS port - event system (events.c)
//
//   1. Data structures
//   2. Minimal JSON readers (params arrays, page conditions)
//   3. Loading compiled event files into memory
//   4. Page selection
//   5. Resumable interpreter
//   6. Triggers
//   7. Message window
//   8. Self-tests
//
// Everything game-specific comes from the compiled files on the SD card.
// ---------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "msgtext.h"
#include "rng.h"
#include "actors.h"
#include "gamedata.h"
#include "battle.h"

// ===========================================================================
// 1. Data structures
// ===========================================================================

#define FH_MAX_PARAMS 12
#define FH_MAX_INDENT 32
#define FH_MAX_FRAMES 16
#define FH_MAX_CHOICES 8
#define FH_MSG_LINES 16

typedef struct {
    uint16_t code;
    int16_t indent;
    uint8_t paramCount;
    int32_t params[FH_MAX_PARAMS];  // positional; strings/arrays occupy a 0 slot
    int16_t sub[4];                 // first nested numeric array (tint colours)
    char *str;                      // first string, or choices joined by '\n'
} EventCommand;

typedef struct {
    bool switch1Valid, switch2Valid, variableValid;
    bool selfSwitchValid, itemValid, actorValid;
    uint16_t switch1Id, switch2Id, variableId, itemId, actorId;
    int32_t variableValue;
    char selfSwitchCh;
    uint8_t trigger;     // map: 0 action 1 player touch 2 event touch 3 autorun 4 parallel
                         // common: 0 none 1 autorun 2 parallel
    uint8_t priority;    // 0 below, 1 same as characters, 2 above
    bool through;
    uint16_t switchId;   // common event trigger switch
    // graphic + autonomous movement (build_events.py)
    char imgName[48];
    uint8_t imgIndex, imgDir, imgPattern;
    uint16_t tileId;
    uint8_t moveType, moveSpeed, moveFreq;
    bool walkAnime, stepAnime, dirFix;
    char *route;         // "code:p0:p1;..." (custom move route), may be NULL
    bool routeRepeat, routeSkip;
    // troop battle event pages
    bool turnEnding, turnValid, enemyValid, switchValid;
    int16_t turnA, turnB, enemyIndex, enemyHp, actorHp;
    uint8_t span;
} PageConditions;

// Live state of a map event as a character on the map (Game_Event).
typedef struct {
    int page;                // page the character was set up for (-2 = none yet)
    float rx, ry;            // real position in tiles (interpolates to x, y)
    uint8_t dir;             // 2 down 4 left 6 right 8 up
    uint8_t pattern, origPattern;
    float anim;
    int stop;                // frames standing still
    char img[48];
    uint8_t imgIndex;
    uint16_t tileId;
    uint8_t speed, freq;
    bool walkAnime, stepAnime, dirFix, through, transparent;
    // custom route (page) and forced route (Set Move Route, 205)
    const char *route;
    bool routeRepeat, routeSkip;
    int routeIdx, routeWait;
    char *forced;
    bool forcedRepeat, forcedSkip, forcedWaitFor;
    int forcedIdx;
} CharState;

typedef struct {
    PageConditions cond;
    EventCommand *cmds;
    uint16_t count;
} EventPage;

typedef struct {
    uint16_t id, x, y, pageCount;
    EventPage *pages;
    bool erased;
    CharState ch;
} MapEvent;

typedef struct {
    uint16_t mapId;
    uint32_t count;
    MapEvent *events;
} EventFile;

static EventFile s_map, s_common, s_troopEv;
// After Transfer Player the running script still points into the previous
// map's commands, so that map is kept until the main interpreter finishes.
static EventFile s_oldMap;

// ===========================================================================
// 2. Minimal JSON readers
// ===========================================================================

static const char *skipWs(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

static const char *skipBracketed(const char *p) {
    int depth = 1;
    p++;
    while (depth > 0 && *p) {
        if (*p == '"') {
            p++;
            while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
            if (*p != '"') return NULL;
            p++;
            continue;
        }
        if (*p == '[' || *p == '{') depth++;
        else if (*p == ']' || *p == '}') depth--;
        p++;
    }
    return depth == 0 ? p : NULL;
}

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Reads a JSON string at p (opening quote). Unescapes into buf (may be
// NULL). Non-ASCII \uXXXX becomes '?'. Returns pointer past the quote.
static const char *readJsonString(const char *p, char *buf, size_t bufSize, size_t *outLen) {
    if (*p != '"') return NULL;
    p++;
    size_t n = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) {
            char e = *p++;
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = ' '; break;
                case 'r': c = '\r'; break;
                case 'b': case 'f': c = ' '; break;
                case 'u': {
                    int v = 0;
                    for (int k = 0; k < 4; k++) {
                        int h = hexVal(*p);
                        if (h < 0) return NULL;
                        v = v * 16 + h;
                        p++;
                    }
                    c = (v >= 32 && v < 127) ? (char)v : (v == '\n' ? '\n' : '?');
                    break;
                }
                default: c = e; break;   // \" \\ \/
            }
        }
        if (buf && n + 1 < bufSize) buf[n] = c;
        n++;
    }
    if (*p != '"') return NULL;
    if (buf && bufSize) buf[n < bufSize ? n : bufSize - 1] = '\0';
    if (outLen) *outLen = n;
    return p + 1;
}

static const char *readJsonNumber(const char *p, int32_t *out) {
    int neg = 0;
    int64_t v = 0;
    bool any = false;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') { if (v < 0x7FFFFFFF) v = v * 10 + (*p - '0'); any = true; p++; }
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
    if (*p == 'e' || *p == 'E') { p++; if (*p == '+' || *p == '-') p++; while (*p >= '0' && *p <= '9') p++; }
    if (!any) return NULL;
    if (v > 0x7FFFFFFF) v = 0x7FFFFFFF;
    if (out) *out = (int32_t)(neg ? -v : v);
    return p;
}

static char *dupString(const char *p, const char **end) {
    size_t len = 0;
    if (!readJsonString(p, NULL, 0, &len)) return NULL;
    char *s = malloc(len + 1);
    if (!s) return NULL;
    *end = readJsonString(p, s, len + 1, NULL);
    return s;
}

// Parses a nested array: strings are joined into cmd->str with '\n'
// (Show Choices), numbers fill cmd->sub (Tint Screen colour).
static const char *parseNested(const char *p, EventCommand *cmd, bool first) {
    const char *start = p;
    p++;
    char joined[1024];
    size_t jn = 0;
    int nums = 0;
    bool anyString = false;

    for (;;) {
        p = skipWs(p);
        while (*p == ',') p = skipWs(p + 1);
        if (!*p) return NULL;
        if (*p == ']') { p++; break; }
        if (*p == '"') {
            char tmp[256];
            p = readJsonString(p, tmp, sizeof(tmp), NULL);
            if (!p) return NULL;
            if (first) {
                if (anyString && jn + 1 < sizeof(joined)) joined[jn++] = '\n';
                for (char *t = tmp; *t && jn + 1 < sizeof(joined); t++) joined[jn++] = *t;
            }
            anyString = true;
        } else if (*p == '[' || *p == '{') {
            p = skipBracketed(p);
            if (!p) return NULL;
        } else if (*p == 't' || *p == 'f' || *p == 'n') {
            while (*p >= 'a' && *p <= 'z') p++;
        } else {
            int32_t v;
            p = readJsonNumber(p, &v);
            if (!p) return NULL;
            if (first && nums < 4) cmd->sub[nums] = (int16_t)v;
            nums++;
        }
    }
    if (first && anyString && !cmd->str) {
        joined[jn] = '\0';
        cmd->str = malloc(jn + 1);
        if (cmd->str) memcpy(cmd->str, joined, jn + 1);
    }
    (void)start;
    return p;
}

// Positional parse of a command's params array.
static bool parseParams(const char *json, EventCommand *cmd) {
    const char *p = skipWs(json);
    if (*p != '[') return false;
    p++;
    bool firstNested = true;

    for (;;) {
        p = skipWs(p);
        while (*p == ',') p = skipWs(p + 1);
        if (!*p) return false;
        if (*p == ']') return true;

        int32_t value = 0;
        if (*p == '"') {
            if (!cmd->str) {
                const char *end = NULL;
                cmd->str = dupString(p, &end);
                p = end;
            } else {
                p = readJsonString(p, NULL, 0, NULL);
            }
            if (!p) return false;
        } else if (*p == '[') {
            p = parseNested(p, cmd, firstNested);
            firstNested = false;
            if (!p) return false;
        } else if (*p == '{') {
            p = skipBracketed(p);
            if (!p) return false;
        } else if (*p == 't' || *p == 'f' || *p == 'n') {
            value = (*p == 't');
            while (*p >= 'a' && *p <= 'z') p++;
        } else {
            p = readJsonNumber(p, &value);
            if (!p) return false;
        }
        if (cmd->paramCount < FH_MAX_PARAMS)
            cmd->params[cmd->paramCount++] = value;
    }
}

static bool parsePageConditions(const char *json, PageConditions *out) {
    memset(out, 0, sizeof(*out));
    out->priority = 1;
    out->imgDir = 2; out->imgPattern = 1;
    out->moveSpeed = 3; out->moveFreq = 3;
    out->walkAnime = true;
    out->routeRepeat = true;
    const char *p = skipWs(json);
    if (*p != '{') return false;
    p++;
    for (;;) {
        p = skipWs(p);
        while (*p == ',') p = skipWs(p + 1);
        if (!*p) return false;
        if (*p == '}') return true;

        char key[32];
        p = readJsonString(p, key, sizeof(key), NULL);
        if (!p) return false;
        p = skipWs(p);
        if (*p != ':') return false;
        p = skipWs(p + 1);

        if (*p == '"') {
            if (!strcmp(key, "route")) {
                const char *end = NULL;
                char *r = dupString(p, &end);
                if (!r || !end) { free(r); return false; }
                if (r[0]) out->route = r; else free(r);
                p = end;
                continue;
            }
            char str[48];
            p = readJsonString(p, str, sizeof(str), NULL);
            if (!p) return false;
            if (!strcmp(key, "selfSwitchCh") && str[0] >= 'A' && str[0] <= 'D')
                out->selfSwitchCh = str[0];
            else if (!strcmp(key, "imgName"))
                snprintf(out->imgName, sizeof(out->imgName), "%s", str);
        } else if (*p == 't' || *p == 'f') {
            bool v = (*p == 't');
            while (*p >= 'a' && *p <= 'z') p++;
            if (!strcmp(key, "switch1Valid")) out->switch1Valid = v;
            else if (!strcmp(key, "switch2Valid")) out->switch2Valid = v;
            else if (!strcmp(key, "variableValid")) out->variableValid = v;
            else if (!strcmp(key, "selfSwitchValid")) out->selfSwitchValid = v;
            else if (!strcmp(key, "itemValid")) out->itemValid = v;
            else if (!strcmp(key, "actorValid")) out->actorValid = v;
            else if (!strcmp(key, "through")) out->through = v;
            else if (!strcmp(key, "walkAnime")) out->walkAnime = v;
            else if (!strcmp(key, "stepAnime")) out->stepAnime = v;
            else if (!strcmp(key, "dirFix")) out->dirFix = v;
            else if (!strcmp(key, "routeRepeat")) out->routeRepeat = v;
            else if (!strcmp(key, "routeSkip")) out->routeSkip = v;
            else if (!strcmp(key, "turnEnding")) out->turnEnding = v;
            else if (!strcmp(key, "turnValid")) out->turnValid = v;
            else if (!strcmp(key, "enemyValid")) out->enemyValid = v;
            else if (!strcmp(key, "switchValid")) out->switchValid = v;
        } else if (*p == 'n') {
            while (*p >= 'a' && *p <= 'z') p++;
        } else if (*p == '[' || *p == '{') {
            p = skipBracketed(p);
            if (!p) return false;
        } else {
            int32_t v = 0;
            p = readJsonNumber(p, &v);
            if (!p) return false;
            if (!strcmp(key, "switch1Id")) out->switch1Id = (uint16_t)v;
            else if (!strcmp(key, "switch2Id")) out->switch2Id = (uint16_t)v;
            else if (!strcmp(key, "variableId")) out->variableId = (uint16_t)v;
            else if (!strcmp(key, "variableValue")) out->variableValue = v;
            else if (!strcmp(key, "itemId")) out->itemId = (uint16_t)v;
            else if (!strcmp(key, "actorId")) out->actorId = (uint16_t)v;
            else if (!strcmp(key, "trigger")) out->trigger = (uint8_t)v;
            else if (!strcmp(key, "priority")) out->priority = (uint8_t)v;
            else if (!strcmp(key, "switchId")) out->switchId = (uint16_t)v;
            else if (!strcmp(key, "imgIndex")) out->imgIndex = (uint8_t)v;
            else if (!strcmp(key, "imgDir")) out->imgDir = (uint8_t)v;
            else if (!strcmp(key, "imgPattern")) out->imgPattern = (uint8_t)v;
            else if (!strcmp(key, "tileId")) out->tileId = (uint16_t)v;
            else if (!strcmp(key, "moveType")) out->moveType = (uint8_t)v;
            else if (!strcmp(key, "moveSpeed")) out->moveSpeed = (uint8_t)v;
            else if (!strcmp(key, "moveFreq")) out->moveFreq = (uint8_t)v;
            else if (!strcmp(key, "turnA")) out->turnA = (int16_t)v;
            else if (!strcmp(key, "turnB")) out->turnB = (int16_t)v;
            else if (!strcmp(key, "enemyIndex")) out->enemyIndex = (int16_t)v;
            else if (!strcmp(key, "enemyHp")) out->enemyHp = (int16_t)v;
            else if (!strcmp(key, "actorHp")) out->actorHp = (int16_t)v;
            else if (!strcmp(key, "span")) out->span = (uint8_t)v;
        }
    }
}

// ===========================================================================
// 3. Loading
// ===========================================================================

static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void freeFile(EventFile *f) {
    for (uint32_t e = 0; e < f->count; e++) {
        MapEvent *ev = &f->events[e];
        for (uint16_t p = 0; p < ev->pageCount; p++) {
            EventPage *pg = &ev->pages[p];
            free(pg->cond.route);
            for (uint16_t c = 0; c < pg->count; c++) free(pg->cmds[c].str);
            free(pg->cmds);
        }
        free(ev->pages);
        free(ev->ch.forced);
    }
    free(f->events);
    memset(f, 0, sizeof(*f));
}

// Scratch buffer for NUL-terminating JSON blobs.
static char *s_scratch;
static size_t s_scratchSize;

static const char *terminated(const unsigned char *src, uint32_t len) {
    if (len + 1 > s_scratchSize) {
        size_t n = len + 1 < 4096 ? 4096 : len + 1;
        char *b = realloc(s_scratch, n);
        if (!b) return NULL;
        s_scratch = b;
        s_scratchSize = n;
    }
    memcpy(s_scratch, src, len);
    s_scratch[len] = '\0';
    return s_scratch;
}

static bool loadFile(const char *path, EventFile *f) {
    freeFile(f);
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 12) { fclose(fp); return false; }
    unsigned char *buf = malloc((size_t)size);
    if (!buf) { fclose(fp); return false; }
    bool ok = fread(buf, 1, (size_t)size, fp) == (size_t)size;
    fclose(fp);

    const unsigned char *p = buf, *end = buf + size;
    #define NEED(n) do { if (p + (n) > end) { ok = false; goto done; } } while (0)

    if (!ok || memcmp(p, "FHEC", 4) != 0) { ok = false; goto done; }
    f->mapId = rd16(p + 6);
    f->count = rd32(p + 8);
    p += 12;
    f->events = calloc(f->count ? f->count : 1, sizeof(MapEvent));
    if (!f->events) { ok = false; goto done; }

    for (uint32_t e = 0; e < f->count; e++) {
        NEED(8);
        MapEvent *ev = &f->events[e];
        ev->id = rd16(p); ev->x = rd16(p + 2); ev->y = rd16(p + 4);
        ev->pageCount = rd16(p + 6);
        ev->ch.page = -2;
        ev->ch.rx = ev->x; ev->ch.ry = ev->y;
        ev->ch.dir = 2;
        p += 8;
        ev->pages = calloc(ev->pageCount ? ev->pageCount : 1, sizeof(EventPage));
        if (!ev->pages) { ok = false; goto done; }

        for (uint16_t pi = 0; pi < ev->pageCount; pi++) {
            NEED(8);
            EventPage *pg = &ev->pages[pi];
            uint16_t count = rd16(p + 2);
            uint32_t condLen = rd32(p + 4);
            p += 8;
            NEED(condLen);
            const char *cj = terminated(p, condLen);
            if (!cj || !parsePageConditions(cj, &pg->cond)) {
                // Missing conditions = always-true page (old files).
                free(pg->cond.route);
                memset(&pg->cond, 0, sizeof(pg->cond));
                pg->cond.priority = 1;
                pg->cond.imgDir = 2; pg->cond.imgPattern = 1;
                pg->cond.moveSpeed = 3; pg->cond.moveFreq = 3;
            }
            p += condLen;

            pg->cmds = calloc(count ? count : 1, sizeof(EventCommand));
            if (!pg->cmds) { ok = false; goto done; }
            pg->count = count;

            for (uint16_t c = 0; c < count; c++) {
                NEED(8);
                EventCommand *cmd = &pg->cmds[c];
                cmd->code = rd16(p);
                cmd->indent = (int16_t)rd16(p + 2);
                uint32_t len = rd32(p + 4);
                p += 8;
                NEED(len);
                const char *js = terminated(p, len);
                if (len && js) parseParams(js, cmd);   // tolerate odd params
                p += len;
            }
        }
    }
done:
    #undef NEED
    free(buf);
    if (!ok) freeFile(f);
    return ok;
}

uint16_t eventsMapId(void) { return s_map.mapId; }
uint32_t eventsCount(void) { return s_map.count; }

// ===========================================================================
// 4. Page selection
// ===========================================================================

static bool pageOk(const PageConditions *c, GameState *s, uint16_t mapId, uint16_t eventId) {
    if (c->switch1Valid && !runtimeGetSwitch(s, c->switch1Id)) return false;
    if (c->switch2Valid && !runtimeGetSwitch(s, c->switch2Id)) return false;
    // MV page variable condition is "variable >= value".
    if (c->variableValid && runtimeGetVariable(s, c->variableId) < c->variableValue) return false;
    if (c->selfSwitchValid &&
        !runtimeGetSelfSwitch(s, mapId, eventId, c->selfSwitchCh - 'A')) return false;
    if (c->itemValid && runtimeGetOwned(s, DB_ITEM, c->itemId) <= 0) return false;
    if (c->actorValid && (c->actorId > FH_MAX_ACTORS || !s->inParty[c->actorId])) return false;
    return true;
}

// Highest-numbered page whose conditions pass, or -1 (event is absent).
static int activePage(const MapEvent *ev, GameState *s) {
    if (ev->erased) return -1;
    for (int p = ev->pageCount - 1; p >= 0; p--)
        if (pageOk(&ev->pages[p].cond, s, s_map.mapId, ev->id)) return p;
    return -1;
}

// ===========================================================================
// 5. Resumable interpreter
// ===========================================================================

typedef struct {
    const EventCommand *cmds;
    uint16_t count;
    uint32_t pc;
    uint16_t eventId;            // map event that owns this script (0 = none)
    uint16_t mapId;              // map the script started on (self switches)
    MapEvent *owner;             // event/common event that started it (root only)
    bool branch[FH_MAX_INDENT];  // last If result per indent
} Frame;

enum { W_NONE, W_FRAMES, W_MESSAGE, W_BATTLE, W_TRANSFER, W_ROUTE, W_NAME };

// One interpreter = one call stack + wait state. The main interpreter runs
// action/touch/autorun events (and freezes the player); every parallel map
// event and parallel common event gets its own, like RPG Maker MV.
typedef struct {
    Frame stack[FH_MAX_FRAMES];
    int depth;
    int wait, waitFrames;
    int32_t choicePc;    // 102 awaiting a result (-1 none)
    int32_t battlePc;    // 301 awaiting a result (-1 none)
    int page;            // parallel: page this interpreter was set up for
    MapEvent *routeEv;   // W_ROUTE: waiting for this event's forced route
} Interp;

static Interp s_main = { .choicePc = -1, .battlePc = -1, .page = -1 };
static Interp s_battleInterp = { .choicePc = -1, .battlePc = -1, .page = -1 };
static Interp *I = &s_main;          // interpreter currently executing
static Interp *s_mapPar;             // one per map event (parallel)
static Interp *s_commonPar;          // one per common event (parallel)
static Interp *s_msgOwner, *s_battleOwner, *s_xferOwner;

// Player info fed by main.c, for conditions and character data.
static int s_playerX, s_playerY, s_playerDir;   // dir: 2/4/6/8 like MV
static uint32_t s_buttons;

// Pictures (Show/Move/Tint/Erase Picture). Coordinates in MV screen pixels.
static EventPicture s_pics[FH_MAX_PICTURES + 1];
static bool s_gameOver;
static bool s_toTitle;           // 354 Return to Title (no Game Over screen)
static char s_gameOverWho[48];   // which event called it, for diagnostics
static int s_tint[4];
static void (*s_gab)(const char *) = NULL;
static char s_gabText[256];

// Message window state
static bool s_msgOpen;
static char s_msgLines[FH_MSG_LINES][MSG_COLS + 1];
static int s_msgLineCount;
static char s_speaker[64];
static char s_choices[FH_MAX_CHOICES][MSG_COLS + 1];
static int s_choiceCount, s_choiceCursor, s_choiceCancel;
static int s_choiceResult;         // index, or FH_MAX_CHOICES for cancel branch

// Pending requests
static int s_battleTroop;
static bool s_battleEscape, s_battleLose;
static int s_xferMap, s_xferX, s_xferY, s_xferDir;
static Interp *s_nameOwner;
static int s_nameActor, s_nameMax;

static GameState *s_state;   // state used by message formatting

void eventsSetGabHandler(void (*fn)(const char *)) { s_gab = fn; }
// The player is frozen while the main interpreter runs or a message is up
// (MV: $gameMap.isEventRunning() || $gameMessage.isBusy()).
bool eventsBusy(void) { return s_main.depth > 0 || s_msgOpen; }
bool eventsGameOver(void) { return s_gameOver; }
bool eventsReturnToTitle(void) { return s_gameOver && s_toTitle; }
const char *eventsGameOverCause(void) { return s_gameOverWho; }
void eventsGetTint(int *r, int *g, int *b, int *gray) {
    *r = s_tint[0]; *g = s_tint[1]; *b = s_tint[2]; *gray = s_tint[3];
}

static MapEvent *findMapEvent(uint16_t id);

static bool pushFrame(const EventCommand *cmds, uint16_t count, uint16_t eventId) {
    if (I->depth >= FH_MAX_FRAMES || !cmds) return false;
    Frame *f = &I->stack[I->depth++];
    memset(f, 0, sizeof(*f));
    f->cmds = cmds;
    f->count = count;
    f->eventId = eventId;
    f->mapId = s_map.mapId;
    return true;
}

static bool startOwned(MapEvent *owner, const EventPage *pg, uint16_t eventId) {
    if (!pushFrame(pg->cmds, pg->count, eventId)) return false;
    I->stack[I->depth - 1].owner = owner;
    return true;
}

static Interp *makeInterps(uint32_t n) {
    Interp *a = calloc(n ? n : 1, sizeof(Interp));
    for (uint32_t i = 0; a && i < n; i++) {
        a[i].choicePc = -1; a[i].battlePc = -1; a[i].page = -1;
    }
    return a;
}

static void releaseInterps(Interp **arr) {
    if (*arr) {
        if (s_msgOwner >= *arr && s_msgOwner < *arr + 1) s_msgOwner = NULL;
        free(*arr);
        *arr = NULL;
    }
    // Ownership pointers into the freed array are no longer valid.
    s_msgOwner = (s_msgOwner == &s_main) ? s_msgOwner : NULL;
    if (s_battleOwner != &s_main) s_battleOwner = NULL;
    if (s_xferOwner != &s_main) s_xferOwner = NULL;
}

bool eventsLoadMap(const char *path) {
    releaseInterps(&s_mapPar);
    if (s_main.wait == W_ROUTE) { s_main.wait = W_NONE; s_main.routeEv = NULL; }
    freeFile(&s_oldMap);
    if (s_main.depth > 0) {
        s_oldMap = s_map;
        memset(&s_map, 0, sizeof(s_map));
    }
    bool ok = loadFile(path, &s_map);
    s_mapPar = makeInterps(s_map.count);
    return ok;
}

bool eventsLoadCommon(const char *path) {
    releaseInterps(&s_commonPar);
    bool ok = loadFile(path, &s_common);
    s_commonPar = makeInterps(s_common.count);
    return ok;
}

const EventPicture *eventsGetPicture(int id) {
    if (id < 1 || id > FH_MAX_PICTURES || !s_pics[id].used) return NULL;
    return &s_pics[id];
}

void eventsSetPlayer(int x, int y, int dir) {
    static const int toMv[4] = { 2, 4, 6, 8 };
    s_playerX = x; s_playerY = y; s_playerDir = toMv[dir & 3];
}

void eventsSetButtons(uint32_t held) { s_buttons = held; }

// MV picture animation: each frame moves 1/d of the remaining distance.
static void updatePictures(void) {
    for (int i = 1; i <= FH_MAX_PICTURES; i++) {
        EventPicture *p = &s_pics[i];
        if (!p->used) continue;
        if (p->duration > 0) {
            float d = (float)p->duration;
            p->x = (p->x * (d - 1) + p->tx) / d;
            p->y = (p->y * (d - 1) + p->ty) / d;
            p->scaleX = (p->scaleX * (d - 1) + p->tsx) / d;
            p->scaleY = (p->scaleY * (d - 1) + p->tsy) / d;
            p->opacity = (p->opacity * (d - 1) + p->topacity) / d;
            p->duration--;
        }
        if (p->toneDuration > 0) {
            float d = (float)p->toneDuration;
            for (int k = 0; k < 4; k++)
                p->tone[k] = (p->tone[k] * (d - 1) + p->toneTarget[k]) / d;
            p->toneDuration--;
        }
    }
}
static void resetInterp(Interp *in) {
    if (s_msgOwner == in) { s_msgOpen = false; s_msgOwner = NULL; }
    if (s_battleOwner == in) s_battleOwner = NULL;
    if (s_xferOwner == in) s_xferOwner = NULL;
    if (s_nameOwner == in) s_nameOwner = NULL;
    in->depth = 0;
    in->wait = W_NONE;
    in->choicePc = -1;
    in->battlePc = -1;
    in->page = -1;
}

void eventsReset(void) {
    resetInterp(&s_main);
    for (uint32_t i = 0; s_mapPar && i < s_map.count; i++) resetInterp(&s_mapPar[i]);
    for (uint32_t i = 0; s_commonPar && i < s_common.count; i++) resetInterp(&s_commonPar[i]);
    for (uint32_t i = 0; i < s_common.count; i++) s_common.events[i].erased = false;
    memset(s_pics, 0, sizeof(s_pics));
    memset(s_tint, 0, sizeof(s_tint));
    s_gameOver = s_toTitle = false;
    s_gameOverWho[0] = '\0';
    s_gabText[0] = '\0';
    s_msgOpen = false;
}

void eventsAbort(void) {
    resetInterp(&s_main);
}

// Stops one interpreter and disables the event that started it until the
// map reloads. Picks whoever is holding things up: a pending transfer,
// the open message, or else the main interpreter.
void eventsSuspendCurrent(void) {
    Interp *target = s_xferOwner ? s_xferOwner
                   : (s_msgOpen && s_msgOwner) ? s_msgOwner : &s_main;
    if (target->depth > 0 && target->stack[0].owner)
        target->stack[0].owner->erased = true;
    resetInterp(target);
}

static int countParallel(void) {
    int n = 0;
    for (uint32_t i = 0; s_mapPar && i < s_map.count; i++) n += s_mapPar[i].depth > 0;
    for (uint32_t i = 0; s_commonPar && i < s_common.count; i++) n += s_commonPar[i].depth > 0;
    return n;
}

const char *eventsDebugStatus(void) {
    static char buf[64];
    Interp *in = s_main.depth ? &s_main : NULL;
    if (!in) {
        int n = countParallel();
        if (!n) return "";
        snprintf(buf, sizeof(buf), "parallel events running: %d", n);
        return buf;
    }
    const Frame *f = &in->stack[in->depth - 1];
    const Frame *root = &in->stack[0];
    bool common = root->owner && root->eventId == 0;
    static const char *waits[] = { "run", "wait", "msg", "battle", "xfer", "route", "name" };
    unsigned code = f->pc < f->count ? f->cmds[f->pc].code : 0;
    snprintf(buf, sizeof(buf), "%s %u pc %lu/%u cmd %u %s d%d +%dp",
        common ? "CE" : "EV",
        root->owner ? root->owner->id : 0,
        (unsigned long)f->pc, f->count, code, waits[in->wait], in->depth,
        countParallel());
    return buf;
}

static const MapEvent *findCommon(int id) {
    for (uint32_t i = 0; i < s_common.count; i++)
        if (s_common.events[i].id == id) return &s_common.events[i];
    return NULL;
}

static MapEvent *findMapEvent(uint16_t id) {
    for (uint32_t i = 0; i < s_map.count; i++)
        if (s_map.events[i].id == id) return &s_map.events[i];
    return NULL;
}

// Operand helper: [operandType, operand] -> value (0 const, 1 variable).
static int32_t operandValue(GameState *s, int32_t type, int32_t operand) {
    return type == 0 ? operand : runtimeGetVariable(s, (uint16_t)operand);
}

// Character data for conditions/variables: -1 player, 0 this event, n event n.
static bool characterInfo(const Frame *f, int charId, int *x, int *y, int *dir) {
    if (charId < 0) { *x = s_playerX; *y = s_playerY; *dir = s_playerDir; return true; }
    const MapEvent *ev = findMapEvent(charId == 0 ? f->eventId : (uint16_t)charId);
    if (!ev) return false;
    *x = ev->x; *y = ev->y; *dir = ev->ch.dir ? ev->ch.dir : 2;
    return true;
}

static uint32_t buttonBit(const char *name) {
    static const struct { const char *n; uint32_t b; } map[] = {
        {"ok", EVB_OK}, {"cancel", EVB_CANCEL}, {"shift", EVB_SHIFT},
        {"down", EVB_DOWN}, {"left", EVB_LEFT}, {"right", EVB_RIGHT},
        {"up", EVB_UP}, {"pageup", EVB_PAGEUP}, {"pagedown", EVB_PAGEDOWN},
    };
    for (unsigned i = 0; name && i < sizeof(map) / sizeof(map[0]); i++)
        if (!strcmp(name, map[i].n)) return map[i].b;
    return 0;
}

static bool evalBranch(const Frame *f, const EventCommand *c, GameState *s) {
    if (c->paramCount < 1) return false;
    const int32_t *a = c->params;
    switch (a[0]) {
        case 0: return c->paramCount >= 3 && runtimeGetSwitch(s, (uint16_t)a[1]) == (a[2] == 0);
        case 1: {
            if (c->paramCount < 5) return false;
            int32_t l = runtimeGetVariable(s, (uint16_t)a[1]);
            int32_t r = operandValue(s, a[2], a[3]);
            switch (a[4]) {
                case 0: return l == r; case 1: return l >= r; case 2: return l <= r;
                case 3: return l > r;  case 4: return l < r;  case 5: return l != r;
            }
            return false;
        }
        case 2: { // self switch: [2, "A", 0=ON]
            if (!c->str || c->paramCount < 3 || c->str[0] < 'A' || c->str[0] > 'D') return false;
            bool on = runtimeGetSelfSwitch(s, f->mapId, f->eventId, c->str[0] - 'A');
            return on == (a[2] == 0);
        }
        case 4: { // actor: [4, actorId, subtype, value]
            if (c->paramCount < 3 || a[1] < 1 || a[1] > FH_MAX_ACTORS) return false;
            switch (a[2]) {
                case 0: return s->inParty[a[1]];
                case 1: return c->str && !strcmp(actorName(s, a[1]), c->str);
                case 6: return c->paramCount >= 4 && actorHasState(s, a[1], a[3]);
                default: return false;   // class, skill, weapon, armor: later
            }
        }
        case 6: { // character facing: [6, charId, dir]
            int x, y, d;
            return c->paramCount >= 3 && characterInfo(f, a[1], &x, &y, &d) && d == a[2];
        }
        case 11:  // button held: [11, "ok"]
            return (s_buttons & buttonBit(c->str)) != 0;
        case 7: { // gold: [7, amount, 0 >=, 1 <=, 2 <]
            if (c->paramCount < 3) return false;
            if (a[2] == 0) return s->gold >= a[1];
            if (a[2] == 1) return s->gold <= a[1];
            return s->gold < a[1];
        }
        case 8:  return c->paramCount >= 2 && runtimeGetOwned(s, DB_ITEM, a[1]) > 0;
        case 9:  return c->paramCount >= 2 && runtimeGetOwned(s, DB_WEAPON, a[1]) > 0;
        case 10: return c->paramCount >= 2 && runtimeGetOwned(s, DB_ARMOR, a[1]) > 0;
        default: return false;   // direction, button, script, vehicle, timer
    }
}

// Next command at `indent` with one of the given codes, scanning forward
// from `from`; stops (returns count) if the block closes first.
static uint32_t scanForward(const Frame *f, uint32_t from, int indent,
                            uint16_t a, uint16_t b, uint16_t c) {
    for (uint32_t i = from; i < f->count; i++) {
        const EventCommand *k = &f->cmds[i];
        if (k->indent < indent) return f->count;
        if (k->indent == indent && (k->code == a || k->code == b || k->code == c)) return i;
    }
    return f->count;
}

static void openMessage(const char *raw, const char *choices, int cancelType) {
    char formatted[2048];
    msgFormat(raw ? raw : "", formatted, sizeof(formatted),
              s_speaker, sizeof(s_speaker), s_state);
    msgReflow(formatted);
    s_msgLineCount = raw ? msgWrap(formatted, s_msgLines, FH_MSG_LINES) : 0;

    s_choiceCount = 0;
    if (choices) {
        const char *p = choices;
        while (*p && s_choiceCount < FH_MAX_CHOICES) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            char tmp[256], fmt[256];
            if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
            memcpy(tmp, p, len);
            tmp[len] = '\0';
            msgFormat(tmp, fmt, sizeof(fmt), NULL, 0, s_state);
            snprintf(s_choices[s_choiceCount++], MSG_COLS + 1, "%.*s", MSG_COLS, fmt);
            if (!nl) break;
            p = nl + 1;
        }
    }
    s_choiceCursor = 0;
    s_choiceCancel = cancelType;
    s_msgOpen = true;
    s_msgOwner = I;
    I->wait = W_MESSAGE;
}

// Collects consecutive lines of `lineCode` starting at f->pc.
static void collectLines(Frame *f, uint16_t lineCode, char *out, size_t outSize) {
    size_t n = 0;
    out[0] = '\0';
    while (f->pc < f->count && f->cmds[f->pc].code == lineCode) {
        const char *line = f->cmds[f->pc].str ? f->cmds[f->pc].str : "";
        if (n && n + 1 < outSize) out[n++] = '\n';
        for (; *line && n + 1 < outSize; line++) out[n++] = *line;
        f->pc++;
    }
    out[n] = '\0';
}

static void changeOwned(GameState *s, DbKind kind, const EventCommand *c) {
    if (c->paramCount < 4) return;
    int32_t v = operandValue(s, c->params[2], c->params[3]);
    runtimeAddOwned(s, kind, c->params[0], c->params[1] == 0 ? v : -v);
}

static void controlVariables(GameState *s, const Frame *f, const EventCommand *c) {
    if (c->paramCount < 5) return;
    int32_t start = c->params[0], end = c->params[1], op = c->params[2], type = c->params[3];
    for (int32_t id = start; id <= end && id <= FH_MAX_ID; id++) {
        int32_t value = 0;
        if (type == 0 || type == 1) {
            value = operandValue(s, type, c->params[4]);
        } else if (type == 2 && c->paramCount >= 6) {   // random [min, max]
            int32_t lo = c->params[4], hi = c->params[5];
            if (hi < lo) { int32_t t = lo; lo = hi; hi = t; }
            value = lo + rngNext(hi - lo + 1);
        } else if (type == 3 && c->paramCount >= 6) {   // game data
            int32_t kind = c->params[4], p1 = c->params[5];
            if (kind == 0) value = runtimeGetOwned(s, DB_ITEM, p1);
            else if (kind == 1) value = runtimeGetOwned(s, DB_WEAPON, p1);
            else if (kind == 2) value = runtimeGetOwned(s, DB_ARMOR, p1);
            else if (kind == 3 && c->paramCount >= 7) {     // actor stats
                ActorState *ac = actorGet(s, p1);
                int which = c->params[6];
                if (ac) {
                    if (which == 0) value = ac->level;
                    else if (which == 1) value = ac->exp;
                    else if (which == 2) value = ac->hp;
                    else if (which == 3) value = ac->mp;
                    else if (which >= 4 && which <= 11) value = actorParam(s, p1, which - 4);
                    else if (which == 12) value = ac->tp;
                }
            }
            else if (kind == 5 && c->paramCount >= 7) {     // character x/y/dir
                int x, y, d;
                if (characterInfo(f, p1, &x, &y, &d)) {
                    int which = c->params[6];
                    value = which == 0 ? x : which == 1 ? y : which == 2 ? d : 0;
                }
            }
            else if (kind == 7 && p1 == 4) value = s->gold;  // other: gold
        }
        int32_t p[5] = { id, id, op, 0, value };
        runtimeExecuteCommand(s, 122, p, 5);
    }
}

// Actor commands: [designation 0 fixed / 1 variable, id (0 = whole party), ...]
// Calls fn for every target actor.
static void forActors(GameState *s, int32_t designation, int32_t id,
                      void (*fn)(GameState *, int, const EventCommand *),
                      const EventCommand *c) {
    int target = designation == 1 ? runtimeGetVariable(s, (uint16_t)id) : id;
    if (target == 0) {
        for (int a = 1; a <= FH_MAX_ACTORS; a++)
            if (s->inParty[a]) fn(s, a, c);
    } else if (target >= 1 && target <= FH_MAX_ACTORS) {
        fn(s, target, c);
    }
}

// [design, id, op 0 increase / 1 decrease, operandType, operand, (extra)]
static int32_t signedOperand(GameState *s, const EventCommand *c) {
    int32_t v = operandValue(s, c->params[3], c->params[4]);
    return c->params[2] == 0 ? v : -v;
}
static void doHp(GameState *s, int a, const EventCommand *c) {
    actorChangeHp(s, a, signedOperand(s, c), c->paramCount >= 6 && c->params[5]);
}
static void doMp(GameState *s, int a, const EventCommand *c)  { actorChangeMp(s, a, signedOperand(s, c)); }
static void doTp(GameState *s, int a, const EventCommand *c)  { actorChangeTp(s, a, signedOperand(s, c)); }
static void doExp(GameState *s, int a, const EventCommand *c) { actorChangeExp(s, a, signedOperand(s, c)); }
static void doLevel(GameState *s, int a, const EventCommand *c) { actorChangeLevel(s, a, signedOperand(s, c)); }
static void doRecover(GameState *s, int a, const EventCommand *c) { (void)c; actorRecoverAll(s, a); }
static void doState(GameState *s, int a, const EventCommand *c) {
    // [design, id, op 0 add / 1 remove, stateId]
    if (c->params[2] == 0) actorAddState(s, a, c->params[3]);
    else actorRemoveState(s, a, c->params[3]);
}
static void doParam(GameState *s, int a, const EventCommand *c) {
    // [design, id, param, op, operandType, operand]
    int32_t v = operandValue(s, c->params[4], c->params[5]);
    ActorState *ac = actorGet(s, a);
    if (ac && c->params[2] >= 0 && c->params[2] < 8)
        ac->plus[c->params[2]] += c->params[3] == 0 ? v : -v;
}

static void doSkill(GameState *s, int a, const EventCommand *c) {
    // [design, id, op 0 learn / 1 forget, skillId]
    ActorState *ac = actorGet(s, a);
    int sk = c->params[3];
    if (!ac || sk < 1 || sk >= 512) return;
    uint8_t bit = (uint8_t)(1 << (sk & 7));
    if (c->params[2] == 0) { ac->learned[sk >> 3] |= bit; ac->forgotten[sk >> 3] &= (uint8_t)~bit; }
    else { ac->forgotten[sk >> 3] |= bit; ac->learned[sk >> 3] &= (uint8_t)~bit; }
}

// Executes one command. Returns false when the frame stack changed in a
// way that should end the current step loop (e.g. a wait started).
static void step(GameState *s) {
    Frame *f = &I->stack[I->depth - 1];
    if (f->pc >= f->count) { I->depth--; return; }

    uint32_t at = f->pc;
    const EventCommand *c = &f->cmds[f->pc++];
    int ind = c->indent;

    // One message window / battle / transfer at a time: if another
    // interpreter holds it, try this command again next frame.
    bool needsMsg = c->code == 101 || c->code == 102 || c->code == 105;
    if ((needsMsg && s_msgOpen) ||
        (c->code == 301 && s_battleOwner) ||
        (c->code == 201 && s_xferOwner)) {
        f->pc = at;
        I->wait = W_FRAMES;
        I->waitFrames = 1;
        return;
    }

    switch (c->code) {
    case 101: {   // Show Text (+ following 401 lines, + an attached 102)
        char raw[2048];
        collectLines(f, 401, raw, sizeof(raw));
        if (f->pc < f->count && f->cmds[f->pc].code == 102) {
            const EventCommand *ch = &f->cmds[f->pc];
            I->choicePc = (int32_t)f->pc;
            f->pc++;
            openMessage(raw, ch->str, ch->paramCount > 1 ? ch->params[1] : -1);
        } else {
            openMessage(raw, NULL, -1);
        }
        break;
    }
    case 102:     // Show Choices without text
        I->choicePc = (int32_t)at;
        openMessage(NULL, c->str, c->paramCount > 1 ? c->params[1] : -1);
        break;
    case 402: case 403: {   // reached the end of a choice branch
        uint32_t j = scanForward(f, f->pc, ind, 404, 404, 404);
        f->pc = j < f->count ? j + 1 : f->count;
        break;
    }
    case 105: {   // Show Scrolling Text
        char raw[2048];
        collectLines(f, 405, raw, sizeof(raw));
        openMessage(raw, NULL, -1);
        break;
    }
    case 111: {   // Conditional Branch
        if (ind < 0 || ind >= FH_MAX_INDENT) break;
        bool r = evalBranch(f, c, s);
        f->branch[ind] = r;
        if (!r) {
            uint32_t j = scanForward(f, f->pc, ind, 411, 412, 412);
            f->pc = j < f->count ? j + 1 : f->count;
        }
        break;
    }
    case 411:     // Else
        if (ind >= 0 && ind < FH_MAX_INDENT && f->branch[ind]) {
            uint32_t j = scanForward(f, f->pc, ind, 412, 412, 412);
            f->pc = j < f->count ? j + 1 : f->count;
        }
        break;
    case 413: {   // Repeat Above: back to the matching Loop
        for (int32_t j = (int32_t)at - 1; j >= 0; j--) {
            if (f->cmds[j].indent == ind && f->cmds[j].code == 112) { f->pc = (uint32_t)j + 1; break; }
            if (f->cmds[j].indent < ind) break;
        }
        break;
    }
    case 113: {   // Break Loop: past the next Repeat at a shallower indent
        for (uint32_t j = f->pc; j < f->count; j++)
            if (f->cmds[j].code == 413 && f->cmds[j].indent < ind) { f->pc = j + 1; break; }
        break;
    }
    case 115:     // Exit Event Processing
        f->pc = f->count;
        break;
    case 117: {   // Common Event
        const MapEvent *ce = c->paramCount ? findCommon(c->params[0]) : NULL;
        if (ce && ce->pageCount)
            pushFrame(ce->pages[0].cmds, ce->pages[0].count, f->eventId);
        break;
    }
    case 119: {   // Jump to Label
        if (!c->str) break;
        for (uint32_t j = 0; j < f->count; j++)
            if (f->cmds[j].code == 118 && f->cmds[j].str && !strcmp(f->cmds[j].str, c->str)) {
                f->pc = j + 1;
                break;
            }
        break;
    }
    case 121:
        runtimeExecuteCommand(s, 121, c->params, c->paramCount);
        break;
    case 122:
        controlVariables(s, f, c);
        break;
    case 123:     // Self Switch ["A", 0=ON]
        if (c->str && c->str[0] >= 'A' && c->str[0] <= 'D' && c->paramCount >= 2 && f->eventId)
            runtimeSetSelfSwitch(s, f->mapId, f->eventId, c->str[0] - 'A', c->params[1] == 0);
        break;
    case 125:     // Change Gold [op, type, operand]
        if (c->paramCount >= 3) {
            int32_t v = operandValue(s, c->params[1], c->params[2]);
            s->gold += c->params[0] == 0 ? v : -v;
            if (s->gold < 0) s->gold = 0;
        }
        break;
    case 126: changeOwned(s, DB_ITEM, c); break;
    case 127: changeOwned(s, DB_WEAPON, c); break;
    case 128: changeOwned(s, DB_ARMOR, c); break;
    case 129:     // Change Party Member [actorId, 0 add / 1 remove, init]
        if (c->paramCount >= 2 && c->params[0] >= 1 && c->params[0] <= FH_MAX_ACTORS) {
            if (c->params[1] == 0) {
                if (c->paramCount >= 3 && c->params[2]) actorSetup(s, c->params[0]);
                runtimePartyAdd(s, c->params[0]);
            } else {
                runtimePartyRemove(s, c->params[0]);
            }
        }
        break;
    case 303:     // Name Input Processing [actorId, maxLength]
        if (c->paramCount >= 1 && !s_nameOwner) {
            s_nameActor = c->params[0];
            s_nameMax = c->paramCount >= 2 && c->params[1] > 0 ? c->params[1] : 8;
            s_nameOwner = I;
            I->wait = W_NAME;
        } else if (s_nameOwner) {          // another input is open: retry
            f->pc = at;
            I->wait = W_FRAMES;
            I->waitFrames = 1;
        }
        break;
    case 320:     // Change Name [actorId, name]
        if (c->paramCount >= 1) actorSetName(s, c->params[0], c->str ? c->str : "");
        break;
    case 322: {   // Change Actor Images [actorId, character, index, face, faceIdx, battler]
        if (c->paramCount < 3) break;
        char sheet[48];
        size_t n = 0;
        for (const char *q = c->str ? c->str : ""; *q && n + 1 < sizeof(sheet); q++) {
            char ch = *q;
            bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                      (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
            sheet[n++] = ok ? ch : '_';
        }
        sheet[n] = '\0';
        actorSetSprite(s, c->params[0], sheet, c->params[2]);
        break;
    }
    case 311: if (c->paramCount >= 5) forActors(s, c->params[0], c->params[1], doHp, c); break;
    case 312: if (c->paramCount >= 5) forActors(s, c->params[0], c->params[1], doMp, c); break;
    case 326: if (c->paramCount >= 5) forActors(s, c->params[0], c->params[1], doTp, c); break;
    case 313: if (c->paramCount >= 4) forActors(s, c->params[0], c->params[1], doState, c); break;
    case 314: if (c->paramCount >= 2) forActors(s, c->params[0], c->params[1], doRecover, c); break;
    case 315: if (c->paramCount >= 5) forActors(s, c->params[0], c->params[1], doExp, c); break;
    case 316: if (c->paramCount >= 5) forActors(s, c->params[0], c->params[1], doLevel, c); break;
    case 317: if (c->paramCount >= 6) forActors(s, c->params[0], c->params[1], doParam, c); break;
    case 318: if (c->paramCount >= 4) forActors(s, c->params[0], c->params[1], doSkill, c); break;
    case 201:     // Transfer Player [designation, map, x, y, dir, fade]
        if (c->paramCount >= 5) {
            bool byVar = c->params[0] == 1;
            s_xferMap = byVar ? runtimeGetVariable(s, (uint16_t)c->params[1]) : c->params[1];
            s_xferX   = byVar ? runtimeGetVariable(s, (uint16_t)c->params[2]) : c->params[2];
            s_xferY   = byVar ? runtimeGetVariable(s, (uint16_t)c->params[3]) : c->params[3];
            s_xferDir = c->params[4];
            I->wait = W_TRANSFER;
            s_xferOwner = I;
        }
        break;
    // --- battle-only commands (troop events): act on the current troop ---
    case 331:     // Change Enemy HP [index, op, operandType, operand, allowDeath]
        if (c->paramCount >= 4) {
            int32_t v = operandValue(s, c->params[2], c->params[3]);
            battleEnemyHp(c->params[0], c->params[1] == 0 ? v : -v,
                          c->paramCount >= 5 && c->params[4]);
        }
        break;
    case 332:     // Change Enemy MP
        if (c->paramCount >= 4) {
            int32_t v = operandValue(s, c->params[2], c->params[3]);
            battleEnemyMp(c->params[0], c->params[1] == 0 ? v : -v);
        }
        break;
    case 333:     // Change Enemy State [index, op 0 add / 1 remove, stateId]
        if (c->paramCount >= 3) battleEnemyState(c->params[0], c->params[1] == 0, c->params[2]);
        break;
    case 334: if (c->paramCount >= 1) battleEnemyRecover(c->params[0]); break;
    case 335: if (c->paramCount >= 1) battleEnemyAppear(c->params[0]); break;
    case 336: if (c->paramCount >= 2) battleEnemyTransform(c->params[0], c->params[1]); break;
    case 340: battleAbort(); break;
    case 205: {   // Set Move Route [charId, repeat, skippable, wait, "route"]
        if (c->paramCount < 4 || !c->str) break;
        if (c->params[0] < 0) break;     // player routes: not supported yet
        MapEvent *ev = (f->mapId == s_map.mapId)
            ? findMapEvent(c->params[0] == 0 ? f->eventId : (uint16_t)c->params[0]) : NULL;
        if (!ev) break;
        free(ev->ch.forced);
        size_t len = strlen(c->str);
        ev->ch.forced = malloc(len + 1);
        if (!ev->ch.forced) break;
        memcpy(ev->ch.forced, c->str, len + 1);
        ev->ch.forcedRepeat = c->params[1] != 0;
        ev->ch.forcedSkip = c->params[2] != 0;
        ev->ch.forcedIdx = 0;
        ev->ch.routeWait = 0;
        if (c->params[3] && !ev->ch.forcedRepeat) {
            I->wait = W_ROUTE;
            I->routeEv = ev;
        }
        break;
    }
    case 203: {   // Set Event Location [charId, designation, x, y, dir]
        if (c->paramCount < 5 || f->mapId != s_map.mapId) break;
        MapEvent *ev = findMapEvent(c->params[0] == 0 ? f->eventId : (uint16_t)c->params[0]);
        if (!ev) break;
        int nx = c->params[2], ny = c->params[3];
        if (c->params[1] == 1) {
            nx = runtimeGetVariable(s, (uint16_t)c->params[2]);
            ny = runtimeGetVariable(s, (uint16_t)c->params[3]);
        } else if (c->params[1] == 2) {             // exchange with event
            MapEvent *o = findMapEvent(c->params[2] == 0 ? f->eventId : (uint16_t)c->params[2]);
            if (!o) break;
            nx = o->x; ny = o->y;
            o->x = ev->x; o->y = ev->y; o->ch.rx = o->x; o->ch.ry = o->y;
        }
        if (nx < 0 || ny < 0) break;
        ev->x = (uint16_t)nx; ev->y = (uint16_t)ny;
        ev->ch.rx = nx; ev->ch.ry = ny;
        if (c->params[4] && !ev->ch.dirFix) ev->ch.dir = (uint8_t)c->params[4];
        break;
    }
    case 214: {   // Erase Event
        MapEvent *ev = (f->eventId && f->mapId == s_map.mapId) ? findMapEvent(f->eventId) : NULL;
        if (ev) ev->erased = true;
        break;
    }
    case 223:     // Tint Screen [[r,g,b,gray], duration, wait]
        for (int k = 0; k < 4; k++) s_tint[k] = c->sub[k];
        if (c->paramCount >= 3 && c->params[2] && c->params[1] > 0) {
            I->wait = W_FRAMES;
            I->waitFrames = c->params[1];
        }
        break;
    case 231: {   // Show Picture [id, name, origin, designation, x, y, sx, sy, opacity, blend]
        int id = c->params[0];
        if (id < 1 || id > FH_MAX_PICTURES || c->paramCount < 10) break;
        EventPicture *pic = &s_pics[id];
        memset(pic, 0, sizeof(*pic));
        snprintf(pic->name, sizeof(pic->name), "%s", c->str ? c->str : "");
        pic->used = pic->name[0] != '\0';
        pic->origin = c->params[2];
        bool byVar = c->params[3] == 1;
        pic->x = (float)(byVar ? runtimeGetVariable(s, (uint16_t)c->params[4]) : c->params[4]);
        pic->y = (float)(byVar ? runtimeGetVariable(s, (uint16_t)c->params[5]) : c->params[5]);
        pic->scaleX = (float)c->params[6];
        pic->scaleY = (float)c->params[7];
        pic->opacity = (float)c->params[8];
        pic->blend = c->params[9];
        break;
    }
    case 232: {   // Move Picture [id, -, origin, designation, x, y, sx, sy, opacity, blend, duration, wait]
        int id = c->params[0];
        if (id < 1 || id > FH_MAX_PICTURES || c->paramCount < 12) break;
        EventPicture *pic = &s_pics[id];
        bool byVar = c->params[3] == 1;
        pic->origin = c->params[2];
        pic->tx = (float)(byVar ? runtimeGetVariable(s, (uint16_t)c->params[4]) : c->params[4]);
        pic->ty = (float)(byVar ? runtimeGetVariable(s, (uint16_t)c->params[5]) : c->params[5]);
        pic->tsx = (float)c->params[6];
        pic->tsy = (float)c->params[7];
        pic->topacity = (float)c->params[8];
        pic->blend = c->params[9];
        pic->duration = c->params[10] > 0 ? c->params[10] : 1;
        if (c->params[11]) { I->wait = W_FRAMES; I->waitFrames = pic->duration; }
        break;
    }
    case 234: {   // Tint Picture [id, [r,g,b,gray], duration, wait]
        int id = c->params[0];
        if (id < 1 || id > FH_MAX_PICTURES || c->paramCount < 4) break;
        EventPicture *pic = &s_pics[id];
        for (int k = 0; k < 4; k++) pic->toneTarget[k] = c->sub[k];
        pic->toneDuration = c->params[2] > 0 ? c->params[2] : 1;
        if (c->params[3]) { I->wait = W_FRAMES; I->waitFrames = pic->toneDuration; }
        break;
    }
    case 235: {   // Erase Picture [id]
        int id = c->params[0];
        if (id >= 1 && id <= FH_MAX_PICTURES) memset(&s_pics[id], 0, sizeof(s_pics[id]));
        break;
    }
    case 230:     // Wait
        if (c->paramCount >= 1 && c->params[0] > 0) {
            I->wait = W_FRAMES;
            I->waitFrames = c->params[0];
        }
        break;
    case 301:     // Battle Processing [designation, troop, canEscape, canLose]
        if (c->paramCount >= 4) {
            s_battleTroop = c->params[0] == 1 ? runtimeGetVariable(s, (uint16_t)c->params[1])
                                              : c->params[1];
            s_battleEscape = c->params[2] != 0;
            s_battleLose = c->params[3] != 0;
            I->battlePc = (int32_t)at;
            I->wait = W_BATTLE;
            s_battleOwner = I;
        }
        break;
    case 601: case 602: case 603: {   // end of a battle-result branch
        uint32_t j = scanForward(f, f->pc, ind, 604, 604, 604);
        f->pc = j < f->count ? j + 1 : f->count;
        break;
    }
    case 353: case 354: {
        const Frame *root = &I->stack[0];
        bool common = root->owner && root->eventId == 0;
        snprintf(s_gameOverWho, sizeof(s_gameOverWho), "%s %u on map %u",
            common ? "common event" : "map event",
            root->owner ? root->owner->id : 0, s_map.mapId);
        s_gameOver = true;
        s_toTitle = (c->code == 354);
        I->depth = 0;
        break;
    }
    case 356:     // Plugin Command
        if (c->str) {
            if (!strncmp(c->str, "GabText ", 8)) {
                snprintf(s_gabText, sizeof(s_gabText), "%s", c->str + 8);
            } else if (!strncmp(c->str, "ShowGab", 7) && s_gab && s_gabText[0]) {
                char fmt[256];
                msgFormat(s_gabText, fmt, sizeof(fmt), NULL, 0, s);
                s_gab(fmt);
            }
        }
        break;
    default:      // 0, 108/408, 112, 118, 404, 412, 604, and unimplemented
        break;
    }
}

static void interpUpdate(Interp *in, GameState *s) {
    I = in;
    if (I->wait == W_ROUTE) {
        if (I->routeEv && I->routeEv->ch.forced) return;
        I->wait = W_NONE;
        I->routeEv = NULL;
    }
    if (I->wait == W_FRAMES) {
        if (--I->waitFrames > 0) return;
        I->wait = W_NONE;
    }
    if (I->wait == W_MESSAGE) {
        if (s_msgOpen) return;
        I->wait = W_NONE;
        if (I->choicePc >= 0 && I->depth > 0) {
            Frame *f = &I->stack[I->depth - 1];
            uint32_t at = (uint32_t)I->choicePc;
            int ind = f->cmds[at].indent;
            I->choicePc = -1;
            if (s_choiceResult >= 0) {
                for (uint32_t j = at + 1; j < f->count; j++) {
                    const EventCommand *k = &f->cmds[j];
                    if (k->indent < ind) break;
                    if (k->indent != ind) continue;
                    if (k->code == 404) { f->pc = j + 1; break; }
                    bool match = (s_choiceResult == FH_MAX_CHOICES)
                        ? (k->code == 403)
                        : (k->code == 402 && k->paramCount && k->params[0] == s_choiceResult);
                    if (match) { f->pc = j + 1; break; }
                }
            }
        }
    }
    if (I->wait != W_NONE) return;   // battle / transfer: main loop answers

    int budget = 20000;             // guards against runaway loops
    while (I->depth > 0 && I->wait == W_NONE && budget-- > 0)
        step(s);
}

static void updatePictures(void);
static void updateCharacters(GameState *s);

// --- battle: common events started by skills/items (effect 44) ---
void eventsBattleCommon(int id) {
    const MapEvent *ce = findCommon(id);
    if (!ce || !ce->pageCount || ce->pages[0].count <= 1) return;
    Interp *saved = I;
    I = &s_battleInterp;
    pushFrame(ce->pages[0].cmds, ce->pages[0].count, 0);
    I = saved;
}

bool eventsBattleBusy(void) { return s_battleInterp.depth > 0; }

// Field: run a common event on the main interpreter (item effect 44).
void eventsStartCommon(int id) {
    const MapEvent *ce = findCommon(id);
    if (!ce || !ce->pageCount || ce->pages[0].count <= 1) return;
    Interp *saved = I;
    I = &s_main;
    pushFrame(ce->pages[0].cmds, ce->pages[0].count, 0);
    I = saved;
}

bool eventsLoadTroopEvents(const char *path) { return loadFile(path, &s_troopEv); }

static const MapEvent *findTroopEv(int troopId) {
    for (uint32_t i = 0; i < s_troopEv.count; i++)
        if (s_troopEv.events[i].id == troopId) return &s_troopEv.events[i];
    return NULL;
}

int eventsTroopPages(int troopId) {
    const MapEvent *t = findTroopEv(troopId);
    return t ? t->pageCount : 0;
}

bool eventsTroopPage(int troopId, int page, TroopPageCond *out) {
    const MapEvent *t = findTroopEv(troopId);
    if (!t || page < 0 || page >= t->pageCount) return false;
    const PageConditions *c = &t->pages[page].cond;
    out->turnEnding = c->turnEnding; out->turnValid = c->turnValid;
    out->turnA = c->turnA; out->turnB = c->turnB;
    out->enemyValid = c->enemyValid; out->enemyIndex = c->enemyIndex; out->enemyHp = c->enemyHp;
    out->actorValid = c->actorValid; out->actorId = c->actorId; out->actorHp = c->actorHp;
    out->switchValid = c->switchValid; out->switchId = c->switchId;
    out->span = c->span;
    out->empty = t->pages[page].count <= 1;
    return true;
}

void eventsTroopRun(int troopId, int page) {
    const MapEvent *t = findTroopEv(troopId);
    if (!t || page < 0 || page >= t->pageCount || t->pages[page].count <= 1) return;
    Interp *saved = I;
    I = &s_battleInterp;
    pushFrame(t->pages[page].cmds, t->pages[page].count, 0);
    I = saved;
}

void eventsBattleUpdate(GameState *s) {
    s_state = s;
    Interp *saved = I;
    interpUpdate(&s_battleInterp, s);
    I = saved;
    updatePictures();
}

// Runs a parallel interpreter for `ev` if its active page is parallel.
static void runParallel(Interp *in, MapEvent *ev, int page, bool isParallel,
                        uint16_t eventId, GameState *s) {
    if (!isParallel) {               // page changed away: drop the run
        if (in->page != -1 || in->depth) resetInterp(in);
        return;
    }
    if (in->page != page) {          // new page: fresh interpreter
        resetInterp(in);
        in->page = page;
    }
    if (in->depth == 0) {            // finished: start the list again
        const EventPage *pg = &ev->pages[page];
        if (pg->count <= 1) return;  // empty page: nothing to run
        I = in;
        startOwned(ev, pg, eventId);
    }
    interpUpdate(in, s);
}

void eventsUpdate(GameState *s) {
    s_state = s;
    if (s_gameOver) return;          // everything stops, like Scene_Gameover
    interpUpdate(&s_main, s);
    if (s_main.depth == 0 && s_oldMap.events)
        freeFile(&s_oldMap);

    for (uint32_t i = 0; s_mapPar && i < s_map.count; i++) {
        MapEvent *ev = &s_map.events[i];
        int p = activePage(ev, s);
        bool par = p >= 0 && ev->pages[p].cond.trigger == 4;
        runParallel(&s_mapPar[i], ev, p, par, ev->id, s);
    }
    for (uint32_t i = 0; s_commonPar && i < s_common.count; i++) {
        MapEvent *ce = &s_common.events[i];
        bool par = ce->pageCount && !ce->erased &&
                   ce->pages[0].cond.trigger == 2 &&
                   runtimeGetSwitch(s, ce->pages[0].cond.switchId);
        runParallel(&s_commonPar[i], ce, 0, par, 0, s);
    }
    I = &s_main;
    updateCharacters(s);
    updatePictures();
}

// --- requests --------------------------------------------------------------

bool eventsPendingBattle(int *troopId, bool *canEscape, bool *canLose) {
    if (!s_battleOwner || s_battleOwner->wait != W_BATTLE) return false;
    *troopId = s_battleTroop; *canEscape = s_battleEscape; *canLose = s_battleLose;
    return true;
}

void eventsResolveBattle(int result) {
    if (!s_battleOwner) return;
    I = s_battleOwner;
    s_battleOwner = NULL;
    if (I->wait != W_BATTLE) { I = &s_main; return; }
    I->wait = W_NONE;
    if (I->depth == 0 || I->battlePc < 0) { I = &s_main; return; }
    Frame *f = &I->stack[I->depth - 1];
    uint32_t at = (uint32_t)I->battlePc;
    int ind = f->cmds[at].indent;
    I->battlePc = -1;
    uint32_t j = scanForward(f, at + 1, ind, (uint16_t)(601 + result), 604, 604);
    if (j < f->count) f->pc = j + 1;   // into the result branch, or past the end
    I = &s_main;
}

bool eventsPendingNameInput(int *actorId, int *maxLen) {
    if (!s_nameOwner || s_nameOwner->wait != W_NAME) return false;
    *actorId = s_nameActor;
    *maxLen = s_nameMax;
    return true;
}

void eventsNameInputDone(GameState *s, const char *name) {
    if (!s_nameOwner) return;
    if (name) actorSetName(s, s_nameActor, name);
    if (s_nameOwner->wait == W_NAME) s_nameOwner->wait = W_NONE;
    s_nameOwner = NULL;
}

bool eventsPendingTransfer(int *mapId, int *x, int *y, int *dir) {
    if (!s_xferOwner || s_xferOwner->wait != W_TRANSFER) return false;
    *mapId = s_xferMap; *x = s_xferX; *y = s_xferY; *dir = s_xferDir;
    return true;
}

void eventsTransferDone(void) {
    if (s_xferOwner && s_xferOwner->wait == W_TRANSFER) s_xferOwner->wait = W_NONE;
    s_xferOwner = NULL;
}

// ===========================================================================
// 5b. Characters: map events moving on the map (Game_Event / Game_Character)
// ===========================================================================

static bool (*s_passFn)(int x, int y, int dir03);
void eventsSetPassability(bool (*fn)(int x, int y, int dir03)) { s_passFn = fn; }

static int mvToOur(int d) { return d == 4 ? 1 : d == 6 ? 2 : d == 8 ? 3 : 0; }
static int dxOf(int d) { return d == 4 ? -1 : d == 6 ? 1 : 0; }
static int dyOf(int d) { return d == 2 ? 1 : d == 8 ? -1 : 0; }

static bool startEvent(MapEvent *ev, int page);

static void setupCharPage(MapEvent *ev, int page) {
    CharState *c = &ev->ch;
    c->page = page;
    c->routeIdx = 0;
    c->routeWait = 0;
    c->stop = 0;
    if (page < 0) {
        c->img[0] = '\0';
        c->tileId = 0;
        c->route = NULL;
        c->through = true;
        return;
    }
    const PageConditions *pc = &ev->pages[page].cond;
    snprintf(c->img, sizeof(c->img), "%s", pc->imgName);
    c->imgIndex = pc->imgIndex;
    c->tileId = pc->tileId;
    c->origPattern = c->pattern = pc->imgPattern;
    if (!c->dirFix || ev->ch.page == -2) c->dir = pc->imgDir ? pc->imgDir : 2;
    c->dirFix = pc->dirFix;
    if (pc->imgDir) c->dir = pc->imgDir;
    c->speed = pc->moveSpeed ? pc->moveSpeed : 3;
    c->freq = pc->moveFreq ? pc->moveFreq : 3;
    c->walkAnime = pc->walkAnime;
    c->stepAnime = pc->stepAnime;
    c->through = pc->through;
    c->transparent = false;
    c->route = pc->route;
    c->routeRepeat = pc->routeRepeat;
    c->routeSkip = pc->routeSkip;
}

static bool charMoving(const MapEvent *ev) { return ev->ch.rx != ev->x || ev->ch.ry != ev->y; }

// Normal-priority, non-through event standing at (x, y), other than `self`.
static bool eventAt(int x, int y, const MapEvent *self, GameState *s) {
    for (uint32_t i = 0; i < s_map.count; i++) {
        const MapEvent *o = &s_map.events[i];
        if (o == self || o->x != x || o->y != y) continue;
        int p = activePage(o, s);
        if (p < 0 || o->ch.through) continue;
        if (o->pages[p].cond.priority == 1) return true;
    }
    return false;
}

static int eventPriority(const MapEvent *ev) {
    return ev->ch.page >= 0 ? ev->pages[ev->ch.page].cond.priority : 0;
}

// Event touch (trigger 2): bumping into the player starts the event.
static void checkTouch(MapEvent *ev, int x, int y) {
    if (eventsBusy() || x != s_playerX || y != s_playerY) return;
    int p = ev->ch.page;
    if (p < 0 || ev->pages[p].cond.trigger != 2 || eventPriority(ev) != 1) return;
    startEvent(ev, p);
}

static void setDir(MapEvent *ev, int d) {
    if (!ev->ch.dirFix && d) ev->ch.dir = (uint8_t)d;
}

static bool moveStraight(MapEvent *ev, int d, GameState *s) {
    int nx = ev->x + dxOf(d), ny = ev->y + dyOf(d);
    bool ok = true;
    if (!ev->ch.through) {
        if (s_passFn && !s_passFn(ev->x, ev->y, mvToOur(d))) ok = false;
        else if (eventPriority(ev) == 1 && nx == s_playerX && ny == s_playerY) ok = false;
        else if (eventAt(nx, ny, ev, s)) ok = false;
    }
    setDir(ev, d);
    if (ok) {
        ev->x = (uint16_t)nx;
        ev->y = (uint16_t)ny;
    } else {
        checkTouch(ev, nx, ny);
    }
    return ok;
}

static bool moveToward(MapEvent *ev, int tx, int ty, bool away, GameState *s) {
    int sx = ev->x - tx, sy = ev->y - ty;
    if (away) { sx = -sx; sy = -sy; }
    int h = sx > 0 ? 4 : 6, v = sy > 0 ? 8 : 2;
    if (abs(sx) > abs(sy)) {
        if (moveStraight(ev, h, s)) return true;
        return sy != 0 && moveStraight(ev, v, s);
    }
    if (sy != 0) {
        if (moveStraight(ev, v, s)) return true;
        return sx != 0 && moveStraight(ev, h, s);
    }
    return false;
}

static void turnToward(MapEvent *ev, int tx, int ty, bool away) {
    int sx = ev->x - tx, sy = ev->y - ty;
    if (away) { sx = -sx; sy = -sy; }
    if (abs(sx) > abs(sy)) setDir(ev, sx > 0 ? 4 : 6);
    else if (sy != 0) setDir(ev, sy > 0 ? 8 : 2);
}

static int turnRight90(int d) { return d == 2 ? 4 : d == 4 ? 8 : d == 6 ? 2 : 6; }
static int turnLeft90(int d)  { return d == 2 ? 6 : d == 4 ? 2 : d == 6 ? 8 : 4; }
static int reverseDir(int d)  { return 10 - d; }

// Runs one move-route command at *offset in `route`. Returns true if the
// route reached its end (and does not repeat).
static bool routeStep(MapEvent *ev, const char *route, int *offset, bool repeat,
                      bool skippable, GameState *s) {
    CharState *c = &ev->ch;
    if (c->routeWait > 0) { c->routeWait--; return false; }
    if (!route || !route[*offset]) {
        if (repeat && route && route[0]) *offset = 0;
        return !repeat || !route || !route[0];
    }
    const char *cmd = route + *offset;
    int code = atoi(cmd);
    const char *f1 = strchr(cmd, ':');
    const char *f2 = f1 ? strchr(f1 + 1, ':') : NULL;
    int p0 = f1 ? atoi(f1 + 1) : 0, p1 = f2 ? atoi(f2 + 1) : 0;
    bool ok = true;

    switch (code) {
        case 1: ok = moveStraight(ev, 2, s); break;
        case 2: ok = moveStraight(ev, 4, s); break;
        case 3: ok = moveStraight(ev, 6, s); break;
        case 4: ok = moveStraight(ev, 8, s); break;
        case 5: ok = moveStraight(ev, 2, s) || moveStraight(ev, 4, s); break;   // diagonals:
        case 6: ok = moveStraight(ev, 2, s) || moveStraight(ev, 6, s); break;   // approximated
        case 7: ok = moveStraight(ev, 8, s) || moveStraight(ev, 4, s); break;
        case 8: ok = moveStraight(ev, 8, s) || moveStraight(ev, 6, s); break;
        case 9: ok = moveStraight(ev, 2 + rngNext(4) * 2, s); break;
        case 10: ok = moveToward(ev, s_playerX, s_playerY, false, s); break;
        case 11: ok = moveToward(ev, s_playerX, s_playerY, true, s); break;
        case 12: ok = moveStraight(ev, c->dir, s); break;
        case 13: { bool fix = c->dirFix; c->dirFix = true;
                   ok = moveStraight(ev, reverseDir(c->dir), s); c->dirFix = fix; break; }
        case 14: ev->x = (uint16_t)(ev->x + p0); ev->y = (uint16_t)(ev->y + p1);
                 c->rx = ev->x; c->ry = ev->y; break;                       // jump (instant)
        case 15: c->routeWait = p0 > 0 ? p0 - 1 : 0; break;
        case 16: setDir(ev, 2); break;
        case 17: setDir(ev, 4); break;
        case 18: setDir(ev, 6); break;
        case 19: setDir(ev, 8); break;
        case 20: setDir(ev, turnRight90(c->dir)); break;
        case 21: setDir(ev, turnLeft90(c->dir)); break;
        case 22: setDir(ev, reverseDir(c->dir)); break;
        case 23: setDir(ev, rngNext(2) ? turnRight90(c->dir) : turnLeft90(c->dir)); break;
        case 24: setDir(ev, 2 + rngNext(4) * 2); break;
        case 25: turnToward(ev, s_playerX, s_playerY, false); break;
        case 26: turnToward(ev, s_playerX, s_playerY, true); break;
        case 27: runtimeSetSwitch(s, (uint16_t)p0, true); break;
        case 28: runtimeSetSwitch(s, (uint16_t)p0, false); break;
        case 29: c->speed = (uint8_t)p0; break;
        case 30: c->freq = (uint8_t)p0; break;
        case 31: c->walkAnime = true; break;
        case 32: c->walkAnime = false; break;
        case 33: c->stepAnime = true; break;
        case 34: c->stepAnime = false; break;
        case 35: c->dirFix = true; break;
        case 36: c->dirFix = false; break;
        case 37: c->through = true; break;
        case 38: c->through = false; break;
        case 39: c->transparent = true; break;
        case 40: c->transparent = false; break;
        case 41: {                          // change image [name, index]
            char name[48] = "";
            if (f1) {
                const char *e = f2 ? f2 : f1 + 1 + strlen(f1 + 1);
                size_t n = (size_t)(e - (f1 + 1));
                if (n >= sizeof(name)) n = sizeof(name) - 1;
                memcpy(name, f1 + 1, n);
                name[n] = '\0';
            }
            snprintf(c->img, sizeof(c->img), "%s", strcmp(name, "-") ? name : "");
            c->imgIndex = (uint8_t)p1;
            c->tileId = 0;
            break;
        }
        default: break;                     // opacity, blend, sound, script
    }

    if (ok || skippable) {
        const char *next = strchr(cmd, ';');
        *offset = next ? (int)(next + 1 - route) : (int)strlen(route);
        if (!route[*offset]) {
            if (repeat) *offset = 0;
            else return true;
        }
    }
    return false;
}

static bool lockedByMain(const MapEvent *ev) {
    return s_main.depth > 0 && s_main.stack[0].owner == ev;
}

static void updateCharacter(MapEvent *ev, GameState *s) {
    CharState *c = &ev->ch;
    int page = activePage(ev, s);
    if (page != c->page) setupCharPage(ev, page);
    if (page < 0) return;

    // Walk the real position toward the tile position.
    float dist = (float)(1 << (c->speed > 6 ? 6 : c->speed)) / 256.0f;
    if (c->rx < ev->x) { c->rx += dist; if (c->rx > ev->x) c->rx = ev->x; }
    if (c->rx > ev->x) { c->rx -= dist; if (c->rx < ev->x) c->rx = ev->x; }
    if (c->ry < ev->y) { c->ry += dist; if (c->ry > ev->y) c->ry = ev->y; }
    if (c->ry > ev->y) { c->ry -= dist; if (c->ry < ev->y) c->ry = ev->y; }
    bool moving = charMoving(ev);

    // Step animation (MV updateAnimation).
    int wait = (9 - c->speed) * 3;
    if (moving && c->walkAnime) c->anim += 1.5f;
    else if (c->stepAnime || c->pattern != c->origPattern) c->anim += 1.0f;
    if (c->anim >= wait) {
        if (!c->stepAnime && c->stop > 0) c->pattern = c->origPattern;
        else c->pattern = (uint8_t)((c->pattern + 1) % 4);
        c->anim = 0;
    }

    if (moving) { c->stop = 0; return; }
    c->stop++;

    if (c->forced) {                                   // Set Move Route
        if (routeStep(ev, c->forced, &c->forcedIdx, c->forcedRepeat, c->forcedSkip, s)) {
            free(c->forced);
            c->forced = NULL;
        }
        return;
    }
    if (lockedByMain(ev)) { c->stop = 0; return; }

    // Autonomous movement only near the screen, after the frequency wait.
    if (abs(ev->x - s_playerX) > 10 || abs(ev->y - s_playerY) > 8) return;
    if (c->stop <= 30 * (5 - c->freq)) return;
    switch (ev->pages[page].cond.moveType) {
        case 1: moveStraight(ev, 2 + rngNext(4) * 2, s); break;           // random
        case 2:                                                           // approach
            if (abs(ev->x - s_playerX) + abs(ev->y - s_playerY) >= 20) {
                moveStraight(ev, 2 + rngNext(4) * 2, s);
            } else switch (rngNext(6)) {
                case 4: moveStraight(ev, 2 + rngNext(4) * 2, s); break;
                case 5: moveStraight(ev, c->dir, s); break;
                default: moveToward(ev, s_playerX, s_playerY, false, s); break;
            }
            break;
        case 3:                                                           // custom
            routeStep(ev, c->route, &c->routeIdx, c->routeRepeat, c->routeSkip, s);
            break;
        default: break;
    }
    c->stop = 0;
}

static void updateCharacters(GameState *s) {
    for (uint32_t i = 0; i < s_map.count; i++) updateCharacter(&s_map.events[i], s);
}

int eventsSprites(EventSprite *out, int max) {
    int n = 0;
    for (uint32_t i = 0; i < s_map.count && n < max; i++) {
        const MapEvent *ev = &s_map.events[i];
        const CharState *c = &ev->ch;
        if (c->page < 0 || ev->erased || c->transparent) continue;
        if (!c->img[0] && !c->tileId) continue;
        EventSprite *e = &out[n++];
        e->img = c->img;
        e->index = c->imgIndex;
        e->dir = mvToOur(c->dir);
        e->pattern = c->pattern < 3 ? c->pattern : 1;
        e->tileId = c->tileId;
        e->x = c->rx;
        e->y = c->ry;
        e->priority = eventPriority(ev);
    }
    return n;
}

// ===========================================================================
// 6. Triggers
// ===========================================================================

static const int TDX[4] = { 0, -1, 1, 0 };   // 0 down 1 left 2 right 3 up
static const int TDY[4] = { 1, 0, 0, -1 };

static bool startEvent(MapEvent *ev, int page) {
    const EventPage *pg = &ev->pages[page];
    // RPG Maker (Game_Event.start) ignores pages whose list is only the
    // closing end marker; starting them would freeze the player forever
    // on empty autorun pages.
    if (pg->count <= 1) return false;
    // MV "lock": the event turns to face the player when it starts.
    if (pg->cond.trigger <= 2) turnToward(ev, s_playerX, s_playerY, false);
    return startOwned(ev, pg, ev->id);
}

// Starts the first event at (x, y) whose active page matches.
// samePriority: true = only priority 1, false = only priority 0/2.
static bool triggerAt(int x, int y, bool samePriority, uint32_t triggerMask, GameState *s) {
    if (eventsBusy()) return false;
    for (uint32_t i = 0; i < s_map.count; i++) {
        MapEvent *ev = &s_map.events[i];
        if (ev->x != x || ev->y != y) continue;
        int p = activePage(ev, s);
        if (p < 0) continue;
        const PageConditions *c = &ev->pages[p].cond;
        if ((c->priority == 1) != samePriority) continue;
        if (!(triggerMask & (1u << c->trigger))) continue;
        if (startEvent(ev, p)) return true;
    }
    return false;
}

bool eventsTriggerAction(int px, int py, int dir, GameState *s) {
    if (triggerAt(px, py, false, 1u << 0, s)) return true;
    return triggerAt(px + TDX[dir & 3], py + TDY[dir & 3], true, 1u << 0, s);
}

bool eventsTriggerTouchHere(int px, int py, GameState *s) {
    return triggerAt(px, py, false, (1u << 1) | (1u << 2), s);
}

bool eventsTriggerTouchThere(int px, int py, int dir, GameState *s) {
    return triggerAt(px + TDX[dir & 3], py + TDY[dir & 3], true, (1u << 1) | (1u << 2), s);
}

void eventsCheckAutorun(GameState *s) {
    if (eventsBusy() || s_gameOver) return;
    for (uint32_t i = 0; i < s_map.count; i++) {
        MapEvent *ev = &s_map.events[i];
        int p = activePage(ev, s);
        if (p >= 0 && ev->pages[p].cond.trigger == 3 && startEvent(ev, p)) return;
    }
    for (uint32_t i = 0; i < s_common.count; i++) {
        MapEvent *ce = &s_common.events[i];
        if (!ce->pageCount || ce->erased) continue;
        const PageConditions *c = &ce->pages[0].cond;
        if (c->trigger == 1 && runtimeGetSwitch(s, c->switchId) && ce->pages[0].count > 1) {
            startOwned(ce, &ce->pages[0], 0);
            return;
        }
    }
}

bool eventsBlocking(int x, int y, GameState *s) {
    for (uint32_t i = 0; i < s_map.count; i++) {
        MapEvent *ev = &s_map.events[i];
        if (ev->x != x || ev->y != y) continue;
        int p = activePage(ev, s);
        if (p < 0) continue;
        const PageConditions *c = &ev->pages[p].cond;
        bool through = (ev->ch.page == p) ? ev->ch.through : c->through;
        if (c->priority == 1 && !through) return true;
    }
    return false;
}

// ===========================================================================
// 7. Message window
// ===========================================================================

bool eventsMessageOpen(void) { return s_msgOpen; }

int eventsMessageLines(const char **lines, int maxLines) {
    int n = s_msgLineCount < maxLines ? s_msgLineCount : maxLines;
    for (int i = 0; i < n; i++) lines[i] = s_msgLines[i];
    return n;
}

const char *eventsMessageSpeaker(void) { return s_speaker; }
int eventsChoiceCount(void) { return s_choiceCount; }
const char *eventsChoiceText(int i) { return (i >= 0 && i < s_choiceCount) ? s_choices[i] : ""; }
int eventsChoiceCursor(void) { return s_choiceCursor; }

void eventsMessageInput(bool up, bool down, bool confirm, bool cancel) {
    if (!s_msgOpen) return;
    if (s_choiceCount == 0) {
        if (confirm || cancel) { s_msgOpen = false; s_choiceResult = -1; }
        return;
    }
    if (up)   s_choiceCursor = (s_choiceCursor + s_choiceCount - 1) % s_choiceCount;
    if (down) s_choiceCursor = (s_choiceCursor + 1) % s_choiceCount;
    if (confirm) {
        s_choiceResult = s_choiceCursor;
        s_msgOpen = false;
    } else if (cancel && s_choiceCancel >= 0) {
        // cancelType == choice count means "Branch" (the 403 block).
        s_choiceResult = (s_choiceCancel >= s_choiceCount) ? FH_MAX_CHOICES : s_choiceCancel;
        s_msgOpen = false;
    }
}

// ===========================================================================
// 8. Self-tests: build tiny command lists and run them.
// ===========================================================================

static GameState s_t;   // large; keep off the stack

static EventCommand mk(uint16_t code, int16_t indent, const int32_t *p, int n, const char *str) {
    EventCommand c;
    memset(&c, 0, sizeof(c));
    c.code = code; c.indent = indent; c.paramCount = (uint8_t)n;
    for (int i = 0; i < n; i++) c.params[i] = p[i];
    c.str = (char *)str;
    return c;
}

#define P(...) (const int32_t[]){__VA_ARGS__}, (int)(sizeof((const int32_t[]){__VA_ARGS__}) / sizeof(int32_t))

static void runAll(EventCommand *cmds, int n) {
    I->depth = 0; I->wait = W_NONE; s_msgOpen = false; I->choicePc = -1;
    pushFrame(cmds, (uint16_t)n, 1);
    for (int guard = 0; guard < 100 && I->depth > 0; guard++) {
        eventsUpdate(&s_t);
        if (s_msgOpen) eventsMessageInput(false, false, true, false);
    }
}

static int32_t V(int id) { return runtimeGetVariable(&s_t, (uint16_t)id); }

bool eventsRunSelfTests(int *passed, int *total) {
    int ok = 0, all = 0;
    #define CHECK(cond) do { all++; if (cond) ok++; } while (0)

    // If false -> Else; code 0 inside a branch body must not stop the page.
    runtimeInit(&s_t);
    {
        EventCommand l[] = {
            mk(111, 0, P(0, 5, 0), NULL),        // if switch 5 ON (it's OFF)
            mk(122, 1, P(1, 1, 0, 0, 10), NULL),
            mk(0,   1, P(0), NULL),
            mk(411, 0, P(0), NULL),
            mk(122, 1, P(1, 1, 0, 0, 20), NULL),
            mk(0,   1, P(0), NULL),
            mk(412, 0, P(0), NULL),
            mk(122, 0, P(2, 2, 0, 0, 7), NULL),  // after the branch
            mk(0,   0, P(0), NULL),
        };
        runAll(l, 9);
        CHECK(V(1) == 20 && V(2) == 7);
    }
    // Loop with break after 3 iterations.
    runtimeInit(&s_t);
    {
        EventCommand l[] = {
            mk(112, 0, P(0), NULL),
            mk(122, 1, P(3, 3, 1, 0, 1), NULL),  // v3 += 1
            mk(111, 1, P(1, 3, 0, 3, 1), NULL),  // if v3 >= 3
            mk(113, 2, P(0), NULL),
            mk(0,   2, P(0), NULL),
            mk(412, 1, P(0), NULL),
            mk(0,   1, P(0), NULL),
            mk(413, 0, P(0), NULL),
            mk(122, 0, P(4, 4, 0, 0, 1), NULL),
            mk(0,   0, P(0), NULL),
        };
        runAll(l, 10);
        CHECK(V(3) == 3 && V(4) == 1);
    }
    // Label jump skips a command.
    runtimeInit(&s_t);
    {
        EventCommand l[] = {
            mk(119, 0, P(0), "end"),
            mk(122, 0, P(5, 5, 0, 0, 99), NULL),
            mk(118, 0, P(0), "end"),
            mk(122, 0, P(6, 6, 0, 0, 1), NULL),
            mk(0,   0, P(0), NULL),
        };
        runAll(l, 5);
        CHECK(V(5) == 0 && V(6) == 1);
    }
    // Choices: confirm picks choice 0's branch only.
    runtimeInit(&s_t);
    {
        EventCommand l[] = {
            mk(101, 0, P(0, 0, 0, 2), NULL),
            mk(401, 0, P(0), "Test line"),
            mk(102, 0, P(0, -1, 0, 2, 0), "Yes\nNo"),
            mk(402, 0, P(0), "Yes"),
            mk(122, 1, P(7, 7, 0, 0, 1), NULL),
            mk(0,   1, P(0), NULL),
            mk(402, 0, P(1), "No"),
            mk(122, 1, P(7, 7, 0, 0, 2), NULL),
            mk(0,   1, P(0), NULL),
            mk(404, 0, P(0), NULL),
            mk(122, 0, P(8, 8, 0, 0, 1), NULL),
            mk(0,   0, P(0), NULL),
        };
        runAll(l, 12);
        CHECK(V(7) == 1 && V(8) == 1);
    }
    // Self switch set + branch on it; Change Items adds to inventory.
    runtimeInit(&s_t);
    {
        EventCommand l[] = {
            mk(123, 0, P(0, 0), "A"),
            mk(111, 0, P(2, 0, 0), "A"),
            mk(126, 1, P(3, 0, 0, 2), NULL),     // +2 of item 3
            mk(0,   1, P(0), NULL),
            mk(412, 0, P(0), NULL),
            mk(0,   0, P(0), NULL),
        };
        runAll(l, 6);
        CHECK(runtimeGetOwned(&s_t, DB_ITEM, 3) == 2);
        CHECK(runtimeGetSelfSwitch(&s_t, s_map.mapId, 1, 0));
        runtimeSetSelfSwitch(&s_t, s_map.mapId, 1, 0, false);
        CHECK(!runtimeGetSelfSwitch(&s_t, s_map.mapId, 1, 0));
    }
    // Battle result branch: lose -> 603 branch.
    runtimeInit(&s_t);
    {
        EventCommand l[] = {
            mk(301, 0, P(0, 1, 1, 1), NULL),
            mk(601, 0, P(0), NULL),
            mk(122, 1, P(9, 9, 0, 0, 1), NULL),
            mk(0,   1, P(0), NULL),
            mk(603, 0, P(0), NULL),
            mk(122, 1, P(9, 9, 0, 0, 3), NULL),
            mk(0,   1, P(0), NULL),
            mk(604, 0, P(0), NULL),
            mk(0,   0, P(0), NULL),
        };
        I->depth = 0; I->wait = W_NONE;
        pushFrame(l, 9, 1);
        eventsUpdate(&s_t);
        int troop; bool esc, lose;
        bool pending = eventsPendingBattle(&troop, &esc, &lose);
        eventsResolveBattle(2);
        for (int g = 0; g < 10 && I->depth; g++) eventsUpdate(&s_t);
        CHECK(pending && troop == 1 && V(9) == 3);
    }

    I->depth = 0; I->wait = W_NONE; s_msgOpen = false;
    runtimeInit(&s_t);
    if (passed) *passed = ok;
    if (total) *total = all;
    return ok == all;
}
