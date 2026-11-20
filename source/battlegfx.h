#ifndef BATTLEGFX_H
#define BATTLEGFX_H
#include <stdbool.h>

// Top-screen battle drawing: the map's battle background and the troop's
// enemy images, converted from YOUR copy by build_battle.py.
// The 816x624 battle screen is fitted into 400x240 (thin side bars).

void battlegfxInit(const char *dir);   // e.g. "sdmc:/3ds/fearmap/battle/"
void battlegfxPrepare(void);           // after battleStart: load textures
void battlegfxRelease(void);           // after the battle: free them
void battlegfxDraw(void);              // inside the top-screen scene

#endif
