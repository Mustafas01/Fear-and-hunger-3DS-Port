#ifndef PICTURES_H
#define PICTURES_H

#include <stdbool.h>

// Draws event pictures (Show Picture etc.) on the top screen using the
// textures made by build_pictures.py from YOUR copy of the game.

// dir ends with '/', e.g. "sdmc:/3ds/fearmap/pictures/". Reads index.txt.
bool picturesInit(const char *dir);
void picturesExit(void);

// Call inside the top-screen scene, after the map and before the tint.
// camX/camY: this port's camera (pixels at 24px tiles); px/py: player tile;
// mapW/mapH: map size in tiles. Pictures are placed where RPG Maker's own
// 816x624 screen would put them over the map, at the map's 0.5 scale.
void picturesDraw(int camX, int camY, int px, int py, int mapW, int mapH);

// Number of picture names that had no texture (for diagnostics).
int picturesMissingCount(void);

#endif
