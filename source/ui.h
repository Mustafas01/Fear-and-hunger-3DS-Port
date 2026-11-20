#ifndef UI_H
#define UI_H

#include <3ds.h>
#include <citro2d.h>
#include "battle.h"

typedef enum {
    UI_MODE_FIELD = 0,
    UI_MODE_COMBAT,
    UI_MODE_INVENTORY,
    UI_MODE_MESSAGE,
    UI_MODE_TITLE,
    UI_MODE_GAMEOVER,
    UI_MODE_BATTLE
} UIMode;

// Call once after C2D_Prepare(). Returns false if the font/target failed.
bool uiInit(void);
void uiExit(void);

void uiSetMode(UIMode mode);

// Adds a line to the on-screen message log (newest at bottom).
void uiLog(const char* fmt, ...);

// Per-frame state feeds.
void uiSetSurvival(int hunger, const char* hungerStage,
                   int mind, const char* mindStage);
void uiSetPos(int x, int y);
// Field screen: the party leader's HP/MP with the game's own labels.
void uiSetVitals(const char* leader, const char* hpLabel, int hp, int mhp,
                 const char* mpLabel, int mp, int mmp);
// One-line debug status shown in field mode ("" hides it).
void uiSetStatus(const char* status);
void uiSetCombat(const char* enemyName, int ehp, int emax,
                 const char* const* partNames, int partCount, int partCursor,
                 const char* weaponName, int playerHp, int playerMax);
void uiSetInventory(const char* const* names, const int* counts,
                    int n, int cursor);

// Message window (Show Text / Show Choices). Copies the strings.
void uiSetMessage(const char *speaker, const char *const *lines, int lineCount,
                  const char *const *choices, int choiceCount, int cursor);

// Title menu: the game's name (from title.txt) and the cursor row
// (0 New Game, 1 Continue, 2 Quit). Continue is greyed out until saves exist.
void uiSetTitle(const char* gameTitle, int cursor, bool canContinue, bool hasArt);

// Game Over screen: what triggered it and the party leader's HP/MP.
void uiSetGameOver(const char* cause, const char* leaderName,
                   int hp, int maxHp, int mp, int maxMp);

// Battle screen (the game's own battle data, see battle.h).
void uiSetBattle(const BattleView* view);

// Draws the whole bottom screen. Call between C3D_FrameBegin/End,
// AFTER drawing the top screen.
void uiRender(void);

#endif