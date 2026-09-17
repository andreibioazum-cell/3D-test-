/* Internal API of the voxel game. Cell coordinates are half-block integers.
 * The shared rasterizer, voxel world, water, light and edits live in the
 * engine (src/engine/render/); this header covers the game layer on top:
 * player, picking, touch layout, HUD and the first-person hand. */
#ifndef GEOMETRIUM_INTERNAL_H
#define GEOMETRIUM_INTERNAL_H
#include "geometrium/geometrium.h"
#include "engine/render/rend_internal.h"
#include "engine/render/voxel_internal.h"

#define GEOMETRIUM_PLAYER_RADIUS .30f
#define GEOMETRIUM_PLAYER_HEIGHT 1.80f
#define GEOMETRIUM_PLAYER_EYE_HEIGHT 1.62f
/* Crouch (crawl): the hitbox flattens to ~1 block tall with a longer
 * profile, so it fits through the one-block openings in the terrain. */
#define GEOMETRIUM_CROUCH_HEIGHT .90f
#define GEOMETRIUM_CROUCH_EYE .45f
#define GEOMETRIUM_FOG_START 32.0f
#define GEOMETRIUM_FOG_END 64.0f
#define GEOMETRIUM_FAR_Z 80.0f
#define GEOMETRIUM_WORLD_SEED 20260905u

enum { ACTION_BREAK,ACTION_PLACE,ACTION_COUNT,HOTBAR_SLOTS=6 };
typedef struct { int x,y,z,nx,ny,nz,block;float distance; } GeometriumHit;

/* ── player and camera (geometrium_player.c) ── */
void geometrium_player_spawn(void);
void geometrium_player_update(float d);
void geometrium_player_pos(float *x,float *y,float *z);
int geometrium_player_overlaps(int sx,int sy,int sz);
void geometrium_player_jump(int down);
void geometrium_player_crouch_touch(int down);
int  geometrium_player_crouching(void);
float geometrium_player_eye(void);
int geometrium_player_flying(void);
int geometrium_player_grounded(void);
void geometrium_player_toggle_flight(void);
void geometrium_camera_look(float dx,float dy);
void geometrium_camera_angles(float *yaw,float *pitch);
void geometrium_key_state(const char *name,int down);
void geometrium_key_reset(void);

/* ── picking and block editing (geometrium_interact.c) ── */
int geometrium_raycast(float x,float y,float z,float dx,float dy,float dz,float reach,GeometriumHit *hit);
void geometrium_actions_reset(void);
void geometrium_action_hold(int action,int down,int source); /* 0 keyboard, 1 touch */
void geometrium_action_cancel(int action,int source);
void geometrium_action_pulse(int action);
void geometrium_actions_update(float d);
int geometrium_action_apply(int action,const GeometriumHit *hit);
int geometrium_target(GeometriumHit *hit);
void geometrium_target_draw(void);
void geometrium_select(int index);
int geometrium_selected(void);
int geometrium_slot_block(int index);

/* ── touch layout (geometrium_input.c) ── */
void geometrium_input_layout(void);
void geometrium_input_reset(void);
void geometrium_input_joy(float *x,float *y);
void geometrium_input_joy_geom(float *x,float *y,float *r);
void geometrium_input_jump_geom(float *x,float *y,float *r);
void geometrium_input_flight_geom(float *x,float *y,float *w,float *h);
void geometrium_input_crouch_geom(float *x,float *y,float *w,float *h);
void geometrium_input_action_geom(int action,float *x,float *y,float *r);
void geometrium_input_slot_geom(int index,float *x,float *y,float *size);
void geometrium_input_touch(float x,float y,int action,int pointer_id);

/* ── presentation (geometrium_scene.c, geometrium_hud.c, geometrium_hand.c) ── */
void geometrium_scene_draw(Buffer *buffer);
void geometrium_hud_draw(void);
void geometrium_hand_reset(void);
void geometrium_hand_update(float d);
void geometrium_hand_swing(void);
void geometrium_hand_equip(void);
void geometrium_hand_draw(void);
#endif
