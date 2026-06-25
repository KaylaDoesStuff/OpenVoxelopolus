#ifndef RENDER_H
#define RENDER_H

#include "chunk.h"

void initRenderer(void);
void renderScene(ChunkData **visible, int numChunks, int numGen,
                 float camX, float camY, float camZ,
                 float fx, float fy, float fz,
                 int winW, int winH, int renderDist, float feetY);
void updateFpsStats(void);
void getFrustumPlanes(float m[16], float planes[6][4]);
int cubeInFrustum(float planes[6][4], float min[3], float max[3]);

#endif
