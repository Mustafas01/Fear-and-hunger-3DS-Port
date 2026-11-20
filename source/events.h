#ifndef EVENTS_H
#define EVENTS_H

// ---------------------------------------------------------------------------
// Event system: loads compiled map/common events (built by build_events.py
// from YOUR copy of the game) and runs them with a resumable interpreter,
// so messages, choices, waits and battles can pause a script across frames.
// ---------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>
#include "runtime.h"

// --- pictures (Show/Move/Tint/Erase Picture) -------------------------------
#define FH_MAX_PICTURES 100
typedef struct {
    bool used;
    char name[64];          // file name in img/pictures (non-ASCII -> '?')
    int origin;             // 0 top-left, 1 centre
    float x, y;             // MV screen pixels (816x624)
    float scaleX, scaleY;   // percent
    float opacity;          // 0..255
    int blend;              // 0 normal, 1 add, 2 multiply, 3 screen
    float tx, ty, tsx, tsy, topacity;   // Move Picture targets
    int duration;
    float tone[4], toneTarget[4];       // r, g, b, gray
    int toneDuration;
} EventPicture;

// Picture 1..100, or NULL if not shown.
const EventPicture *eventsGetPicture(int id);

// --- player / input feed (call every frame before eventsUpdate) -----------
// dir uses this port's numbering: 0 down 1 left 2 right 3 up.
void eventsSetPlayer(int x, int y, int dir);
// Buttons currently held, for "Button is pressed" conditions.
enum {
    EVB_OK = 1, EVB_CANCEL = 2, EVB_SHIFT = 4, EVB_DOWN = 8, EVB_LEFT = 16,
    EVB_RIGHT = 32, EVB_UP = 64, EVB_PAGEUP = 128, EVB_PAGEDOWN = 256
};
void eventsSetButtons(uint32_t held);

// --- map events as characters -------------------------------------------
// Passability for moving events: can something step from (x, y) in
// direction dir03 (0 down 1 left 2 right 3 up)? Main supplies its tile check.
void eventsSetPassability(bool (*fn)(int x, int y, int dir03));

typedef struct {
    const char *img;      // characters/<img>.t3x ("" for tile events)
    int index;            // character index inside the sheet
    int dir;              // 0 down 1 left 2 right 3 up
    int pattern;          // 0..2
    int tileId;           // >0: drawn as a map tile instead
    float x, y;           // position in tiles (fractional while walking)
    int priority;         // 0 below player, 1 same level, 2 above
} EventSprite;
int eventsSprites(EventSprite *out, int max);

// --- loading ---------------------------------------------------------------
bool eventsLoadMap(const char *path);      // events_compiled.bin
bool eventsLoadCommon(const char *path);   // common_compiled.bin
uint16_t eventsMapId(void);
uint32_t eventsCount(void);                // events on the loaded map

// --- triggering (RPG Maker MV rules) ---------------------------------------
// A button: events here (below/above priority) then the tile in front
// (same-as-characters priority).
bool eventsTriggerAction(int px, int py, int dir, GameState *s);
// After a step: player-touch / event-touch events on the new tile.
bool eventsTriggerTouchHere(int px, int py, GameState *s);
// Bumping into a blocking event in front.
bool eventsTriggerTouchThere(int px, int py, int dir, GameState *s);
// Starts autorun map events / autorun common events when idle.
void eventsCheckAutorun(GameState *s);
// True if an active event on (x, y) blocks movement.
bool eventsBlocking(int x, int y, GameState *s);

// --- running ---------------------------------------------------------------
void eventsUpdate(GameState *s);  // call once per frame
bool eventsBusy(void);            // an event is running: freeze the player

// --- message window --------------------------------------------------------
bool eventsMessageOpen(void);
// Formatted, word-wrapped lines of the current message (0 when choices
// are shown without text).
int eventsMessageLines(const char **lines, int maxLines);
const char *eventsMessageSpeaker(void);        // "" if none
int eventsChoiceCount(void);
const char *eventsChoiceText(int i);
int eventsChoiceCursor(void);
// Feed button presses while the window is open.
void eventsMessageInput(bool up, bool down, bool confirm, bool cancel);

// --- requests the main loop must service -----------------------------------
// Battle Processing (301). Answer with eventsResolveBattle(0 win,
// 1 escape, 2 lose).
bool eventsPendingBattle(int *troopId, bool *canEscape, bool *canLose);
void eventsResolveBattle(int result);
// Name Input Processing (303): main shows a keyboard, then answers with
// eventsNameInputDone (NULL name = keep the current one).
bool eventsPendingNameInput(int *actorId, int *maxLen);
void eventsNameInputDone(GameState *s, const char *name);
// Transfer Player (201). Answer with eventsTransferDone().
bool eventsPendingTransfer(int *mapId, int *x, int *y, int *dir);
void eventsTransferDone(void);
// Game Over (353) / Return to Title (354). Once set, events stop running.
bool eventsGameOver(void);
bool eventsReturnToTitle(void);        // it was 354: skip the Game Over screen
const char *eventsGameOverCause(void); // e.g. "common event 35 on map 73"
// Clears interpreters, pictures, tint and the game-over flag (new game).
void eventsReset(void);
// Tint Screen (223): r, g, b in -255..255, gray 0..255.
void eventsGetTint(int *r, int *g, int *b, int *gray);

// Gab window lines (YEP_GabWindow plugin commands) are sent here.
void eventsSetGabHandler(void (*fn)(const char *text));

// Debug / recovery.
void eventsAbort(void);              // stop whatever is running
void eventsSuspendCurrent(void);     // stop it AND disable that event until
                                     // the map reloads (used when it tries
                                     // to leave for an unconverted map)
const char *eventsDebugStatus(void); // "" when idle

// Battles: common events called by skills/items run on their own
// interpreter while the battle waits (messages still use the window).
void eventsBattleCommon(int id);
void eventsStartCommon(int id);       // field: run a common event now
bool eventsBattleBusy(void);
void eventsBattleUpdate(GameState *s);

// Troop battle events (battle/troop_events.bin): per-fight scripts.
typedef struct {
    bool turnEnding, turnValid, enemyValid, actorValid, switchValid, empty;
    int turnA, turnB, enemyIndex, enemyHp, actorId, actorHp, switchId, span;
} TroopPageCond;
bool eventsLoadTroopEvents(const char *path);
int  eventsTroopPages(int troopId);
bool eventsTroopPage(int troopId, int page, TroopPageCond *out);
void eventsTroopRun(int troopId, int page);   // runs on the battle interpreter

// Interpreter self-tests (pure logic). Returns true if all pass.
bool eventsRunSelfTests(int *passed, int *total);

#endif
