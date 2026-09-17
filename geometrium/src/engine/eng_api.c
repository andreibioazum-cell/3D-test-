/* Small runtime services exposed to C scripts. */
#include "eng_internal.h"
#include "eng_api.h"
#include "engine/render/rend_internal.h"
#include <string.h>

/* Engine bootstrap: project file IO and the textures voxel meshes sample. */
int eng_init(AAssetManager *assets) {
    eng_fs_set_assets(assets);
    if (!rend_materials_load(assets)) {
        app_fail("Engine: could not load materials");
        return 0;
    }
    eng_physics_init();
    eng_input_reset();
    return 1;
}

/* Material names shared by scene files and scripts; 0 is air. */
int eng_block_from_name(const char *name) {
    if (!name) return BLOCK_AIR;
    if (!strcmp(name, "grass")) return BLOCK_GRASS;
    if (!strcmp(name, "dirt")) return BLOCK_DIRT;
    if (!strcmp(name, "stone")) return BLOCK_STONE;
    if (!strcmp(name, "sand")) return BLOCK_SAND;
    if (!strcmp(name, "water")) return BLOCK_WATER;
    if (!strcmp(name, "log")) return BLOCK_LOG;
    if (!strcmp(name, "leaves")) return BLOCK_LEAVES;
    return BLOCK_AIR;
}

int eng_voxel_get_cell(int x, int y, int z) { return voxel_world_cell(x, y, z); }

int eng_voxel_set_cell(int x, int y, int z, const char *material) {
    return voxel_world_set(x, y, z, eng_block_from_name(material));
}

static double time_now, time_delta, time_origin = -1;

void eng_print(const char *message) { app_log("%s", message ? message : ""); }

/* Load-on-demand so project scripts can just name an effect under
 * assets/sounds/; the host preview stubs snd_* to no-ops. */
void eng_play_sound(const char *name) {
    if (!name || !name[0]) return;
    snd_load(name);
    snd_play(name);
}

void eng_time_internal(double now, double dt) {
    if (time_origin < 0) time_origin = now;
    time_now = now - time_origin;
    time_delta = dt;
}
double eng_time(void) { return time_now; }
float eng_delta(void) { return (float)time_delta; }
