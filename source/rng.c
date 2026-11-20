#include "rng.h"

#include <stdlib.h>
#include <time.h>

void rngInit(void) {
    srand((unsigned int)time(NULL));
}

int rngNext(int maxExclusive) {
    if (maxExclusive <= 0)
        return 0;
    return rand() % maxExclusive;
}

bool rngChance(int percent) {
    if (percent <= 0)
        return false;
    if (percent >= 100)
        return true;
    return rngNext(100) < percent;
}

bool coinFlip(bool lucky) {
    return rngChance(lucky ? 75 : 50);
}