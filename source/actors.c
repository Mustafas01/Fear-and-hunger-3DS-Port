#include "actors.h"
#include "gamedata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint16_t classId, initLevel, maxLevel;
    int32_t exp[100];
    int32_t params[8][100];
} ActorTable;

static ActorTable *s_tables;
static int s_count;

bool actorsLoad(const char *dir) {
    char path[256];
    snprintf(path, sizeof(path), "%sactors.bin", dir);
    free(s_tables);
    s_tables = NULL;
    s_count = 0;

    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint16_t n = 0;
    if (fread(&n, 2, 1, f) != 1) { fclose(f); return false; }
    s_tables = calloc(n ? n : 1, sizeof(ActorTable));
    if (!s_tables) { fclose(f); return false; }
    for (int i = 0; i < n; i++) {
        ActorTable *t = &s_tables[i];
        if (fread(&t->classId, 2, 1, f) != 1 || fread(&t->initLevel, 2, 1, f) != 1 ||
            fread(&t->maxLevel, 2, 1, f) != 1 || fread(t->exp, 4, 100, f) != 100 ||
            fread(t->params, 4, 800, f) != 800)
            break;
        if (t->maxLevel < 1 || t->maxLevel > 99) t->maxLevel = 99;
        if (t->initLevel < 1 || t->initLevel > t->maxLevel) t->initLevel = 1;
        s_count = i + 1;
    }
    fclose(f);
    return s_count > 0;
}

static const ActorTable *table(int id) {
    return (id >= 1 && id < s_count) ? &s_tables[id] : NULL;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void actorSetup(GameState *s, int id) {
    if (id < 1 || id > FH_MAX_ACTORS) return;
    ActorState *a = &s->actors[id];
    memset(a, 0, sizeof(*a));
    a->ready = true;
    const ActorTable *t = table(id);
    a->level = t ? t->initLevel : 1;
    a->exp = t ? t->exp[a->level] : 0;
    a->hp = actorParam(s, id, 0);
    a->mp = actorParam(s, id, 1);
}

ActorState *actorGet(GameState *s, int id) {
    if (id < 1 || id > FH_MAX_ACTORS) return NULL;
    if (!s->actors[id].ready) actorSetup(s, id);
    return &s->actors[id];
}

int actorParam(GameState *s, int id, int param) {
    if (param < 0 || param > 7 || id < 1 || id > FH_MAX_ACTORS) return 0;
    ActorState *a = &s->actors[id];
    if (!a->ready) { a->ready = true; a->level = table(id) ? table(id)->initLevel : 1; }
    const ActorTable *t = table(id);
    int lv = clampi(a->level, 1, 99);
    int v = (t ? t->params[param][lv] : 0) + a->plus[param];
    int lo = (param == 0) ? 1 : 0;              // MV: MaxHP is at least 1
    return clampi(v, lo, param <= 1 ? 9999 : 999);
}

static void refresh(GameState *s, int id) {
    ActorState *a = &s->actors[id];
    a->hp = clampi(a->hp, 0, actorParam(s, id, 0));
    a->mp = clampi(a->mp, 0, actorParam(s, id, 1));
    a->tp = clampi(a->tp, 0, 100);
    if (a->hp == 0) a->states[0] |= 1 << 1;          // state 1 = dead
    else if (actorHasState(s, id, 1)) a->hp = 0;
}

bool actorHasState(GameState *s, int id, int st) {
    ActorState *a = actorGet(s, id);
    if (!a || st < 1 || st >= FH_MAX_STATES) return false;
    return (a->states[st >> 3] >> (st & 7)) & 1;
}

void actorAddState(GameState *s, int id, int st) {
    ActorState *a = actorGet(s, id);
    if (!a || st < 1 || st >= FH_MAX_STATES) return;
    a->states[st >> 3] |= (uint8_t)(1 << (st & 7));
    if (st == 1) a->hp = 0;
}

void actorRemoveState(GameState *s, int id, int st) {
    ActorState *a = actorGet(s, id);
    if (!a || st < 1 || st >= FH_MAX_STATES) return;
    a->states[st >> 3] &= (uint8_t)~(1 << (st & 7));
    if (st == 1 && a->hp == 0) a->hp = 1;            // revived
}

void actorChangeHp(GameState *s, int id, int delta, bool allowDeath) {
    ActorState *a = actorGet(s, id);
    if (!a || actorHasState(s, id, 1)) return;       // MV ignores dead actors
    a->hp += delta;
    if (a->hp <= 0 && !allowDeath) a->hp = 1;
    refresh(s, id);
}

void actorChangeMp(GameState *s, int id, int delta) {
    ActorState *a = actorGet(s, id);
    if (!a) return;
    a->mp += delta;
    refresh(s, id);
}

void actorChangeTp(GameState *s, int id, int delta) {
    ActorState *a = actorGet(s, id);
    if (!a) return;
    a->tp += delta;
    refresh(s, id);
}

void actorChangeExp(GameState *s, int id, int delta) {
    ActorState *a = actorGet(s, id);
    if (!a) return;
    const ActorTable *t = table(id);
    a->exp += delta;
    if (a->exp < 0) a->exp = 0;
    if (!t) return;
    while (a->level < t->maxLevel && a->exp >= t->exp[a->level + 1]) a->level++;
    while (a->level > 1 && a->exp < t->exp[a->level]) a->level--;
    refresh(s, id);
}

void actorChangeLevel(GameState *s, int id, int delta) {
    ActorState *a = actorGet(s, id);
    if (!a) return;
    const ActorTable *t = table(id);
    int maxL = t ? t->maxLevel : 99;
    a->level = (int16_t)clampi(a->level + delta, 1, maxL);
    if (t) a->exp = t->exp[a->level];
    refresh(s, id);
}

void actorRecoverAll(GameState *s, int id) {
    ActorState *a = actorGet(s, id);
    if (!a) return;
    memset(a->states, 0, sizeof(a->states));
    a->hp = actorParam(s, id, 0);
    a->mp = actorParam(s, id, 1);
}

const char *actorName(GameState *s, int id) {
    if (id < 1 || id > FH_MAX_ACTORS) return "";
    return s->actors[id].name[0] ? s->actors[id].name : gamedataActorName(id);
}

void actorSetName(GameState *s, int id, const char *name) {
    ActorState *a = actorGet(s, id);
    if (a) snprintf(a->name, sizeof(a->name), "%s", name ? name : "");
}

const char *actorSprite(GameState *s, int id, int *index) {
    if (id >= 1 && id <= FH_MAX_ACTORS && s->actors[id].charSet) {
        *index = s->actors[id].charIndex;
        return s->actors[id].charName;
    }
    return gamedataActorSheet(id, index);
}

void actorSetSprite(GameState *s, int id, const char *sheet, int index) {
    ActorState *a = actorGet(s, id);
    if (!a) return;
    snprintf(a->charName, sizeof(a->charName), "%s", sheet ? sheet : "");
    a->charIndex = (int8_t)index;
    a->charSet = true;
}
