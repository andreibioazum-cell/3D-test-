/* Diffuse skylight, baked only when geometry changes. No sun rays or contact AO.
 * A one-byte half-cell cache keeps openings accurate without expanding terrain.
 * The scratch halo is shared by all chunks; its 15-cell reach bounds updates. */
#include "voxel_internal.h"

enum { PAD=16, SIDE=CHUNK_SIZE*2+PAD*2, PLANE=SIDE*SIDE,
       VOLUME=PLANE*WORLD_HEIGHT*2, SOLID=64, LEAF=32, QUEUED=128,
       CACHE_SIDE_LIGHT=CHUNK_SIZE*2+2, CACHE_PLANE_LIGHT=CACHE_SIDE_LIGHT*CACHE_SIDE_LIGHT };
static unsigned char work[VOLUME];
static int queue[VOLUME],head,tail,queued,height;
static unsigned char fallback[BASE_CELLS];

static int opacity(int block) {
    return block==BLOCK_AIR || block==BLOCK_WATER ? 0 : block==BLOCK_LEAVES ? LEAF : SOLID;
}
static void cell(int x,int y,int z,int block) {
    if (x<0 || x>=SIDE || z<0 || z>=SIDE || y<0 || y>=WORLD_HEIGHT*2) return;
    int value=opacity(block);
    work[y*PLANE+z*SIDE+x]=(unsigned char)value;
    if (value && y+2>height) height=y+2;
}
static void enqueue(int at) {
    if (work[at]&QUEUED) return;
    work[at]|=QUEUED;
    queue[tail]=at;tail=(tail+1)%VOLUME;queued++;
}
static void spread(int at,int level) {
    unsigned char value=work[at];
    if (value&SOLID) return;
    int next=level-((value&LEAF)?2:1);
    if (next<=(value&15)) return;
    work[at]=(unsigned char)((value&0xf0)|next);
    if (next>1) enqueue(at);
}

void voxel_light_bake(VoxelChunk *c) {
    if (c->light_valid) return;
    memset(work,0,sizeof(work));height=1;
    int ox=c->cx*CHUNK_SIZE-PAD/2,oz=c->cz*CHUNK_SIZE-PAD/2;
    /* Bulk copies, not hundreds of thousands of world/hash lookups per bake. */
    for (int cz=c->cz-1;cz<=c->cz+1;cz++) for (int cx=c->cx-1;cx<=c->cx+1;cx++) {
        const unsigned char *blocks=cx==c->cx && cz==c->cz ? c->blocks : voxel_world_chunk_blocks(cx,cz);
        if (!blocks) {voxel_terrain_chunk(cx,cz,fallback);blocks=fallback;}
        int x0=cx*CHUNK_SIZE-ox,z0=cz*CHUNK_SIZE-oz;
        int xbegin=x0<0 ? -x0 : 0,zbegin=z0<0 ? -z0 : 0;
        int xend=x0+CHUNK_SIZE>SIDE/2 ? SIDE/2-x0 : CHUNK_SIZE;
        int zend=z0+CHUNK_SIZE>SIDE/2 ? SIDE/2-z0 : CHUNK_SIZE;
        for (int y=0;y<WORLD_HEIGHT;y++) for (int z=zbegin;z<zend;z++) for (int x=xbegin;x<xend;x++) {
            int value=opacity(blocks[(y*CHUNK_SIZE+z)*CHUNK_SIZE+x]);
            if (!value) continue;
            int at=y*2*PLANE+(z+z0)*2*SIDE+(x+x0)*2;
            work[at]=work[at+1]=work[at+SIDE]=work[at+SIDE+1]=(unsigned char)value;
            work[at+PLANE]=work[at+PLANE+1]=work[at+PLANE+SIDE]=work[at+PLANE+SIDE+1]=(unsigned char)value;
            if (y*2+3>height) height=y*2+3;
        }
        for (const VoxelEdit *e=voxel_edit_first(cx,cz);e;e=voxel_edit_next(e)) {
            int lx=(e->x-ox)*2,lz=(e->z-oz)*2;
            for (int i=0;i<8;i++) cell(lx+(i&1),e->y*2+((i>>1)&1),lz+(i>>2),e->cells[i]);
        }
    }
    if (height>WORLD_HEIGHT*2) height=WORLD_HEIGHT*2;
    /* Open sky travels vertically without decay. Leaves transmit diffuse light;
     * rock stops it. Water is transparent to light, not an opaque shadow caster. */
    int bottom=height;
    for (int z=0;z<SIDE;z++) for (int x=0;x<SIDE;x++) {
        int sky=VOXEL_LIGHT_MAX;
        for (int y=height-1;y>=0;y--) {
            int at=y*PLANE+z*SIDE+x,value=work[at];
            if (!(value&SOLID) && y<bottom) bottom=y;
            if (value&SOLID) sky=0;
            else if (value&LEAF) sky=sky>2 ? sky-2 : 0;
            work[at]=(unsigned char)(value|sky);
        }
    }
    head=tail=queued=0;
    /* Seed only light/dark boundaries, not every already-lit sky cell. */
    for (int y=bottom;y<height;y++) for (int z=0;z<SIDE;z++) for (int x=0;x<SIDE;x++) {
        int at=y*PLANE+z*SIDE+x,level=work[at]&15;
        if (level<=1 || (work[at]&SOLID)) continue;
        /* The vertical sweep already dominates both vertical neighbours. */
        int offsets[4]={-1,1,-SIDE,SIDE},valid[4]={x>0,x+1<SIDE,z>0,z+1<SIDE};
        for (int d=0;d<4;d++) if (valid[d]) {
            int n=work[at+offsets[d]];
            if (!(n&SOLID) && (n&15)<level-((n&LEAF)?2:1)) {enqueue(at);break;}
        }
    }
    while (queued) {
        int at=queue[head];head=(head+1)%VOLUME;queued--;
        work[at]&=~QUEUED;
        int level=work[at]&15,x=at%SIDE,z=(at/SIDE)%SIDE,y=at/PLANE;
        if (x) spread(at-1,level);
        if (x+1<SIDE) spread(at+1,level);
        if (z) spread(at-SIDE,level);
        if (z+1<SIDE) spread(at+SIDE,level);
        if (y>bottom) spread(at-PLANE,level);
        if (y+1<height) spread(at+PLANE,level);
    }
    size_t need=(size_t)CACHE_PLANE_LIGHT*(height-bottom);
    if (need>c->light_capacity) {
        unsigned char *p=realloc(c->light,need);
        if (!p) {app_fail("Not enough memory for lighting");return;}
        c->light=p;c->light_capacity=need;
    }
    for (int y=bottom;y<height;y++) for (int z=0;z<CACHE_SIDE_LIGHT;z++) for (int x=0;x<CACHE_SIDE_LIGHT;x++) {
        int value=work[y*PLANE+(z+PAD-1)*SIDE+x+PAD-1];
        /* Opaque samples are skipped, never averaged as black contact shadows. */
        c->light[(y-bottom)*CACHE_PLANE_LIGHT+z*CACHE_SIDE_LIGHT+x]=(unsigned char)((value&SOLID)?VOXEL_LIGHT_OPAQUE:value&15);
    }
    c->light_y=bottom;c->light_height=height;c->light_valid=1;
}

int voxel_light_cell(const VoxelChunk *c,int x,int y,int z) {
    if (y<c->light_y) return VOXEL_LIGHT_OPAQUE;
    if (y>=c->light_height) return VOXEL_LIGHT_MAX;
    if (!c->light || x< -1 || x>CHUNK_SIZE*2 || z< -1 || z>CHUNK_SIZE*2) return 0;
    return c->light[(y-c->light_y)*CACHE_PLANE_LIGHT+(z+1)*CACHE_SIDE_LIGHT+x+1];
}
void voxel_light_face(const VoxelChunk *c,const int origin[3],int u,int v,int face,unsigned char light[4]) {
    int a=face<2 ? 1 : face<4 ? 2 : 0,ua=face<4 ? 0 : 2,va=face<2 ? 2 : 1;
    for (int i=0;i<4;i++) {
        int p[3]={origin[0],origin[1],origin[2]};
        p[a]-=face&1;p[ua]+=(i&1)*u;p[va]+=(i>>1)*v;
        int sum=0,count=0;
        for (int dv=-1;dv<=0;dv++) for (int du=-1;du<=0;du++) {
            int s[3]={p[0],p[1],p[2]};s[ua]+=du;s[va]+=dv;
            int l=voxel_light_cell(c,s[0],s[1],s[2]);
            if (l!=VOXEL_LIGHT_OPAQUE) {sum+=l;count++;}
        }
        light[i]=(unsigned char)(count ? (sum*17+count/2)/count : 0);
    }
}
