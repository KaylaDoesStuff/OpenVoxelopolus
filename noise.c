#include "noise.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

static int perm[512];
static int permInited = 0;

void noiseSeed(unsigned int seed)
{
    int p[256];
    for (int i = 0; i < 256; i++)
        p[i] = i;

    unsigned int state = seed;
    for (int i = 255; i > 0; i--) {
        state = state * 1103515245u + 12345u;
        int j = (state >> 16) % (i + 1);
        int t = p[i]; p[i] = p[j]; p[j] = t;
    }

    for (int i = 0; i < 256; i++)
        perm[i] = perm[i + 256] = p[i];
    permInited = 1;
}

static float fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float lerp(float a, float b, float t)
{
    return a + t * (b - a);
}

static float grad2D(int hash, float x, float z)
{
    int h = hash & 3;
    float u = h < 2 ? x : z;
    float v = h < 2 ? z : x;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float noise2D(float x, float z)
{
    if (!permInited)
        noiseSeed((unsigned int)time(NULL));

    int X = (int)floorf(x) & 255;
    int Z = (int)floorf(z) & 255;

    x -= floorf(x);
    z -= floorf(z);

    float u = fade(x);
    float v = fade(z);

    int aa = perm[perm[X] + Z];
    int ab = perm[perm[X] + Z + 1];
    int ba = perm[perm[X + 1] + Z];
    int bb = perm[perm[X + 1] + Z + 1];

    return lerp(
        lerp(grad2D(aa, x, z),     grad2D(ba, x - 1, z),     u),
        lerp(grad2D(ab, x, z - 1), grad2D(bb, x - 1, z - 1), u),
        v
    );
}

float fbm(float x, float z, int octaves, float lacunarity, float gain)
{
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxVal = 0.0f;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise2D(x * frequency, z * frequency);
        maxVal += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return value / maxVal;
}
