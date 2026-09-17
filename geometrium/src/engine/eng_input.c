/* Engine input: a small polled state table for C scripts.
 *
 * The host (preview server / future mobile glue) feeds keys and pointer
 * events through eng_input_feed_key / eng_input_feed_pointer; scripts read
 * them every frame with eng_input_is_pressed / eng_input_pointer. This keeps
 * the engine decoupled from any particular input backend, the same way the
 * graphics API is decoupled from the rasterizer.
 */
#include "eng_internal.h"
#include <string.h>

#define ENG_NUM_KEYS 128

static unsigned char key_state[ENG_NUM_KEYS];
static int pointer_down;
static float pointer_x, pointer_y;
static int pointer_seen;

/* Map an arbitrary integer code (ASCII or ENG_KEY_* enum) to a dense slot. */
static int slot_for(int key) {
    if (key >= ' ' && key <= '~') return key - ' ' + 0;      /* 0..94 */
    if (key >= ENG_KEY_LEFT && key <= ENG_KEY_SPACE) return 95 + (key - ENG_KEY_LEFT);
    return -1;
}

void eng_input_reset(void) {
    memset(key_state, 0, sizeof(key_state));
    pointer_down = 0;
    pointer_seen = 0;
}

void eng_input_feed_key(int key, int down) {
    int s = slot_for(key);
    if (s >= 0 && s < ENG_NUM_KEYS) key_state[s] = down ? 1 : 0;
}

void eng_input_feed_pointer(float x, float y, int down) {
    pointer_x = x; pointer_y = y; pointer_down = down ? 1 : 0;
    pointer_seen = 1;
}

int eng_input_is_pressed(int key) {
    int s = slot_for(key);
    return s >= 0 && s < ENG_NUM_KEYS ? key_state[s] : 0;
}

int eng_input_pointer(float *x, float *y, int *down) {
    if (x) *x = pointer_x;
    if (y) *y = pointer_y;
    if (down) *down = pointer_down;
    return pointer_seen;
}
