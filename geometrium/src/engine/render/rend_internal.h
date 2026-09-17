/* Engine render backend: software 3D rasterizer, materials, voxel world.
 * This is the low level the engine (src/engine/eng_*.c) draws through; it has
 * no player, no HUD and no game rules. Cell coordinates are half-block
 * integers. */
#ifndef REND_INTERNAL_H
#define REND_INTERNAL_H
#include "engine.h"
#define REND_FOG_START 32.0f
#define REND_FOG_END 64.0f
#define REND_FAR_Z 80.0f
#define REND_WORLD_SEED 20260905u

enum { BLOCK_AIR,BLOCK_GRASS,BLOCK_DIRT,BLOCK_STONE,BLOCK_SAND,BLOCK_WATER,BLOCK_LOG,BLOCK_LEAVES,BLOCK_COUNT };
/* WORLD_RADIUS is the visible chunk radius; one ring of chunks is prefetched
 * beyond it (CACHE_RADIUS) so the fog never exposes an unmeshed horizon. */
enum { CHUNK_SIZE=16,WORLD_HEIGHT=64,WATER_LEVEL=8,WORLD_RADIUS=4 };

/* ── 3D rasterizer (rend3d.c) ── */
int rend3d_begin(Buffer *,int,float,float,float,float,float,float);
void rend3d_sky(uint32_t top,uint32_t bottom);
void rend3d_fog(float start,float end);
void rend3d_surface(int sx,int sy,int sz,int u,int v,int face,int block,const unsigned char light[4]);
int rend3d_visible(float x,float y,float z,float hx,float hy,float hz);
int rend3d_face_visible(int face,float plane);
void rend3d_end(void);

/* ── materials (rend_material.c) ── */
int rend_materials_load(AAssetManager *assets);
const Image *rend_material_icon(int block); /* hotbar preview of a block */

/* ── procedural terrain (voxel_terrain.c) ── */
void voxel_terrain_seed(uint32_t seed);
int voxel_terrain_height(int x,int z);
int voxel_terrain_block(int x,int y,int z);
void voxel_terrain_chunk(int cx,int cz,unsigned char *blocks);

/* ── streamed voxel world (voxel_world.c) ── */
void voxel_world_build(uint32_t seed);
void voxel_world_update(float x,float z);
void voxel_world_draw(void);
int voxel_world_cell(int sx,int sy,int sz);
int voxel_cell_solid(int sx,int sy,int sz);
int voxel_world_set(int sx,int sy,int sz,int block);
void voxel_world_stats(int *chunks,int *quads);
int voxel_world_pending(void);
float voxel_world_distance(void);
float voxel_world_light(float x,float y,float z);

/* ── water simulation (voxel_water.c) ── */
void voxel_water_update(float d);

/* ── frame cost / render scale (rend_perf.c) ── */
enum { REND_QUALITY_AUTO=0,REND_QUALITY_HIGH=1,REND_QUALITY_MEDIUM=2,REND_QUALITY_LOW=3 };
void rend_perf_reset(void);
void rend_perf_frame(double interval);
float rend_fps(void);
int rend_render_scale(int width,int height);
void rend_render_time(double elapsed);
void rend_set_quality(int quality);   /* REND_QUALITY_*; AUTO adapts to ~60 fps */
#endif
