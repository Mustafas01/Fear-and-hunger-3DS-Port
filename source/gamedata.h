#ifndef GAMEDATA_H
#define GAMEDATA_H

#include <stdbool.h>
#include "runtime.h"

// Name tables exported by build_events.py from YOUR copy of the game
// (actors.txt, items.txt, weapons.txt, armors.txt; line N = id N).
// Nothing from the game is compiled into the port itself.

// dir must end with '/'. Returns true if at least actors.txt loaded.
bool gamedataLoad(const char *dir);

// Puts the starting party (party.txt) into state->inParty.
void gamedataInitParty(const char *dir, GameState *state);

const char *gamedataActorName(int id);          // database name

// Database sprite of an actor (actorchars.txt). "" if none.
void gamedataLoadActorChars(const char *dir);
const char *gamedataActorSheet(int id, int *index);          // "" if unknown
const char *gamedataName(DbKind kind, int id);  // "???" if unknown

// One row of the inventory screen.
typedef struct { DbKind kind; int id; int count; } InvEntry;

// Lists owned items, then weapons, then armors. Returns row count.
int gamedataInventory(const GameState *state, InvEntry *out, int maxOut);

#endif
