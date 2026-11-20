#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define FH_MAX_ID 8192
#define FH_SELF_SWITCHES 4   // A, B, C, D
#define FH_SELF_SLOTS 16384  // hash slots for self switches that are ON
#define FH_MAX_DB 1000       // max database id for items/weapons/armors
#define FH_MAX_ACTORS 64

typedef enum { DB_ITEM = 0, DB_WEAPON, DB_ARMOR } DbKind;

#define FH_MAX_STATES 128

// One party character's live stats (RPG Maker's Game_Actor, minus
// equipment/skills for now). Set up lazily from actors.bin on first use.
typedef struct {
    bool ready;
    int16_t level;
    int32_t exp, hp, mp, tp;
    int32_t plus[8];                     // Change Parameter bonuses
    uint8_t states[FH_MAX_STATES / 8];   // bit set of state ids
    uint8_t learned[64], forgotten[64];  // Change Skill (318), skill ids < 512
    int8_t stateTurns[FH_MAX_STATES];    // battle: turns left (0 = none)
    char name[24];                       // Change Name / Name Input ("" = database)
    char charName[48];                   // Change Actor Images ("" = database)
    int8_t charIndex;
    bool charSet, equipsSet;
    int16_t equips[5];                   // weapon, shield, head, body, accessory
} ActorState;

typedef struct {
    uint8_t switches[FH_MAX_ID + 1];
    int32_t variables[FH_MAX_ID + 1];
    // Self switches are keyed by (map, event, letter) like RPG Maker.
    // Stored as an open-addressing set of keys that are ON (0 = empty).
    uint32_t selfKeys[FH_SELF_SLOTS];
    // Party inventory by database id (Items/Weapons/Armors.json).
    int16_t owned[3][FH_MAX_DB + 1];
    int32_t gold;
    // Party membership by actor id (Change Party Member, 129).
    uint8_t inParty[FH_MAX_ACTORS + 1];
    uint8_t partyOrder[8];               // actor ids in joining order (leader first)
    uint8_t partyCount;
    ActorState actors[FH_MAX_ACTORS + 1];
} GameState;

void runtimeInit(GameState *state);

bool runtimeGetSwitch(const GameState *state, uint16_t id);
void runtimeSetSwitch(GameState *state, uint16_t id, bool value);

// Self-switches are per map, per event, per letter (0=A 1=B 2=C 3=D).
bool runtimeGetSelfSwitch(const GameState *state, uint16_t mapId,
                          uint16_t eventId, int letter);
void runtimeSetSelfSwitch(GameState *state, uint16_t mapId,
                          uint16_t eventId, int letter, bool value);

// Inventory by database id. Counts clamp to 0..99 like RPG Maker.
// Party in joining order, like RPG Maker ($gameParty._actors).
void runtimePartyAdd(GameState *state, int actorId);
void runtimePartyRemove(GameState *state, int actorId);
int  runtimePartyLeader(const GameState *state);    // 0 if empty

int  runtimeGetOwned(const GameState *state, DbKind kind, int id);
void runtimeAddOwned(GameState *state, DbKind kind, int id, int delta);

int32_t runtimeGetVariable(const GameState *state, uint16_t id);
void runtimeSetVariable(GameState *state, uint16_t id, int32_t value);

// Executes RPG Maker commands 121 and 122.
bool runtimeExecuteCommand(
    GameState *state,
    uint16_t commandCode,
    const int32_t *params,
    size_t paramCount
);

#endif