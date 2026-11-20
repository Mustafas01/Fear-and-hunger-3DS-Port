#ifndef RNG_H
#define RNG_H

#include <stdbool.h>

// Seed the RNG. Call once at startup.
void rngInit(void);

// Returns a random integer in [0, maxExclusive).
int rngNext(int maxExclusive);

// Returns true with the given percent chance (0-100).
// This is the heart of the Fear & Hunger coin flip.
bool rngChance(int percent);

// A Fear & Hunger coin flip.
// The player's pick (heads/tails) is pure theater - the game
// pre-decides win/lose, so we just roll a percentage.
//   lucky=false -> 50/50
//   lucky=true  -> 75/25 (the SHIFT / lucky-coin flip)
bool coinFlip(bool lucky);

#endif