// ---------------------------------------------------------------------------
// battle.c - data-driven battles (see battle.h)
//
//   1. Database loading (tab-separated files from build_battle.py)
//   2. Damage formula evaluator
//   3. Battlers
//   4. Actions: hit, damage, effects, states
//   5. Enemy AI (MV action patterns)
//   6. Flow: input menus, turn, victory / defeat / escape
// ---------------------------------------------------------------------------

#include "battle.h"
#include "actors.h"
#include "gamedata.h"
#include "events.h"
#include "rng.h"

#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===========================================================================
// 1. Database
// ===========================================================================

typedef struct { int code, dataId; float v1, v2; } Effect;

typedef struct {
    int id;
    int mpCost, tpCost, stype;      // items: consumable, itype, 0
    int scope, occasion, hitType, success;
    int dmgType, element, variance, critical;
    Effect eff[8];
    int nEff;
    char formula[128];
    char message[64];
    char name[32];
} ActionDef;

typedef struct { int skillId, cond, rating; float p1, p2; } EnemyAction;

typedef struct {
    int id;
    int params[8];
    int exp, gold;
    char battler[64];
    EnemyAction act[16];
    int nAct;
    int drop[3][3];                 // kind, id, denominator
    int nDrop;
    char name[32];
} EnemyDef;

typedef struct { int enemyId, x, y, hidden; } Member;
typedef struct { int id; Member m[BT_MAX_ENEMIES]; int n; } TroopDef;

typedef struct {
    int id, restriction, timing, minT, maxT, endRemove, dmgRemove, dmgChance, priority, slip;
    char msgActor[64], msgEnemy[64];   // MV message1 / message2
    char name[32];
} StateDef;

typedef struct { int kind, id, params[8], etype; } EquipDef;
typedef struct { int id; int equips[5]; int learnLv[32], learnSk[32], nLearn; } ActorDef;
typedef struct { char key[24]; char text[64]; } Term;

static EnemyDef *s_enemies;  static int s_nEnemies;
static TroopDef *s_troops;   static int s_nTroops;
static ActionDef *s_skills;  static int s_nSkills;
static ActionDef *s_items;   static int s_nItems;
static StateDef *s_states;   static int s_nStates;
static EquipDef *s_equips;   static int s_nEquips;
static ActorDef *s_actorDefs; static int s_nActorDefs;
static Term s_terms[64];     static int s_nTerms;
static bool s_loaded;

// Splits a line on tabs in place. Returns field count.
static int splitTabs(char *line, char **f, int max) {
    int n = 0;
    char *p = line;
    while (n < max) {
        f[n++] = p;
        char *t = strchr(p, '\t');
        if (!t) break;
        *t = '\0';
        p = t + 1;
    }
    char *nl = strpbrk(f[n - 1], "\r\n");
    if (nl) *nl = '\0';
    return n;
}

// Copies with truncation (names longer than the field are cut on purpose).
static void copyStr(char *dst, size_t n, const char *src) {
    if (!n) return;
    if (!src || !strcmp(src, "-")) src = "";
    size_t len = strlen(src);
    if (len >= n) len = n - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void parseEffects(const char *src, Effect *e, int *n) {
    *n = 0;
    if (!src || !strcmp(src, "-")) return;
    const char *p = src;
    while (*p && *n < 8) {
        Effect x;
        if (sscanf(p, "%d:%d:%f:%f", &x.code, &x.dataId, &x.v1, &x.v2) == 4) e[(*n)++] = x;
        const char *semi = strchr(p, ';');
        if (!semi) break;
        p = semi + 1;
    }
}

static void *grow(void *arr, int *cap, int n, size_t size) {
    if (n < *cap) return arr;
    *cap = *cap ? *cap * 2 : 64;
    return realloc(arr, (size_t)*cap * size);
}

static FILE *openData(const char *dir, const char *name) {
    char path[256];
    snprintf(path, sizeof(path), "%s%s", dir, name);
    return fopen(path, "r");
}

static bool loadActions(const char *dir, const char *file, ActionDef **out, int *count) {
    FILE *f = openData(dir, file);
    if (!f) return false;
    char line[1024];
    char *fl[20];
    int cap = 0;
    while (fgets(line, sizeof(line), f)) {
        if (splitTabs(line, fl, 20) < 16) continue;
        ActionDef *arr = grow(*out, &cap, *count, sizeof(ActionDef));
        if (!arr) break;
        *out = arr;
        ActionDef *a = &arr[(*count)++];
        memset(a, 0, sizeof(*a));
        a->id = atoi(fl[0]);
        a->mpCost = atoi(fl[1]); a->tpCost = atoi(fl[2]); a->stype = atoi(fl[3]);
        a->scope = atoi(fl[4]); a->occasion = atoi(fl[5]); a->hitType = atoi(fl[6]);
        a->success = atoi(fl[7]); a->dmgType = atoi(fl[8]); a->element = atoi(fl[9]);
        a->variance = atoi(fl[10]); a->critical = atoi(fl[11]);
        parseEffects(fl[12], a->eff, &a->nEff);
        copyStr(a->formula, sizeof(a->formula), fl[13]);
        copyStr(a->message, sizeof(a->message), fl[14]);
        copyStr(a->name, sizeof(a->name), fl[15]);
    }
    fclose(f);
    return *count > 0;
}

bool battleLoadData(const char *dir) {
    char line[2048];
    char *fl[24];
    int cap;
    FILE *f;

    if ((f = openData(dir, "enemies.txt"))) {
        cap = 0;
        while (fgets(line, sizeof(line), f)) {
            if (splitTabs(line, fl, 24) < 15) continue;
            EnemyDef *arr = grow(s_enemies, &cap, s_nEnemies, sizeof(EnemyDef));
            if (!arr) break;
            s_enemies = arr;
            EnemyDef *e = &arr[s_nEnemies++];
            memset(e, 0, sizeof(*e));
            e->id = atoi(fl[0]);
            for (int i = 0; i < 8; i++) e->params[i] = atoi(fl[1 + i]);
            e->exp = atoi(fl[9]); e->gold = atoi(fl[10]);
            copyStr(e->battler, sizeof(e->battler), fl[11]);
            for (const char *p = fl[12]; *p && strcmp(p, "-") && e->nAct < 16; ) {
                EnemyAction *a = &e->act[e->nAct];
                if (sscanf(p, "%d:%d:%f:%f:%d", &a->skillId, &a->cond, &a->p1, &a->p2, &a->rating) == 5)
                    e->nAct++;
                const char *semi = strchr(p, ';');
                if (!semi) break;
                p = semi + 1;
            }
            for (const char *p = fl[13]; *p && strcmp(p, "-") && e->nDrop < 3; ) {
                int *d = e->drop[e->nDrop];
                if (sscanf(p, "%d:%d:%d", &d[0], &d[1], &d[2]) == 3) e->nDrop++;
                const char *semi = strchr(p, ';');
                if (!semi) break;
                p = semi + 1;
            }
            copyStr(e->name, sizeof(e->name), fl[14]);
        }
        fclose(f);
    }

    if ((f = openData(dir, "troops.txt"))) {
        cap = 0;
        while (fgets(line, sizeof(line), f)) {
            if (splitTabs(line, fl, 4) < 3) continue;
            TroopDef *arr = grow(s_troops, &cap, s_nTroops, sizeof(TroopDef));
            if (!arr) break;
            s_troops = arr;
            TroopDef *t = &arr[s_nTroops++];
            memset(t, 0, sizeof(*t));
            t->id = atoi(fl[0]);
            for (const char *p = fl[1]; *p && strcmp(p, "-") && t->n < BT_MAX_ENEMIES; ) {
                Member *m = &t->m[t->n];
                if (sscanf(p, "%d:%d:%d:%d", &m->enemyId, &m->x, &m->y, &m->hidden) == 4) t->n++;
                const char *semi = strchr(p, ';');
                if (!semi) break;
                p = semi + 1;
            }
        }
        fclose(f);
    }

    loadActions(dir, "skills.txt", &s_skills, &s_nSkills);
    loadActions(dir, "useitems.txt", &s_items, &s_nItems);

    if ((f = openData(dir, "states.txt"))) {
        cap = 0;
        while (fgets(line, sizeof(line), f)) {
            int nf = splitTabs(line, fl, 14);
            if (nf < 12) continue;
            StateDef *arr = grow(s_states, &cap, s_nStates, sizeof(StateDef));
            if (!arr) break;
            s_states = arr;
            StateDef *st = &arr[s_nStates++];
            memset(st, 0, sizeof(*st));
            st->id = atoi(fl[0]); st->restriction = atoi(fl[1]); st->timing = atoi(fl[2]);
            st->minT = atoi(fl[3]); st->maxT = atoi(fl[4]); st->endRemove = atoi(fl[5]);
            st->dmgRemove = atoi(fl[6]); st->dmgChance = atoi(fl[7]); st->priority = atoi(fl[8]);
            st->slip = atoi(fl[9]);
            copyStr(st->msgActor, sizeof(st->msgActor), fl[10]);
            if (nf >= 13) {               // newer export: separate enemy message
                copyStr(st->msgEnemy, sizeof(st->msgEnemy), fl[11]);
                copyStr(st->name, sizeof(st->name), fl[12]);
            } else {
                copyStr(st->msgEnemy, sizeof(st->msgEnemy), fl[10]);
                copyStr(st->name, sizeof(st->name), fl[11]);
            }
        }
        fclose(f);
    }

    if ((f = openData(dir, "equips.txt"))) {
        cap = 0;
        while (fgets(line, sizeof(line), f)) {
            int nf = splitTabs(line, fl, 13);
            if (nf < 11) continue;
            EquipDef *arr = grow(s_equips, &cap, s_nEquips, sizeof(EquipDef));
            if (!arr) break;
            s_equips = arr;
            EquipDef *e = &arr[s_nEquips++];
            e->kind = atoi(fl[0]); e->id = atoi(fl[1]);
            for (int i = 0; i < 8; i++) e->params[i] = atoi(fl[2 + i]);
            e->etype = nf >= 12 ? atoi(fl[10]) : (e->kind == 1 ? 1 : 2);
        }
        fclose(f);
    }

    if ((f = openData(dir, "actors.txt"))) {
        cap = 0;
        while (fgets(line, sizeof(line), f)) {
            if (splitTabs(line, fl, 6) < 4) continue;
            ActorDef *arr = grow(s_actorDefs, &cap, s_nActorDefs, sizeof(ActorDef));
            if (!arr) break;
            s_actorDefs = arr;
            ActorDef *a = &arr[s_nActorDefs++];
            memset(a, 0, sizeof(*a));
            a->id = atoi(fl[0]);
            sscanf(fl[1], "%d:%d:%d:%d:%d", &a->equips[0], &a->equips[1], &a->equips[2],
                   &a->equips[3], &a->equips[4]);
            for (const char *p = fl[2]; *p && strcmp(p, "-") && a->nLearn < 32; ) {
                if (sscanf(p, "%d:%d", &a->learnLv[a->nLearn], &a->learnSk[a->nLearn]) == 2) a->nLearn++;
                const char *semi = strchr(p, ';');
                if (!semi) break;
                p = semi + 1;
            }
        }
        fclose(f);
    }

    if ((f = openData(dir, "terms.txt"))) {
        while (s_nTerms < 64 && fgets(line, sizeof(line), f)) {
            if (splitTabs(line, fl, 2) < 2) continue;
            copyStr(s_terms[s_nTerms].key, sizeof(s_terms[0].key), fl[0]);
            copyStr(s_terms[s_nTerms].text, sizeof(s_terms[0].text), fl[1]);
            s_nTerms++;
        }
        fclose(f);
    }

    s_loaded = s_nTroops > 0 && s_nEnemies > 0;
    return s_loaded;
}

bool battleDataLoaded(void) { return s_loaded; }

static const EnemyDef *enemyDef(int id) {
    for (int i = 0; i < s_nEnemies; i++) if (s_enemies[i].id == id) return &s_enemies[i];
    return NULL;
}
static const TroopDef *troopDef(int id) {
    for (int i = 0; i < s_nTroops; i++) if (s_troops[i].id == id) return &s_troops[i];
    return NULL;
}
static const ActionDef *skillDef(int id) {
    for (int i = 0; i < s_nSkills; i++) if (s_skills[i].id == id) return &s_skills[i];
    return NULL;
}
static const ActionDef *itemDef(int id) {
    for (int i = 0; i < s_nItems; i++) if (s_items[i].id == id) return &s_items[i];
    return NULL;
}
static const StateDef *stateDef(int id) {
    for (int i = 0; i < s_nStates; i++) if (s_states[i].id == id) return &s_states[i];
    return NULL;
}
static const EquipDef *equipDef(int kind, int id) {
    for (int i = 0; i < s_nEquips; i++)
        if (s_equips[i].kind == kind && s_equips[i].id == id) return &s_equips[i];
    return NULL;
}
static const ActorDef *actorDef(int id) {
    for (int i = 0; i < s_nActorDefs; i++) if (s_actorDefs[i].id == id) return &s_actorDefs[i];
    return NULL;
}

static const char *term(const char *key, const char *fallback);
const char *battleTerm(const char *key, const char *fallback) { return term(key, fallback); }

// The game's own message for `key`; `fallback` only if the game has none.
static const char *term(const char *key, const char *fallback) {
    for (int i = 0; i < s_nTerms; i++)
        if (!strcmp(s_terms[i].key, key) && s_terms[i].text[0]) return s_terms[i].text;
    return fallback;
}

static void fmt3(char *out, size_t n, const char *pattern,
                 const char *a1, const char *a2, const char *a3);

static GameState *s_gs;

// $gameParty.name(): the leader's name, or partyName ("%1's party") with it.
static const char *partyName(void) {
    static char buf[48];
    int leader = runtimePartyLeader(s_gs);
    const char *ln = leader ? actorName(s_gs, leader) : "";
    if (s_gs->partyCount <= 1) return ln;
    fmt3(buf, sizeof(buf), term("partyName", "%1's party"), ln, "", "");
    return buf;
}

// RPG Maker's "%1 %2 %3" substitution.
static void fmt3(char *out, size_t n, const char *pattern,
                 const char *a1, const char *a2, const char *a3) {
    size_t o = 0;
    for (const char *p = pattern; *p && o + 1 < n; p++) {
        if (p[0] == '%' && p[1] >= '1' && p[1] <= '3') {
            const char *arg = p[1] == '1' ? a1 : p[1] == '2' ? a2 : a3;
            for (; arg && *arg && o + 1 < n; arg++) out[o++] = *arg;
            p++;
        } else {
            out[o++] = *p;
        }
    }
    out[o] = '\0';
}

// ===========================================================================
// 2. Damage formula evaluator: numbers, + - * / %, parentheses,
//    a.X / b.X stats, v[n], Math.floor/round/ceil/abs/max/min/random.
// ===========================================================================

typedef struct {
    const char *p;
    const int *a, *b;           // 13 values: mhp mmp atk def mat mdf agi luk hp mp tp level -
    const GameState *s;
    bool bad;
} Expr;

static double exprAdd(Expr *e);

static void ws(Expr *e) { while (*e->p == ' ') e->p++; }

static int statIndex(const char *name, size_t len) {
    static const char *names[] = { "mhp", "mmp", "atk", "def", "mat", "mdf", "agi", "luk",
                                   "hp", "mp", "tp", "level" };
    for (int i = 0; i < 12; i++)
        if (strlen(names[i]) == len && !strncmp(names[i], name, len)) return i;
    return -1;
}

static double exprAtom(Expr *e) {
    ws(e);
    if (*e->p == '(') {
        e->p++;
        double v = exprAdd(e);
        ws(e);
        if (*e->p == ')') e->p++; else e->bad = true;
        return v;
    }
    if (*e->p == '-') { e->p++; return -exprAtom(e); }
    if (*e->p == '+') { e->p++; return exprAtom(e); }
    if (isdigit((unsigned char)*e->p) || *e->p == '.') {
        char *end;
        double v = strtod(e->p, &end);
        e->p = end;
        return v;
    }
    if ((e->p[0] == 'a' || e->p[0] == 'b') && e->p[1] == '.') {
        const int *who = e->p[0] == 'a' ? e->a : e->b;
        e->p += 2;
        const char *n = e->p;
        while (isalpha((unsigned char)*e->p)) e->p++;
        int idx = statIndex(n, (size_t)(e->p - n));
        if (idx < 0) { e->bad = true; return 0; }
        return who ? who[idx] : 0;
    }
    if (e->p[0] == 'v' && e->p[1] == '[') {
        e->p += 2;
        double id = exprAdd(e);
        ws(e);
        if (*e->p == ']') e->p++; else e->bad = true;
        return runtimeGetVariable(e->s, (uint16_t)id);
    }
    if (!strncmp(e->p, "Math.", 5)) {
        e->p += 5;
        const char *n = e->p;
        while (isalpha((unsigned char)*e->p)) e->p++;
        size_t len = (size_t)(e->p - n);
        ws(e);
        if (*e->p != '(') { e->bad = true; return 0; }
        e->p++;
        double args[2] = { 0, 0 };
        int argc = 0;
        ws(e);
        if (*e->p != ')') {
            for (;;) {
                double v = exprAdd(e);
                if (argc < 2) args[argc] = v;
                argc++;
                ws(e);
                if (*e->p == ',') { e->p++; continue; }
                break;
            }
        }
        if (*e->p == ')') e->p++; else e->bad = true;
        if (len == 5 && !strncmp(n, "floor", 5)) return floor(args[0]);
        if (len == 5 && !strncmp(n, "round", 5)) return floor(args[0] + 0.5);
        if (len == 4 && !strncmp(n, "ceil", 4))  return ceil(args[0]);
        if (len == 3 && !strncmp(n, "abs", 3))   return fabs(args[0]);
        if (len == 3 && !strncmp(n, "max", 3))   return args[0] > args[1] ? args[0] : args[1];
        if (len == 3 && !strncmp(n, "min", 3))   return args[0] < args[1] ? args[0] : args[1];
        if (len == 6 && !strncmp(n, "random", 6)) return rngNext(10000) / 10000.0;
        e->bad = true;
        return 0;
    }
    e->bad = true;
    return 0;
}

static double exprMul(Expr *e) {
    double v = exprAtom(e);
    for (;;) {
        ws(e);
        char op = *e->p;
        if (op != '*' && op != '/' && op != '%') return v;
        e->p++;
        double r = exprAtom(e);
        if (op == '*') v *= r;
        else if (op == '/') v = r != 0 ? v / r : 0;
        else v = r != 0 ? fmod(v, r) : 0;
    }
}

static double exprAdd(Expr *e) {
    double v = exprMul(e);
    for (;;) {
        ws(e);
        char op = *e->p;
        if (op != '+' && op != '-') return v;
        e->p++;
        double r = exprMul(e);
        v = op == '+' ? v + r : v - r;
    }
}

double battleEvalFormula(const char *f, const int *aParams, const int *bParams,
                         const GameState *s) {
    Expr e = { f ? f : "0", aParams, bParams, s, false };
    double v = exprAdd(&e);
    ws(&e);
    if (*e.p == ';') e.p++;
    ws(&e);
    if (e.bad || *e.p) return 0;     // unsupported JavaScript: no damage
    return v;
}

// ===========================================================================
// 3. Battlers
// ===========================================================================

typedef struct {
    bool used, isEnemy, hidden, guarding, escaped;
    int dbId;                       // enemy id or actor id
    char name[32];
    int hp, mp, tp;
    int params[8];
    uint8_t states[FH_MAX_STATES / 8];
    int8_t turns[FH_MAX_STATES];
    int x, y;                       // enemies: MV screen position
    char battler[64];
} Battler;

static Battler s_enemyB[BT_MAX_ENEMIES], s_actorB[BT_MAX_ACTORS];
static int s_nEnemyB, s_nActorB;

static bool hasState(const Battler *b, int st) {
    return st > 0 && st < FH_MAX_STATES && ((b->states[st >> 3] >> (st & 7)) & 1);
}
static bool isAlive(const Battler *b) { return b->used && !b->hidden && !b->escaped && !hasState(b, 1); }

static void statArray(const Battler *b, int out[13]) {
    for (int i = 0; i < 8; i++) out[i] = b->params[i];
    out[8] = b->hp; out[9] = b->mp; out[10] = b->tp; out[11] = 1; out[12] = 0;
}

// Highest restriction among current states (4 = cannot move).
static int restriction(const Battler *b) {
    int r = 0;
    for (int st = 1; st < FH_MAX_STATES; st++)
        if (hasState(b, st)) {
            const StateDef *d = stateDef(st);
            if (d && d->restriction > r) r = d->restriction;
        }
    return r;
}

// ===========================================================================
// 6a. Battle state & log (used by sections 4 and 5)
// ===========================================================================

enum { PH_NONE, PH_START, PH_INPUT, PH_TURN, PH_TURNEND, PH_END };
enum { MENU_CMD, MENU_SKILL, MENU_ITEM, MENU_ENEMY, MENU_ALLY };

typedef struct { Battler *subject; bool isItem; int id; int target; bool guard, escape; } Action;

static int s_phase = PH_NONE;
static int s_troopId;
static bool s_ranBattle[64], s_ranTurn[64];   // troop pages already run
static bool s_turnEnding, s_turnEndDone, s_abort;
static int s_result = -1, s_pendingResult = -1;
static bool s_canEscape, s_canLose;
static int s_turn, s_escapeTries;
static int s_delay;
static BattleView s_view;
static char s_back1[64], s_back2[64];

static Action s_queue[BT_MAX_ENEMIES + BT_MAX_ACTORS];
static int s_nQueue, s_qPos;

// input state
static int s_actorIdx;              // whose command
static int s_menu, s_cursor;
static int s_menuIds[BT_MENU_MAX];  // skill/item ids or battler indexes
static int s_menuN;
static Action s_pending;            // being built
static bool s_kUp, s_kDown, s_kLeft, s_kRight, s_kOk, s_kBack;

static void blog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void blog(const char *fmt, ...) {
    for (int i = 0; i < BT_LOG_LINES - 1; i++)
        memcpy(s_view.log[i], s_view.log[i + 1], sizeof(s_view.log[0]));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_view.log[BT_LOG_LINES - 1], sizeof(s_view.log[0]), fmt, ap);
    va_end(ap);
}

// ===========================================================================
// 4. Actions
// ===========================================================================

static void addStateEx(Battler *b, int st, bool quiet) {
    if (st < 1 || st >= FH_MAX_STATES || !b->used) return;
    const StateDef *d = stateDef(st);
    bool had = hasState(b, st);
    b->states[st >> 3] |= (uint8_t)(1 << (st & 7));
    if (st == 1) b->hp = 0;
    if (d) {
        int span = d->maxT - d->minT + 1;
        b->turns[st] = (int8_t)(d->minT + (span > 0 ? rngNext(span) : 0));
        const char *msg = b->isEnemy ? d->msgEnemy : d->msgActor;
        if (!had && !quiet && msg[0]) blog("%s%s", b->name, msg);
    }
}

static void addState(Battler *b, int st) { addStateEx(b, st, false); }

static void removeState(Battler *b, int st) {
    if (st < 1 || st >= FH_MAX_STATES) return;
    b->states[st >> 3] &= (uint8_t)~(1 << (st & 7));
    b->turns[st] = 0;
    if (st == 1 && b->hp <= 0) b->hp = 1;
}

static void checkDeath(Battler *b) {
    if (b->hp <= 0 && !hasState(b, 1)) {
        b->hp = 0;
        addState(b, 1);                        // the state's own death message
    }
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void applyEffects(Battler *user, Battler *t, const ActionDef *a) {
    for (int i = 0; i < a->nEff; i++) {
        const Effect *e = &a->eff[i];
        switch (e->code) {
            case 11: {  // recover HP: rate * mhp + flat
                int v = (int)floor(t->params[0] * e->v1 + e->v2);
                t->hp = clampi(t->hp + v, 0, t->params[0]);
                if (v) {
                    char n[16]; snprintf(n, sizeof(n), "%d", v > 0 ? v : -v);
                    char line[64];
                    fmt3(line, sizeof(line), term(t->isEnemy ? "enemyRecovery" : "actorRecovery",
                         "%1 recovered %2 %3."), t->name, "HP", n);
                    blog("%s", line);
                }
                checkDeath(t);
                break;
            }
            case 12: {  // recover MP
                int v = (int)floor(t->params[1] * e->v1 + e->v2);
                t->mp = clampi(t->mp + v, 0, t->params[1]);
                break;
            }
            case 13: t->tp = clampi(t->tp + (int)e->v1, 0, 100); break;
            case 21:    // add state (dataId 0 = attack's states: not modelled)
                if (e->dataId > 0 && rngNext(1000) < (int)(e->v1 * 1000)) addState(t, e->dataId);
                break;
            case 22:
                if (rngNext(1000) < (int)(e->v1 * 1000)) removeState(t, e->dataId);
                break;
            case 41:    // special: escape
                t->escaped = true;
                break;
            case 44:    // common event: runs through the event system
                eventsBattleCommon(e->dataId);
                break;
            default: break;
        }
    }
    (void)user;
}

// One action against one target (MV Game_Action.apply).
static void applyTo(Battler *user, Battler *t, const ActionDef *a) {
    if (!t->used || t->hidden) return;

    // Hit / evasion (physical uses 95% hit, 5% evasion like MV's defaults).
    int roll = rngNext(100);
    int hit = a->success;
    if (a->hitType == 1) hit = hit * 95 / 100;
    if (roll >= hit) {
        char line[64];
        fmt3(line, sizeof(line), term(t->isEnemy ? "enemyNoHit" : "actorNoHit",
             "%1 was not hit."), t->name, "", "");
        blog("%s", line);
        return;
    }
    if (a->hitType == 1 && rngNext(100) < 5) {
        char line[64];
        fmt3(line, sizeof(line), term("evasion", "%1 evaded the attack!"), t->name, "", "");
        blog("%s", line);
        return;
    }

    if (a->dmgType > 0) {
        int ua[13], ta[13];
        statArray(user, ua);
        statArray(t, ta);
        double base = battleEvalFormula(a->formula, ua, ta, s_gs);
        if (base < 0) base = 0;
        if (a->dmgType == 3 || a->dmgType == 4) base = -base;     // recovery
        int amp = (int)floor(fabs(base) * a->variance / 100.0);
        int v = (int)floor(base + 0.5);
        if (amp > 0) v += rngNext(amp + 1) + rngNext(amp + 1) - amp;
        if (v > 0 && t->guarding) v /= 2;

        bool mp = (a->dmgType == 2 || a->dmgType == 4 || a->dmgType == 6);
        char num[16], line[64];
        snprintf(num, sizeof(num), "%d", v < 0 ? -v : v);
        if (mp) {
            t->mp = clampi(t->mp - v, 0, t->params[1]);
        } else {
            t->hp = clampi(t->hp - v, 0, t->params[0]);
            if (a->dmgType == 5 && v > 0) user->hp = clampi(user->hp + v, 0, user->params[0]);
        }
        if (v > 0) {
            fmt3(line, sizeof(line), term(t->isEnemy ? "enemyDamage" : "actorDamage",
                 "%1 took %2 damage!"), t->name, num, "");
            // damage can lift states marked "remove by damage"
            for (int st = 2; st < FH_MAX_STATES; st++)
                if (hasState(t, st)) {
                    const StateDef *d = stateDef(st);
                    if (d && d->dmgRemove && rngNext(100) < d->dmgChance) removeState(t, st);
                }
        } else if (v < 0) {
            fmt3(line, sizeof(line), term(t->isEnemy ? "enemyRecovery" : "actorRecovery",
                 "%1 recovered %2 %3."), t->name, mp ? "MP" : "HP", num);
        } else {
            fmt3(line, sizeof(line), term(t->isEnemy ? "enemyNoDamage" : "actorNoDamage",
                 "%1 took no damage!"), t->name, "", "");
        }
        blog("%s", line);
        checkDeath(t);
    }
    applyEffects(user, t, a);
}

static Battler *opponents(Battler *b, int *n) {
    if (b->isEnemy) { *n = s_nActorB; return s_actorB; }
    *n = s_nEnemyB; return s_enemyB;
}
static Battler *friends(Battler *b, int *n) {
    if (b->isEnemy) { *n = s_nEnemyB; return s_enemyB; }
    *n = s_nActorB; return s_actorB;
}

static Battler *randomAlive(Battler *arr, int n) {
    int alive[BT_MAX_ENEMIES], k = 0;
    for (int i = 0; i < n; i++) if (isAlive(&arr[i])) alive[k++] = i;
    return k ? &arr[alive[rngNext(k)]] : NULL;
}

static void performAction(Action *act) {
    Battler *u = act->subject;
    if (!isAlive(u)) return;

    if (act->guard) { u->guarding = true; return; }

    const ActionDef *a = act->isItem ? itemDef(act->id) : skillDef(act->id);
    if (!a) return;

    // Costs / item use
    if (!act->isItem) {
        if (u->mp < a->mpCost || u->tp < a->tpCost) return;
        u->mp -= a->mpCost;
        u->tp -= a->tpCost;
    } else if (!u->isEnemy && a->mpCost /* consumable */) {
        runtimeAddOwned(s_gs, DB_ITEM, a->id, -1);
    }

    // Message: subject name + the skill's own message (MV message1).
    char line[96];
    if (act->isItem) fmt3(line, sizeof(line), term("useItem", "%1 uses %2!"), u->name, a->name, "");
    else if (a->message[0]) {
        char msg[64];
        fmt3(msg, sizeof(msg), a->message, a->name, "", "");
        snprintf(line, sizeof(line), "%s%s", u->name, msg);
    } else line[0] = '\0';
    if (line[0]) blog("%s", line);

    int n;
    Battler *opp = opponents(u, &n);
    int fn;
    Battler *fr = friends(u, &fn);

    // Restriction 1-3 (confusion etc.): attack a random opponent.
    int r = restriction(u);
    switch (r >= 1 && r <= 3 ? 3 : a->scope) {
        case 1: {
            Battler *t = (act->target >= 0 && act->target < n && isAlive(&opp[act->target]))
                       ? &opp[act->target] : randomAlive(opp, n);
            if (t) applyTo(u, t, a);
            break;
        }
        case 2:
            for (int i = 0; i < n; i++) if (isAlive(&opp[i])) applyTo(u, &opp[i], a);
            break;
        case 3: case 4: case 5: case 6: {
            int times = (r >= 1 && r <= 3) ? 1 : a->scope - 2;
            for (int k = 0; k < times; k++) {
                Battler *t = randomAlive(opp, n);
                if (t) applyTo(u, t, a);
            }
            break;
        }
        case 7: case 9: {
            Battler *t = (act->target >= 0 && act->target < fn) ? &fr[act->target] : u;
            applyTo(u, t, a);
            break;
        }
        case 8: case 10:
            for (int i = 0; i < fn; i++) if (fr[i].used) applyTo(u, &fr[i], a);
            break;
        case 11:
            applyTo(u, u, a);
            break;
        default:
            applyEffects(u, u, a);
            break;
    }

    // States that wear off after acting
    for (int st = 2; st < FH_MAX_STATES; st++)
        if (hasState(u, st)) {
            const StateDef *d = stateDef(st);
            if (d && d->timing == 1 && --u->turns[st] <= 0) removeState(u, st);
        }
}

// ===========================================================================
// 5. Enemy AI (Game_Enemy.selectAllActions)
// ===========================================================================

static bool conditionMet(const Battler *b, const EnemyAction *a) {
    switch (a->cond) {
        case 1: {  // turn: p1 + p2 * X
            int t = s_turn, p1 = (int)a->p1, p2 = (int)a->p2;
            if (p2 == 0) return t == p1;
            return t > 0 && t >= p1 && (t - p1) % p2 == 0;
        }
        case 2: {  float r = b->params[0] ? (float)b->hp / b->params[0] : 0; return r >= a->p1 && r <= a->p2; }
        case 3: {  float r = b->params[1] ? (float)b->mp / b->params[1] : 0; return r >= a->p1 && r <= a->p2; }
        case 4: return hasState(b, (int)a->p1);
        case 5: return true;  // party level >= p1: levels are low; treat as met
        case 6: return runtimeGetSwitch(s_gs, (uint16_t)a->p1);
        default: return true;
    }
}

static int chooseEnemySkill(const Battler *b) {
    const EnemyDef *d = enemyDef(b->dbId);
    if (!d || !d->nAct) return 1;
    int ok[16], k = 0, maxR = 0;
    for (int i = 0; i < d->nAct; i++) {
        const ActionDef *sk = skillDef(d->act[i].skillId);
        if (!sk || b->mp < sk->mpCost || !conditionMet(b, &d->act[i])) continue;
        ok[k++] = i;
        if (d->act[i].rating > maxR) maxR = d->act[i].rating;
    }
    if (!k) return 0;
    int zero = maxR - 3, total = 0;
    for (int i = 0; i < k; i++) if (d->act[ok[i]].rating > zero) total += d->act[ok[i]].rating - zero;
    int roll = rngNext(total > 0 ? total : 1);
    for (int i = 0; i < k; i++) {
        int w = d->act[ok[i]].rating - zero;
        if (w <= 0) continue;
        if (roll < w) return d->act[ok[i]].skillId;
        roll -= w;
    }
    return d->act[ok[0]].skillId;
}

// ===========================================================================
// 6. Flow
// ===========================================================================

static void ensureEquips(GameState *s, int actorId) {
    ActorState *a = actorGet(s, actorId);
    if (!a || a->equipsSet) return;
    const ActorDef *def = actorDef(actorId);
    for (int i = 0; i < 5; i++) a->equips[i] = def ? (int16_t)def->equips[i] : 0;
    a->equipsSet = true;
}

static void readActor(Battler *b, int actorId) {
    ActorState *a = actorGet(s_gs, actorId);
    memset(b, 0, sizeof(*b));
    b->used = true;
    b->dbId = actorId;
    snprintf(b->name, sizeof(b->name), "%s", actorName(s_gs, actorId));
    for (int i = 0; i < 8; i++) b->params[i] = actorParam(s_gs, actorId, i);
    ensureEquips(s_gs, actorId);
    for (int slot = 0; a && slot < 5; slot++) {
        const EquipDef *e = equipDef(slot == 0 ? 1 : 2, a->equips[slot]);
        if (e) for (int i = 0; i < 8; i++) b->params[i] += e->params[i];
    }
    if (b->params[0] < 1) b->params[0] = 1;
    b->hp = a ? a->hp : b->params[0];
    b->mp = a ? a->mp : b->params[1];
    b->tp = a ? a->tp : 0;
    if (a) {
        memcpy(b->states, a->states, sizeof(b->states));
        memcpy(b->turns, a->stateTurns, sizeof(b->turns));
    }
}

static void writeBackActors(void) {
    for (int i = 0; i < s_nActorB; i++) {
        Battler *b = &s_actorB[i];
        ActorState *a = actorGet(s_gs, b->dbId);
        if (!a) continue;
        a->hp = b->hp; a->mp = b->mp; a->tp = b->tp;
        for (int st = 1; st < FH_MAX_STATES; st++) {
            const StateDef *d = stateDef(st);
            if (d && d->endRemove) removeState(b, st);
        }
        memcpy(a->states, b->states, sizeof(a->states));
        memcpy(a->stateTurns, b->turns, sizeof(a->stateTurns));
    }
}

bool battleStart(GameState *s, int troopId, bool canEscape, bool canLose) {
    const TroopDef *t = troopDef(troopId);
    if (!t) return false;
    s_gs = s;
    memset(&s_view, 0, sizeof(s_view));
    memset(s_enemyB, 0, sizeof(s_enemyB));
    s_nEnemyB = 0;
    for (int i = 0; i < t->n; i++) {
        const EnemyDef *d = enemyDef(t->m[i].enemyId);
        Battler *b = &s_enemyB[s_nEnemyB++];   // slot i = troop member i
        if (!d) continue;                       // unknown enemy: empty slot
        b->used = true;
        b->isEnemy = true;
        b->dbId = d->id;
        b->hidden = t->m[i].hidden;
        snprintf(b->name, sizeof(b->name), "%s", d->name);
        memcpy(b->params, d->params, sizeof(b->params));
        b->hp = d->params[0];
        b->mp = d->params[1];
        b->x = t->m[i].x;
        b->y = t->m[i].y;
        snprintf(b->battler, sizeof(b->battler), "%s", d->battler);
    }
    s_nActorB = 0;
    for (int i = 0; i < s->partyCount && s_nActorB < BT_MAX_ACTORS; i++)
        readActor(&s_actorB[s_nActorB++], s->partyOrder[i]);

    s_canEscape = canEscape;
    s_canLose = canLose;
    s_troopId = troopId;
    memset(s_ranBattle, 0, sizeof(s_ranBattle));
    memset(s_ranTurn, 0, sizeof(s_ranTurn));
    s_turnEnding = s_turnEndDone = s_abort = false;
    s_turn = 0;
    s_escapeTries = 0;
    s_result = -1;
    s_pendingResult = -1;
    s_phase = PH_START;
    s_delay = 45;
    for (int i = 0; i < s_nEnemyB; i++)
        if (!s_enemyB[i].hidden) {
            char line[64];
            fmt3(line, sizeof(line), term("emerge", "%1 emerged!"), s_enemyB[i].name, "", "");
            blog("%s", line);
        }
    return true;
}

bool battleActive(void) { return s_phase != PH_NONE; }
int battleResult(void) { return s_result; }
void battleFinish(void) { s_result = -1; s_phase = PH_NONE; }

void battleInput(bool up, bool down, bool left, bool right, bool confirm, bool cancel) {
    s_kUp = up; s_kDown = down; s_kLeft = left; s_kRight = right; s_kOk = confirm; s_kBack = cancel;
}

static bool allDead(Battler *arr, int n) {
    for (int i = 0; i < n; i++) if (isAlive(&arr[i])) return false;
    return true;
}

static bool canAct(const Battler *b) { return isAlive(b) && restriction(b) < 4; }

static bool skillKnown(int actorId, int skillId, int level) {
    const ActorState *a = &s_gs->actors[actorId];
    if (skillId < 512 && ((a->forgotten[skillId >> 3] >> (skillId & 7)) & 1)) return false;
    if (skillId < 512 && ((a->learned[skillId >> 3] >> (skillId & 7)) & 1)) return true;
    const ActorDef *d = actorDef(actorId);
    for (int i = 0; d && i < d->nLearn; i++)
        if (d->learnSk[i] == skillId && d->learnLv[i] <= level) return true;
    return false;
}

static void openMenu(int menu) {
    s_menu = menu;
    s_cursor = 0;
    s_menuN = 0;
    Battler *a = &s_actorB[s_actorIdx];
    switch (menu) {
        case MENU_CMD: {
            static const char *cmds[] = { "Attack", "Skills", "Items", "Guard", "Escape" };
            for (int i = 0; i < 5; i++) {
                snprintf(s_view.menuItems[i], 32, "%s", cmds[i]);
                s_view.menuEnabled[i] = (i != 4) || s_canEscape;
                s_menuIds[i] = i;
            }
            s_menuN = 5;
            snprintf(s_view.menuTitle, sizeof(s_view.menuTitle), "%.20s", a->name);
            break;
        }
        case MENU_SKILL: {
            int level = s_gs->actors[a->dbId].level;
            for (int i = 0; i < s_nSkills && s_menuN < BT_MENU_MAX; i++) {
                const ActionDef *sk = &s_skills[i];
                if (sk->id <= 2 || (sk->occasion != 0 && sk->occasion != 1)) continue;
                if (!skillKnown(a->dbId, sk->id, level)) continue;
                s_menuIds[s_menuN] = sk->id;
                if (sk->mpCost) snprintf(s_view.menuItems[s_menuN], 32, "%.22s %d", sk->name, sk->mpCost);
                else snprintf(s_view.menuItems[s_menuN], 32, "%.30s", sk->name);
                s_view.menuEnabled[s_menuN] = a->mp >= sk->mpCost && a->tp >= sk->tpCost;
                s_menuN++;
            }
            snprintf(s_view.menuTitle, sizeof(s_view.menuTitle), "Skills");
            break;
        }
        case MENU_ITEM: {
            for (int i = 0; i < s_nItems && s_menuN < BT_MENU_MAX; i++) {
                const ActionDef *it = &s_items[i];
                int cnt = runtimeGetOwned(s_gs, DB_ITEM, it->id);
                if (cnt <= 0 || (it->occasion != 0 && it->occasion != 1)) continue;
                s_menuIds[s_menuN] = it->id;
                snprintf(s_view.menuItems[s_menuN], 32, "%.24s x%d", it->name, cnt > 99 ? 99 : cnt);
                s_view.menuEnabled[s_menuN] = true;
                s_menuN++;
            }
            snprintf(s_view.menuTitle, sizeof(s_view.menuTitle), "Items");
            break;
        }
        case MENU_ENEMY:
            for (int i = 0; i < s_nEnemyB; i++) {
                if (!isAlive(&s_enemyB[i])) continue;
                s_menuIds[s_menuN] = i;
                snprintf(s_view.menuItems[s_menuN], 32, "%.30s", s_enemyB[i].name);
                s_view.menuEnabled[s_menuN] = true;
                s_menuN++;
            }
            snprintf(s_view.menuTitle, sizeof(s_view.menuTitle), "Target");
            break;
        case MENU_ALLY:
            for (int i = 0; i < s_nActorB; i++) {
                s_menuIds[s_menuN] = i;
                snprintf(s_view.menuItems[s_menuN], 32, "%.30s", s_actorB[i].name);
                s_view.menuEnabled[s_menuN] = true;
                s_menuN++;
            }
            snprintf(s_view.menuTitle, sizeof(s_view.menuTitle), "Target");
            break;
    }
    if (s_menuN == 0) {
        snprintf(s_view.menuItems[0], 32, "(none)");
        s_view.menuEnabled[0] = false;
    }
}

static void nextActorOrTurn(void);

static Action s_actorActions[BT_MAX_ACTORS];
static int s_nActorActions;

static void startTurn(void) {
    s_turn++;
    s_nQueue = 0;
    // actors' chosen actions were queued during input; add enemies
    for (int i = 0; i < s_nEnemyB; i++) {
        Battler *e = &s_enemyB[i];
        if (!canAct(e)) continue;
        e->guarding = false;
        int sk = chooseEnemySkill(e);
        if (!sk) continue;
        Action a = { e, false, sk, -1, false, false };
        s_queue[s_nQueue++] = a;
    }
    // add the actions the party chose
    for (int i = 0; i < s_nActorActions; i++) s_queue[s_nQueue++] = s_actorActions[i];

    // speed = agi + random(agi/4), highest first
    int speed[BT_MAX_ENEMIES + BT_MAX_ACTORS];
    for (int i = 0; i < s_nQueue; i++) {
        int agi = s_queue[i].subject->params[6];
        speed[i] = agi + rngNext(agi / 4 + 1) + (s_queue[i].guard ? 2000 : 0);
    }
    for (int i = 1; i < s_nQueue; i++)
        for (int j = i; j > 0 && speed[j] > speed[j - 1]; j--) {
            int ts = speed[j]; speed[j] = speed[j - 1]; speed[j - 1] = ts;
            Action ta = s_queue[j]; s_queue[j] = s_queue[j - 1]; s_queue[j - 1] = ta;
        }
    s_qPos = 0;
    s_phase = PH_TURN;
    s_delay = 10;
    s_view.menuCount = 0;
    s_view.activeActor = -1;
}

static void beginInput(void) {
    s_nActorActions = 0;
    s_actorIdx = -1;
    for (int i = 0; i < s_nActorB; i++) s_actorB[i].guarding = false;
    s_phase = PH_INPUT;
    nextActorOrTurn();
}

static void nextActorOrTurn(void) {
    do { s_actorIdx++; } while (s_actorIdx < s_nActorB && !canAct(&s_actorB[s_actorIdx]));
    if (s_actorIdx >= s_nActorB) { startTurn(); return; }
    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.subject = &s_actorB[s_actorIdx];
    s_pending.target = -1;
    openMenu(MENU_CMD);
}

static void commit(void) {
    s_actorActions[s_nActorActions++] = s_pending;
    nextActorOrTurn();
}

static void chooseTarget(const ActionDef *a) {
    if (a->scope == 1) openMenu(MENU_ENEMY);
    else if (a->scope == 7 || a->scope == 9) openMenu(MENU_ALLY);
    else commit();
}

static void tryEscape(void) {
    int partyAgi = 0, troopAgi = 0, pn = 0, tn = 0;
    for (int i = 0; i < s_nActorB; i++) if (isAlive(&s_actorB[i])) { partyAgi += s_actorB[i].params[6]; pn++; }
    for (int i = 0; i < s_nEnemyB; i++) if (isAlive(&s_enemyB[i])) { troopAgi += s_enemyB[i].params[6]; tn++; }
    double ratio = 0.5 * (pn ? partyAgi / (double)pn : 1) / (tn && troopAgi ? troopAgi / (double)tn : 1)
                 + 0.1 * s_escapeTries;
    char line[64];
    fmt3(line, sizeof(line), term("escapeStart", "%1 has started to escape!"),
         partyName(), "", "");
    blog("%s", line);
    if (rngNext(1000) < (int)(ratio * 1000)) {
        s_pendingResult = 1;
        s_phase = PH_END;
        s_delay = 40;
    } else {
        s_escapeTries++;
        blog("%s", term("escapeFailure", "However, it was unable to escape!"));
        s_nActorActions = 0;      // the party loses its turn
        startTurn();
    }
}

static void handleInput(void) {
    int n = s_menuN ? s_menuN : 1;
    if (s_kUp)   s_cursor = (s_cursor + n - 1) % n;
    if (s_kDown) s_cursor = (s_cursor + 1) % n;
    if (s_kLeft && (s_menu == MENU_ENEMY)) s_cursor = (s_cursor + n - 1) % n;
    if (s_kRight && (s_menu == MENU_ENEMY)) s_cursor = (s_cursor + 1) % n;

    if (s_kBack) {
        if (s_menu != MENU_CMD) openMenu(MENU_CMD);
        return;
    }
    if (!s_kOk || !s_menuN || !s_view.menuEnabled[s_cursor]) return;

    int id = s_menuIds[s_cursor];
    switch (s_menu) {
        case MENU_CMD:
            if (id == 0) {            // Attack = skill 1
                s_pending.isItem = false;
                s_pending.id = 1;
                const ActionDef *a = skillDef(1);
                if (a) chooseTarget(a); else commit();
            } else if (id == 1) openMenu(MENU_SKILL);
            else if (id == 2) openMenu(MENU_ITEM);
            else if (id == 3) { s_pending.guard = true; commit(); }
            else if (id == 4) tryEscape();
            break;
        case MENU_SKILL:
        case MENU_ITEM: {
            s_pending.isItem = (s_menu == MENU_ITEM);
            s_pending.id = id;
            const ActionDef *a = s_pending.isItem ? itemDef(id) : skillDef(id);
            if (a) chooseTarget(a);
            break;
        }
        case MENU_ENEMY:
        case MENU_ALLY:
            s_pending.target = id;
            commit();
            break;
    }
}

static void endOfTurn(void) {
    Battler *all[BT_MAX_ENEMIES + BT_MAX_ACTORS];
    int n = 0;
    for (int i = 0; i < s_nActorB; i++) all[n++] = &s_actorB[i];
    for (int i = 0; i < s_nEnemyB; i++) all[n++] = &s_enemyB[i];
    for (int k = 0; k < n; k++) {
        Battler *b = all[k];
        if (!isAlive(b)) continue;
        int slip = 0;
        for (int st = 2; st < FH_MAX_STATES; st++) {
            if (!hasState(b, st)) continue;
            const StateDef *d = stateDef(st);
            if (!d) continue;
            slip += d->slip;
            if (d->timing == 2 && --b->turns[st] <= 0) removeState(b, st);
        }
        if (slip) {
            int v = b->params[0] * slip / 100;
            b->hp = clampi(b->hp + v, 0, b->params[0]);
            if (v < 0) {
                char num[16], line[64];
                snprintf(num, sizeof(num), "%d", -v);
                fmt3(line, sizeof(line), term(b->isEnemy ? "enemyDamage" : "actorDamage",
                     "%1 took %2 damage!"), b->name, num, "");
                blog("%s", line);
            }
            checkDeath(b);
        }
    }
}

static void victory(void) {
    int exp = 0, gold = 0;
    for (int i = 0; i < s_nEnemyB; i++) {
        const EnemyDef *d = enemyDef(s_enemyB[i].dbId);
        if (!d || s_enemyB[i].hidden) continue;
        exp += d->exp;
        gold += d->gold;
        for (int k = 0; k < d->nDrop; k++) {
            int den = d->drop[k][2] > 0 ? d->drop[k][2] : 1;
            if (rngNext(den) == 0) {
                int kind = d->drop[k][0];             // 1 item 2 weapon 3 armor
                DbKind dk = kind == 2 ? DB_WEAPON : kind == 3 ? DB_ARMOR : DB_ITEM;
                runtimeAddOwned(s_gs, dk, d->drop[k][1], 1);
                char line[64];
                fmt3(line, sizeof(line), term("obtainItem", "%1 found!"),
                     gamedataName(dk, d->drop[k][1]), "", "");
                blog("%s", line);
            }
        }
    }
    char line[64], num[16];
    fmt3(line, sizeof(line), term("victory", "%1 was victorious!"),
         partyName(), "", "");
    blog("%s", line);
    if (exp > 0) {
        snprintf(num, sizeof(num), "%d", exp);
        fmt3(line, sizeof(line), term("obtainExp", "%1 EXP received!"), num, "", "");
        blog("%s", line);
        for (int i = 0; i < s_nActorB; i++)
            if (isAlive(&s_actorB[i])) actorChangeExp(s_gs, s_actorB[i].dbId, exp);
    }
    if (gold > 0) {
        s_gs->gold += gold;
        snprintf(num, sizeof(num), "%d", gold);
        fmt3(line, sizeof(line), term("obtainGold", "%1 gold found!"), num, "", "");
        blog("%s", line);
    }
}

static void refreshView(void) {
    s_view.actorCount = s_nActorB;
    for (int i = 0; i < s_nActorB; i++) {
        Battler *b = &s_actorB[i];
        snprintf(s_view.actorName[i], sizeof(s_view.actorName[i]), "%.18s", b->name);
        s_view.hp[i] = b->hp; s_view.mhp[i] = b->params[0];
        s_view.mp[i] = b->mp; s_view.mmp[i] = b->params[1];
        s_view.actorDead[i] = !isAlive(b);
    }
    if (s_phase == PH_INPUT) {
        s_view.activeActor = s_actorIdx;
        s_view.menuCount = s_menuN ? s_menuN : 1;
        s_view.menuCursor = s_cursor;
        snprintf(s_view.hint, sizeof(s_view.hint), "%s",
                 s_menu == MENU_CMD ? "UP/DOWN:choose A:ok" : "UP/DOWN:choose A:ok B:back");
    } else {
        s_view.activeActor = -1;
        s_view.menuCount = 0;
        snprintf(s_view.hint, sizeof(s_view.hint), "%s", s_phase == PH_END ? "A:continue" : "A:faster");
    }
}

// --- troop battle events (Game_Troop.meetsConditions / setupBattleEvent) ---

static bool troopPageMet(const TroopPageCond *c) {
    if (!c->turnEnding && !c->turnValid && !c->enemyValid && !c->actorValid && !c->switchValid)
        return false;                       // MV: a page with no conditions never runs
    if (c->turnEnding && !s_turnEnding) return false;
    if (c->turnValid) {
        int n = s_turn, a = c->turnA, b = c->turnB;
        if (b == 0 && n != a) return false;
        if (b > 0 && (n < 1 || n < a || n % b != a % b)) return false;
    }
    if (c->enemyValid) {
        if (c->enemyIndex < 0 || c->enemyIndex >= s_nEnemyB) return false;
        const Battler *e = &s_enemyB[c->enemyIndex];
        if (!e->used) return false;
        int rate = e->params[0] ? e->hp * 100 / e->params[0] : 0;
        if (rate > c->enemyHp) return false;
    }
    if (c->actorValid) {
        const Battler *a = NULL;
        for (int i = 0; i < s_nActorB; i++) if (s_actorB[i].dbId == c->actorId) a = &s_actorB[i];
        if (!a) return false;
        int rate = a->params[0] ? a->hp * 100 / a->params[0] : 0;
        if (rate > c->actorHp) return false;
    }
    if (c->switchValid && !runtimeGetSwitch(s_gs, (uint16_t)c->switchId)) return false;
    return true;
}

// Starts the first troop page whose conditions hold and that hasn't run in
// its span. Returns true if one started (the battle then waits for it).
static bool checkTroopEvents(void) {
    int n = eventsTroopPages(s_troopId);
    for (int p = 0; p < n && p < 64; p++) {
        TroopPageCond c;
        if (!eventsTroopPage(s_troopId, p, &c) || c.empty) continue;
        if ((c.span == 0 && s_ranBattle[p]) || (c.span == 1 && s_ranTurn[p])) continue;
        if (!troopPageMet(&c)) continue;
        if (c.span == 0) s_ranBattle[p] = true;
        if (c.span == 1) s_ranTurn[p] = true;
        eventsTroopRun(s_troopId, p);
        return true;
    }
    return false;
}

static void forEnemies(int index, void (*fn)(Battler *, int, int), int a, int b) {
    for (int i = 0; i < s_nEnemyB; i++)
        if ((index < 0 || index == i) && s_enemyB[i].used) fn(&s_enemyB[i], a, b);
}
static void hpFn(Battler *e, int delta, int allowDeath) {
    if (hasState(e, 1)) return;
    e->hp = clampi(e->hp + delta, 0, e->params[0]);
    if (e->hp == 0) { if (allowDeath) checkDeath(e); else e->hp = 1; }
}
static void mpFn(Battler *e, int delta, int unused) { (void)unused; e->mp = clampi(e->mp + delta, 0, e->params[1]); }
static void stateFn(Battler *e, int add, int st) {
    if (add) addStateEx(e, st, false); else removeState(e, st);
}
static void recoverFn(Battler *e, int a, int b) {
    (void)a; (void)b;
    memset(e->states, 0, sizeof(e->states));
    e->hp = e->params[0]; e->mp = e->params[1];
}
static void appearFn(Battler *e, int a, int b) {
    (void)a; (void)b;
    if (!e->hidden) return;
    e->hidden = false;
    char line[64];
    fmt3(line, sizeof(line), term("emerge", "%1 emerged!"), e->name, "", "");
    blog("%s", line);
}

void battleEnemyHp(int index, int delta, bool allowDeath) { forEnemies(index, hpFn, delta, allowDeath); }
void battleEnemyMp(int index, int delta) { forEnemies(index, mpFn, delta, 0); }
void battleEnemyState(int index, bool add, int stateId) { forEnemies(index, stateFn, add, stateId); }
void battleEnemyRecover(int index) { forEnemies(index, recoverFn, 0, 0); }
void battleEnemyAppear(int index) { forEnemies(index, appearFn, 0, 0); }
void battleEnemyTransform(int index, int enemyId) {
    const EnemyDef *d = enemyDef(enemyId);
    if (!d || index < 0 || index >= s_nEnemyB || !s_enemyB[index].used) return;
    Battler *b = &s_enemyB[index];
    b->dbId = d->id;
    snprintf(b->name, sizeof(b->name), "%s", d->name);
    memcpy(b->params, d->params, sizeof(b->params));
    snprintf(b->battler, sizeof(b->battler), "%s", d->battler);
    b->hp = clampi(b->hp, 0, b->params[0]);
    b->mp = clampi(b->mp, 0, b->params[1]);
}
void battleAbort(void) { s_abort = true; }

void battleUpdate(GameState *s) {
    s_gs = s;
    if (s_phase == PH_NONE) return;

    // Troop events and skill common events run first, like MV.
    if (eventsBattleBusy()) { eventsBattleUpdate(s); refreshView(); return; }

    // Abort Battle (340): ends at once, counted as an escape.
    if (s_abort && s_phase != PH_END) {
        writeBackActors();
        s_result = 1;
        s_phase = PH_END;
        refreshView();
        return;
    }

    switch (s_phase) {
    case PH_START:
        if (checkTroopEvents()) break;          // turn-0 / battle-start scripts
        if (--s_delay <= 0 || s_kOk) beginInput();
        break;
    case PH_INPUT:
        handleInput();
        break;
    case PH_TURN:
        if (s_delay > 0 && !s_kOk) { s_delay--; break; }
        if (checkTroopEvents()) break;          // e.g. "head destroyed" scripts
        // a kill (or a troop event) can end the battle mid-turn
        if (allDead(s_enemyB, s_nEnemyB) || allDead(s_actorB, s_nActorB)) s_qPos = s_nQueue;
        if (s_qPos < s_nQueue) {
            Action *a = &s_queue[s_qPos++];
            if (canAct(a->subject)) performAction(a);
            s_delay = 40;
        } else {
            endOfTurn();
            s_phase = PH_TURNEND;
            s_turnEnding = true;
        }
        break;
    case PH_TURNEND:
        if (checkTroopEvents()) break;          // "end of turn" scripts
        s_turnEnding = false;
        memset(s_ranTurn, 0, sizeof(s_ranTurn));
        if (allDead(s_enemyB, s_nEnemyB)) { victory(); s_pendingResult = 0; s_phase = PH_END; s_delay = 60; }
        else if (allDead(s_actorB, s_nActorB)) {
            blog("%s", term("defeat", "The party was defeated."));
            s_pendingResult = 2; s_phase = PH_END; s_delay = 60;
        } else beginInput();
        break;
    case PH_END:
        if (s_delay > 0) s_delay--;
        if (s_delay <= 0 && s_kOk) {
            writeBackActors();
            s_result = s_pendingResult;
        }
        break;
    }
    refreshView();
}

const BattleView *battleView(void) { return &s_view; }

int battleEnemyCount(void) { return s_nEnemyB; }

bool battleEnemySprite(int i, const char **battler, int *x, int *y, bool *blink) {
    if (i < 0 || i >= s_nEnemyB || !isAlive(&s_enemyB[i])) return false;
    *battler = s_enemyB[i].battler;
    *x = s_enemyB[i].x;
    *y = s_enemyB[i].y;
    *blink = s_phase == PH_INPUT && s_menu == MENU_ENEMY && s_menuN &&
             s_menuIds[s_cursor] == i;
    return true;
}

void battleSetBacks(const char *b1, const char *b2) {
    snprintf(s_back1, sizeof(s_back1), "%s", (b1 && strcmp(b1, "-")) ? b1 : "");
    snprintf(s_back2, sizeof(s_back2), "%s", (b2 && strcmp(b2, "-")) ? b2 : "");
}

void battleBackNames(const char **b1, const char **b2) { *b1 = s_back1; *b2 = s_back2; }

// ===========================================================================
// Field menu: use an item / equip a weapon or armor (Scene_Item / Scene_Equip)
// ===========================================================================

static void fieldEffects(GameState *s, int actorId, const ActionDef *a, char *msg, size_t n) {
    ActorState *ac = actorGet(s, actorId);
    if (!ac) return;
    if (a->dmgType == 3 || a->dmgType == 4) {        // recovery formula
        Battler tmp;
        readActor(&tmp, actorId);
        int st[13];
        statArray(&tmp, st);
        int v = (int)battleEvalFormula(a->formula, st, st, s);
        if (a->dmgType == 3) actorChangeHp(s, actorId, v, false);
        else actorChangeMp(s, actorId, v);
    }
    for (int i = 0; i < a->nEff; i++) {
        const Effect *e = &a->eff[i];
        switch (e->code) {
            case 11: actorChangeHp(s, actorId, (int)floor(actorParam(s, actorId, 0) * e->v1 + e->v2), false); break;
            case 12: actorChangeMp(s, actorId, (int)floor(actorParam(s, actorId, 1) * e->v1 + e->v2)); break;
            case 13: actorChangeTp(s, actorId, (int)e->v1); break;
            case 21: if (e->dataId > 0 && rngNext(1000) < (int)(e->v1 * 1000)) actorAddState(s, actorId, e->dataId); break;
            case 22: if (rngNext(1000) < (int)(e->v1 * 1000)) actorRemoveState(s, actorId, e->dataId); break;
            default: break;
        }
    }
    snprintf(msg, n, "%s used %s", actorName(s, actorId), a->name);
}

const char *battleFieldUse(GameState *s, int kind, int id) {
    static char msg[64];
    s_gs = s;
    int leader = runtimePartyLeader(s);
    if (runtimeGetOwned(s, (DbKind)kind, id) <= 0) return "none left";

    if (kind == DB_WEAPON || kind == DB_ARMOR) {      // equip on the leader
        const EquipDef *e = equipDef(kind == DB_WEAPON ? 1 : 2, id);
        if (!e || !leader) return "can't equip that";
        ensureEquips(s, leader);
        ActorState *a = actorGet(s, leader);
        int slot = clampi(e->etype - 1, 0, 4);
        int old = a->equips[slot];
        if (old) runtimeAddOwned(s, slot == 0 ? DB_WEAPON : DB_ARMOR, old, 1);
        runtimeAddOwned(s, (DbKind)kind, id, -1);
        a->equips[slot] = (int16_t)id;
        snprintf(msg, sizeof(msg), "%s equipped %s", actorName(s, leader),
                 gamedataName((DbKind)kind, id));
        return msg;
    }

    const ActionDef *a = itemDef(id);
    if (!a) return "can't use that";
    if (a->occasion == 1) return "only usable in battle";
    if (a->occasion == 3) return "can't use that";

    msg[0] = '\0';
    if (a->scope == 8 || a->scope == 10) {            // whole party
        for (int i = 0; i < s->partyCount; i++)
            fieldEffects(s, s->partyOrder[i], a, msg, sizeof(msg));
    } else if (a->scope >= 7 && leader) {            // one ally / user
        fieldEffects(s, leader, a, msg, sizeof(msg));
    }
    if (a->mpCost /* consumable */) runtimeAddOwned(s, DB_ITEM, id, -1);
    // Items that run a common event (reading books, crafting, ...)
    for (int i = 0; i < a->nEff; i++)
        if (a->eff[i].code == 44) eventsStartCommon(a->eff[i].dataId);
    if (!msg[0]) snprintf(msg, sizeof(msg), "Used %s", a->name);
    return msg;
}

// ===========================================================================
// Self-test: formula evaluator
// ===========================================================================

bool battleSelfTest(void) {
    int a[13] = { 100, 20, 30, 10, 5, 5, 7, 3, 80, 15, 0, 1, 0 };
    int b[13] = { 50, 0, 12, 8, 2, 2, 4, 1, 50, 0, 0, 1, 0 };
    bool ok = true;
    ok &= fabs(battleEvalFormula("a.atk * 4 - b.def * 2", a, b, NULL) - 104) < 0.01;
    ok &= fabs(battleEvalFormula("15", a, b, NULL) - 15) < 0.01;
    ok &= fabs(battleEvalFormula("Math.max(a.atk - b.def, 1) * 2", a, b, NULL) - 44) < 0.01;
    ok &= fabs(battleEvalFormula("(a.mhp - a.hp) / 2", a, b, NULL) - 10) < 0.01;
    ok &= battleEvalFormula("a.foo + 1", a, b, NULL) == 0;          // unsupported
    ok &= battleEvalFormula("a.isStateAffected(5) ? 1 : 2", a, b, NULL) == 0;
    return ok;
}
