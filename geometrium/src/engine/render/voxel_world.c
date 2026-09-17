/* Bounded, nearest-first chunk streaming. Never rebuild an entire row on movement. */
#include "voxel_internal.h"
#include <limits.h>

static VoxelChunk chunks[CACHE_COUNT];
static int center_x,center_z;
static float draw_distance=32;
enum { HALF_SIDE=CHUNK_SIZE*2 };
static int wrap(int x) { int n=x%CACHE_SIDE;return n<0 ? n+CACHE_SIDE : n; }
static VoxelChunk *slot(int cx,int cz) { return &chunks[wrap(cz)*CACHE_SIDE+wrap(cx)]; }
static int matches(const VoxelChunk *c,int cx,int cz) { return c->valid && c->cx==cx && c->cz==cz; }
static int base_block(int x,int y,int z) {
    if (y<0) return BLOCK_STONE;
    if (y>=WORLD_HEIGHT) return BLOCK_AIR;
    int cx=voxel_floor_div(x,CHUNK_SIZE),cz=voxel_floor_div(z,CHUNK_SIZE);
    VoxelChunk *c=slot(cx,cz);
    if (!matches(c,cx,cz)) return voxel_terrain_block(x,y,z);
    return c->blocks[(y*CHUNK_SIZE+z-cz*CHUNK_SIZE)*CHUNK_SIZE+x-cx*CHUNK_SIZE];
}
int voxel_world_uniform(int x,int y,int z) {
    const VoxelEdit *e=voxel_edit_find(x,y,z);
    if (!e) return base_block(x,y,z);
    for (int i=1;i<8;i++) if (e->cells[i]!=e->cells[0]) return BLOCK_PARTIAL;
    return e->cells[0];
}
int voxel_world_cell(int sx,int sy,int sz) {
    int x=voxel_floor_div(sx,2),y=voxel_floor_div(sy,2),z=voxel_floor_div(sz,2);
    const VoxelEdit *e=voxel_edit_find(x,y,z);
    return e ? e->cells[sx-x*2+(sy-y*2)*2+(sz-z*2)*4] : base_block(x,y,z);
}
const unsigned char *voxel_world_chunk_blocks(int cx,int cz) {
    VoxelChunk *c=slot(cx,cz);
    return matches(c,cx,cz) ? c->blocks : NULL;
}
int voxel_world_active(int sx,int sz) {
    int cx=voxel_floor_div(sx,HALF_SIDE),cz=voxel_floor_div(sz,HALF_SIDE);
    return abs(cx-center_x)<=WORLD_RADIUS && abs(cz-center_z)<=WORLD_RADIUS && matches(slot(cx,cz),cx,cz);
}
float voxel_world_light(float x,float y,float z) {
    if (!isfinite(x+y+z) || fabsf(x)>10000000 || fabsf(z)>10000000 || fabsf(y)>10000000) return 1;
    int cx=(int)floorf(x/CHUNK_SIZE),cz=(int)floorf(z/CHUNK_SIZE);
    VoxelChunk *c=slot(cx,cz);
    if (!matches(c,cx,cz) || !c->light_height) return y>voxel_terrain_height((int)floorf(x),(int)floorf(z))+1 ? 1 : 0;
    float p[3]={(x-cx*CHUNK_SIZE)*2-.5f,y*2-.5f,(z-cz*CHUNK_SIZE)*2-.5f};
    int b[3];float f[3];
    for (int a=0;a<3;a++) {b[a]=(int)floorf(p[a]);f[a]=p[a]-b[a];}
    float total=0,weight=0;
    for (int i=0;i<8;i++) {
        int dx=i&1,dy=(i>>1)&1,dz=i>>2;
        int l=voxel_light_cell(c,b[0]+dx,b[1]+dy,b[2]+dz);
        if (l==VOXEL_LIGHT_OPAQUE) continue;
        float w=(dx?f[0]:1-f[0])*(dy?f[1]:1-f[1])*(dz?f[2]:1-f[2]);
        total+=w*l;weight+=w;
    }
    return weight>1e-6f ? total/(weight*VOXEL_LIGHT_MAX) : 0;
}
int voxel_cell_solid(int sx,int sy,int sz) {
    int b=voxel_world_cell(sx,sy,sz);return b!=BLOCK_AIR && b!=BLOCK_WATER;
}
static void invalidate(int cx,int cz) {
    VoxelChunk *c=slot(cx,cz);
    if (matches(c,cx,cz)) c->dirty=1;
}
static int light_opacity(int block) {
    return block==BLOCK_AIR || block==BLOCK_WATER ? 0 : block==BLOCK_LEAVES ? 1 : 2;
}
static void changed(int sx,int sz,int old,int block) {
    int cx=voxel_floor_div(sx,HALF_SIDE),cz=voxel_floor_div(sz,HALF_SIDE);
    invalidate(cx,cz);
    int lx=sx-cx*HALF_SIDE,lz=sz-cz*HALF_SIDE;
    if (lx==0) invalidate(cx-1,cz);
    if (lx==HALF_SIDE-1) invalidate(cx+1,cz);
    if (lz==0) invalidate(cx,cz-1);
    if (lz==HALF_SIDE-1) invalidate(cx,cz+1);
    if (light_opacity(old)!=light_opacity(block)) {
        /* Diffusion reaches in every horizontal direction, including across zero.
         * Water/air changes reuse lighting and only remesh visible geometry. */
        int r=VOXEL_LIGHT_MAX+1;
        for (int z=voxel_floor_div(sz-r,HALF_SIDE);z<=voxel_floor_div(sz+r,HALF_SIDE);z++)
            for (int x=voxel_floor_div(sx-r,HALF_SIDE);x<=voxel_floor_div(sx+r,HALF_SIDE);x++) {
                VoxelChunk *c=slot(x,z);
                if (matches(c,x,z)) {c->dirty=1;c->light_valid=0;}
            }
    }
}
static int set_cell(int sx,int sy,int sz,int block,int flow) {
    if (sy<2 || sy>=WORLD_HEIGHT*2 || block<0 || block>=BLOCK_COUNT ||
        sx < -20000000 || sx>20000000 || sz < -20000000 || sz>20000000) return 0;
    int x=voxel_floor_div(sx,2),y=voxel_floor_div(sy,2),z=voxel_floor_div(sz,2);
    int old=voxel_world_cell(sx,sy,sz);
    if (!voxel_edit_set_flow(sx,sy,sz,block,flow,base_block(x,y,z))) return 0;
    if (old!=block) changed(sx,sz,old,block);
    voxel_water_wake(sx,sy,sz);
    return 1;
}
int voxel_world_set(int sx,int sy,int sz,int block) {return set_cell(sx,sy,sz,block,0);}
int voxel_world_water_set(int sx,int sy,int sz,int level) {
    if (level< -1 || level>VOXEL_WATER_FALLING) return 0;
    return set_cell(sx,sy,sz,level<0 ? BLOCK_AIR : BLOCK_WATER,level<0 ? 0 : level);
}
static VoxelChunk *ensure_data(int cx,int cz) {
    VoxelChunk *c=slot(cx,cz);
    if (!matches(c,cx,cz)) {
        c->cx=cx;c->cz=cz;c->valid=1;c->ready=c->dirty=0;c->count=0;c->light_valid=0;c->light_y=c->light_height=0;
        voxel_terrain_chunk(cx,cz,c->blocks);
        voxel_water_activate(cx,cz);
    }
    return c;
}
static void prepare(int cx,int cz) {
    VoxelChunk *c=ensure_data(cx,cz);
    for (int dz=-1;dz<=1;dz++) for (int dx=-1;dx<=1;dx++) {
        if (!dx && !dz) continue;
        int x=cx+dx,z=cz+dz;
        if (abs(x-center_x)<=CACHE_RADIUS && abs(z-center_z)<=CACHE_RADIUS) ensure_data(x,z);
    }
    voxel_chunk_mesh(c);
}
static int worker(void) {
    /* Edited chunks take precedence over distant prefetch. */
    VoxelChunk *edit=NULL;int best=INT_MAX;
    for (int i=0;i<CACHE_COUNT;i++) {
        VoxelChunk *c=&chunks[i];int dx=c->cx-center_x,dz=c->cz-center_z;
        if (!c->valid || !c->dirty || abs(dx)>CACHE_RADIUS || abs(dz)>CACHE_RADIUS) continue;
        int distance=dx*dx+dz*dz;
        if (distance<best) {best=distance;edit=c;}
    }
    if (edit) { prepare(edit->cx,edit->cz);return 1; }
    for (int ring=0;ring<=CACHE_RADIUS;ring++) for (int dz=-ring;dz<=ring;dz++) for (int dx=-ring;dx<=ring;dx++) {
        if (abs(dx)!=ring && abs(dz)!=ring) continue;
        int cx=center_x+dx,cz=center_z+dz;VoxelChunk *c=slot(cx,cz);
        if (!matches(c,cx,cz) || !c->ready) { prepare(cx,cz);return 1; }
    }
    return 0;
}
static float coverage(void) {
    int ready_radius=0;
    for (int r=0;r<=WORLD_RADIUS;r++) {
        for (int z=-r;z<=r;z++) for (int x=-r;x<=r;x++) {
            if (abs(x)!=r && abs(z)!=r) continue;
            VoxelChunk *c=slot(center_x+x,center_z+z);
            if (!matches(c,center_x+x,center_z+z) || !c->ready) return fmaxf(12,ready_radius*CHUNK_SIZE);
        }
        ready_radius=r;
    }
    return REND_FOG_END;
}
void voxel_world_update(float x,float z) {
    if (!isfinite(x+z) || fabsf(x)>10000000 || fabsf(z)>10000000) return;
    int old_x=center_x,old_z=center_z;
    center_x=(int)floorf(x/CHUNK_SIZE);center_z=(int)floorf(z/CHUNK_SIZE);
    if (center_x!=old_x || center_z!=old_z) {
        for (int cz=center_z-WORLD_RADIUS;cz<=center_z+WORLD_RADIUS;cz++)
            for (int cx=center_x-WORLD_RADIUS;cx<=center_x+WORLD_RADIUS;cx++)
                if ((abs(cx-old_x)>WORLD_RADIUS || abs(cz-old_z)>WORLD_RADIUS) && matches(slot(cx,cz),cx,cz))
                    voxel_water_activate(cx,cz);
    }
    double start=app_now();
    for (int jobs=0;jobs<2;jobs++) { if (!worker() || app_now()-start>.0025) break; }
    float target=coverage(),d=(float)dt;
    if (!isfinite(d) || d<0) d=0;
    if (draw_distance>target) draw_distance=target; /* don't expose unmeshed edges */
    else draw_distance=fminf(target,draw_distance+fminf(d,.05f)*40);
}
void voxel_world_build(uint32_t seed) {
    voxel_water_reset();voxel_terrain_seed(seed);voxel_edits_reset(seed);voxel_edits_load();
    center_x=center_z=0;draw_distance=32;
    for (int i=0;i<CACHE_COUNT;i++) chunks[i].valid=chunks[i].ready=chunks[i].dirty=chunks[i].count=0;
    for (int z=-3;z<=3;z++) for (int x=-3;x<=3;x++) ensure_data(x,z);
    for (int z=-2;z<=2;z++) for (int x=-2;x<=2;x++) prepare(x,z);
}
float voxel_world_distance(void) { return draw_distance; }
/* Two passes: every opaque quad first, then transparent water everywhere, so
 * water blends over the scene behind it regardless of chunk draw order.
 * The water pass skips chunks without water quads entirely. */
static void draw_pass(int water) {
    for (int ring=0;ring<=WORLD_RADIUS;ring++) for (int dz=-ring;dz<=ring;dz++) for (int dx=-ring;dx<=ring;dx++) {
        if (abs(dx)!=ring && abs(dz)!=ring) continue;
        int cx=center_x+dx,cz=center_z+dz;VoxelChunk *c=slot(cx,cz);
        if (!matches(c,cx,cz) || !c->ready || !c->count) continue;
        if (water && !c->water_count) continue;
        float ox=cx*CHUNK_SIZE,oz=cz*CHUNK_SIZE,hy=(c->max_y-c->min_y)*.25f;
        if (!rend3d_visible(ox+8,c->min_y*.5f+hy,oz+8,8,hy,8)) continue;
        for (int i=0;i<c->count;i++) {
            const VoxelQuad *f=&c->quads[i];
            if ((f->block==BLOCK_WATER)!=water) continue;
            int sx=cx*CHUNK_SIZE*2+f->x, sy=f->y, sz=cz*CHUNK_SIZE*2+f->z;
            if (!rend3d_face_visible(f->face,(f->face<2 ? sy : f->face<4 ? sz : sx)*.5f)) continue;
            float hx, hy, hz, cxq, cyq, czq;
            if (f->face<2) {
                hx=f->u*.25f; hz=f->v*.25f; hy=.02f;
                cxq=sx*.5f+hx; cyq=sy*.5f; czq=sz*.5f+hz;
            } else if (f->face<4) {
                hx=f->u*.25f; hy=f->v*.25f; hz=.02f;
                cxq=sx*.5f+hx; cyq=sy*.5f+hy; czq=sz*.5f;
            } else {
                hz=f->u*.25f; hy=f->v*.25f; hx=.02f;
                cxq=sx*.5f; cyq=sy*.5f+hy; czq=sz*.5f+hz;
            }
            if (!rend3d_visible(cxq,cyq,czq,hx,hy,hz)) continue;
            rend3d_surface(sx,sy,sz,f->u,f->v,f->face,f->block,f->light);
        }
    }
}
void voxel_world_draw(void) { draw_pass(0); draw_pass(1); }
void voxel_world_stats(int *loaded,int *quads) {
    int n=0,q=0;
    for (int i=0;i<CACHE_COUNT;i++) {
        VoxelChunk *c=&chunks[i];
        if (!c->valid || abs(c->cx-center_x)>CACHE_RADIUS || abs(c->cz-center_z)>CACHE_RADIUS) continue;
        n++;if(c->ready)q+=c->count;
    }
    if (loaded) *loaded=n;
    if (quads) *quads=q;
}
int voxel_world_pending(void) {
    int n=0;
    for (int z=-CACHE_RADIUS;z<=CACHE_RADIUS;z++) for (int x=-CACHE_RADIUS;x<=CACHE_RADIUS;x++) {
        VoxelChunk *c=slot(center_x+x,center_z+z);
        if (!matches(c,center_x+x,center_z+z) || !c->ready || c->dirty) n++;
    }
    return n;
}
