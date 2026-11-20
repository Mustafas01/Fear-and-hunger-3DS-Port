#include "gamedata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *blob;      // whole file, '\n' replaced by '\0'
    char **rows;     // rows[i] points into blob
    int count;
} NameTable;

static NameTable s_actors, s_db[3];

static void freeTable(NameTable *t) {
    free(t->blob);
    free(t->rows);
    memset(t, 0, sizeof(*t));
}

static bool loadTable(NameTable *t, const char *dir, const char *file) {
    char path[256];
    snprintf(path, sizeof(path), "%s%s", dir, file);
    freeTable(t);

    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return false; }

    t->blob = malloc((size_t)size + 1);
    if (!t->blob) { fclose(f); return false; }
    size_t got = fread(t->blob, 1, (size_t)size, f);
    fclose(f);
    t->blob[got] = '\0';

    int lines = 1;
    for (size_t i = 0; i < got; i++)
        if (t->blob[i] == '\n') lines++;
    t->rows = malloc(sizeof(char *) * (size_t)lines);
    if (!t->rows) { freeTable(t); return false; }

    char *p = t->blob;
    t->count = 0;
    while (t->count < lines) {
        t->rows[t->count++] = p;
        char *nl = strchr(p, '\n');
        if (!nl) break;
        if (nl > p && nl[-1] == '\r') nl[-1] = '\0';
        *nl = '\0';
        p = nl + 1;
    }
    return true;
}

bool gamedataLoad(const char *dir) {
    bool ok = loadTable(&s_actors, dir, "actors.txt");
    loadTable(&s_db[DB_ITEM], dir, "items.txt");
    loadTable(&s_db[DB_WEAPON], dir, "weapons.txt");
    loadTable(&s_db[DB_ARMOR], dir, "armors.txt");
    return ok;
}

const char *gamedataActorName(int id) {
    if (id < 1 || id >= s_actors.count) return "";
    return s_actors.rows[id];
}

const char *gamedataName(DbKind kind, int id) {
    if (kind > DB_ARMOR) return "???";
    const NameTable *t = &s_db[kind];
    if (id < 1 || id >= t->count || !t->rows[id][0]) return "???";
    return t->rows[id];
}

int gamedataInventory(const GameState *state, InvEntry *out, int maxOut) {
    int n = 0;
    for (int k = DB_ITEM; k <= DB_ARMOR; k++) {
        for (int id = 1; id <= FH_MAX_DB && n < maxOut; id++) {
            int c = runtimeGetOwned(state, (DbKind)k, id);
            if (c > 0) {
                out[n].kind = (DbKind)k;
                out[n].id = id;
                out[n].count = c;
                n++;
            }
        }
    }
    return n;
}

void gamedataInitParty(const char *dir, GameState *state) {
    char path[256];
    snprintf(path, sizeof(path), "%sparty.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) {                       // no file: assume actor 1
        runtimePartyAdd(state, 1);
        return;
    }
    int id;
    while (fscanf(f, "%d", &id) == 1)
        runtimePartyAdd(state, id);
    fclose(f);
}

// actorchars.txt: "<id> <sheet> <index>" (the actor's own sprite)
static struct { char sheet[48]; int index; } s_chars[FH_MAX_ACTORS + 1];

void gamedataLoadActorChars(const char *dir) {
    char path[256], sheet[48];
    snprintf(path, sizeof(path), "%sactorchars.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    int id, idx;
    while (fscanf(f, "%d %47s %d", &id, sheet, &idx) == 3)
        if (id >= 1 && id <= FH_MAX_ACTORS) {
            snprintf(s_chars[id].sheet, sizeof(s_chars[id].sheet), "%s",
                     strcmp(sheet, "-") ? sheet : "");
            s_chars[id].index = idx;
        }
    fclose(f);
}

const char *gamedataActorSheet(int id, int *index) {
    if (id < 1 || id > FH_MAX_ACTORS) { *index = 0; return ""; }
    *index = s_chars[id].index;
    return s_chars[id].sheet;
}
