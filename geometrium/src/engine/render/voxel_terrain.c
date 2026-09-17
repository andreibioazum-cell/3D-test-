/* Deterministic generation: gentle heights, caves, soil layers, water, trees.
 * One seed + world coordinates gives identical blocks across chunk borders.
 *
 * The height field is deliberately low-frequency: neighbour columns differ by
 * at most one block almost everywhere, so the ground climbs in short single
 * steps instead of whole-block walls. Most stone stays sealed under a
 * four-block crust, but scarred columns are carved by surface holes and cave
 * mouths, so the landscape is a hollowed, pockmarked rock body instead of a
 * solid slab. */
#include "rend_internal.h"
#include <math.h>
#include <string.h>

static uint32_t world_seed = REND_WORLD_SEED;
static uint32_t hash(int x, int z, uint32_t salt) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)z * 0xd8163841u ^ world_seed ^ salt;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu;
    return h ^ (h >> 16);
}
static uint32_t hash3(int x, int y, int z, uint32_t salt) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0x9e3779b1u ^
                 (uint32_t)z * 0xd8163841u ^ world_seed ^ salt;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu;
    return h ^ (h >> 16);
}
static int floor_div(int x, int size) { return x / size - (x < 0 && x % size != 0); }
static float smooth(float x) { return x * x * (3 - 2 * x); }
static float noise(float x, float z, uint32_t salt) {
    int ix = (int)floorf(x), iz = (int)floorf(z);
    float u = smooth(x - ix), v = smooth(z - iz);
    float a = (hash(ix, iz, salt) & 65535) / 32767.5f - 1;
    float b = (hash(ix + 1, iz, salt) & 65535) / 32767.5f - 1;
    float c = (hash(ix, iz + 1, salt) & 65535) / 32767.5f - 1;
    float d = (hash(ix + 1, iz + 1, salt) & 65535) / 32767.5f - 1;
    return (a + (b - a) * u) * (1 - v) + (c + (d - c) * u) * v;
}
static float noise3(float x, float y, float z, uint32_t salt) {
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float u = smooth(x - ix), v = smooth(y - iy), w = smooth(z - iz);
    float c000 = (hash3(ix, iy, iz, salt) & 65535) / 32767.5f - 1;
    float c100 = (hash3(ix + 1, iy, iz, salt) & 65535) / 32767.5f - 1;
    float c010 = (hash3(ix, iy + 1, iz, salt) & 65535) / 32767.5f - 1;
    float c110 = (hash3(ix + 1, iy + 1, iz, salt) & 65535) / 32767.5f - 1;
    float c001 = (hash3(ix, iy, iz + 1, salt) & 65535) / 32767.5f - 1;
    float c101 = (hash3(ix + 1, iy, iz + 1, salt) & 65535) / 32767.5f - 1;
    float c011 = (hash3(ix, iy + 1, iz + 1, salt) & 65535) / 32767.5f - 1;
    float c111 = (hash3(ix + 1, iy + 1, iz + 1, salt) & 65535) / 32767.5f - 1;
    float x00 = c000 + (c100 - c000) * u, x10 = c010 + (c110 - c010) * u;
    float x01 = c001 + (c101 - c001) * u, x11 = c011 + (c111 - c011) * u;
    float y0 = x00 + (x10 - x00) * v, y1 = x01 + (x11 - x01) * v;
    return y0 + (y1 - y0) * w;
}
void voxel_terrain_seed(uint32_t seed) { world_seed = seed; }

int voxel_terrain_height(int x, int z) {
    /* Two soft octaves: the slope between neighbours stays well below one
     * block, so hills rise gradually instead of jumping a whole block. */
    float h = 10 + 7 * noise(x * .025f, z * .025f, 11) +
              3 * noise(x * .055f, z * .055f, 83);
    /* Small safe clearing at the spawn point; beyond it, pure generator. */
    float dx = x - 8.0f, dz = z - 8.0f, r = sqrtf(dx * dx + dz * dz);
    if (r < 10) { float t = smooth(fmaxf(0, (r - 4) / 6)); h = 12 + (h - 12) * t; }
    int result = (int)floorf(h);
    return result < 3 ? 3 : result > 25 ? 25 : result;
}

/* Surface scar of a column: -1 near the spawn (the landing stays clean),
 * otherwise a 2D noise value. Columns with a high scar are pockmarked —
 * open holes in the ground and caves that break the crust, so the terrain
 * spawns with holes instead of being solid everywhere. */
static float scar(int x, int z) {
    int dx = x - 8, dz = z - 8;
    if (dx * dx + dz * dz < 30) return -1.0f;
    return .60f * noise(x * .055f, z * .055f, 913) +
           .40f * noise(x * .11f, z * .11f, 947);
}

/* Depth in blocks of the open hole at the top of a scarred column; 0 = intact. */
static int surface_hole(int x, int z, int height) {
    if (height <= WATER_LEVEL + 1) return 0;
    float s = scar(x, z);
    if (s < .52f) return 0;
    int depth = 1 + (int)((s - .52f) * 8.0f);
    return depth > 3 ? 3 : depth;
}

/* Generated caves: pockets and tunnels inside the stone. The four top blocks
 * of an unscarred dry column stay solid (a sealed crust under grass); a
 * scarred column keeps only a one-block crust, so cave air can break through
 * to the surface. The two bottom layers are bedrock, and lake beds
 * (height <= WATER_LEVEL + 1) are never carved so natural water can never
 * drain into the underground. */
static int cave(int x, int y, int z, int height) {
    if (height <= WATER_LEVEL + 1) return 0;
    float s = scar(x, z);
    if (y < 2 || y > height - (s > .40f ? 1 : 4)) return 0;
    float n = .72f * noise3(x * .08f, y * .11f, z * .08f, 313) +
              .28f * noise3(x * .16f, y * .22f, z * .16f, 547);
    return n > .5f;
}

typedef struct { int x, z, ground, height, valid; } Tree;
static Tree tree_at(int gx, int gz) {
    uint32_t h = hash(gx, gz, 701);
    Tree t = {gx * 8 + 2 + (int)(h % 4), gz * 8 + 2 + (int)((h >> 8) % 4), 0, 4 + (int)((h >> 16) & 1), 0};
    if (h % 100 > 62) return t;
    float dx = (float)t.x - 8, dz = (float)t.z - 8;
    if (dx * dx + dz * dz < 64) return t;
    t.ground = voxel_terrain_height(t.x, t.z);
    /* No trees on scarred ground: the top block there may be carved away,
     * which would leave the trunk floating. */
    t.valid = t.ground > WATER_LEVEL + 1 && noise(gx * .12f, gz * .12f, 97) > -.45f &&
              scar(t.x, t.z) <= .40f;
    return t;
}
static int tree_block(const Tree *t, int x, int y, int z) {
    if (!t->valid) return BLOCK_AIR;
    int dx = abs(x - t->x), dz = abs(z - t->z), top = t->ground + t->height;
    if (dx == 0 && dz == 0 && y > t->ground && y <= top) return BLOCK_LOG;
    if (y < top - 1 || y > top + 2) return BLOCK_AIR;
    int radius = y == top + 2 ? 1 : 2;
    if (dx > radius || dz > radius || (dx == 2 && dz == 2)) return BLOCK_AIR;
    if (y == top + 2 && dx == 1 && dz == 1) return BLOCK_AIR;
    return BLOCK_LEAVES;
}
static int ground_block(int x, int y, int z, int height) {
    if (y < 0) return BLOCK_STONE;
    if (y > height) return y <= WATER_LEVEL ? BLOCK_WATER : BLOCK_AIR;
    if (y > height - surface_hole(x, z, height)) return BLOCK_AIR;   /* open hole */
    if (cave(x, y, z, height)) return BLOCK_AIR;
    if (height <= WATER_LEVEL + 1 && y >= height - 2) return BLOCK_SAND;
    if (y == height) return height > 20 ? BLOCK_STONE : BLOCK_GRASS;
    (void)x; (void)z;
    return y >= height - 3 ? BLOCK_DIRT : BLOCK_STONE;
}
int voxel_terrain_block(int x, int y, int z) {
    if (y > 32 || y >= WORLD_HEIGHT) return BLOCK_AIR; /* max generated canopy; edits can build higher */
    if (y < 0) return BLOCK_STONE;
    int b = ground_block(x, y, z, voxel_terrain_height(x, z));
    if (b != BLOCK_AIR) return b;
    int gx = floor_div(x, 8), gz = floor_div(z, 8), result = BLOCK_AIR;
    for (int iz = gz - 1; iz <= gz + 1; iz++) {
        for (int ix = gx - 1; ix <= gx + 1; ix++) {
            Tree t = tree_at(ix, iz);
            int tb = tree_block(&t, x, y, z);
            if (tb == BLOCK_LOG) return tb;
            if (tb == BLOCK_LEAVES) result = tb;
        }
    }
    return result;
}
static void put(unsigned char *out, int x, int y, int z, int block) {
    if (x < 0 || x >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE || y < 0 || y >= WORLD_HEIGHT) return;
    int index = (y * CHUNK_SIZE + z) * CHUNK_SIZE + x;
    if (block == BLOCK_LOG || out[index] == BLOCK_AIR) out[index] = (unsigned char)block;
}
void voxel_terrain_chunk(int cx, int cz, unsigned char *out) {
    int ox = cx * CHUNK_SIZE, oz = cz * CHUNK_SIZE;
    memset(out, 0, CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT);
    for (int z = 0; z < CHUNK_SIZE; z++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {
            int height = voxel_terrain_height(ox + x, oz + z);
            int top = height > WATER_LEVEL ? height : WATER_LEVEL;
            for (int y = 0; y <= top; y++)
                out[(y * CHUNK_SIZE + z) * CHUNK_SIZE + x] = (unsigned char)ground_block(ox + x, y, oz + z, height);
        }
    }
    for (int gz = floor_div(oz - 2, 8); gz <= floor_div(oz + CHUNK_SIZE + 1, 8); gz++) {
        for (int gx = floor_div(ox - 2, 8); gx <= floor_div(ox + CHUNK_SIZE + 1, 8); gx++) {
            Tree t = tree_at(gx, gz);
            if (!t.valid) continue;
            for (int y = t.ground + 1; y <= t.ground + t.height + 2; y++) {
                for (int z = t.z - 2; z <= t.z + 2; z++) {
                    for (int x = t.x - 2; x <= t.x + 2; x++) {
                        int b = tree_block(&t, x, y, z);
                        if (b) put(out, x - ox, y, z - oz, b);
                    }
                }
            }
        }
    }
}
