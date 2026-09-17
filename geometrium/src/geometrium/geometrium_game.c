/* Shared Android / host lifecycle of the voxel playset. No script runtime.
 * The generic game_* entry points live in the app router (src/core/game.c). */
#include "geometrium_internal.h"
static float save_timer;
void geometrium_key(const char *name,int down) {geometrium_key_state(name,down);}
void geometrium_cancel_input(void) {geometrium_input_reset();geometrium_key_reset();geometrium_actions_reset();}
void geometrium_game_save(void) {voxel_edits_save();save_timer=0;}
void geometrium_game_reset(void) {
    if (voxel_edits_dirty() && !voxel_edits_save()) {app_fail("Could not save the world edits");return;}
    geometrium_cancel_input();voxel_world_build(GEOMETRIUM_WORLD_SEED);geometrium_player_spawn();
    rend_perf_reset();geometrium_select(1);save_timer=0;geometrium_input_layout();geometrium_hand_reset();
}
void geometrium_game_init(AAssetManager *assets) {
    if (!rend_materials_load(assets)) return;
    snd_load("jump.wav");snd_load("break.wav");snd_load("place.wav");
    geometrium_game_reset();app_log("Enjoer: PNG voxel world with half-block building");
}
void geometrium_game_update(void) {
    rend_perf_frame(dt);
    float d=(float)dt;
    if (!isfinite(d) || d<0) d=0;
    if (d>.05f) d=.05f;
    geometrium_input_layout();geometrium_player_update(d);geometrium_actions_update(d);geometrium_hand_update(d);
    float x,z;geometrium_player_pos(&x,NULL,&z);voxel_water_update(d);voxel_world_update(x,z);
    /* Debounced atomic autosave; focus loss and shutdown save immediately. */
    if (voxel_edits_dirty()) {
        save_timer+=d;
        if (save_timer>=3) geometrium_game_save();
    } else save_timer=0;
}
void geometrium_game_draw(Buffer *buffer) {geometrium_scene_draw(buffer);geometrium_hud_draw();}
void geometrium_game_touch(float x,float y,int action,int id) {geometrium_input_touch(x,y,action,id);}
