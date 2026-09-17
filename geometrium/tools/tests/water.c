/* Fixed-tick half-voxel water: sources, gravity, drains, borders and persistence. */
#define _POSIX_C_SOURCE 200809L
#include "engine/render/voxel_internal.h"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x);exit(1);}}while(0)
int rend3d_visible(float x,float y,float z,float a,float b,float c) {(void)x;(void)y;(void)z;(void)a;(void)b;(void)c;return 0;}
int rend3d_face_visible(int f,float p) {(void)f;(void)p;return 0;}
void rend3d_surface(int x,int y,int z,int u,int v,int f,int b,const unsigned char l[4])
{(void)x;(void)y;(void)z;(void)u;(void)v;(void)f;(void)b;(void)l;}
static void fresh(void) {app_set_storage(NULL);voxel_world_build(REND_WORLD_SEED);}
static void set(int x,int y,int z,int b) {if(voxel_world_cell(x,y,z)!=b)CHECK(voxel_world_set(x,y,z,b));}
static void tick(void) {
    voxel_water_update(.05f);CHECK(voxel_water_last_work()<=512);
    voxel_water_update(.05f);CHECK(voxel_water_last_work()<=512);
}
static void ticks(int n) {for(int i=0;i<n;i++)tick();}
static void settle(void) {for(int i=0;i<500 && voxel_water_pending();i++)tick();CHECK(!voxel_water_pending());}
static void platform(void) {
    for(int z=-18;z<=18;z++)for(int x=-18;x<=18;x++)set(x,80,z,BLOCK_STONE);
}
static void test_lake_hole(void) {
    fresh();int x=0,z=0,found=0;
    for(z=-30;z<=30 && !found;z++)for(x=-30;x<=30;x++)
        if(voxel_terrain_height(x,z)<WATER_LEVEL) {found=1;break;}
    CHECK(found);z--;
    int sx=x*2,sz=z*2,sy=voxel_terrain_height(x,z)*2+1;
    CHECK(voxel_cell_solid(sx,sy,sz));CHECK(voxel_world_cell(sx,sy+1,sz)==BLOCK_WATER);
    CHECK(voxel_world_set(sx,sy,sz,BLOCK_AIR));
    CHECK(voxel_world_cell(sx,sy,sz)==BLOCK_AIR);tick();
    CHECK(voxel_world_cell(sx,sy,sz)==BLOCK_WATER);
    CHECK(voxel_water_level(sx,sy,sz)==VOXEL_WATER_FALLING);
    CHECK(voxel_cell_solid(sx+1,sy,sz));CHECK(voxel_cell_solid(sx,sy-1,sz));
    CHECK(voxel_water_level(sx,sy+1,sz)==0);
    puts("PASS water refills a broken half-block under an original lake; other seven pieces remain intact");
}
static void test_lateral_and_dam(void) {
    fresh();platform();CHECK(!voxel_water_pending());
    set(-1,81,-1,BLOCK_WATER);tick();
    CHECK(voxel_water_level(0,81,-1)==1); /* negative-to-positive chunk seam */
    CHECK(voxel_water_level(-2,81,-1)==1);
    CHECK(voxel_world_cell(0,81,0)==BLOCK_AIR); /* cannot jump diagonally in one tick */
    CHECK(voxel_world_cell(1,81,-1)==BLOCK_AIR);
    CHECK(voxel_world_cell(-1,82,-1)==BLOCK_AIR); /* never climbs */
    tick();CHECK(voxel_water_level(0,81,0)==2);settle();
    CHECK(voxel_water_level(6,81,-1)==VOXEL_WATER_REACH);
    CHECK(voxel_world_cell(7,81,-1)==BLOCK_AIR);
    CHECK(voxel_water_level(-8,81,-1)==VOXEL_WATER_REACH);
    CHECK(voxel_world_cell(-9,81,-1)==BLOCK_AIR);
    set(-1,79,-1,BLOCK_STONE); /* catch the drop without flooding distant terrain */
    set(-1,80,-1,BLOCK_AIR);settle();CHECK(voxel_world_cell(0,81,-1)==BLOCK_AIR);
    set(-1,80,-1,BLOCK_STONE);settle();CHECK(voxel_water_level(0,81,-1)==1);
    for(int z=-18;z<=18;z++)set(1,81,z,BLOCK_STONE);
    settle();CHECK(voxel_world_cell(2,81,-1)==BLOCK_AIR);
    CHECK(voxel_water_level(0,81,-1)==1);
    set(1,81,-1,BLOCK_AIR);settle();
    CHECK(voxel_water_level(1,81,-1)==2);CHECK(voxel_water_level(2,81,-1)==3);
    set(-1,81,-1,BLOCK_STONE);settle();
    for(int z=-12;z<=10;z++)for(int x=-12;x<=10;x++)CHECK(voxel_world_cell(x,81,z)!=BLOCK_WATER);
    puts("PASS face-by-face finite lateral flow, no uphill/diagonal teleport, chunk seam, floor drains, dam closure/reopening and draining");
}
static void test_gravity(void) {
    fresh();platform();set(5,94,5,BLOCK_WATER);tick();
    CHECK(voxel_water_level(5,93,5)==VOXEL_WATER_FALLING);
    CHECK(voxel_world_cell(6,94,5)==BLOCK_AIR);CHECK(voxel_world_cell(5,92,5)==BLOCK_AIR);
    ticks(40);settle();
    for(int y=81;y<94;y++)CHECK(voxel_water_level(5,y,5)==VOXEL_WATER_FALLING);
    CHECK(voxel_water_level(6,81,5)==1);
    CHECK(voxel_world_cell(6,90,5)==BLOCK_AIR); /* no sideways sheets in mid-air */
    set(5,94,5,BLOCK_STONE);settle();
    for(int y=81;y<94;y++)CHECK(voxel_world_cell(5,y,5)==BLOCK_AIR);
    CHECK(voxel_world_cell(6,81,5)==BLOCK_AIR);
    voxel_water_update(NAN);CHECK(!voxel_water_last_work());
    voxel_water_update(-1);CHECK(!voxel_water_last_work());
    puts("PASS gravity-first waterfall, spreading at its foot, removal of unsupported water and bounded/invalid-dt work");
}
static void test_queue_pressure(void) {
    fresh();
    /* More than 32K quiet source cells in a sealed tank. Overflow recovery must
     * terminate, keep its per-tick budget, and leave the ring/hash reusable. */
    for(int y=99;y<=109;y++)for(int z=0;z<=65;z++)for(int x=0;x<=65;x++)
        if(y==99 || y==109 || z==0 || z==65 || x==0 || x==65)set(x,y,z,BLOCK_STONE);
    for(int y=100;y<=108;y++)for(int z=1;z<=64;z++)for(int x=1;x<=64;x++)set(x,y,z,BLOCK_WATER);
    CHECK(voxel_water_pending()==32769);int parents=voxel_edits_count();
    for(int i=0;i<3000 && voxel_water_pending();i++)tick();
    CHECK(!voxel_water_pending() && voxel_edits_count()==parents);
    CHECK(voxel_water_level(32,104,32)==0 && voxel_world_cell(66,104,32)==BLOCK_AIR);
    set(65,104,32,BLOCK_AIR);tick();CHECK(voxel_water_level(65,104,32)==1);
    puts("PASS bounded water queue: 32K overflow recovery terminates, ring/hash wrap safely and a new opening still flows");
}
static uint32_t state(void) {
    uint32_t h=2166136261u;
    for(int y=80;y<=83;y++)for(int z=-10;z<=10;z++)for(int x=-10;x<=10;x++) {
        h=(h^(unsigned)voxel_world_cell(x,y,z))*16777619u;
        h=(h^(unsigned)(voxel_water_level(x,y,z)+1))*16777619u;
    }
    return h;
}
static void warm(float x,float z) {for(int i=0;i<600;i++) {voxel_world_update(x,z);if(!voxel_world_pending())return;}CHECK(0);}
static void test_reload(const char *directory) {
    fresh();platform();set(-1,81,-1,BLOCK_WATER);settle();
    uint32_t before=state();
    mkdir(directory,0700);app_set_storage(directory);CHECK(voxel_edits_save());
    char path[600];CHECK(app_save_path(path,sizeof(path),"world.edits"));
    FILE *f=fopen(path,"rb");CHECK(f);char version[8];CHECK(fread(version,1,8,f)==8);fclose(f);
    CHECK(!memcmp(version,"EJVOX02\0",8));
    voxel_world_build(REND_WORLD_SEED);CHECK(state()==before);
    CHECK(voxel_water_pending());settle();CHECK(state()==before);
    CHECK(voxel_water_level(-1,81,-1)==0 && voxel_water_level(0,81,-1)==1);
    /* Eviction must not promote flowing water to sources or lose its wake-up. */
    warm(1024,-1024);ticks(4);warm(0,0);settle();CHECK(state()==before);
    set(-1,81,-1,BLOCK_STONE);settle();CHECK(voxel_world_cell(0,81,-1)==BLOCK_AIR);
    CHECK(voxel_edits_save());voxel_world_build(REND_WORLD_SEED);settle();
    CHECK(voxel_world_cell(0,81,-1)==BLOCK_AIR);
    remove(path);app_set_storage(NULL);voxel_edits_reset(REND_WORLD_SEED);
    puts("PASS source/flow levels survive atomic save, restart and eviction; reloaded streams still drain after source removal");
}
int main(int argc,char **argv) {
    CHECK(argc==2);dt=1.0/60;test_lake_hole();test_lateral_and_dam();test_gravity();test_queue_pressure();test_reload(argv[1]);return 0;
}
