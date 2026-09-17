/* Regressions for walking, toggleable flight, multitouch and the clean HUD. */
#include "geometrium/geometrium_internal.h"
#include "geometrium/geometrium_render_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x); exit(1); } } while(0)
#define CLOSE(a,b) CHECK(fabsf((a)-(b)) < .0004f)
static float scene_x,scene_y,scene_z,scene_yaw,scene_pitch;
static int scene_scale,drawn_faces,jump_labels,flight_labels,crouch_labels,joy_rings,knobs,backgrounds,cross_rects;

int rend_materials_load(AAssetManager *assets) {(void)assets;return 1;}
const Image *rend_material_icon(int block) {static Image image;CHECK(block>0 && block<BLOCK_COUNT);return &image;}
static int images,fps_labels,action_labels;
void image_draw(const Image *image,float x,float y,float w,float h) {CHECK(image && x>0 && y>0 && w>0 && w==h);images++;}
void rend3d_fog(float start,float end) {CHECK(start>=0 && end>start && end<=GEOMETRIUM_FOG_END);}
void rend3d_segment(float x,float y,float z,float a,float b,float c,uint32_t color) {
    CHECK(isfinite(x+y+z+a+b+c) && color==0xffffffffu);
}
int rend3d_begin(Buffer *b,int sc,float x,float y,float z,float yaw,float pitch,float fov) {
    (void)b; CHECK(fov==66); scene_scale=sc; scene_x=x; scene_y=y; scene_z=z; scene_yaw=yaw; scene_pitch=pitch; return 1;
}
void rend3d_sky(uint32_t a,uint32_t b) { (void)a; (void)b; }
void rend3d_end(void) {}
int rend3d_visible(float x,float y,float z,float hx,float hy,float hz) { (void)x;(void)y;(void)z;(void)hx;(void)hy;(void)hz;return 1; }
void rend3d_surface(int x,int y,int z,int u,int v,int face,int block,const unsigned char light[4]) {
    (void)x;(void)y;(void)z; CHECK(u>0 && v>0); CHECK(face>=0&&face<6&&block>0&&block<BLOCK_COUNT);
    CHECK(light);drawn_faces++;
}
/* Minimal software-renderer stubs for the first-person hand. */
void rend3d_polygon(const RendVertex *w,int n,float nx,float ny,float nz,uint32_t color,RendMaterial *material,const unsigned char *light) {
    (void)nx;(void)ny;(void)nz;(void)color;(void)material;(void)light;
    CHECK(w && n>=3 && n<=8);
}
int rend3d_project(float x,float y,float z,float *sx,float *sy) {
    CHECK(isfinite(x+y+z) && sx && sy); *sx=screen_w*.5f; *sy=screen_h*.5f; return 1;
}
void rend3d_viewmodel(int enabled) {CHECK(enabled==0 || enabled==1);}
int rend3d_face_visible(int face,float plane) {(void)face;(void)plane;return 1;}
void rend3d_depth_clear(float x0,float y0,float x1,float y1) {
    CHECK(isfinite(x0+y0+x1+y1));
}
RendMaterial *rend_material(int block,int face) {
    CHECK(block>0 && block<BLOCK_COUNT && face>=0 && face<6);
    return (RendMaterial *)1; /* the hand never dereferences the material in these tests */
}
void rect(float x,float y,float w,float h,uint32_t c) {
    CHECK(c==0xFFFFFFFFu); CHECK(w<=20 && h<=20); CHECK(fabsf(x-screen_w*.5f)<20 && fabsf(y-screen_h*.5f)<20); cross_rects++;
}
void roundrect(float x,float y,float w,float h,float r,uint32_t c) {
    float bx,by,bw,bh;
    geometrium_input_flight_geom(&bx,&by,&bw,&bh);
    if(fabsf(x-bx)<.01f && fabsf(y-by)<.01f) {CLOSE(w,bw);CLOSE(h,bh);CHECK(r>0&&c==0xffffffffu);return;}
    geometrium_input_crouch_geom(&bx,&by,&bw,&bh);
    if(fabsf(x-bx)<.01f && fabsf(y-by)<.01f) {CLOSE(w,bw);CLOSE(h,bh);CHECK(r>0&&c==0xffffffffu);return;}
    CHECK(x>screen_w*.2f && y>screen_h*.75f && w==h && r>0 && (c==0xffffffffu || c==0xb3222929u));
}
void ring(float x,float y,float r,float th,uint32_t c) {
    float jx,jy,jr; geometrium_input_joy_geom(&jx,&jy,&jr);
    CHECK(c==0xFF000000u);
    if (fabsf(x-jx)<.01f && fabsf(y-jy)<.01f) { CLOSE(r,jr); CHECK(th>=2 && th<=6); joy_rings++; }
}
void circle(float x,float y,float r,uint32_t c) {
    float jr; geometrium_input_joy_geom(NULL,NULL,&jr); (void)x;(void)y;
    if (fabsf(r-jr)<.001f) backgrounds++;
    if (fabsf(r-jr*.38f)<.001f) { CHECK(c==0xFF000000u); knobs++; }
    else {
        CHECK(c==0xffffffffu);
        float bx,by,br;geometrium_input_jump_geom(&bx,&by,&br);
        if(fabsf(x-bx)<.01f && fabsf(y-by)<.01f) {CLOSE(r,br);CHECK(!geometrium_player_flying());}
    }
}
void line(float x,float y,float x2,float y2,float th,uint32_t c) { (void)x;(void)y;(void)x2;(void)y2;(void)th; CHECK(c==0xFF000000u); }
int text_width(const char *s) { return (int)strlen(s)*12; }
void text_scaled(const char *s,float x,float y,uint32_t c,float scale) {
    CHECK(scale>0);
    if(!strncmp(s,"FPS ",4)) {
        CHECK(x>screen_w*.8f && y<screen_h*.15f && (c==0xffffffffu || c==0xaa000000u));
        if(c==0xffffffffu)fps_labels++;
    } else if(s[0]>='1' && s[0]<='6' && !s[1]) CHECK(c==0xffffffffu && y>screen_h*.9f);
    else {
        CHECK(c==0xff000000u);
        if(!strcmp(s,"Flight"))flight_labels++;
        else if(!strcmp(s,"Crouch"))crouch_labels++;
        else if(!strcmp(s,"Jump"))jump_labels++;
        else if(!strcmp(s,"Break") || !strcmp(s,"Place"))action_labels++;
        else CHECK(!"Unexpected HUD text");
    }
}

typedef struct { float x,y,z; } Pos;
static Pos pos(void) { Pos p; geometrium_player_pos(&p.x,&p.y,&p.z); return p; }
static float pos_dist(Pos a,Pos b) { return sqrtf((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z)); }
static void fresh(void) { geometrium_cancel_input(); geometrium_player_spawn(); geometrium_input_layout(); geometrium_player_update(.016f); }
static void aim(float yaw,float pitch) {
    float y,p; geometrium_camera_angles(&y,&p); float size=fminf(screen_w,screen_h);
    geometrium_camera_look((yaw-y)*size/2.7f,(p-pitch)*size/2.4f);
}
static void tap_flight(int id) {
    float x,y,w,h; geometrium_input_flight_geom(&x,&y,&w,&h);
    geometrium_input_touch(x+w*.5f,y+h*.5f,0,id); geometrium_input_touch(x+w*.5f,y+h*.5f,1,id);
}
static void test_walk_jump(void) {
    fresh(); CHECK(!geometrium_player_flying()&&geometrium_player_grounded());
    Pos start=pos(); aim(0,.9f); geometrium_key("w",1); geometrium_player_update(.05f); geometrium_key("w",0);
    CLOSE(pos().y,start.y); CLOSE(pos().z-start.z,4.6f*.05f);
    fresh(); start=pos(); float peak=start.y;
    geometrium_player_jump(1);
    for (int i=0;i<150;i++) { geometrium_player_update(.016f); if(pos().y>peak)peak=pos().y; }
    CHECK(peak>start.y+1.0f&&peak<start.y+1.5f);
    CLOSE(pos().y,start.y); CHECK(geometrium_player_grounded());
    geometrium_player_jump(0); geometrium_player_jump(1); geometrium_player_update(.05f); CHECK(pos().y>start.y);
    geometrium_player_jump(0);
    puts("PASS walk mode ignores camera pitch, has gravity and a grounded single jump");
}
static void test_toggle_and_flight(void) {
    fresh(); tap_flight(1001); CHECK(geometrium_player_flying());
    Pos start=pos(); for(int i=0;i<60;i++)geometrium_player_update(.016f); CLOSE(pos_dist(start,pos()),0);
    float cx,cy,r,yaw,pitch; geometrium_input_joy_geom(&cx,&cy,&r);
    geometrium_input_touch(cx,cy,0,37); geometrium_input_touch(cx,cy-r,2,37);
    geometrium_input_touch(600,220,0,809); geometrium_input_touch(630,130,2,809);
    geometrium_camera_angles(&yaw,&pitch); CHECK(pitch>0);
    geometrium_player_update(.05f); Pos end=pos();
    CLOSE(end.x-start.x,sinf(yaw)*cosf(pitch)*8*.05f);
    CLOSE(end.y-start.y,sinf(pitch)*8*.05f);
    CLOSE(end.z-start.z,cosf(yaw)*cosf(pitch)*8*.05f);
    float jy; geometrium_input_touch(630,130,1,809); geometrium_input_joy(NULL,&jy); CLOSE(jy,-1);
    geometrium_input_touch(cx,cy-r,1,37);
    start=pos(); for(int i=0;i<10;i++)geometrium_player_update(.05f); CLOSE(pos_dist(start,pos()),0);
    /* Turning flight off mid-air restores falling, not a hidden hover. */
    tap_flight(2001); CHECK(!geometrium_player_flying());
    geometrium_player_update(.05f); CHECK(pos().y<start.y);
    geometrium_key("f",1); CHECK(geometrium_player_flying());
    geometrium_key("f",1); CHECK(geometrium_player_flying());
    geometrium_key("f",0); geometrium_key("f",1); CHECK(!geometrium_player_flying()); geometrium_key("f",0);
    puts("PASS white-button toggle, keyboard debounce, two-finger flight, hover and gravity restoration");
}
static void test_touch_lifetimes(void) {
    fresh();
    float jx,jy,jr,bx,by,bw,bh,cx,cy,cr;
    geometrium_input_joy_geom(&jx,&jy,&jr); geometrium_input_jump_geom(&cx,&cy,&cr); geometrium_input_flight_geom(&bx,&by,&bw,&bh);
    geometrium_input_touch(cx,cy,0,81); geometrium_player_update(.016f); CHECK(pos().y>13);
    geometrium_input_touch(jx,jy-jr,0,71); geometrium_input_touch(600,200,0,91);
    tap_flight(111); CHECK(geometrium_player_flying());
    geometrium_input_touch(cx,cy,1,81); /* the finger of a vanished jump button must not release the stick */
    geometrium_input_joy(NULL,&jy); CLOSE(jy,-1);
    geometrium_input_touch(0,0,3,-1); geometrium_input_joy(&jx,&jy); CLOSE(jx,0); CLOSE(jy,0);
    CHECK(geometrium_player_flying());
    geometrium_input_touch(bx+10,by+10,0,45); geometrium_input_touch(bx-50,by+100,2,45); geometrium_input_touch(bx+10,by+10,1,45);
    CHECK(geometrium_player_flying()); /* dragging on the button is not a tap */
    geometrium_input_touch(bx+10,by+10,0,45); geometrium_input_touch(bx+10,by+10,4,45);
    CHECK(geometrium_player_flying()); /* pointercancel must not press the button */
    geometrium_key("w",1); geometrium_key("space",1); geometrium_cancel_input();
    Pos start=pos(); geometrium_player_update(.05f); CLOSE(pos_dist(start,pos()),0);
    geometrium_input_joy_geom(&jx,&jy,&jr); geometrium_input_touch(jx,jy-jr,0,301);
    screen_w=1280; screen_h=720; geometrium_input_layout(); geometrium_input_joy(&jx,&jy); CLOSE(jx,0); CLOSE(jy,0);
    screen_w=960; screen_h=540; geometrium_input_layout();
    puts("PASS simultaneous jump/look/move, toggle while held, cancelled taps, blur and resize reset");
}
static void no_overlap(void) {
    Pos p=pos(); float r=GEOMETRIUM_PLAYER_RADIUS,h=geometrium_player_crouching()?GEOMETRIUM_CROUCH_HEIGHT:GEOMETRIUM_PLAYER_HEIGHT;
    for(int y=(int)floorf((p.y+.001f)*2);y<=(int)floorf((p.y+h-.001f)*2);y++)
        for(int z=(int)floorf((p.z-r+.001f)*2);z<=(int)floorf((p.z+r-.001f)*2);z++)
            for(int x=(int)floorf((p.x-r+.001f)*2);x<=(int)floorf((p.x+r-.001f)*2);x++) CHECK(!voxel_cell_solid(x,y,z));
}
static void test_voxel_collisions(void) {
    for(int direction=0;direction<8;direction++) {
        fresh(); aim(direction*.785398f,0); geometrium_key("w",1);
        for(int i=0;i<150;i++) { geometrium_player_update(.016f); no_overlap(); }
    }
    fresh(); tap_flight(5); geometrium_key("space",1);
    for(int i=0;i<120;i++) { geometrium_player_update(.016f); no_overlap(); }
    geometrium_key("space",0); geometrium_key("Shift",1);
    for(int i=0;i<200;i++) { geometrium_player_update(.016f); no_overlap(); }
    CHECK(pos().y>=13-.001f);
    geometrium_cancel_input();
    puts("PASS voxel walls, terrain steps and vertical flight never penetrate solid blocks");
}

static void test_crouch(void) {
    fresh();
    float x0,y0,z0; geometrium_player_pos(&x0,&y0,&z0);
    int fy0=(int)floorf(y0*2); /* first air cell above the feet (floor top) */
    /* Build a room: solid floor (top = fy0), then a one-block (1.0 unit)
     * clearance, then a solid ceiling from z=10 to z=20. Open sky at the start. */
    for (int sy=fy0-10;sy<=fy0+12;sy++)
      for (int sz=16;sz<=44;sz++)
        for (int sx=12;sx<=24;sx++) voxel_world_set(sx,sy,sz,BLOCK_AIR);
    for (int sy=fy0-8;sy<fy0;sy++)
      for (int sz=16;sz<=44;sz++)
        for (int sx=12;sx<=24;sx++) voxel_world_set(sx,sy,sz,BLOCK_STONE);
    for (int sy=fy0+2;sy<=fy0+10;sy++)
      for (int sz=20;sz<=40;sz++)
        for (int sx=12;sx<=24;sx++) voxel_world_set(sx,sy,sz,BLOCK_STONE);
    aim(0,-0.1f); /* face +z into the tunnel */
    /* Crawl is slower than walking, measured in the open start area. */
    {
        Pos a=pos(); geometrium_key("w",1);
        for (int i=0;i<3;i++) geometrium_player_update(.016f);
        float dz_walk=pos().z-a.z;
        geometrium_key("w",0);
        Pos b=pos(); geometrium_key("c",1); geometrium_player_update(.016f);
        geometrium_key("w",1);
        for (int i=0;i<3;i++) geometrium_player_update(.016f);
        float dz_crouch=pos().z-b.z;
        geometrium_key("w",0);
        CHECK(dz_walk>0 && dz_crouch>0 && dz_crouch<dz_walk);
    }
    /* Standing: the one-block ceiling blocks forward movement. */
    geometrium_key("c",0);
    for (int i=0;i<30;i++) geometrium_player_update(.016f); /* stand back up in the open */
    CHECK(!geometrium_player_crouching());
    geometrium_key("w",1);
    for (int i=0;i<200;i++) geometrium_player_update(.016f);
    CHECK(pos().z<9.95f);
    CHECK(!geometrium_player_crouching());
    /* Crouch and crawl into the one-block tunnel. */
    geometrium_key("c",1); geometrium_player_update(.016f);
    CHECK(geometrium_player_crouching());
    CLOSE(geometrium_player_eye(),GEOMETRIUM_CROUCH_EYE);
    for (int i=0;i<200 && pos().z<10.6f;i++) geometrium_player_update(.016f);
    CHECK(pos().z>10.6f);
    CHECK(geometrium_player_crouching());
    Pos under=pos();
    drawn_faces=0; geometrium_scene_draw(NULL);
    CLOSE(scene_y,under.y+GEOMETRIUM_CROUCH_EYE);
    /* Releasing under the ceiling keeps the crawl box (cannot stand up). */
    geometrium_key("c",0);
    for (int i=0;i<60;i++) geometrium_player_update(.016f);
    CHECK(geometrium_player_crouching());
    CLOSE(geometrium_player_eye(),GEOMETRIUM_CROUCH_EYE);
    no_overlap();
    /* No jump while crouched: the head never rises off the floor. */
    float peak=pos().y;
    geometrium_key("space",1);
    for (int i=0;i<40;i++) { geometrium_player_update(.016f); if(pos().y>peak)peak=pos().y; }
    geometrium_key("space",0);
    CHECK(peak<under.y+0.35f);
    /* Walk out past the ceiling, then stand up in the open. */
    geometrium_key("c",1); geometrium_key("w",1);
    for (int i=0;i<600 && pos().z<21.0f;i++) geometrium_player_update(.016f);
    CHECK(pos().z>21.0f);
    geometrium_key("c",0); geometrium_key("w",0);
    for (int i=0;i<60;i++) geometrium_player_update(.016f);
    CHECK(!geometrium_player_crouching());
    CLOSE(geometrium_player_eye(),GEOMETRIUM_PLAYER_EYE_HEIGHT);
    Pos out=pos(); drawn_faces=0; geometrium_scene_draw(NULL);
    CLOSE(scene_y,out.y+GEOMETRIUM_PLAYER_EYE_HEIGHT);
    /* Rebuild the seeded terrain so later tests see the original world. */
    voxel_world_build(GEOMETRIUM_WORLD_SEED);
    puts("PASS crouch: holds the one-block crawl box, slow crawl, no jump, drop camera, stand on exit");
}
static void test_clean_hud_and_camera(void) {
    fresh();
    joy_rings=knobs=backgrounds=cross_rects=flight_labels=jump_labels=crouch_labels=0;
    geometrium_hud_draw();
    CHECK(images==6 && fps_labels==1 && action_labels==2);
    CHECK(joy_rings==1&&knobs==1&&backgrounds==0&&cross_rects==2&&flight_labels==1&&jump_labels==1&&crouch_labels==1);
    tap_flight(100); geometrium_hud_draw(); CHECK(jump_labels==1&&flight_labels==2&&crouch_labels==1);
    tap_flight(100); geometrium_hud_draw(); CHECK(jump_labels==2&&flight_labels==3&&crouch_labels==2);
    Pos p=pos(); float yaw,pitch; geometrium_camera_angles(&yaw,&pitch);
    drawn_faces=0; geometrium_scene_draw(NULL); CHECK(drawn_faces>0&&scene_scale==1);
    CLOSE(scene_x,p.x); CLOSE(scene_y,p.y+GEOMETRIUM_PLAYER_EYE_HEIGHT); CLOSE(scene_z,p.z);
    CLOSE(scene_yaw,yaw); CLOSE(scene_pitch,pitch);
    screen_w=2400; screen_h=1080; geometrium_input_layout(); geometrium_scene_draw(NULL); CHECK(scene_scale==2);
    const int sizes[][2]={{960,540},{2400,1080},{640,360},{800,600}};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);i++) {
        screen_w=sizes[i][0]; screen_h=sizes[i][1]; geometrium_input_layout();
        float x,y,r; geometrium_input_joy_geom(&x,&y,&r); CHECK(x-r>0&&x+r<screen_w*.5f&&y+r<screen_h);
        float w,h; geometrium_input_flight_geom(&x,&y,&w,&h); CHECK(x>screen_w*.5f&&y>0&&x+w<screen_w&&y+h<screen_h*.5f);
        geometrium_input_crouch_geom(&x,&y,&w,&h);
        CHECK(x>screen_w*.5f&&y>0&&x+w<screen_w&&y+h<screen_h*.5f);
    }
    geometrium_key("w",1); geometrium_game_reset(); CHECK(!geometrium_player_flying());
    dt=-1; p=pos(); geometrium_game_update(); CLOSE(pos_dist(p,pos()),0);
    puts("PASS clean HUD: transparent black joystick, thick outer ring, white crosshair, conditional jump and eye camera");
}
static void test_fps_and_quality(void) {
    rend_perf_reset();for(int i=0;i<60;i++)rend_perf_frame(1.0/60);CLOSE(rend_fps(),60);
    rend_perf_reset();dt=.1;for(int i=0;i<10;i++)geometrium_game_update();CLOSE(rend_fps(),10); /* NOT clamped physics 20 FPS */
    rend_perf_frame(NAN);rend_perf_frame(-1);CLOSE(rend_fps(),10);
    rend_perf_reset();CHECK(rend_render_scale(960,540)==1);
    for(int i=0;i<30;i++)rend_render_time(.03);
    CHECK(rend_render_scale(960,540)==2);
    for(int i=0;i<120;i++)rend_render_time(.003);
    CHECK(rend_render_scale(960,540)==1);
    CHECK(rend_render_scale(2400,1080)==2);
    puts("PASS genuine wall-clock FPS, invalid interval guard, adaptive resolution with hysteresis");
}
int main(void) {
    screen_w=960;screen_h=540;dt=1.0/60;geometrium_game_init(NULL);
    test_walk_jump(); test_toggle_and_flight(); test_touch_lifetimes(); test_voxel_collisions(); test_crouch(); test_clean_hud_and_camera();test_fps_and_quality();
    return 0;
}
