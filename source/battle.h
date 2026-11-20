#ifndef BATTLE_H
#define BATTLE_H

// ---------------------------------------------------------------------------
// Battle system driven by the game's own database (exported from YOUR copy
// by build_battle.py): troops, enemies, skills, items, states and messages.
// Follows RPG Maker MV's front-view rules: each troop member (in this game,
// each body part) is its own target; turn order by agility; damage from the
// skill's formula; states with turn counts; EXP/gold/drops on victory.
// Pure logic: drawing lives in ui.c (bottom) and battlegfx.c (top).
// ---------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>
#include "runtime.h"

#define BT_MAX_ENEMIES 8
#define BT_MAX_ACTORS 4
#define BT_LOG_LINES 4
#define BT_MENU_MAX 64

bool battleLoadData(const char *dir);   // e.g. "sdmc:/3ds/fearmap/battle/"
bool battleDataLoaded(void);

// Starts troop `troopId`. Returns false if the troop is unknown.
bool battleStart(GameState *s, int troopId, bool canEscape, bool canLose);
bool battleActive(void);

// Per frame: buttons pressed this frame, then advance.
void battleInput(bool up, bool down, bool left, bool right, bool confirm, bool cancel);
void battleUpdate(GameState *s);

// When the battle is over: 0 win, 1 escape, 2 lose. -1 while running.
int battleResult(void);
void battleFinish(void);                // clears the result after main read it

// --- what to draw ----------------------------------------------------------
typedef struct {
    // log
    char log[BT_LOG_LINES][52];
    // party
    int actorCount;
    char actorName[BT_MAX_ACTORS][20];
    int hp[BT_MAX_ACTORS], mhp[BT_MAX_ACTORS], mp[BT_MAX_ACTORS], mmp[BT_MAX_ACTORS];
    bool actorDead[BT_MAX_ACTORS];
    int activeActor;                    // whose command is being chosen (-1)
    // menu (empty when not choosing)
    char menuTitle[24];
    int menuCount, menuCursor;
    char menuItems[BT_MENU_MAX][32];
    bool menuEnabled[BT_MENU_MAX];
    char hint[52];
} BattleView;

const BattleView *battleView(void);

// Enemies for the top screen.
int battleEnemyCount(void);
// Returns false if member i is not shown (dead/hidden). x, y: MV screen
// coordinates (816x624) of the sprite's bottom centre. blink: selected.
bool battleEnemySprite(int i, const char **battler, int *x, int *y, bool *blink);
void battleBackNames(const char **back1, const char **back2);
void battleSetBacks(const char *back1, const char *back2);   // from the map

// Called by troop/skill events (index = troop member, -1 = all).
void battleEnemyHp(int index, int delta, bool allowDeath);
void battleEnemyMp(int index, int delta);
void battleEnemyState(int index, bool add, int stateId);
void battleEnemyRecover(int index);
void battleEnemyAppear(int index);
void battleEnemyTransform(int index, int enemyId);
void battleAbort(void);

// Inventory screen: use an item (kind DB_ITEM) or equip a weapon/armor on
// the party leader. Returns a short message for the log.
const char *battleFieldUse(GameState *s, int kind, int id);

// The game's own text for a System.json term (e.g. "termHp").
const char *battleTerm(const char *key, const char *fallback);

// Damage formula evaluator, exposed for self-tests.
double battleEvalFormula(const char *f, const int *aParams, const int *bParams,
                         const GameState *s);

bool battleSelfTest(void);

#endif
