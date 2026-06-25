#define GL_GLEXT_PROTOTYPES
#include <GL/glut.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include "chunk.h"
#include "render.h"
#include "noise.h"
#include "shader.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SPEED 0.064f
#define GRAVITY       -9.81f
#define JUMP_VY        4.85f
#define PLAYER_MASS   70.0f
#define AIR_DENSITY    1.225f
#define DRAG_COEFF     0.8f
#define CROSS_SECTION  0.35f
#define MAX_THREADS 64

typedef struct {
    pthread_t thread;
    volatile int running;
    volatile int assigned;
    int startIdx, endIdx;
    ChunkData *genPtrs[256];
    int numGen;
    float *fillBuf;
    float *outBuf;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} WorkerThread;

static WorkerThread workers[MAX_THREADS];
static int numPoolThreads;
static volatile int poolDoneCount;
static pthread_mutex_t poolDoneMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t poolDoneCond = PTHREAD_COND_INITIALIZER;

float camX = -17.6f, camY = 4.8f, camZ = 17.6f;
float yaw = -45.0f, pitch = -28.0f;
float fov = 110.0f;
int winW = 800, winH = 600;
int mouseSens = 5;
int renderDist = 2;
float feetY = 3.0f;
float playerHeight = 1.8f;

float camFX, camFY, camFZ;
int keyW, keyS, keyA, keyD, keySpace, keyC;
int mouseInited;
int fullscreen;
float *fillBufs[MAX_THREADS];

static float playerVy = 0.0f;
static int prevPhysTime = 0;
static int onGround = 1;

static int playerCollides(float px, float py, float pz, float h)
{
    float minX = px - 0.3f, maxX = px + 0.3f;
    float minY = py, maxY = py + h;
    float minZ = pz - 0.2f, maxZ = pz + 0.2f;
    int bx0 = (int)floorf(minX), bx1 = (int)floorf(maxX);
    int by0 = (int)floorf(minY), by1 = (int)floorf(maxY);
    int bz0 = (int)floorf(minZ), bz1 = (int)floorf(maxZ);
    for (int bx = bx0; bx <= bx1; bx++)
        for (int by = by0; by <= by1; by++)
            for (int bz = bz0; bz <= bz1; bz++)
                if (blockExists(bx, by, bz))
                    return 1;
    return 0;
}

void *poolWorker(void *arg)
{
    WorkerThread *w = (WorkerThread*)arg;
    while (1) {
        pthread_mutex_lock(&w->mutex);
        while (!w->assigned && w->running)
            pthread_cond_wait(&w->cond, &w->mutex);
        if (!w->running) {
            pthread_mutex_unlock(&w->mutex);
            break;
        }
        w->assigned = 0;
        pthread_mutex_unlock(&w->mutex);

        for (int i = 0; i < w->numGen; i++) {
            ChunkData *c = w->genPtrs[i];
            if (c && c->cachedVerts == NULL)
                generateChunk(c, w->fillBuf);
        }

        pthread_mutex_lock(&poolDoneMutex);
        poolDoneCount++;
        pthread_cond_signal(&poolDoneCond);
        pthread_mutex_unlock(&poolDoneMutex);
    }
    return NULL;
}

void updateCam(int value)
{
    float yaw_r = yaw * M_PI / 180.0f;
    float hx = 0.0f, hz = 0.0f;
    int now = glutGet(GLUT_ELAPSED_TIME);
    float dt = (now - prevPhysTime) / 1000.0f;
    if (dt > 0.05f) dt = 0.05f;
    prevPhysTime = now;

    // Crouch
    float targetHeight = keyC ? 1.5f : 1.8f;
    if (!keyC && targetHeight > playerHeight &&
        playerCollides(camX, feetY, camZ, targetHeight))
        targetHeight = playerHeight;
    playerHeight = targetHeight;
    float eyeOffset = playerHeight - 0.2f;

    // Horizontal movement
    if (keyW) { hx -= sinf(yaw_r); hz -= cosf(yaw_r); }
    if (keyS) { hx += sinf(yaw_r); hz += cosf(yaw_r); }
    if (keyA) { hx -= cosf(yaw_r); hz += sinf(yaw_r); }
    if (keyD) { hx += cosf(yaw_r); hz -= sinf(yaw_r); }

    float len = sqrtf(hx * hx + hz * hz);
    if (len > 0.001f) {
        hx = hx / len * SPEED;
        hz = hz / len * SPEED;

        float newX = camX + hx;
        if (playerCollides(newX, feetY, camZ, playerHeight)) {
            newX = hx > 0
                ? (float)(int)floorf(camX + 0.3f + hx) - 0.3f - 0.01f
                : (float)(int)ceilf(camX - 0.3f + hx) + 0.3f + 0.01f;
            if (!playerCollides(newX, feetY, camZ, playerHeight))
                camX = newX;
        } else {
            camX = newX;
        }

        float newZ = camZ + hz;
        if (playerCollides(camX, feetY, newZ, playerHeight)) {
            newZ = hz > 0
                ? (float)(int)floorf(camZ + 0.2f + hz) - 0.2f - 0.01f
                : (float)(int)ceilf(camZ - 0.2f + hz) + 0.2f + 0.01f;
            if (!playerCollides(camX, feetY, newZ, playerHeight))
                camZ = newZ;
        } else {
            camZ = newZ;
        }
    }

    // Jump
    if (keySpace && onGround) {
        playerVy = JUMP_VY;
        onGround = 0;
    }

    // Gravity + air resistance
    if (!onGround) {
        float v = playerVy;
        float drag = 0.5f * AIR_DENSITY * DRAG_COEFF * CROSS_SECTION * v * v;
        if (v > 0) drag = -drag;
        float accel = GRAVITY + drag / PLAYER_MASS;
        playerVy += accel * dt;
    }

    // Integrate vertical position
    float newY = feetY + playerVy * dt;

    // Collision resolution
    if (playerCollides(camX, newY, camZ, playerHeight)) {
        if (playerVy < 0) {
            // Landing
            float lo = newY, hi = feetY;
            for (int i = 0; i < 20; i++) {
                float mid = (lo + hi) / 2.0f;
                if (playerCollides(camX, mid, camZ, playerHeight))
                    lo = mid;
                else
                    hi = mid;
            }
            feetY = hi;
            playerVy = 0;
        } else {
            // Head bump
            float lo = feetY, hi = newY;
            for (int i = 0; i < 20; i++) {
                float mid = (lo + hi) / 2.0f;
                if (playerCollides(camX, mid, camZ, playerHeight))
                    hi = mid;
                else
                    lo = mid;
            }
            feetY = lo;
            playerVy = 0;
        }
    } else {
        feetY = newY;
    }

    // Ground check
    onGround = playerCollides(camX, feetY - 0.001f, camZ, playerHeight);

    camY = feetY + eyeOffset;
}

void display()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(fov, (float)winW / (float)winH, 0.1f, 500.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float yaw_r = yaw * M_PI / 180.0f;
    float pitch_r = pitch * M_PI / 180.0f;
    float cp = cosf(pitch_r);
    float fx = -cp * sinf(yaw_r);
    float fy = sinf(pitch_r);
    float fz = -cp * cosf(yaw_r);

    gluLookAt(camX, camY, camZ, camX + fx, camY + fy, camZ + fz, 0, 1, 0);
    camFX = fx; camFY = fy; camFZ = fz;

    float proj[16], view[16], m[16];
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glGetFloatv(GL_MODELVIEW_MATRIX, view);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            m[r + c * 4] = proj[r]     * view[c * 4]     +
                           proj[r + 4] * view[c * 4 + 1] +
                           proj[r + 8] * view[c * 4 + 2] +
                           proj[r + 12] * view[c * 4 + 3];

    float planes[6][4];
    getFrustumPlanes(m, planes);

    int camCX = (int)floorf((camX + 8.0f) / 16.0f);
    int camCZ = (int)floorf((camZ + 8.0f) / 16.0f);

    int effRD = renderDist * 4;
    int maxChunks = (2 * effRD + 1) * (2 * effRD + 1) * (UNDER_LAYERS + ABOVE_LAYERS);
    static ChunkData **visible = NULL;
    static int visibleCap = 0;
    if (maxChunks > visibleCap) {
        free(visible);
        visibleCap = maxChunks * 2;
        visible = malloc(visibleCap * sizeof(ChunkData*));
    }
    int numChunks = 0;
    for (int dx = -effRD; dx <= effRD; dx++) {
        for (int dz = -effRD; dz <= effRD; dz++) {
            if (dx * dx + dz * dz > effRD * effRD) continue;
            int cx = camCX + dx;
            int cz = camCZ + dz;
            for (int cy = 0; cy < UNDER_LAYERS; cy++) {
                float min[3] = {cx*16.0f - 8.5f, -cy*16.0f - 16.5f, cz*16.0f - 8.5f};
                float max[3] = {cx*16.0f + 8.5f, -cy*16.0f + 2.5f, cz*16.0f + 8.5f};
                if (!cubeInFrustum(planes, min, max)) continue;
                ChunkData *c = getOrCreateChunk(cx, cy, cz);
                if (!c) continue;
                visible[numChunks] = c;
                numChunks++;
            }
            for (int cy = -1; cy >= -ABOVE_LAYERS; cy--) {
                float min[3] = {cx*16.0f - 8.5f, -cy*16.0f - 16.5f, cz*16.0f - 8.5f};
                float max[3] = {cx*16.0f + 8.5f, -cy*16.0f + 2.5f, cz*16.0f + 8.5f};
                if (!cubeInFrustum(planes, min, max)) continue;
                ChunkData *c = getOrCreateChunk(cx, cy, cz);
                if (!c) continue;
                visible[numChunks] = c;
                numChunks++;
            }
        }
    }

    // Collect chunks that need geometry generation
    ChunkData *genPtrs[256];
    int numGen = 0;
    for (int i = 0; i < numChunks; i++) {
        if (!visible[i]->genDone && numGen < 256) {
            genPtrs[numGen++] = visible[i];
        }
    }

    if (numGen > 0) {
        int numThreads = numGen < numPoolThreads ? numGen : numPoolThreads;
        if (numThreads < 1) numThreads = 1;

        poolDoneCount = 0;
        for (int t = 0; t < numThreads; t++) {
            int start = (t * numGen) / numThreads;
            int end = ((t + 1) * numGen) / numThreads;
            pthread_mutex_lock(&workers[t].mutex);
            workers[t].numGen = end - start;
            for (int j = 0; j < workers[t].numGen; j++)
                workers[t].genPtrs[j] = genPtrs[start + j];
            workers[t].assigned = 1;
            pthread_cond_signal(&workers[t].cond);
            pthread_mutex_unlock(&workers[t].mutex);
        }

        pthread_mutex_lock(&poolDoneMutex);
        while (poolDoneCount < numThreads)
            pthread_cond_wait(&poolDoneCond, &poolDoneMutex);
        pthread_mutex_unlock(&poolDoneMutex);
    }

    renderScene(visible, numChunks, numGen, camX, camY, camZ, fx, fy, fz, winW, winH, renderDist, feetY);

    glutSwapBuffers();

    updateFpsStats();
}

void idle(void)
{
    static int lastUpdate = 0;
    int now = glutGet(GLUT_ELAPSED_TIME);
    if (now - lastUpdate >= 16) {
        lastUpdate = now;
        updateCam(0);
    }
    glutPostRedisplay();
}

void reshape(int w, int h)
{
    winW = w; winH = h;
    glViewport(0, 0, w, h);
    glutWarpPointer(w / 2, h / 2);
}

void specialKeys(int key, int x, int y)
{
    if (key == GLUT_KEY_F11) {
        if (!fullscreen) { glutFullScreen(); fullscreen = 1; }
        else { glutReshapeWindow(800, 600); fullscreen = 0; }
    }
}

void mouseMove(int x, int y)
{
    if (!mouseInited) {
        mouseInited = 1;
        glutWarpPointer(winW / 2, winH / 2);
        return;
    }
    int dx = x - winW / 2;
    int dy = y - winH / 2;
    if (dx == 0 && dy == 0) return;
    yaw -= dx * mouseSens * 0.1f;
    pitch -= dy * mouseSens * 0.1f;
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
    float yaw_r = yaw * M_PI / 180.0f;
    float pitch_r = pitch * M_PI / 180.0f;
    float cp = cosf(pitch_r);
    camFX = -cp * sinf(yaw_r);
    camFY = sinf(pitch_r);
    camFZ = -cp * cosf(yaw_r);
    glutWarpPointer(winW / 2, winH / 2);
    glutPostRedisplay();
}

void keyDown(unsigned char key, int x, int y)
{
    switch (key) {
        case 'w': case 'W': keyW = 1; break;
        case 's': case 'S': keyS = 1; break;
        case 'a': case 'A': keyA = 1; break;
        case 'd': case 'D': keyD = 1; break;
        case ' ': keySpace = 1; break;
        case 'c': case 'C': keyC = 1; break;
        case 'f': case 'F': if (renderDist < 6) renderDist++; break;
        case 'g': case 'G':
            renderDist--;
            if (renderDist < 1) renderDist = 1;
            break;
        case 'i': case 'I':
            mouseSens += 1;
            return;
        case 'o': case 'O':
            mouseSens -= 1;
            if (mouseSens < 1) mouseSens = 1;
            return;
        case '=': case '+':
            fov += 1.0f;
            if (fov > 140.0f) fov = 140.0f;
            glutPostRedisplay();
            return;
        case '-': case '_':
            fov -= 1.0f;
            if (fov < 1.0f) fov = 1.0f;
            glutPostRedisplay();
            return;
        case 27: exit(0);
    }
}

void keyUp(unsigned char key, int x, int y)
{
    switch (key) {
        case 'w': case 'W': keyW = 0; break;
        case 's': case 'S': keyS = 0; break;
        case 'a': case 'A': keyA = 0; break;
        case 'd': case 'D': keyD = 0; break;
        case ' ': keySpace = 0; break;
        case 'c': case 'C': keyC = 0; break;
    }
}

void breakBlock(int bx, int by, int bz)
{
    int cx = (int)floorf((bx + 8) / 16.0f);
    int cy = blockToChunkY(by);
    int cz = (int)floorf((bz + 8) / 16.0f);
    ChunkData *c = findChunk(cx, cy, cz);
    if (!c) return;
    int i = bx - cx * 16 + 8;
    int j = by + 15 + cy * 16;
    int k = bz - cz * 16 + 8;
    if (i < 0 || i >= GRID_SIZE || j < 0 || j >= GRID_SIZE || k < 0 || k >= GRID_SIZE)
        return;
    if (c->blocks[i][j][k] == BLOCK_AIR) return;
    c->blocks[i][j][k] = BLOCK_AIR;
    invalidateChunkGeometry(c);
    invalidateNeighbors(cx, cy, cz);
}

void placeBlock(int bx, int by, int bz)
{
    int cx = (int)floorf((bx + 8) / 16.0f);
    int cy = blockToChunkY(by);
    int cz = (int)floorf((bz + 8) / 16.0f);
    ChunkData *c = getOrCreateChunk(cx, cy, cz);
    if (!c) return;
    int i = bx - cx * 16 + 8;
    int j = by + 15 + cy * 16;
    int k = bz - cz * 16 + 8;
    if (i < 0 || i >= GRID_SIZE || j < 0 || j >= GRID_SIZE || k < 0 || k >= GRID_SIZE)
        return;
    // Don't place inside the player
    float pxMin = camX - 0.3f, pxMax = camX + 0.3f;
    float pyMin = feetY, pyMax = feetY + playerHeight;
    float pzMin = camZ - 0.2f, pzMax = camZ + 0.2f;
    if (bx < pxMax && bx + 1 > pxMin &&
        by < pyMax && by + 1 > pyMin &&
        bz < pzMax && bz + 1 > pzMin)
        return;
    if (c->blocks[i][j][k] != BLOCK_AIR) return;
    c->blocks[i][j][k] = BLOCK_STONE;
    invalidateChunkGeometry(c);
    invalidateNeighbors(cx, cy, cz);
}

void mouseButton(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        int hitBX, hitBY, hitBZ;
        if (rayCastBlock(camX, camY, camZ, camFX, camFY, camFZ, 5.0f, &hitBX, &hitBY, &hitBZ, NULL, NULL, NULL))
            breakBlock(hitBX, hitBY, hitBZ);
        glutPostRedisplay();
    }
    if (button == GLUT_RIGHT_BUTTON && state == GLUT_DOWN) {
        int hitBX, hitBY, hitBZ, fnx, fny, fnz;
        if (rayCastBlock(camX, camY, camZ, camFX, camFY, camFZ, 5.0f, &hitBX, &hitBY, &hitBZ, &fnx, &fny, &fnz))
            placeBlock(hitBX + fnx, hitBY + fny, hitBZ + fnz);
        glutPostRedisplay();
    }
}

int main(int argc, char **argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    putenv("__GL_SYNC_TO_VBLANK=0");
    glutInitWindowSize(winW, winH);
    glutCreateWindow("16x16x16 Cube Grid");

    // Disable VSync (try all three extensions)
    {
        typedef void (*SwapIntSGI)(int);
        SwapIntSGI sgi = (SwapIntSGI)
            glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalSGI");
        if (sgi) sgi(0);
    }
    {
        typedef void (*SwapIntEXT)(Display*, unsigned long, int);
        SwapIntEXT ext = (SwapIntEXT)
            glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalEXT");
        if (ext)
            ext(glXGetCurrentDisplay(), glXGetCurrentDrawable(), 0);
    }
    {
        typedef int (*SwapIntMESA)(int);
        SwapIntMESA mesa = (SwapIntMESA)
            glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalMESA");
        if (mesa) mesa(0);
    }

    glEnable(GL_DEPTH_TEST);
    glutSetCursor(GLUT_CURSOR_NONE);
    glutIgnoreKeyRepeat(1);
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutPassiveMotionFunc(mouseMove);
    glutMouseFunc(mouseButton);
    glutKeyboardFunc(keyDown);
    glutKeyboardUpFunc(keyUp);
    glutSpecialFunc(specialKeys);
    glutIdleFunc(idle);
    initRenderer();
    initShaders();

    numPoolThreads = sysconf(_SC_NPROCESSORS_ONLN);
    if (numPoolThreads > MAX_THREADS) numPoolThreads = MAX_THREADS;
    if (numPoolThreads < 1) numPoolThreads = 1;

    for (int i = 0; i < numPoolThreads; i++) {
        fillBufs[i] = malloc(MAX_FILL_VERTS * 9 * sizeof(float));
        workers[i].fillBuf = fillBufs[i];
        workers[i].running = 1;
        workers[i].assigned = 0;
        pthread_mutex_init(&workers[i].mutex, NULL);
        pthread_cond_init(&workers[i].cond, NULL);
        pthread_create(&workers[i].thread, NULL, poolWorker, &workers[i]);
    }

    noiseSeed(time(NULL));

    float spawnSurfY = getSurfaceHeight((int)floorf(camX), (int)floorf(camZ));
    feetY = spawnSurfY + 1.0f;
    camY = feetY + (playerHeight - 0.2f);

    prevPhysTime = glutGet(GLUT_ELAPSED_TIME);
    glutMainLoop();
    return 0;
}
