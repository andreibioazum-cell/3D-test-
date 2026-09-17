/* Native-resolution HUD over the adaptive 3D buffer.
 * Buttons are text-only circles/pills; no pictograms. */
#include "geometrium_internal.h"
#include <stdio.h>
static void centered(const char *s,float x,float y,float scale) {
    text_scaled(s,x-text_width(s)*scale*.5f,y,0xff000000u,scale);
}
void geometrium_hud_draw(void) {
    float u=fminf(screen_w/960.0f,screen_h/540.0f);
    if(u<=0)return;
    float cx,cy,r,jx,jy;geometrium_input_joy_geom(&cx,&cy,&r);geometrium_input_joy(&jx,&jy);
    ring(cx,cy,r,3*u,0xff000000u);
    circle(cx+jx*r*.56f,cy+jy*r*.56f,r*.38f,0xff000000u);
    float x,y,w,h;geometrium_input_flight_geom(&x,&y,&w,&h);
    roundrect(x,y,w,h,7*u,0xffffffffu);centered("Flight",x+w*.5f,y+h*.5f-9*u,.48f*u);
    if (!geometrium_player_flying()) {
        geometrium_input_crouch_geom(&x,&y,&w,&h);
        roundrect(x,y,w,h,7*u,0xffffffffu);centered("Crouch",x+w*.5f,y+h*.5f-9*u,.48f*u);
    }
    char label[32];
    if(rend_fps()<0) snprintf(label,sizeof(label),"FPS —");
    else snprintf(label,sizeof(label),"FPS %.0f",rend_fps());
    float fs=.44f*u,fx=screen_w-18*u-text_width(label)*fs;
    text_scaled(label,fx+u,22*u,0xaa000000u,fs);
    text_scaled(label,fx,21*u,0xffffffffu,fs);
    if (!geometrium_player_flying()) {
        geometrium_input_jump_geom(&cx,&cy,&r);circle(cx,cy,r,0xffffffffu);
        centered("Jump",cx,cy-6*u,.36f*u);
    }
    for (int a=0;a<ACTION_COUNT;a++) {
        geometrium_input_action_geom(a,&cx,&cy,&r);circle(cx,cy,r,0xffffffffu);
        centered(a==ACTION_BREAK ? "Break" : "Place",cx,cy-5*u,.28f*u);
    }
    for (int i=0;i<HOTBAR_SLOTS;i++) {
        float size;geometrium_input_slot_geom(i,&x,&y,&size);
        if(i==geometrium_selected())roundrect(x-2*u,y-2*u,size+4*u,size+4*u,5*u,0xffffffffu);
        roundrect(x,y,size,size,3*u,0xb3222929u);
        image_draw(rend_material_icon(geometrium_slot_block(i)),x+3*u,y+3*u,size-6*u,size-6*u);
        char number[2]={(char)('1'+i),0};
        text_scaled(number,x+16*u,y+40*u,0xffffffffu,.20f*u);
    }
    float mx=screen_w*.5f,my=screen_h*.5f;
    rect(mx-6*u,my-u,12*u,2*u,0xffffffffu);
    rect(mx-u,my-6*u,2*u,12*u,0xffffffffu);
}
