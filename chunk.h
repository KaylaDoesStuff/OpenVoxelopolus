#ifndef CHUNK_H
#define CHUNK_H

#include "blocks.h"

#define GRID_SIZE 16
#define UNDER_LAYERS 4
#define ABOVE_LAYERS 16
#define MAX_FILL_VERTS 98304
#define MAX_CHUNKS 16384

typedef struct {
    int cx, cy, cz;
    int valid;
    BlockType blocks[GRID_SIZE][GRID_SIZE][GRID_SIZE];
    float *cachedVerts;
    int cachedCount;
    int genDone;
} ChunkData;

extern ChunkData world[MAX_CHUNKS];
extern int worldCount;

int chunkHash(int cx, int cy, int cz);
ChunkData *findChunk(int cx, int cy, int cz);
ChunkData *getOrCreateChunk(int cx, int cy, int cz);
int hasChunk(int cx, int cy, int cz);
int blockToChunkY(int by);
BlockType getBlock(int bx, int by, int bz);
int blockExists(int bx, int by, int bz);
float getSurfaceHeight(int bx, int bz);
void invalidateChunkGeometry(ChunkData *c);
void invalidateNeighbors(int cx, int cy, int cz);
void generateChunk(ChunkData *chunk, float *fillBuf);
void blockColor(BlockType t, float *r, float *g, float *b);
int rayCastBlock(float ox, float oy, float oz, float dx, float dy, float dz,
                 float maxDist, int *outBX, int *outBY, int *outBZ,
                 int *outFNX, int *outFNY, int *outFNZ);

#endif
