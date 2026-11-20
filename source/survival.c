#include "survival.h"

static int hunger;
static int mind;
static unsigned long frameCounter;

// At 60 fps: hunger drops 1 point every 2 seconds,
// mind drops 1 point every 3 seconds.
#define HUNGER_FRAMES 120
#define MIND_FRAMES   180

void survivalInit(void) {
    hunger = 100;
    mind = 100;
    frameCounter = 0;
}

void survivalTick(void) {
    frameCounter++;
    if (frameCounter % HUNGER_FRAMES == 0 && hunger > 0)
        hunger--;
    if (frameCounter % MIND_FRAMES == 0 && mind > 0)
        mind--;
}

int survivalGetHunger(void) { return hunger; }
int survivalGetMind(void)   { return mind; }

void survivalEat(int amount) {
    hunger += amount;
    if (hunger > 100)
        hunger = 100;
}

void survivalSoothe(int amount) {
    mind += amount;
    if (mind > 100)
        mind = 100;
}

void survivalDrain(int hungerAmount, int mindAmount) {
    hunger -= hungerAmount;
    if (hunger < 0)
        hunger = 0;
    mind -= mindAmount;
    if (mind < 0)
        mind = 0;
}

const char* survivalHungerStage(void) {
    if (hunger <= 0)  return "starving";
    if (hunger <= 20) return "hungry";
    if (hunger <= 50) return "peckish";
    return "fed";
}

const char* survivalMindStage(void) {
    if (mind <= 0)  return "broken";
    if (mind <= 25) return "afraid";
    if (mind <= 50) return "uneasy";
    return "calm";
}

bool survivalSelfTest(void) {
    int i;

    survivalInit();
    if (survivalGetHunger() != 100 || survivalGetMind() != 100)
        return false;

    // After 2 seconds of frames, hunger should drop to 99.
    for (i = 0; i < HUNGER_FRAMES; i++)
        survivalTick();
    if (survivalGetHunger() != 99)
        return false;

    // Eating clamps at 100, never above.
    survivalEat(50);
    if (survivalGetHunger() != 100)
        return false;

    // Drain a long time: both meters hit 0 and never go negative.
    for (i = 0; i < 40000; i++)
        survivalTick();
    if (survivalGetHunger() != 0 || survivalGetMind() != 0)
        return false;

    // Restore from zero works.
    survivalEat(1);
    survivalSoothe(1);
    if (survivalGetHunger() != 1 || survivalGetMind() != 1)
        return false;

    // Drain clamps at 0, never below.
    survivalDrain(999, 999);
    if (survivalGetHunger() != 0 || survivalGetMind() != 0)
        return false;

    survivalInit();
    return true;
}