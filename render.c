#define GL_GLEXT_PROTOTYPES
#include <GL/glut.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "chunk.h"
#include "shader.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static GLuint fillVBO;
static int frames;
static float lastFpsTime;
static clock_t lastCpuClock;
static float fps, cpuPercent, gpuMs;
static int hasGpuQuery;
static GLuint gpuQuery;

static void checkGL(const char *where)
{
    GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR)
        fprintf(stderr, "GL error 0x%x at %s\n", e, where);
}

void getFrustumPlanes(float m[16], float planes[6][4])
{
    planes[0][0] = m[3] + m[0];
    planes[0][1] = m[7] + m[4];
    planes[0][2] = m[11] + m[8];
    planes[0][3] = m[15] + m[12];

    planes[1][0] = m[3] - m[0];
    planes[1][1] = m[7] - m[4];
    planes[1][2] = m[11] - m[8];
    planes[1][3] = m[15] - m[12];

    planes[2][0] = m[3] + m[1];
    planes[2][1] = m[7] + m[5];
    planes[2][2] = m[11] + m[9];
    planes[2][3] = m[15] + m[13];

    planes[3][0] = m[3] - m[1];
    planes[3][1] = m[7] - m[5];
    planes[3][2] = m[11] - m[9];
    planes[3][3] = m[15] - m[13];

    planes[4][0] = m[3] + m[2];
    planes[4][1] = m[7] + m[6];
    planes[4][2] = m[11] + m[10];
    planes[4][3] = m[15] + m[14];

    planes[5][0] = m[3] - m[2];
    planes[5][1] = m[7] - m[6];
    planes[5][2] = m[11] - m[10];
    planes[5][3] = m[15] - m[14];

    for (int i = 0; i < 6; i++) {
        float len = sqrtf(planes[i][0] * planes[i][0] +
                          planes[i][1] * planes[i][1] +
                          planes[i][2] * planes[i][2]);
        if (len > 0.001f) {
            planes[i][0] /= len;
            planes[i][1] /= len;
            planes[i][2] /= len;
            planes[i][3] /= len;
        }
    }
}

int cubeInFrustum(float planes[6][4], float min[3], float max[3])
{
    for (int i = 0; i < 6; i++) {
        float *p = planes[i];
        float px = p[0] >= 0 ? max[0] : min[0];
        float py = p[1] >= 0 ? max[1] : min[1];
        float pz = p[2] >= 0 ? max[2] : min[2];
        if (p[0] * px + p[1] * py + p[2] * pz + p[3] < 0)
            return 0;
    }
    return 1;
}

static void drawHudText(int x, int y, const char *s)
{
    glRasterPos2i(x, y);
    for (const char *c = s; *c; c++)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
}

void initRenderer(void)
{
    hasGpuQuery = glutExtensionSupported("GL_ARB_timer_query") ||
                  glutExtensionSupported("GL_EXT_timer_query");
    if (hasGpuQuery) glGenQueries(1, &gpuQuery);

    glClearColor(0.68f, 0.85f, 0.9f, 1.0f);
    glGenBuffers(1, &fillVBO);
}

void renderScene(ChunkData **visible, int numChunks, int numGen,
                 float camX, float camY, float camZ,
                 float fx, float fy, float fz,
                 int winW, int winH, int renderDist, float feetY)
{
    static int gpuQueryActive = 0;
    if (hasGpuQuery && !gpuQueryActive) {
        glBeginQuery(GL_TIME_ELAPSED, gpuQuery);
        gpuQueryActive = 1;
    }

    // Gather all visible chunks' cached verts into batch buffer
    int totalVerts = 0;
    for (int i = 0; i < numChunks; i++)
        totalVerts += visible[i]->cachedCount;

    static float *batchVerts = NULL;
    static int batchCap = 0;
    int totalFloats = totalVerts * 9;
    if (totalFloats > batchCap) {
        free(batchVerts);
        batchCap = totalFloats * 2;
        batchVerts = malloc(batchCap * sizeof(float));
    }

    int off = 0;
    for (int i = 0; i < numChunks; i++) {
        int cnt = visible[i]->cachedCount;
        if (cnt > 0) {
            memcpy(batchVerts + off, visible[i]->cachedVerts, cnt * 9 * sizeof(float));
            off += cnt * 9;
        }
    }

    if (totalVerts > 0 && winW > 0 && winH > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, fillVBO);
        glBufferData(GL_ARRAY_BUFFER, totalFloats * sizeof(float), batchVerts, GL_STREAM_DRAW);

        // === Main pass ===
        setLightingUniforms();
        checkGL("main uniforms");

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glDrawArrays(GL_TRIANGLES, 0, totalVerts);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);
        glUseProgram(0);
    }

    if (hasGpuQuery && gpuQueryActive) {
        glEndQuery(GL_TIME_ELAPSED);
        gpuQueryActive = 0;
    }

    // Sun in sky
    {
        float theta = 30.0f * M_PI / 180.0f;
        float phi = 20.0f * M_PI / 180.0f;
        float lx = sinf(theta) * cosf(phi);
        float ly = cosf(theta);
        float lz = -sinf(theta) * sinf(phi);

        float sunDist = 200.0f;
        float sx = camX + lx * sunDist;
        float sy = camY + ly * sunDist;
        float sz = camZ + lz * sunDist;

        float dx = camX - sx, dy = camY - sy, dz = camZ - sz;
        float len = sqrtf(dx*dx + dy*dy + dz*dz);
        dx /= len; dy /= len; dz /= len;

        float upx = 0, upy = 1, upz = 0;
        float rx = dy*upz - dz*upy;
        float ry = dz*upx - dx*upz;
        float rz = dx*upy - dy*upx;
        float rlen = sqrtf(rx*rx + ry*ry + rz*rz);
        if (rlen < 0.001f) { upx = 0; upy = 0; upz = 1;
            rx = dy*upz - dz*upy; ry = dz*upx - dx*upz; rz = dx*upy - dy*upx;
            rlen = sqrtf(rx*rx + ry*ry + rz*rz); }
        rx /= rlen; ry /= rlen; rz /= rlen;

        float ux = ry*dz - rz*dy;
        float uy = rz*dx - rx*dz;
        float uz = rx*dy - ry*dx;

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glTranslatef(sx, sy, sz);
        float m[16] = {
            rx, ry, rz, 0,
            ux, uy, uz, 0,
            dx, dy, dz, 0,
            0,  0,  0,  1
        };
        glMultMatrixf(m);

        int seg = 32;
        glColor3f(1.0f, 0.9f, 0.2f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(0, 0, 0);
        for (int i = 0; i <= seg; i++) {
            float a = 2.0f * M_PI * i / seg;
            glVertex3f(cosf(a) * 12.0f, sinf(a) * 12.0f, 0);
        }
        glEnd();

        glColor3f(1.0f, 1.0f, 0.5f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(0, 0, 0);
        for (int i = 0; i <= seg; i++) {
            float a = 2.0f * M_PI * i / seg;
            glVertex3f(cosf(a) * 5.0f, sinf(a) * 5.0f, 0);
        }
        glEnd();

        glPopMatrix();
    }

    // Wireframe outline on targeted block
    int hitBX = 0, hitBY = 0, hitBZ = 0;
    if (rayCastBlock(camX, camY, camZ, fx, fy, fz, 5.0f, &hitBX, &hitBY, &hitBZ, NULL, NULL, NULL)) {
        glDepthFunc(GL_LEQUAL);
        glLineWidth(3.0f);
        glColor3f(0, 0, 0);
        glBegin(GL_LINES);
        float x0 = hitBX, y0 = hitBY, z0 = hitBZ, x1 = hitBX + 1, y1 = hitBY + 1, z1 = hitBZ + 1;
        glVertex3f(x0, y0, z0); glVertex3f(x1, y0, z0);
        glVertex3f(x1, y0, z0); glVertex3f(x1, y0, z1);
        glVertex3f(x1, y0, z1); glVertex3f(x0, y0, z1);
        glVertex3f(x0, y0, z1); glVertex3f(x0, y0, z0);
        glVertex3f(x0, y1, z0); glVertex3f(x1, y1, z0);
        glVertex3f(x1, y1, z0); glVertex3f(x1, y1, z1);
        glVertex3f(x1, y1, z1); glVertex3f(x0, y1, z1);
        glVertex3f(x0, y1, z1); glVertex3f(x0, y1, z0);
        glVertex3f(x0, y0, z0); glVertex3f(x0, y1, z0);
        glVertex3f(x1, y0, z0); glVertex3f(x1, y1, z0);
        glVertex3f(x1, y0, z1); glVertex3f(x1, y1, z1);
        glVertex3f(x0, y0, z1); glVertex3f(x0, y1, z1);
        glEnd();
        glLineWidth(1.0f);
        glDepthFunc(GL_LESS);
    }

    // HUD overlay
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, winW, 0, winH, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);

    // crosshair
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_XOR);
    glColor3f(1.0f, 1.0f, 1.0f);

    int seg = 32;
    float r = 6.0f, cx_h = winW * 0.5f, cy_h = winH * 0.5f;
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < seg; i++) {
        float a = 2.0f * M_PI * i / seg;
        glVertex2f(cx_h + r * cosf(a), cy_h + r * sinf(a));
    }
    glEnd();

    glDisable(GL_COLOR_LOGIC_OP);

    // HUD text
    glColor3f(1.0f, 1.0f, 1.0f);

    char buf[64];
    snprintf(buf, sizeof(buf), "FPS: %.1f", fps);
    drawHudText(10, winH - 18, buf);

    snprintf(buf, sizeof(buf), "CPU: %.0f%%", cpuPercent);
    drawHudText(10, winH - 34, buf);

    if (hasGpuQuery)
        snprintf(buf, sizeof(buf), "GPU: %.1fms", gpuMs);
    else
        snprintf(buf, sizeof(buf), "GPU: N/A");
    drawHudText(10, winH - 50, buf);

    int effRD = renderDist * 4;
    snprintf(buf, sizeof(buf), "Chunks: %d (%d gen)  RD: %d (%d blk)", numChunks, numGen, renderDist, effRD);
    drawHudText(10, winH - 66, buf);

    snprintf(buf, sizeof(buf), "Pos: %.1f %.1f %.1f", camX, feetY, camZ);
    drawHudText(10, winH - 82, buf);

    glEnable(GL_DEPTH_TEST);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void updateFpsStats(void)
{
    frames++;
    float now = glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
    if (now - lastFpsTime >= 1.0f) {
        float dt = now - lastFpsTime;
        fps = frames / dt;
        frames = 0;
        lastFpsTime = now;

        clock_t c = clock();
        cpuPercent = ((float)(c - lastCpuClock) / CLOCKS_PER_SEC) / dt * 100.0f;
        lastCpuClock = c;

        if (hasGpuQuery) {
            GLint available;
            glGetQueryObjectiv(gpuQuery, GL_QUERY_RESULT_AVAILABLE, &available);
            if (available) {
                GLint gpuNs;
                glGetQueryObjectiv(gpuQuery, GL_QUERY_RESULT, &gpuNs);
                gpuMs = gpuNs / 1000000.0f;
            }
        }
    }
}
