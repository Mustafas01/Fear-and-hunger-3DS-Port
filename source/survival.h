#ifndef SURVIVAL_H
#define SURVIVAL_H

#include <stdbool.h>

// Fear & Hunger survival meters: hunger (body) and mind (sanity).
// Both run 0..100. 100 = full. 0 = starving / broken.

void survivalInit(void);

// Call once per frame from the main loop. Meters drain slowly over time.
void survivalTick(void);

int survivalGetHunger(void);
int survivalGetMind(void);

// Restore meters (capped at 100).
void survivalEat(int amount);
void survivalSoothe(int amount);

// Drain meters directly (floored at 0). Used for darkness, spells, debug.
void survivalDrain(int hungerAmount, int mindAmount);

// Short stage names for the status line.
const char* survivalHungerStage(void);
const char* survivalMindStage(void);

// Pure-logic self test. Returns true if everything works.
bool survivalSelfTest(void);

#endif