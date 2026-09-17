#ifndef VOXEL_INTERNAL_H
#define VOXEL_INTERNAL_H
#include "rend_internal.h"
enum { BASE_CELLS=CHUNK_SIZE*CHUNK_SIZE*WORLD_HEIGHT, BLOCK_PARTIAL=255,
       CACHE_RADIUS=WORLD_RADIUS+1, CACHE_SIDE=CACHE_RADIUS*2+1, CACHE_COUNT=CACHE_SIDE*CACHE_SIDE };
/* Origin and extents are in half-block units; textures remain in full-block units. */
typedef struct { unsigned char x,y,z,u,v,face,block,light[4]; } VoxelQuad;
typedef struct {
    int cx,cz,valid,ready,dirty,min_y,max_y;
    unsigned char blocks[BASE_CELLS];
    VoxelQuad *quads;
    int count,capacity,water_count; /* water quads render in a second pass */
    unsigned char *light; /* half-cell skylight + one-cell halo, only up to local roofs */
    size_t light_capacity;
    int light_y,light_height,light_valid;
} VoxelChunk;
typedef struct { int x,y,z,next; unsigned char cells[8],flow[8]; } VoxelEdit;
enum { VOXEL_LIGHT_MAX=15,VOXEL_LIGHT_OPAQUE=128,VOXEL_WATER_REACH=7,VOXEL_WATER_FALLING=8 };
void voxel_light_bake(VoxelChunk *chunk);
int voxel_light_cell(const VoxelChunk *chunk,int x,int y,int z);
void voxel_light_face(const VoxelChunk *chunk,const int origin[3],int u,int v,int face,unsigned char light[4]);
const unsigned char *voxel_world_chunk_blocks(int cx,int cz);
int voxel_world_active(int sx,int sz);
int voxel_world_water_set(int sx,int sy,int sz,int level); /* -1 = drained, 0 = source */
int voxel_water_level(int sx,int sy,int sz);
void voxel_water_reset(void);
void voxel_water_wake(int sx,int sy,int sz);
void voxel_water_activate(int cx,int cz);
int voxel_water_pending(void);
int voxel_water_last_work(void);
int voxel_edit_set_flow(int sx,int sy,int sz,int block,int flow,int original);
const VoxelEdit *voxel_edit_at(int index);
int voxel_floor_div(int value,int divisor);
void voxel_edits_reset(uint32_t seed);
const VoxelEdit *voxel_edit_find(int x,int y,int z);
const VoxelEdit *voxel_edit_first(int cx,int cz);
const VoxelEdit *voxel_edit_next(const VoxelEdit *edit);
int voxel_edit_set(int sx,int sy,int sz,int block,int original);
int voxel_edits_count(void);
int voxel_edits_save(void);
int voxel_edits_load(void);
int voxel_edits_dirty(void);
int voxel_world_uniform(int x,int y,int z);
void voxel_chunk_mesh(VoxelChunk *chunk);
int voxel_face_exposed(int block,int neighbor);
#endif
