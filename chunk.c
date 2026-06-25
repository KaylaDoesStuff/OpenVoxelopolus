#include "chunk.h"
#include "noise.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

ChunkData world[MAX_CHUNKS];
int worldCount;

#define EMIT_QUAD_TRI(x0,y0,z0, x1,y1,z1, x2,y2,z2, x3,y3,z3, nx,ny,nz, cr,cg,cb) \
    do { \
        if (fcnt + 6 > MAX_FILL_VERTS) goto done; \
        fillBuf[fcnt*9+0]=(x0); fillBuf[fcnt*9+1]=(y0); fillBuf[fcnt*9+2]=(z0); \
        fillBuf[fcnt*9+3]=(nx); fillBuf[fcnt*9+4]=(ny); fillBuf[fcnt*9+5]=(nz); \
        fillBuf[fcnt*9+6]=(cr); fillBuf[fcnt*9+7]=(cg); fillBuf[fcnt*9+8]=(cb); fcnt++; \
        fillBuf[fcnt*9+0]=(x1); fillBuf[fcnt*9+1]=(y1); fillBuf[fcnt*9+2]=(z1); \
        fillBuf[fcnt*9+3]=(nx); fillBuf[fcnt*9+4]=(ny); fillBuf[fcnt*9+5]=(nz); \
        fillBuf[fcnt*9+6]=(cr); fillBuf[fcnt*9+7]=(cg); fillBuf[fcnt*9+8]=(cb); fcnt++; \
        fillBuf[fcnt*9+0]=(x2); fillBuf[fcnt*9+1]=(y2); fillBuf[fcnt*9+2]=(z2); \
        fillBuf[fcnt*9+3]=(nx); fillBuf[fcnt*9+4]=(ny); fillBuf[fcnt*9+5]=(nz); \
        fillBuf[fcnt*9+6]=(cr); fillBuf[fcnt*9+7]=(cg); fillBuf[fcnt*9+8]=(cb); fcnt++; \
        fillBuf[fcnt*9+0]=(x0); fillBuf[fcnt*9+1]=(y0); fillBuf[fcnt*9+2]=(z0); \
        fillBuf[fcnt*9+3]=(nx); fillBuf[fcnt*9+4]=(ny); fillBuf[fcnt*9+5]=(nz); \
        fillBuf[fcnt*9+6]=(cr); fillBuf[fcnt*9+7]=(cg); fillBuf[fcnt*9+8]=(cb); fcnt++; \
        fillBuf[fcnt*9+0]=(x2); fillBuf[fcnt*9+1]=(y2); fillBuf[fcnt*9+2]=(z2); \
        fillBuf[fcnt*9+3]=(nx); fillBuf[fcnt*9+4]=(ny); fillBuf[fcnt*9+5]=(nz); \
        fillBuf[fcnt*9+6]=(cr); fillBuf[fcnt*9+7]=(cg); fillBuf[fcnt*9+8]=(cb); fcnt++; \
        fillBuf[fcnt*9+0]=(x3); fillBuf[fcnt*9+1]=(y3); fillBuf[fcnt*9+2]=(z3); \
        fillBuf[fcnt*9+3]=(nx); fillBuf[fcnt*9+4]=(ny); fillBuf[fcnt*9+5]=(nz); \
        fillBuf[fcnt*9+6]=(cr); fillBuf[fcnt*9+7]=(cg); fillBuf[fcnt*9+8]=(cb); fcnt++; \
    } while(0)

#define VERTEX(x,y,z, nx,ny,nz, cr,cg,cb) \
    do { \
        if (fcnt >= MAX_FILL_VERTS) goto done; \
        fillBuf[fcnt*9+0]=(x); fillBuf[fcnt*9+1]=(y); fillBuf[fcnt*9+2]=(z); \
        fillBuf[fcnt*9+3]=(nx); fillBuf[fcnt*9+4]=(ny); fillBuf[fcnt*9+5]=(nz); \
        fillBuf[fcnt*9+6]=(cr); fillBuf[fcnt*9+7]=(cg); fillBuf[fcnt*9+8]=(cb); fcnt++; \
    } while(0)

int chunkHash(int cx, int cy, int cz)
{
    return ((cx * 73856093) ^ (cy * 19349663) ^ (cz * 83492791)) & (MAX_CHUNKS - 1);
}

ChunkData *findChunk(int cx, int cy, int cz)
{
    int h = chunkHash(cx, cy, cz);
    for (int i = 0; i < MAX_CHUNKS; i++) {
        int idx = (h + i) & (MAX_CHUNKS - 1);
        if (!world[idx].valid) return NULL;
        if (world[idx].cx == cx && world[idx].cy == cy && world[idx].cz == cz)
            return &world[idx];
    }
    return NULL;
}

ChunkData *getOrCreateChunk(int cx, int cy, int cz)
{
    ChunkData *c = findChunk(cx, cy, cz);
    if (c) return c;

    int h = chunkHash(cx, cy, cz);
    for (int i = 0; i < MAX_CHUNKS; i++) {
        int idx = (h + i) & (MAX_CHUNKS - 1);
        if (!world[idx].valid) {
            world[idx].valid = 1;
            world[idx].cx = cx;
            world[idx].cy = cy;
            world[idx].cz = cz;
            world[idx].cachedVerts = NULL;
            world[idx].cachedCount = 0;
            world[idx].genDone = 0;
            for (int a = 0; a < GRID_SIZE; a++) {
                int bx = cx * 16 + a - 8;
                for (int b = 0; b < GRID_SIZE; b++) {
                    int bz = cz * 16 + b - 8;
                    int surfaceY = (int)getSurfaceHeight(bx, bz);
                    for (int j = 0; j < GRID_SIZE; j++) {
                        int by = -cy * 16 - 15 + j;
                        if (by > surfaceY) {
                            world[idx].blocks[a][j][b] = BLOCK_AIR;
                        } else if (by == surfaceY) {
                            world[idx].blocks[a][j][b] = surfaceY >= 0 ? BLOCK_GRASS : BLOCK_STONE;
                        } else if (by > surfaceY - 4) {
                            world[idx].blocks[a][j][b] = surfaceY >= 0 ? BLOCK_DIRT : BLOCK_STONE;
                        } else {
                            world[idx].blocks[a][j][b] = BLOCK_STONE;
                        }
                    }
                }
            }
            worldCount++;
            invalidateNeighbors(cx, cy, cz);
            return &world[idx];
        }
    }
    return NULL;
}

int blockToChunkY(int by)
{
    if (by <= 0) return (-by) / 16;
    return -(by + 15) / 16;
}

BlockType getBlock(int bx, int by, int bz)
{
    if (by > 255 || by < -63) return BLOCK_AIR;
    int cx = (int)floorf((bx + 8) / 16.0f);
    int cz = (int)floorf((bz + 8) / 16.0f);
    int cy = blockToChunkY(by);
    if (cy < -ABOVE_LAYERS || cy >= UNDER_LAYERS) return BLOCK_AIR;
    ChunkData *c = findChunk(cx, cy, cz);
    if (!c) return BLOCK_AIR;
    int i = bx - cx * 16 + 8;
    int j = by + 15 + cy * 16;
    int k = bz - cz * 16 + 8;
    if (i < 0 || i >= GRID_SIZE || j < 0 || j >= GRID_SIZE || k < 0 || k >= GRID_SIZE)
        return BLOCK_AIR;
    return c->blocks[i][j][k];
}

void invalidateChunkGeometry(ChunkData *c)
{
    free(c->cachedVerts);
    c->cachedVerts = NULL;
    c->cachedCount = 0;
    c->genDone = 0;
}

void invalidateNeighbors(int cx, int cy, int cz)
{
    int dirs[6][3] = {{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{1,0,0},{-1,0,0}};
    for (int d = 0; d < 6; d++) {
        ChunkData *n = findChunk(cx + dirs[d][0], cy + dirs[d][1], cz + dirs[d][2]);
        if (n) invalidateChunkGeometry(n);
    }
}

int hasChunk(int cx, int cy, int cz)
{
    return findChunk(cx, cy, cz) != NULL;
}

int blockExists(int bx, int by, int bz)
{
    return getBlock(bx, by, bz) != BLOCK_AIR;
}

float getSurfaceHeight(int bx, int bz)
{
    float n = fbm(bx * 0.006f, bz * 0.006f, 5, 2.0f, 0.5f);
    return 8.0f + n * 16.0f;
}

int rayCastBlock(float ox, float oy, float oz, float dx, float dy, float dz,
                 float maxDist, int *outBX, int *outBY, int *outBZ,
                 int *outFNX, int *outFNY, int *outFNZ)
{
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 0.0001f) return 0;
    float rx = dx / len, ry = dy / len, rz = dz / len;

    int bx = (int)floorf(ox);
    int by = (int)floorf(oy);
    int bz = (int)floorf(oz);

    int stepX = rx > 0 ? 1 : -1;
    int stepY = ry > 0 ? 1 : -1;
    int stepZ = rz > 0 ? 1 : -1;

    float tDeltaX = fabsf(1.0f / rx);
    float tDeltaY = fabsf(1.0f / ry);
    float tDeltaZ = fabsf(1.0f / rz);

    float tMaxX = (rx > 0) ? (floorf(ox) + 1 - ox) * tDeltaX : (ox - floorf(ox)) * tDeltaX;
    float tMaxY = (ry > 0) ? (floorf(oy) + 1 - oy) * tDeltaY : (oy - floorf(oy)) * tDeltaY;
    float tMaxZ = (rz > 0) ? (floorf(oz) + 1 - oz) * tDeltaZ : (oz - floorf(oz)) * tDeltaZ;

    int lastStepX = 0, lastStepY = 0, lastStepZ = 0;
    int skipFirst = 1;
    for (int i = 0; i < (int)maxDist + 2; i++) {
        if (!skipFirst && blockExists(bx, by, bz)) {
            *outBX = bx; *outBY = by; *outBZ = bz;
            if (outFNX) *outFNX = -lastStepX;
            if (outFNY) *outFNY = -lastStepY;
            if (outFNZ) *outFNZ = -lastStepZ;
            return 1;
        }
        skipFirst = 0;

        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            if (tMaxX > maxDist) return 0;
            bx += stepX; tMaxX += tDeltaX;
            lastStepX = stepX; lastStepY = 0; lastStepZ = 0;
        } else if (tMaxY < tMaxZ) {
            if (tMaxY > maxDist) return 0;
            by += stepY; tMaxY += tDeltaY;
            lastStepX = 0; lastStepY = stepY; lastStepZ = 0;
        } else {
            if (tMaxZ > maxDist) return 0;
            bz += stepZ; tMaxZ += tDeltaZ;
            lastStepX = 0; lastStepY = 0; lastStepZ = stepZ;
        }
    }
    return 0;
}

void blockColor(BlockType t, float *r, float *g, float *b)
{
    switch (t) {
        case BLOCK_GRASS: *r=0.22f; *g=0.72f; *b=0.18f; break;
        case BLOCK_DIRT:  *r=0.55f; *g=0.35f; *b=0.15f; break;
        default:          *r=0.60f; *g=0.60f; *b=0.60f; break;
    }
}

void generateChunk(ChunkData *chunk, float *fillBuf)
{
    int cx = chunk->cx, cy = chunk->cy, cz = chunk->cz;
    int fcnt = 0;
    int mask[16][16], taken[16][16];
    // TOP face (normal +Y)
    {
        for (int j = 0; j < 16; j++) {
            int by = -cy*16 - 15 + j;
            int shadow[16][16];
            for (int a = 0; a < 16; a++) for (int b = 0; b < 16; b++) {
                mask[a][b] = 0;
                shadow[a][b] = 0;
                taken[a][b] = 0;
                int bx = cx*16 + a - 8;
                int bz = cz*16 + b - 8;
                BlockType bt = chunk->blocks[a][j][b];
                if (bt != BLOCK_AIR && getBlock(bx, by + 1, bz) == BLOCK_AIR) {
                    mask[a][b] = bt;
                    int hx, hy, hz;
                    if (rayCastBlock(bx + 0.5f, by + 1.0f, bz + 0.5f,
                                     LIGHT_DIR_X, LIGHT_DIR_Y, LIGHT_DIR_Z,
                                     SHADOW_RAY_DIST, &hx, &hy, &hz, NULL, NULL, NULL))
                        shadow[a][b] = 1;
                }
            }
            float yPos = (float)(by + 1);
            for (int a = 0; a < 16; a++) {
                for (int b = 0; b < 16; b++) {
                    int bt = mask[a][b];
                    int sh = bt ? shadow[a][b] : 0;
                    if (!bt || taken[a][b]) continue;
                    int w = 1; while (a+w < 16 && mask[a+w][b] == bt && shadow[a+w][b] == sh && !taken[a+w][b]) w++;
                    int h = 1;
                    for (int y = 1; y < 16-b; y++) {
                        int ok = 1;
                        for (int x = 0; x < w; x++)
                            if (mask[a+x][b+y] != bt || shadow[a+x][b+y] != sh || taken[a+x][b+y]) { ok = 0; break; }
                        if (!ok) break;
                        h = y + 1;
                    }
                    for (int x = 0; x < w; x++) for (int y = 0; y < h; y++) taken[a+x][b+y] = 1;
                    float cr, cg, cb;
                    blockColor(bt, &cr, &cg, &cb);
                    if (sh) { cr *= SHADOW_FACTOR; cg *= SHADOW_FACTOR; cb *= SHADOW_FACTOR; }
                    float x0 = cx*16 + a - 8.0f, x1 = x0 + w;
                    float z0 = cz*16 + b - 8.0f, z1 = z0 + h;
EMIT_QUAD_TRI(x0, yPos, z0, x1, yPos, z0, x1, yPos, z1, x0, yPos, z1,
               0, 1, 0, cr, cg, cb);
                }
            }
        }
    }

    // BOTTOM face (normal -Y)
    {
        for (int j = 0; j < 16; j++) {
            int by = -cy*16 - 15 + j;
            for (int a = 0; a < 16; a++) for (int b = 0; b < 16; b++) {
                mask[a][b] = 0;
                taken[a][b] = 0;
                int bx = cx*16 + a - 8;
                int bz = cz*16 + b - 8;
                BlockType bt = chunk->blocks[a][j][b];
                if (bt != BLOCK_AIR && getBlock(bx, by - 1, bz) == BLOCK_AIR)
                    mask[a][b] = bt;
            }
            float yPos = (float)by;
            for (int a = 0; a < 16; a++) {
                for (int b = 0; b < 16; b++) {
                    int bt = mask[a][b];
                    if (!bt || taken[a][b]) continue;
                    int w = 1; while (a+w < 16 && mask[a+w][b] == bt && !taken[a+w][b]) w++;
                    int h = 1;
                    for (int y = 1; y < 16-b; y++) {
                        int ok = 1;
                        for (int x = 0; x < w; x++)
                            if (mask[a+x][b+y] != bt || taken[a+x][b+y]) { ok = 0; break; }
                        if (!ok) break;
                        h = y + 1;
                    }
                    for (int x = 0; x < w; x++) for (int y = 0; y < h; y++) taken[a+x][b+y] = 1;
                    float cr, cg, cb;
                    blockColor(bt, &cr, &cg, &cb);
                    float x0 = cx*16 + a - 8.0f, x1 = x0 + w;
                    float z0 = cz*16 + b - 8.0f, z1 = z0 + h;
EMIT_QUAD_TRI(x0, yPos, z0, x1, yPos, z0, x1, yPos, z1, x0, yPos, z1,
               0, -1, 0, cr, cg, cb);
                }
            }
        }
    }

    // FRONT face (normal +Z)
    {
        for (int k = 0; k < 16; k++) {
            int bz = cz*16 + k - 8;
            for (int a = 0; a < 16; a++) for (int b = 0; b < 16; b++) {
                mask[a][b] = 0;
                taken[a][b] = 0;
                int bx = cx*16 + a - 8;
                int by = -cy*16 - 15 + b;
                BlockType bt = chunk->blocks[a][b][k];
                if (bt != BLOCK_AIR && getBlock(bx, by, bz + 1) == BLOCK_AIR)
                    mask[a][b] = bt;
            }
            float zPos = (float)(bz + 1);
            for (int a = 0; a < 16; a++) {
                for (int b = 0; b < 16; b++) {
                    int bt = mask[a][b];
                    if (!bt || taken[a][b]) continue;
                    int w = 1; while (a+w < 16 && mask[a+w][b] == bt && !taken[a+w][b]) w++;
                    int h = 1;
                    for (int y = 1; y < 16-b; y++) {
                        int ok = 1;
                        for (int x = 0; x < w; x++)
                            if (mask[a+x][b+y] != bt || taken[a+x][b+y]) { ok = 0; break; }
                        if (!ok) break;
                        h = y + 1;
                    }
                    for (int x = 0; x < w; x++) for (int y = 0; y < h; y++) taken[a+x][b+y] = 1;
                    float cr, cg, cb;
                    blockColor(bt, &cr, &cg, &cb);
                    float x0 = cx*16 + a - 8.0f, x1 = x0 + w;
                    float y0 = b - 15.0f - cy*16, y1 = y0 + h;
EMIT_QUAD_TRI(x0, y0, zPos, x1, y0, zPos, x1, y1, zPos, x0, y1, zPos,
               0, 0, 1, cr, cg, cb);
                }
            }
        }
    }

    // BACK face (normal -Z)
    {
        for (int k = 0; k < 16; k++) {
            int bz = cz*16 + k - 8;
            int shadow[16][16];
            for (int a = 0; a < 16; a++) for (int b = 0; b < 16; b++) {
                mask[a][b] = 0;
                shadow[a][b] = 0;
                taken[a][b] = 0;
                int bx = cx*16 + b - 8;
                int by = -cy*16 - 15 + a;
                BlockType bt = chunk->blocks[b][a][k];
                if (bt != BLOCK_AIR && getBlock(bx, by, bz - 1) == BLOCK_AIR) {
                    mask[a][b] = bt;
                    int hx, hy, hz;
                    if (rayCastBlock(bx + 0.5f, by + 0.5f, (float)bz,
                                     LIGHT_DIR_X, LIGHT_DIR_Y, LIGHT_DIR_Z,
                                     SHADOW_RAY_DIST, &hx, &hy, &hz, NULL, NULL, NULL))
                        shadow[a][b] = 1;
                }
            }
            float zPos = (float)bz;
            for (int a = 0; a < 16; a++) {
                for (int b = 0; b < 16; b++) {
                    int bt = mask[a][b];
                    int sh = bt ? shadow[a][b] : 0;
                    if (!bt || taken[a][b]) continue;
                    int w = 1; while (a+w < 16 && mask[a+w][b] == bt && shadow[a+w][b] == sh && !taken[a+w][b]) w++;
                    int h = 1;
                    for (int y = 1; y < 16-b; y++) {
                        int ok = 1;
                        for (int x = 0; x < w; x++)
                            if (mask[a+x][b+y] != bt || shadow[a+x][b+y] != sh || taken[a+x][b+y]) { ok = 0; break; }
                        if (!ok) break;
                        h = y + 1;
                    }
                    for (int x = 0; x < w; x++) for (int y = 0; y < h; y++) taken[a+x][b+y] = 1;
                    float cr, cg, cb;
                    blockColor(bt, &cr, &cg, &cb);
                    if (sh) { cr *= SHADOW_FACTOR; cg *= SHADOW_FACTOR; cb *= SHADOW_FACTOR; }
                    float x0 = cx*16 + b - 8.0f, x1 = x0 + h;
                    float y0 = a - 15.0f - cy*16, y1 = y0 + w;
EMIT_QUAD_TRI(x0, y0, zPos, x1, y0, zPos, x1, y1, zPos, x0, y1, zPos,
               0, 0, -1, cr, cg, cb);
                }
            }
        }
    }

    // RIGHT face (normal +X)
    {
        for (int i = 0; i < 16; i++) {
            int bx = cx*16 + i - 8;
            int shadow[16][16];
            for (int a = 0; a < 16; a++) for (int b = 0; b < 16; b++) {
                mask[a][b] = 0;
                shadow[a][b] = 0;
                taken[a][b] = 0;
                int by = -cy*16 - 15 + a;
                int bz = cz*16 + b - 8;
                BlockType bt = chunk->blocks[i][a][b];
                if (bt != BLOCK_AIR && getBlock(bx + 1, by, bz) == BLOCK_AIR) {
                    mask[a][b] = bt;
                    int hx, hy, hz;
                    if (rayCastBlock(bx + 1.0f, by + 0.5f, bz + 0.5f,
                                     LIGHT_DIR_X, LIGHT_DIR_Y, LIGHT_DIR_Z,
                                     SHADOW_RAY_DIST, &hx, &hy, &hz, NULL, NULL, NULL))
                        shadow[a][b] = 1;
                }
            }
            float xPos = (float)(bx + 1);
            for (int a = 0; a < 16; a++) {
                for (int b = 0; b < 16; b++) {
                    int bt = mask[a][b];
                    int sh = bt ? shadow[a][b] : 0;
                    if (!bt || taken[a][b]) continue;
                    int w = 1; while (a+w < 16 && mask[a+w][b] == bt && shadow[a+w][b] == sh && !taken[a+w][b]) w++;
                    int h = 1;
                    for (int y = 1; y < 16-b; y++) {
                        int ok = 1;
                        for (int x = 0; x < w; x++)
                            if (mask[a+x][b+y] != bt || shadow[a+x][b+y] != sh || taken[a+x][b+y]) { ok = 0; break; }
                        if (!ok) break;
                        h = y + 1;
                    }
                    for (int x = 0; x < w; x++) for (int y = 0; y < h; y++) taken[a+x][b+y] = 1;
                    float cr, cg, cb;
                    blockColor(bt, &cr, &cg, &cb);
                    if (sh) { cr *= SHADOW_FACTOR; cg *= SHADOW_FACTOR; cb *= SHADOW_FACTOR; }
                    float y0 = a - 15.0f - cy*16, y1 = y0 + w;
                    float z0 = cz*16 + b - 8.0f, z1 = z0 + h;
EMIT_QUAD_TRI(xPos, y0, z0, xPos, y0, z1, xPos, y1, z1, xPos, y1, z0,
               1, 0, 0, cr, cg, cb);
                }
            }
        }
    }

    // LEFT face (normal -X)
    {
        for (int i = 0; i < 16; i++) {
            int bx = cx*16 + i - 8;
            for (int a = 0; a < 16; a++) for (int b = 0; b < 16; b++) {
                mask[a][b] = 0;
                taken[a][b] = 0;
                int by = -cy*16 - 15 + b;
                int bz = cz*16 + a - 8;
                BlockType bt = chunk->blocks[i][b][a];
                if (bt != BLOCK_AIR && getBlock(bx - 1, by, bz) == BLOCK_AIR)
                    mask[a][b] = bt;
            }
            float xPos = (float)bx;
            for (int a = 0; a < 16; a++) {
                for (int b = 0; b < 16; b++) {
                    int bt = mask[a][b];
                    if (!bt || taken[a][b]) continue;
                    int w = 1; while (a+w < 16 && mask[a+w][b] == bt && !taken[a+w][b]) w++;
                    int h = 1;
                    for (int y = 1; y < 16-b; y++) {
                        int ok = 1;
                        for (int x = 0; x < w; x++)
                            if (mask[a+x][b+y] != bt || taken[a+x][b+y]) { ok = 0; break; }
                        if (!ok) break;
                        h = y + 1;
                    }
                    for (int x = 0; x < w; x++) for (int y = 0; y < h; y++) taken[a+x][b+y] = 1;
                    float cr, cg, cb;
                    blockColor(bt, &cr, &cg, &cb);
                    float y0 = b - 15.0f - cy*16, y1 = y0 + h;
                    float z0 = cz*16 + a - 8.0f, z1 = z0 + w;
EMIT_QUAD_TRI(xPos, y0, z0, xPos, y0, z1, xPos, y1, z1, xPos, y1, z0,
               -1, 0, 0, cr, cg, cb);
                }
            }
        }
    }

done:
    chunk->cachedCount = fcnt;
    if (fcnt > 0) {
        chunk->cachedVerts = malloc(fcnt * 9 * sizeof(float));
        memcpy(chunk->cachedVerts, fillBuf, fcnt * 9 * sizeof(float));
    } else {
        chunk->cachedVerts = NULL;
    }
    chunk->genDone = 1;
}
