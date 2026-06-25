#ifndef BLOCKS_H
#define BLOCKS_H

typedef enum {
    BLOCK_AIR   = 0,
    BLOCK_GRASS = 1,
    BLOCK_DIRT  = 2,
    BLOCK_STONE = 3,
} BlockType;

#define LIGHT_DIR_X  0.469846f
#define LIGHT_DIR_Y  0.866025f
#define LIGHT_DIR_Z -0.171010f
#define SHADOW_FACTOR 0.3f
#define SHADOW_RAY_DIST 60.0f

#endif
