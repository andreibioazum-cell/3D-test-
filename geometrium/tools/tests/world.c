/* Deterministic generation, streaming and exact greedy / half-cell coverage. */
#include "engine/render/voxel_internal.h"
#include <stdio.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x);exit(1);}}while(0)
int rend3d_visible(float x,float y,float z,float hx,float hy,float hz) {(void)x;(void)y;(void)z;(void)hx;(void)hy;(void)hz;return 1;}
int rend3d_face_visible(int face,float plane) {(void)face;(void)plane;return 1;}
static int drawn_lit,drawn_dim,drawn_smooth;
void rend3d_surface(int x,int y,int z,int u,int v,int face,int block,const unsigned char light[4]) {
    (void)x;(void)y;(void)z;(void)u;(void)v;(void)face;(void)block;
    CHECK(light);
    if (light[0]==255) drawn_lit++;else drawn_dim++;
    if (light[0]!=light[1] || light[0]!=light[2] || light[0]!=light[3]) drawn_smooth++;
}
static void test_chunks(void) {
    unsigned char a[BASE_CELLS],b[BASE_CELLS];int types[BLOCK_COUNT]={0},low=100,high=0;
    const int coords[][2]={{0,0},{1,-1},{-1,1},{-3,-2},{2,3},{3,-4}};
    voxel_terrain_seed(REND_WORLD_SEED);
    for(unsigned i=0;i<sizeof(coords)/sizeof(*coords);i++) {
        int cx=coords[i][0],cz=coords[i][1];
        voxel_terrain_chunk(cx,cz,a);voxel_terrain_chunk(cx,cz,b);CHECK(!memcmp(a,b,sizeof(a)));
        for(int z=0;z<CHUNK_SIZE;z++)for(int x=0;x<CHUNK_SIZE;x++) {
            int wx=cx*CHUNK_SIZE+x,wz=cz*CHUNK_SIZE+z,h=voxel_terrain_height(wx,wz);
            if(h<low)low=h;
            if(h>high)high=h;
            for(int y=0;y<WORLD_HEIGHT;y++) {
                int block=a[(y*CHUNK_SIZE+z)*CHUNK_SIZE+x];CHECK(block<BLOCK_COUNT);types[block]++;
                if(x==0||x==15||z==0||z==15||(x%5==0&&z%5==0))CHECK(block==voxel_terrain_block(wx,y,wz));
            }
        }
    }
    CHECK(high-low>=5);for(int i=0;i<BLOCK_COUNT;i++)CHECK(types[i]>0);
    voxel_terrain_chunk(3,-4,a);voxel_terrain_seed(REND_WORLD_SEED+1);voxel_terrain_chunk(3,-4,b);CHECK(memcmp(a,b,sizeof(a))!=0);
    puts("PASS deterministic seeds, negative coordinates, tree seams and every PNG terrain material");
}
static uint32_t fingerprint(void) {
    uint32_t hash=2166136261u;
    for(int z=-45;z<60;z+=3)for(int x=-45;x<60;x+=3)for(int y=4;y<30;y+=3)
        hash=(hash^(uint32_t)voxel_world_cell(x*2,y*2,z*2))*16777619u;
    return hash;
}
static void warm(float x,float z) {
    voxel_world_update(x,z);
    for(int i=0;voxel_world_pending() && i<500;i++)voxel_world_update(x,z);
    CHECK(!voxel_world_pending());
    for(int i=0;i<180;i++)voxel_world_update(x,z);
    CHECK(voxel_world_distance()==REND_FOG_END);
}
static void test_streaming(void) {
    voxel_world_build(REND_WORLD_SEED);uint32_t original=fingerprint();
    int n,q;voxel_world_stats(&n,&q);CHECK(n<CACHE_COUNT && voxel_world_pending()>0);
    warm(8.5f,8.5f);voxel_world_stats(&n,&q);CHECK(n==CACHE_COUNT && q>0 && q<CACHE_COUNT*2500);
    CHECK(voxel_cell_solid(16,24,16) && !voxel_cell_solid(16,26,16));
    CHECK(voxel_world_cell(2,-1,2)==BLOCK_STONE && voxel_world_cell(2,WORLD_HEIGHT*2,2)==BLOCK_AIR);
    const int positions[][2]={{80,0},{-80,-96},{1024,-1024},{-10000,20000},{8,8}};
    for(unsigned i=0;i<sizeof(positions)/sizeof(*positions);i++) {
        int x=positions[i][0],z=positions[i][1];warm(x+.5f,z+.5f);voxel_world_stats(&n,&q);
        CHECK(n==CACHE_COUNT && q>0 && q<CACHE_COUNT*2500);
        for(int dx=-17;dx<=17;dx+=3)for(int dz=-17;dz<=17;dz+=3)for(int y=5;y<30;y+=4)
            CHECK(voxel_world_cell((x+dx)*2,y*2,(z+dz)*2)==voxel_terrain_block(x+dx,y,z+dz));
    }
    CHECK(original==fingerprint());voxel_world_build(REND_WORLD_SEED);CHECK(original==fingerprint());
    puts("PASS bounded 11x11 cache, incremental nearest-first streaming, 64-block fog range and return-trip determinism");
}
static const int normals[6][3]={{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{1,0,0},{-1,0,0}};
static size_t index_of(int x,int y,int z,int f) {
    return ((size_t)f*WORLD_HEIGHT*2+y)*(CHUNK_SIZE*2)*(CHUNK_SIZE*2)+z*(CHUNK_SIZE*2)+x;
}
static void check_mesh(int cx,int cz) {
    VoxelChunk c={.cx=cx,.cz=cz};voxel_terrain_chunk(cx,cz,c.blocks);voxel_chunk_mesh(&c);
    unsigned char *cover=calloc((size_t)BASE_CELLS*8*6,1);CHECK(cover);
    for(int i=0;i<c.count;i++) {
        const VoxelQuad *q=&c.quads[i];int f=q->face,a=f<2 ? 1 : f<4 ? 2 : 0,ua=f<4 ? 0 : 2,va=f<2 ? 2 : 1;
        CHECK(q->u>0 && q->v>0 && q->block>0 && q->block<BLOCK_COUNT);
        for(int v=0;v<q->v;v++)for(int u=0;u<q->u;u++) {
            int p[3]={q->x,q->y,q->z};p[a]-=normals[f][a]>0;p[ua]+=u;p[va]+=v;
            CHECK(p[0]>=0 && p[0]<32 && p[1]>=0 && p[1]<WORLD_HEIGHT*2 && p[2]>=0 && p[2]<32);
            size_t at=index_of(p[0],p[1],p[2],f);CHECK(cover[at]==0);cover[at]=q->block;
        }
    }
    int exposed=0;
    for(int y=0;y<WORLD_HEIGHT*2;y++)for(int z=0;z<32;z++)for(int x=0;x<32;x++) {
        int wx=cx*32+x,wz=cz*32+z,b=voxel_world_cell(wx,y,wz);
        for(int f=0;f<6;f++) {
            int material=0;
            if(b && voxel_face_exposed(b,voxel_world_cell(wx+normals[f][0],y+normals[f][1],wz+normals[f][2]))) {
                material=b;exposed++;
                if(b==BLOCK_GRASS && f==0 && (y+1)%2)material=BLOCK_DIRT;
            }
            CHECK(cover[index_of(x,y,z,f)]==material);
        }
    }
    /* Non-uniform vertex light intentionally prevents some greedy merges. */
    CHECK(c.count*3<exposed);free(cover);free(c.quads);free(c.light);
}
static void test_mesh(void) {
    voxel_world_build(REND_WORLD_SEED);
    CHECK(voxel_world_set(17,25,16,BLOCK_AIR));
    int original=voxel_terrain_block(15,voxel_terrain_height(15,0),0);CHECK(original>0);
    CHECK(voxel_world_set(31,voxel_terrain_height(15,0)*2+1,0,BLOCK_AIR));
    /* Full water beside a partial water/stone/air parent must not emit internal water faces. */
    for(int z=0;z<2;z++)for(int y=80;y<82;y++)for(int x=30;x<32;x++)CHECK(voxel_world_set(x,y,z,BLOCK_WATER));
    CHECK(voxel_world_set(32,80,0,BLOCK_WATER));CHECK(voxel_world_set(32,81,0,BLOCK_STONE));
    /* Grow and rehash both edit indices, including negative boundaries and tall construction. */
    for(int i=0;i<340;i++) {
        int x=i%20-10,y=40+i/200,z=(i/20)%10-5;
        CHECK(voxel_world_set(x*2+(i&1),y*2,z*2,(i%3)+BLOCK_GRASS));
    }
    check_mesh(0,0);check_mesh(-1,0);check_mesh(1,0);check_mesh(0,-1);check_mesh(-1,-1);
    puts("PASS exact greedy/partial face coverage: no hidden or duplicate faces, water contacts, chunk edges and interior grass cuts");
}
static void set(int x,int y,int z,int block) {
    if(voxel_world_cell(x,y,z)!=block)CHECK(voxel_world_set(x,y,z,block));
}
static void test_diffuse_light(void) {
    voxel_world_build(REND_WORLD_SEED);
    /* A sealed, long room spanning a negative chunk boundary. */
    for(int x=-12;x<=36;x++)for(int z=-6;z<=6;z++)for(int y=70;y<=78;y++) {
        int wall=x==-12 || x==36 || z==-6 || z==6 || y==70 || y==78;
        set(x,y,z,wall ? BLOCK_STONE : BLOCK_AIR);
    }
    warm(8.5f,8.5f);
    CHECK(voxel_world_light(0.25f,36.75f,.25f)==0);
    CHECK(voxel_world_light(0.25f,40.25f,.25f)==1);
    for(int z=-2;z<=2;z++)for(int y=72;y<=76;y++)set(-12,y,z,BLOCK_AIR);
    warm(8.5f,8.5f);
    float entrance=voxel_world_light(-5.75f,36.75f,.25f);
    float middle=voxel_world_light(-3.25f,36.75f,.25f);
    float deep=voxel_world_light(4.25f,36.75f,.25f);
    CHECK(entrance>.85f && entrance<1);
    CHECK(middle>.2f && middle<entrance-.2f);CHECK(deep==0);
    CHECK(fabsf(voxel_world_light(-4.2f,36.75f,.25f)-voxel_world_light(-4.19f,36.75f,.25f))<.005f);
    /* The same propagated value on both sides of a negative chunk seam. */
    VoxelChunk a={.cx=-1,.cz=0},b={.cx=0,.cz=0};
    voxel_terrain_chunk(a.cx,a.cz,a.blocks);voxel_terrain_chunk(b.cx,b.cz,b.blocks);
    voxel_light_bake(&a);voxel_light_bake(&b);
    for(int y=0;y<WORLD_HEIGHT*2;y++)for(int z=0;z<32;z++)for(int x=31;x<=32;x++)
        CHECK(voxel_light_cell(&a,x,y,z)==voxel_light_cell(&b,x-32,y,z));
    free(a.light);free(b.light);
    for(int z=-2;z<=2;z++)for(int y=72;y<=76;y++)set(-12,y,z,BLOCK_STONE);
    warm(8.5f,8.5f);CHECK(voxel_world_light(-5.75f,36.75f,.25f)==0);
    /* One half-cell roof opening admits sky, without opening its other halves. */
    set(0,78,0,BLOCK_AIR);warm(8.5f,8.5f);
    CHECK(voxel_world_light(.25f,36.75f,.25f)==1);
    CHECK(voxel_world_light(.75f,36.75f,.25f)<1);
    CHECK(voxel_cell_solid(1,78,0));
    set(0,78,0,BLOCK_STONE);warm(8.5f,8.5f);CHECK(voxel_world_light(.25f,36.75f,.25f)==0);
    puts("PASS diffuse skylight: sealed mines are dark, portals fade with distance, half-cell apertures, roof edits and seamless negative borders");
}
static void test_canopy_light(void) {
    voxel_world_build(REND_WORLD_SEED);
    set(17,100,17,BLOCK_STONE);warm(8.5f,8.5f);
    float under=voxel_world_light(8.75f,49.75f,8.75f);
    CHECK(under>.85f && under<1); /* not a binary/contact shadow under one block */
    CHECK(voxel_world_light(8.75f,50.75f,8.75f)==1);
    for(int z=10;z<=24;z++)for(int x=10;x<=24;x++)for(int y=100;y<=101;y++)set(x,y,z,BLOCK_LEAVES);
    warm(8.5f,8.5f);
    float canopy=voxel_world_light(8.75f,49.75f,8.75f),edge=voxel_world_light(12.25f,49.75f,8.75f);
    CHECK(canopy>.5f && canopy<.85f);CHECK(edge>canopy && edge<1);
    drawn_lit=drawn_dim=drawn_smooth=0;voxel_world_draw();
    CHECK(drawn_lit>100 && drawn_dim>100 && drawn_smooth>100);
    printf("PASS translucent canopy, no hard contact AO, %d daylight / %d dim / %d smooth-gradient quads\n",drawn_lit,drawn_dim,drawn_smooth);
}
int main(void) {dt=1.0/60;test_chunks();test_streaming();test_mesh();test_diffuse_light();test_canopy_light();return 0;}
