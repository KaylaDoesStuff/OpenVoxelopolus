#ifndef NOISE_H
#define NOISE_H

void noiseSeed(unsigned int seed);
float noise2D(float x, float z);
float fbm(float x, float z, int octaves, float lacunarity, float gain);

#endif
