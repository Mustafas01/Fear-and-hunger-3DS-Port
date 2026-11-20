#ifndef ACTORS_H
#define ACTORS_H

#include <stdbool.h>
#include "runtime.h"

// Character stats driven by actors.bin (class EXP curves and parameter
// tables exported by build_events.py / build_world.py from YOUR copy).

bool actorsLoad(const char *dir);   // dir ends with '/'

// Returns the actor, setting it up from its database entry on first use
// (like RPG Maker's $gameActors.actor(id)). NULL for invalid ids.
ActorState *actorGet(GameState *s, int id);
void actorSetup(GameState *s, int id);          // reset to database values

// param: 0 MaxHP 1 MaxMP 2 ATK 3 DEF 4 MAT 5 MDF 6 AGI 7 LUK
int  actorParam(GameState *s, int id, int param);

void actorChangeHp(GameState *s, int id, int delta, bool allowDeath);
void actorChangeMp(GameState *s, int id, int delta);
void actorChangeTp(GameState *s, int id, int delta);
void actorChangeExp(GameState *s, int id, int delta);   // levels follow EXP
void actorChangeLevel(GameState *s, int id, int delta);
void actorRecoverAll(GameState *s, int id);

// Current name (Name Input / Change Name) and sprite (Change Actor Images).
const char *actorName(GameState *s, int id);
void actorSetName(GameState *s, int id, const char *name);
const char *actorSprite(GameState *s, int id, int *index);
void actorSetSprite(GameState *s, int id, const char *sheet, int index);

bool actorHasState(GameState *s, int id, int stateId);
void actorAddState(GameState *s, int id, int stateId);
void actorRemoveState(GameState *s, int id, int stateId);

#endif
