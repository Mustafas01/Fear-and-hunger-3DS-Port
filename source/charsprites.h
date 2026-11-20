#ifndef CHARSPRITES_H
#define CHARSPRITES_H
#include <stdbool.h>

// Event sprite sheets (characters/<name>.t3x + index.txt, converted by
// build_world.py from YOUR copy). Loaded on demand, least-recently-used
// sheets are freed when the cache is full.

bool charspritesInit(const char *dir);   // e.g. "sdmc:/3ds/fearmap/characters/"
void charspritesExit(void);

// Draws frame (index, dir 0-3, pattern 0-2) with its feet on the tile whose
// top-left screen corner is (sx, sy); ts = tile size on screen.
void charspritesDraw(const char *name, int index, int dir, int pattern,
                     float sx, float sy, int ts);

#endif
