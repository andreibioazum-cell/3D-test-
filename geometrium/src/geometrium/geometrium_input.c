/* Each touch owns just one control; camera, movement and editing coexist. */
#include "geometrium_internal.h"
static int joy_id=-1,look_id=-1,jump_id=-1,flight_id=-1,slot_id=-1,crouch_id=-1;
static int action_id[ACTION_COUNT]={-1,-1},flight_tap;
static float jx,jy,look_x,look_y,flight_start_x,flight_start_y;
static float joy_x,joy_y,joy_r,jump_x,jump_y,jump_r,flight_x,flight_y,flight_w,flight_h,crouch_x,crouch_y,crouch_w,crouch_h,ui_scale;
static int layout_w,layout_h;
void geometrium_input_reset(void) {
    joy_id=look_id=jump_id=flight_id=slot_id=crouch_id=-1;jx=jy=0;flight_tap=0;
    for (int a=0;a<ACTION_COUNT;a++) {action_id[a]=-1;geometrium_action_cancel(a,1);}
    geometrium_player_jump(0);
    geometrium_player_crouch_touch(0);
}
void geometrium_input_layout(void) {
    if (geometrium_player_flying() && jump_id>=0) {jump_id=-1;geometrium_player_jump(0);}
    if (screen_w==layout_w && screen_h==layout_h) return;
    layout_w=screen_w;layout_h=screen_h;geometrium_input_reset();
    ui_scale=fminf(screen_w/960.0f,screen_h/540.0f);
    if (ui_scale<=0) {joy_r=0;return;}
    joy_r=60*ui_scale;joy_x=86*ui_scale;joy_y=screen_h-86*ui_scale;
    jump_r=40*ui_scale;jump_x=screen_w-78*ui_scale;jump_y=screen_h-82*ui_scale;
    flight_w=128*ui_scale;flight_h=44*ui_scale;
    flight_x=screen_w-flight_w-142*ui_scale;flight_y=18*ui_scale;
    crouch_w=flight_w;crouch_h=flight_h;
    crouch_x=flight_x;crouch_y=flight_y+flight_h+10*ui_scale;
}
void geometrium_input_joy(float *x,float *y) {if(x)*x=jx;if(y)*y=jy;}
void geometrium_input_joy_geom(float *x,float *y,float *r) {if(x)*x=joy_x;if(y)*y=joy_y;if(r)*r=joy_r;}
void geometrium_input_jump_geom(float *x,float *y,float *r) {if(x)*x=jump_x;if(y)*y=jump_y;if(r)*r=jump_r;}
void geometrium_input_flight_geom(float *x,float *y,float *w,float *h) {
    if(x)*x=flight_x;
    if(y)*y=flight_y;
    if(w)*w=flight_w;
    if(h)*h=flight_h;
}
void geometrium_input_crouch_geom(float *x,float *y,float *w,float *h) {
    if(x)*x=crouch_x;
    if(y)*y=crouch_y;
    if(w)*w=crouch_w;
    if(h)*h=crouch_h;
}
void geometrium_input_action_geom(int a,float *x,float *y,float *r) {
    if(x)*x=screen_w-(a==ACTION_BREAK ? 264 : 190)*ui_scale;
    if(y)*y=screen_h-76*ui_scale;
    if(r)*r=31*ui_scale;
}
void geometrium_input_slot_geom(int index,float *x,float *y,float *size) {
    if(x)*x=screen_w*.5f+(-129+index*44)*ui_scale;
    if(y)*y=screen_h-55*ui_scale;
    if(size)*size=38*ui_scale;
}
static int hit_circle(float x,float y,float cx,float cy,float r) {
    float dx=x-cx,dy=y-cy;return dx*dx+dy*dy<=r*r;
}
static int hit_flight(float x,float y) {return x>=flight_x && x<=flight_x+flight_w && y>=flight_y && y<=flight_y+flight_h;}
static int hit_crouch(float x,float y) {return x>=crouch_x && x<=crouch_x+crouch_w && y>=crouch_y && y<=crouch_y+crouch_h;}
static void set_joy(float x,float y) {
    float dx=(x-joy_x)/joy_r,dy=(y-joy_y)/joy_r,length=sqrtf(dx*dx+dy*dy);
    if (length<=.10f) {jx=jy=0;return;}
    float strength=(fminf(length,1)-.10f)/.90f;jx=dx/length*strength;jy=dy/length*strength;
}
void geometrium_input_touch(float x,float y,int action,int id) {
    if (action==3) {geometrium_cancel_input();return;}
    if (id<0 || !isfinite(x+y)) return;
    geometrium_input_layout();if(joy_r<=0)return;
    if (action==0) {
        if (id==joy_id || id==look_id || id==jump_id || id==flight_id || id==slot_id || id==crouch_id) return;
        for (int a=0;a<ACTION_COUNT;a++) if (id==action_id[a]) return;
        for (int a=0;a<ACTION_COUNT;a++) {
            float cx,cy,r;geometrium_input_action_geom(a,&cx,&cy,&r);
            if (!hit_circle(x,y,cx,cy,r*1.15f)) continue;
            if (action_id[a]<0) {action_id[a]=id;geometrium_action_hold(a,1,1);}
            return;
        }
        for (int i=0;i<HOTBAR_SLOTS;i++) {
            float sx,sy,size;geometrium_input_slot_geom(i,&sx,&sy,&size);
            if (x>=sx && x<=sx+size && y>=sy && y<=sy+size) {
                if (slot_id<0) {slot_id=id;geometrium_select(i);}return;
            }
        }
        if (hit_flight(x,y)) {
            if (flight_id<0) {flight_id=id;flight_tap=1;flight_start_x=x;flight_start_y=y;}
        } else if (hit_crouch(x,y)) {
            /* Hold to flatten the hitbox into the one-block crawl shape. */
            if (crouch_id<0) {crouch_id=id;geometrium_player_crouch_touch(1);}
        } else if (!geometrium_player_flying() && hit_circle(x,y,jump_x,jump_y,jump_r*1.15f)) {
            if (jump_id<0) {jump_id=id;geometrium_player_jump(1);}
        } else if (x<screen_w*.5f && hit_circle(x,y,joy_x,joy_y,joy_r*1.3f)) {
            if (joy_id<0) {joy_id=id;set_joy(x,y);}
        } else if (x>=screen_w*.5f && look_id<0) {
            look_id=id;look_x=x;look_y=y;
        }
    } else if (action==2) {
        if (id==joy_id) set_joy(x,y);
        else if (id==look_id) {
            geometrium_camera_look(x-look_x,y-look_y);look_x=x;look_y=y;
        } else if (id==flight_id && !hit_circle(x,y,flight_start_x,flight_start_y,12*ui_scale)) flight_tap=0;
        for (int a=0;a<ACTION_COUNT;a++) if (id==action_id[a]) {
            float cx,cy,r;geometrium_input_action_geom(a,&cx,&cy,&r);
            if (hit_circle(x,y,cx,cy,r*1.4f)) geometrium_action_hold(a,1,1);else geometrium_action_cancel(a,1);
        }
    } else if (action==1 || action==4) {
        if (id==joy_id) {joy_id=-1;jx=jy=0;}
        if (id==look_id) {
            look_id=-1;
        }
        if (id==slot_id) slot_id=-1;
        if (id==jump_id) {jump_id=-1;geometrium_player_jump(0);}
        for (int a=0;a<ACTION_COUNT;a++) if (id==action_id[a]) {
            action_id[a]=-1;
            if (action==4) geometrium_action_cancel(a,1);else geometrium_action_hold(a,0,1);
        }
        if (id==flight_id) {
            flight_id=-1;
            if (action==1 && flight_tap && hit_flight(x,y)) {geometrium_player_toggle_flight();jump_id=-1;geometrium_player_jump(0);}
            flight_tap=0;
        }
        if (id==crouch_id) {crouch_id=-1;geometrium_player_crouch_touch(0);}
    }
}
