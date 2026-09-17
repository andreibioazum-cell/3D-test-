/* Voxel walking/jumping plus a separately toggleable first-person flight. */
#include "geometrium_internal.h"
#include <math.h>
#include <string.h>

#define WALK_SPEED 4.6f
#define FLY_SPEED 8.0f
#define GRAVITY 22.0f
#define JUMP_SPEED 7.5f
#define PITCH_LIMIT 1.45f
#define TAU 6.28318530718f

static float px, py, pz, pvy, cyaw, cpitch;
static int flying, grounded, touch_jump, jump_latch, crouching;
static int k_w, k_a, k_s, k_d, k_space, k_sink, k_f, k_crouch;
static int k_left, k_right, k_up, k_down, touch_crouch;

/* Effective hitbox height: a full standing body, or the flattened ~1 block
 * crawl box while crouching (fits the one-block gaps in the terrain). */
static float player_height(void) {
    return crouching ? GEOMETRIUM_CROUCH_HEIGHT : GEOMETRIUM_PLAYER_HEIGHT;
}
float geometrium_player_eye(void) {
    return crouching ? GEOMETRIUM_CROUCH_EYE : GEOMETRIUM_PLAYER_EYE_HEIGHT;
}
int geometrium_player_crouching(void) { return crouching; }

void geometrium_player_spawn(void) {
    px = 8.5f; pz = 8.5f;
    py = 1.002f;
    for (int y=WORLD_HEIGHT*2-1;y>=0;y--) {
        int occupied=0;
        for (int z=16;z<=17;z++) for (int x=16;x<=17;x++) occupied |= voxel_cell_solid(x,y,z);
        if (occupied) { py=(y+1)*.5f+.002f;break; }
    }
    pvy = 0; flying = 0; grounded = 1; touch_jump = jump_latch = 0; crouching = 0;
    cyaw = .7f; cpitch = -.16f;
}
void geometrium_player_pos(float *x, float *y, float *z) {
    if (x) *x = px;
    if (y) *y = py;
    if (z) *z = pz;
}
int geometrium_player_flying(void) { return flying; }
int geometrium_player_grounded(void) { return grounded; }
void geometrium_player_toggle_flight(void) {
    flying = !flying;
    pvy = 0;
    grounded = 0;
    touch_jump = 0;
    jump_latch = k_space; /* holding jump must not become a fresh jump */
}
void geometrium_player_jump(int down) {
    touch_jump = down && !flying;
    if (!touch_jump && !k_space) jump_latch = 0;
}
/* The Crouch button (hold): flatten the hitbox to crawl through low gaps. */
void geometrium_player_crouch_touch(int down) { touch_crouch = down != 0; }
/* A standing-tall box must fit on the spot before the player stands up. */
static int can_stand(void) {
    const float r=GEOMETRIUM_PLAYER_RADIUS,h=GEOMETRIUM_PLAYER_HEIGHT,eps=.0001f;
    int x0=(int)floorf((px-r+eps)*2),x1=(int)floorf((px+r-eps)*2);
    int y0=(int)floorf((py+eps)*2),y1=(int)floorf((py+h-eps)*2);
    int z0=(int)floorf((pz-r+eps)*2),z1=(int)floorf((pz+r-eps)*2);
    for (int y=y0;y<=y1;y++) for (int z=z0;z<=z1;z++) for (int x=x0;x<=x1;x++)
        if (voxel_cell_solid(x,y,z)) return 0;
    return 1;
}
void geometrium_camera_angles(float *yaw, float *pitch) {
    if (yaw) *yaw = cyaw;
    if (pitch) *pitch = cpitch;
}
static void clamp_angles(void) {
    cyaw = remainderf(cyaw, TAU);
    cpitch = fmaxf(-PITCH_LIMIT, fminf(PITCH_LIMIT, cpitch));
}
void geometrium_camera_look(float dx, float dy) {
    if (screen_w <= 0 || screen_h <= 0 || !isfinite(dx + dy)) return;
    float size = fminf((float)screen_w, (float)screen_h);
    cyaw += dx / size * 2.7f;
    cpitch -= dy / size * 2.4f;
    clamp_angles();
}
void geometrium_key_reset(void) {
    k_w = k_a = k_s = k_d = k_space = k_sink = k_f = 0;
    k_crouch = touch_crouch = 0;
    k_left = k_right = k_up = k_down = jump_latch = 0;
}
void geometrium_key_state(const char *name, int down) {
    if (!name) return;
    int d = down != 0;
    if (!strcmp(name,"w") || !strcmp(name,"W")) k_w = d;
    else if (!strcmp(name,"a") || !strcmp(name,"A")) k_a = d;
    else if (!strcmp(name,"s") || !strcmp(name,"S")) k_s = d;
    else if (!strcmp(name,"d") || !strcmp(name,"D")) k_d = d;
    else if (!strcmp(name,"space") || !strcmp(name," ")) { k_space = d; if (!d && !touch_jump) jump_latch = 0; }
    else if (!strcmp(name,"Shift")) k_sink = d;
    else if (!strcmp(name,"ArrowLeft")) k_left = d;
    else if (!strcmp(name,"ArrowRight")) k_right = d;
    else if (!strcmp(name,"ArrowUp")) k_up = d;
    else if (!strcmp(name,"ArrowDown")) k_down = d;
    else if (!strcmp(name,"break") || !strcmp(name,"e") || !strcmp(name,"E")) geometrium_action_hold(ACTION_BREAK,d,0);
    else if (!strcmp(name,"place") || !strcmp(name,"r") || !strcmp(name,"R")) geometrium_action_hold(ACTION_PLACE,d,0);
    else if (name[0]>='1' && name[0]<='6' && !name[1]) { if(d)geometrium_select(name[0]-'1'); }
    else if (!strcmp(name,"f") || !strcmp(name,"F")) { if (d && !k_f) geometrium_player_toggle_flight(); k_f = d; }
    else if (!strcmp(name,"c") || !strcmp(name,"C")) k_crouch = d;
}
int geometrium_player_overlaps(int sx,int sy,int sz) {
    const float r=GEOMETRIUM_PLAYER_RADIUS,h=player_height(),eps=.0001f;
    float x=sx*.5f,y=sy*.5f,z=sz*.5f;
    return px+r>x+eps && px-r<x+.5f-eps && py+h>y+eps && py<y+.5f-eps &&
           pz+r>z+eps && pz-r<z+.5f-eps;
}
static void move_axis(int axis,float delta) {
    if (delta==0) return;
    float *pos=axis==0 ? &px : axis==1 ? &py : &pz;
    *pos+=delta;
    const float r=GEOMETRIUM_PLAYER_RADIUS,h=player_height(),eps=.0001f;
    int x0=(int)floorf((px-r+eps)*2),x1=(int)floorf((px+r-eps)*2);
    int y0=(int)floorf((py+eps)*2),y1=(int)floorf((py+h-eps)*2);
    int z0=(int)floorf((pz-r+eps)*2),z1=(int)floorf((pz+r-eps)*2);
    for (int y=y0;y<=y1;y++) for (int z=z0;z<=z1;z++) for (int x=x0;x<=x1;x++) {
        if (!voxel_cell_solid(x,y,z) || !geometrium_player_overlaps(x,y,z)) continue;
        if (axis==0) px=delta>0 ? x*.5f-r : (x+1)*.5f+r;
        else if (axis==2) pz=delta>0 ? z*.5f-r : (z+1)*.5f+r;
        else {
            py=delta>0 ? y*.5f-h : (y+1)*.5f;
            if (delta<0) grounded=1;
            pvy=0;
        }
    }
}
void geometrium_player_update(float d) {
    if (!isfinite(d) || d <= 0) return;
    if (d > .05f) d = .05f;
    /* Crouch state machine: the crawl box while the button/key is held, and
     * it is kept until a standing-tall box fits again (a 1-block tunnel
     * cannot be stood up in). */
    int crouch_held = !flying && (k_crouch || touch_crouch);
    if (crouch_held) crouching = 1;
    else if (crouching && can_stand()) crouching = 0;
    cyaw += (k_right-k_left)*1.7f*d; cpitch += (k_up-k_down)*1.4f*d;
    clamp_angles();
    float jx, jy;
    geometrium_input_joy(&jx,&jy);
    float side = jx+k_d-k_a, forward = -jy+k_w-k_s;
    float cp = flying ? cosf(cpitch) : 1;
    float vx = sinf(cyaw)*cp*forward + cosf(cyaw)*side;
    float vz = cosf(cyaw)*cp*forward - sinf(cyaw)*side;
    float vy = flying ? sinf(cpitch)*forward + k_space-k_sink : 0;
    float length = sqrtf(vx*vx + vy*vy + vz*vz);
    if (length > 1) { vx/=length; vy/=length; vz/=length; }
    int water = voxel_world_cell((int)floorf(px*2), (int)floorf((py+.6f)*2), (int)floorf(pz*2)) == BLOCK_WATER;
    float speed = flying ? FLY_SPEED : water ? 2.8f : WALK_SPEED;
    if (crouching && !flying) speed *= .55f;   /* crawling is slower than walking */
    vx *= speed; vz *= speed;
    if (flying) pvy = vy*speed;
    else {
        int jump = (k_space || touch_jump) && !crouching;
        if (jump && !jump_latch && grounded) { pvy=JUMP_SPEED; grounded=0; snd_play("jump.wav"); }
        jump_latch = jump;
        pvy -= (water ? 6 : GRAVITY)*d;
        if (water && jump) pvy = 4.5f; /* swimming, not hovering above the water */
        float terminal = water ? -2.5f : -24;
        if (pvy < terminal) pvy = terminal;
    }
    float fastest = fmaxf(fabsf(pvy), fmaxf(fabsf(vx),fabsf(vz)));
    int steps = (int)ceilf(fastest*d/.20f);
    if (steps < 1) steps = 1;
    float step = d/steps;
    grounded = 0;
    for (int i=0; i<steps; i++) {
        move_axis(0,vx*step); move_axis(2,vz*step); move_axis(1,pvy*step);
    }
    if (py < -8 || !isfinite(px+py+pz)) { geometrium_player_spawn(); geometrium_cancel_input(); }
}
