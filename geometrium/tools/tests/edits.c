/* Breaking/building, ray picking, independent controls, half collisions and saves. */
#define _POSIX_C_SOURCE 200809L
#include "engine/render/voxel_internal.h"
#include "geometrium/geometrium_render_internal.h"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x);exit(1);}}while(0)
#define CLOSE(a,b) CHECK(fabsf((a)-(b))<.001f)
void geometrium_cancel_input(void) {geometrium_input_reset();geometrium_key_reset();geometrium_actions_reset();}
int rend3d_visible(float x,float y,float z,float a,float b,float c) {(void)x;(void)y;(void)z;(void)a;(void)b;(void)c;return 0;}
void rend3d_surface(int x,int y,int z,int u,int v,int f,int b,const unsigned char s[4]) {(void)x;(void)y;(void)z;(void)u;(void)v;(void)f;(void)b;(void)s;}
void rend3d_segment(float x,float y,float z,float a,float b,float c,uint32_t color) {(void)x;(void)y;(void)z;(void)a;(void)b;(void)c;(void)color;}
/* Rasterizer stubs: geometrium_hand.c links here because of the shared actions. */
void rend3d_polygon(const RendVertex *w,int n,float nx,float ny,float nz,uint32_t color,RendMaterial *material,const unsigned char *light)
{(void)w;(void)n;(void)nx;(void)ny;(void)nz;(void)color;(void)material;(void)light;}
int rend3d_project(float x,float y,float z,float *sx,float *sy) {(void)x;(void)y;(void)z;if(sx)*sx=0;if(sy)*sy=0;return 1;}
void rend3d_viewmodel(int enabled) {(void)enabled;}
int rend3d_face_visible(int face,float plane) {(void)face;(void)plane;return 1;}
void rend3d_depth_clear(float x0,float y0,float x1,float y1) {(void)x0;(void)y0;(void)x1;(void)y1;}
RendMaterial *rend_material(int block,int face) {(void)block;(void)face;return NULL;}
static void fresh(void) {app_set_storage(NULL);geometrium_cancel_input();voxel_world_build(GEOMETRIUM_WORLD_SEED);geometrium_player_spawn();geometrium_input_layout();}
static void aim(float yaw,float pitch) {
    float y,p;geometrium_camera_angles(&y,&p);float size=fminf(screen_w,screen_h);
    geometrium_camera_look((yaw-y)*size/2.7f,(p-pitch)*size/2.4f);
}
static void test_eight_parts(void) {
    fresh();
    for(int i=0;i<8;i++)CHECK(voxel_world_cell(16+(i&1),24+((i>>1)&1),16+(i>>2))==BLOCK_GRASS);
    CHECK(voxel_world_set(17,25,16,BLOCK_AIR));int empty=0;
    for(int i=0;i<8;i++)empty+=voxel_world_cell(16+(i&1),24+((i>>1)&1),16+(i>>2))==BLOCK_AIR;
    CHECK(empty==1 && voxel_world_uniform(8,12,8)==BLOCK_PARTIAL && voxel_edits_count()==1);
    CHECK(voxel_world_set(17,25,16,BLOCK_GRASS));CHECK(voxel_world_uniform(8,12,8)==BLOCK_GRASS);
    CHECK(!voxel_world_set(17,25,16,BLOCK_GRASS));
    CHECK(!voxel_world_set(17,1,16,BLOCK_AIR));CHECK(!voxel_world_set(17,128,16,BLOCK_STONE));
    CHECK(!voxel_world_set(17,25,16,BLOCK_COUNT));
    puts("PASS a parent contains eight independent 0.5-cubes; unchanged cells, bedrock and height bounds are preserved");
}
static void test_ray_and_build(void) {
    fresh();geometrium_select(0);GeometriumHit h;
    for(int layer=0;layer<2;layer++)for(int z=0;z<2;z++)for(int x=0;x<2;x++) {
        CHECK(geometrium_raycast(8.25f+x*.5f,16.75f,8.25f+z*.5f,0,-1,0,6,&h));
        CHECK(h.x==16+x && h.y==25-layer && h.z==16+z && h.ny==1);
        CLOSE(h.distance,3.75f+layer*.5f);CHECK(geometrium_action_apply(ACTION_BREAK,&h));
    }
    for(int i=0;i<8;i++)CHECK(voxel_world_cell(16+(i&1),24+((i>>1)&1),16+(i>>2))==BLOCK_AIR);
    for(int layer=0;layer<2;layer++)for(int z=0;z<2;z++)for(int x=0;x<2;x++) {
        CHECK(geometrium_raycast(8.25f+x*.5f,16.75f,8.25f+z*.5f,0,-1,0,6,&h));
        CHECK(h.y==23+layer);CHECK(geometrium_action_apply(ACTION_PLACE,&h));
    }
    CHECK(voxel_world_uniform(8,12,8)==BLOCK_GRASS);
    CHECK(!geometrium_raycast(8.25f,30,8.25f,0,-1,0,6,&h));
    CHECK(!geometrium_raycast(NAN,0,0,0,-1,0,6,&h));CHECK(!geometrium_raycast(0,0,0,0,0,0,6,&h));
    CHECK(voxel_world_set(-1,90,-1,BLOCK_STONE));
    CHECK(geometrium_raycast(-.25f,46.75f,-.25f,0,-1,0,6,&h));CHECK(h.x==-1 && h.y==90 && h.z==-1 && h.ny==1);CLOSE(h.distance,1.25f);
    CHECK(geometrium_raycast(-2,45.25f,-.25f,1,0,0,6,&h));CHECK(h.nx==-1);CLOSE(h.distance,1.5f);
    CHECK(geometrium_raycast(0,45.25f,-.25f,-1,0,0,6,&h));CHECK(h.nx==1 && h.x==-1);CLOSE(h.distance,0);
    CHECK(voxel_world_set(17,27,18,BLOCK_STONE));h=(GeometriumHit){17,27,18,0,0,-1,BLOCK_STONE,1};
    CHECK(!geometrium_action_apply(ACTION_PLACE,&h)); /* would overlap the player */
    geometrium_select(2);h.nz=1;CHECK(geometrium_action_apply(ACTION_PLACE,&h));CHECK(voxel_world_cell(17,27,19)==BLOCK_STONE);
    CHECK(!geometrium_action_apply(ACTION_PLACE,&h)); /* occupied */
    h.distance=6.01f;CHECK(!geometrium_action_apply(ACTION_BREAK,&h));h.distance=NAN;CHECK(!geometrium_action_apply(ACTION_BREAK,&h));
    h=(GeometriumHit){4,1,4,0,1,0,BLOCK_STONE,1};CHECK(!geometrium_action_apply(ACTION_BREAK,&h));
    CHECK(voxel_world_set(4,2,4,BLOCK_AIR));CHECK(geometrium_action_apply(ACTION_PLACE,&h)); /* build on bedrock */
    puts("PASS half-cell DDA, exact negative/boundary hits, eight break/place operations, reach and self/occupied-cell rejection");
}
static uint32_t nearby(void) {
    uint32_t h=2166136261u;
    for(int y=10;y<32;y++)for(int z=12;z<24;z++)for(int x=12;x<24;x++)h=(h^(uint32_t)voxel_world_cell(x,y,z))*16777619u;
    return h;
}
static void test_edit_controls(void) {
    fresh();aim(0,-1.3f);geometrium_actions_update(0);GeometriumHit h;CHECK(geometrium_target(&h));
    float x,y,r;geometrium_input_action_geom(ACTION_BREAK,&x,&y,&r);
    uint32_t original=nearby();
    geometrium_input_touch(x,y,0,51);geometrium_input_touch(x,y,4,51);geometrium_actions_update(.3f);CHECK(nearby()==original);
    geometrium_input_touch(x,y,0,52);geometrium_input_touch(x,y,1,52);geometrium_actions_update(.016f);CHECK(voxel_world_cell(h.x,h.y,h.z)==BLOCK_AIR);
    geometrium_input_touch(x,y,0,53);geometrium_key_state("break",1);geometrium_actions_update(.016f);
    geometrium_input_touch(x,y,4,53);CHECK(geometrium_target(&h));geometrium_actions_update(.25f);CHECK(voxel_world_cell(h.x,h.y,h.z)==BLOCK_AIR);
    geometrium_key_state("break",0);original=nearby();geometrium_actions_update(.3f);CHECK(nearby()==original);
    /* A tap of the camera must not release a different finger holding break. */
    geometrium_input_touch(x,y,0,54);geometrium_actions_update(.016f);
    geometrium_input_touch(600,200,0,55);geometrium_input_touch(600,200,1,55);geometrium_actions_update(.016f);
    CHECK(geometrium_target(&h));geometrium_actions_update(.25f);CHECK(voxel_world_cell(h.x,h.y,h.z)==BLOCK_AIR);
    geometrium_input_touch(x,y,1,54);original=nearby();geometrium_actions_update(.3f);CHECK(nearby()==original);
    geometrium_key_state("3",1);CHECK(geometrium_selected()==2);
    geometrium_input_slot_geom(5,&x,&y,&r);geometrium_input_touch(x+r/2,y+r/2,0,81);geometrium_input_touch(x+r/2,y+r/2,1,81);CHECK(geometrium_selected()==5);
    geometrium_key_state("break",1);geometrium_cancel_input();original=nearby();geometrium_actions_update(.3f);CHECK(nearby()==original);
    float jx,jy,jr;geometrium_input_joy_geom(&jx,&jy,&jr);geometrium_input_touch(jx,jy-jr,0,200);
    geometrium_input_touch(600,200,0,201);geometrium_input_touch(650,215,2,201);
    geometrium_input_action_geom(ACTION_PLACE,&x,&y,&r);geometrium_input_touch(x,y,0,202);geometrium_input_touch(x,y,4,202);
    geometrium_input_joy(NULL,&jy);CLOSE(jy,-1);geometrium_cancel_input();
    puts("PASS tap/hold editing, cancelled actions, key/touch ownership, camera-tap independence, hotbar and simultaneous movement/look/place");
}
static void no_overlap(void) {
    float px,py,pz;geometrium_player_pos(&px,&py,&pz);
    for(int y=(int)floorf((py+.001f)*2);y<=(int)floorf((py+GEOMETRIUM_PLAYER_HEIGHT-.001f)*2);y++)
        for(int z=(int)floorf((pz-GEOMETRIUM_PLAYER_RADIUS+.001f)*2);z<=(int)floorf((pz+GEOMETRIUM_PLAYER_RADIUS-.001f)*2);z++)
            for(int x=(int)floorf((px-GEOMETRIUM_PLAYER_RADIUS+.001f)*2);x<=(int)floorf((px+GEOMETRIUM_PLAYER_RADIUS-.001f)*2);x++)CHECK(!voxel_cell_solid(x,y,z));
}
static void test_half_collisions(void) {
    fresh();for(int z=16;z<18;z++)for(int x=16;x<18;x++)CHECK(voxel_world_set(x,25,z,BLOCK_AIR));
    for(int i=0;i<100;i++){geometrium_player_update(.016f);no_overlap();}
    float x,y,z;geometrium_player_pos(&x,&y,&z);CLOSE(y,12.5f);CHECK(geometrium_player_grounded());
    fresh();aim(0,0);
    for(int sy=26;sy<31;sy++)for(int sx=15;sx<20;sx++)if(sx!=17)CHECK(voxel_world_set(sx,sy,18,BLOCK_STONE));
    geometrium_key_state("w",1);for(int i=0;i<30;i++){geometrium_player_update(.016f);no_overlap();}
    geometrium_player_pos(&x,&y,&z);CLOSE(z,8.7f); /* 0.5 opening < 0.6 player diameter */
    for(int sy=26;sy<30;sy++)CHECK(voxel_world_set(16,sy,18,BLOCK_AIR));
    for(int i=0;i<30;i++){geometrium_player_update(.016f);no_overlap();}
    geometrium_player_pos(&x,&y,&z);CHECK(z>10);geometrium_cancel_input();
    puts("PASS standing on a remaining half, accurate half-cell walls and correctly sized editable openings");
}
static void warm(float x,float z) {
    voxel_world_update(x,z);for(int i=0;i<500 && voxel_world_pending();i++)voxel_world_update(x,z);CHECK(!voxel_world_pending());
}
static void test_persistence(const char *directory) {
    fresh();mkdir(directory,0700);app_set_storage(directory);char path[600];CHECK(app_save_path(path,sizeof(path),"world.edits"));remove(path);
    CHECK(!app_save_path(path,sizeof(path),"../escape"));CHECK(app_save_path(path,sizeof(path),"world.edits"));
    for(int i=0;i<600;i++)CHECK(voxel_world_set(i*2-600,100,i%23-11,(i%3)+BLOCK_DIRT));
    CHECK(voxel_edits_count()==600 && voxel_edits_dirty());CHECK(voxel_edits_save());CHECK(!voxel_edits_dirty());
    warm(2048,-2048);warm(-4096,4096);warm(8.5f,8.5f);
    for(int i=0;i<600;i++)CHECK(voxel_world_cell(i*2-600,100,i%23-11)==(i%3)+BLOCK_DIRT);
    voxel_world_build(GEOMETRIUM_WORLD_SEED);CHECK(voxel_edits_count()==600);
    for(int i=0;i<600;i++)CHECK(voxel_world_cell(i*2-600,100,i%23-11)==(i%3)+BLOCK_DIRT);
    voxel_world_build(GEOMETRIUM_WORLD_SEED+1);CHECK(voxel_edits_count()==0);
    voxel_world_build(GEOMETRIUM_WORLD_SEED);CHECK(voxel_edits_count()==600);
    FILE *f=fopen(path,"r+b");CHECK(f);CHECK(!fseek(f,24,SEEK_SET));fputc(0x7f,f);fclose(f);
    voxel_world_build(GEOMETRIUM_WORLD_SEED);CHECK(voxel_edits_count()==0); /* checksum rejects corruption */
    f=fopen(path,"wb");CHECK(f);fwrite("EJVOX01",1,7,f);fclose(f);
    voxel_world_build(GEOMETRIUM_WORLD_SEED);CHECK(voxel_edits_count()==0);
    remove(path);app_set_storage(NULL);
    puts("PASS edit rehashing, distant eviction, atomic disk reload, seed/version/CRC/truncation validation and private save paths");
}
static void put32(unsigned char *p,uint32_t n) {for(int i=0;i<4;i++)p[i]=(unsigned char)(n>>(i*8));}
static void crc(unsigned char *data,size_t n) {
    uint32_t hash=2166136261u;
    for(size_t i=20;i<n;i++)hash=(hash^data[i])*16777619u;
    put32(data+16,hash);
}
static void test_save_migration(const char *directory) {
    fresh();app_set_storage(directory);char path[600];CHECK(app_save_path(path,sizeof(path),"world.edits"));
    unsigned char legacy[40]={0};memcpy(legacy,"EJVOX01\0",8);
    put32(legacy+8,GEOMETRIUM_WORLD_SEED);put32(legacy+12,1);
    put32(legacy+20,(uint32_t)-1);put32(legacy+24,40);put32(legacy+28,0);
    legacy[32]=BLOCK_WATER;legacy[33]=BLOCK_STONE;crc(legacy,sizeof(legacy));
    FILE *f=fopen(path,"wb");CHECK(f);CHECK(fwrite(legacy,1,sizeof(legacy),f)==sizeof(legacy));fclose(f);
    voxel_world_build(GEOMETRIUM_WORLD_SEED);
    CHECK(voxel_edits_count()==1 && voxel_water_level(-2,80,0)==0 && voxel_world_cell(-1,80,0)==BLOCK_STONE);
    CHECK(voxel_world_set(-1,80,0,BLOCK_DIRT));CHECK(voxel_edits_save());
    unsigned char current[48];f=fopen(path,"rb");CHECK(f);CHECK(fread(current,1,sizeof(current),f)==sizeof(current));fclose(f);
    CHECK(!memcmp(current,"EJVOX02\0",8));
    /* Valid checksum, invalid flow attached to a solid: metadata is validated. */
    current[41]=1;crc(current,sizeof(current));
    f=fopen(path,"wb");CHECK(f);CHECK(fwrite(current,1,sizeof(current),f)==sizeof(current));fclose(f);
    voxel_world_build(GEOMETRIUM_WORLD_SEED);CHECK(!voxel_edits_count());
    current[41]=0;current[40]=9;crc(current,sizeof(current));
    f=fopen(path,"wb");CHECK(f);CHECK(fwrite(current,1,sizeof(current),f)==sizeof(current));fclose(f);
    voxel_world_build(GEOMETRIUM_WORLD_SEED);CHECK(!voxel_edits_count());
    remove(path);app_set_storage(NULL);
    puts("PASS legacy EJVOX01 saves upgrade without losing blocks; EJVOX02 rejects invalid fluid metadata even with a valid checksum");
}
int main(int argc,char **argv) {
    CHECK(argc==2);screen_w=960;screen_h=540;dt=1.0/60;
    test_eight_parts();test_ray_and_build();test_edit_controls();test_half_collisions();test_persistence(argv[1]);test_save_migration(argv[1]);return 0;
}
