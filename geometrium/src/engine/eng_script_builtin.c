/* Scripts compiled into the engine binary.
 *
 * A phone has no C compiler and no writable, loadable .so next to the APK, so
 * the C files under a project's scripts/ folder cannot be dlopen'd there.
 * that ship with the sample projects also exist here, by the same name, and
 * are used whenever the compiled version is unavailable. On the PC the
 * own .so wins, so editing a project script file still takes effect.
 */
#include "eng_internal.h"
#include "eng_api.h"
#include <math.h>

/* ── spin: rotate the node around its vertical axis ───────────────────── */
static void spin_process(EngNode *self, float dt) {
    float yaw, pitch, roll;
    eng_node3d_get_rotation(self, &yaw, &pitch, &roll);
    eng_node3d_set_rotation(self, yaw + dt * 0.9f, pitch, roll);
}

/* ── orbit: sweep a camera around the point it was authored to look at ── */
/* Radius, height, tilt and phase all come from the scene file, so one
 * implementation serves every project that names a camera script "orbit". */
typedef struct { float radius, height, pitch, phase; } OrbitState;
static OrbitState orbit_state;

static void orbit_ready(EngNode *self) {
    float x, y, z;
    eng_node3d_get_position(self, &x, &y, &z);
    orbit_state.radius = sqrtf(x * x + z * z);
    if (orbit_state.radius < 1.0f) orbit_state.radius = 9.0f;
    orbit_state.height = y;
    eng_node3d_get_rotation(self, NULL, &orbit_state.pitch, NULL);
    orbit_state.phase = atan2f(x, z);
}

static void orbit_process(EngNode *self, float dt) {
    (void)dt;
    float t = (float)(eng_time() * 0.12) + orbit_state.phase;
    eng_node3d_set_position(self, sinf(t) * orbit_state.radius, orbit_state.height,
                            cosf(t) * orbit_state.radius);
    /* look back at the centre: forward is (sin yaw, 0, cos yaw) */
    eng_node3d_set_rotation(self, t + 3.14159265f, orbit_state.pitch, 0);
}

/* ── roller: drive a RigidBody3D ball with the engine input API ───────── */
#define ROLLER_ACCEL 42.0f      /* world units / s^2 */
#define ROLLER_MAX_SPEED 8.0f
#define ROLLER_JUMP_SPEED 8.0f

static int roller_prev_space, roller_prev_pointer;

static void roller_ready(EngNode *self) {
    (void)self;
    roller_prev_space = 0;
    roller_prev_pointer = 0;
}

static void roller_process(EngNode *self, float dt) {
    float dx = 0.0f, dz = 0.0f;
    float vx, vy, vz;
    if (eng_input_is_pressed(ENG_KEY_LEFT)) dx -= 1.0f;
    if (eng_input_is_pressed(ENG_KEY_RIGHT)) dx += 1.0f;
    if (eng_input_is_pressed(ENG_KEY_UP)) dz -= 1.0f;
    if (eng_input_is_pressed(ENG_KEY_DOWN)) dz += 1.0f;
    if (dx != 0.0f || dz != 0.0f) {
        float len = sqrtf(dx * dx + dz * dz);
        eng_body_apply_impulse(self, dx / len * ROLLER_ACCEL * dt, 0.0f,
                               dz / len * ROLLER_ACCEL * dt);
    }

    /* cap the horizontal speed so the ball cannot run away */
    eng_body_get_linear_velocity(self, &vx, &vy, &vz);
    float h = sqrtf(vx * vx + vz * vz);
    if (h > ROLLER_MAX_SPEED) {
        float k = ROLLER_MAX_SPEED / h;
        eng_body_set_linear_velocity(self, vx * k, vy, vz * k);
    }

    /* jump on the rising edge of Space, only while grounded */
    int space = eng_input_is_pressed(ENG_KEY_SPACE);
    if (space && !roller_prev_space && eng_body_is_grounded(self)) {
        eng_body_get_linear_velocity(self, &vx, &vy, &vz);
        eng_body_set_linear_velocity(self, vx, ROLLER_JUMP_SPEED, vz);
    }
    roller_prev_space = space;

    /* a fresh pointer press pops the ball up and forward */
    float px, py;
    int down = 0;
    eng_input_pointer(&px, &py, &down);
    if (down && !roller_prev_pointer && eng_body_is_grounded(self)) {
        eng_body_get_linear_velocity(self, &vx, &vy, &vz);
        eng_body_set_linear_velocity(self, vx, 6.0f, vz - 4.0f);
    }
    roller_prev_pointer = down;
}

/* ── player: CharacterBody3D platformer controller ─────────────────────── */
/* Run (A/D or arrows), jump (Space, with coyote time + a jump buffer and a
 * variable jump height), collect coins, reach the goal. Mirrors
 * projects/platformer/scripts/player.c so the project behaves identically
 * with and without a compiler. */
#define PLAYER_MOVE 6.0f
#define PLAYER_ACCEL 70.0f
#define PLAYER_GRAVITY 22.0f
#define PLAYER_JUMP 9.5f
#define PLAYER_MAX_FALL 24.0f
#define PLAYER_COYOTE 0.12f
#define PLAYER_BUFFER 0.12f

static int player_prev_space;

static void player_ready(EngNode *self) {
    float x, y, z;
    eng_node3d_get_position(self, &x, &y, &z);
    eng_node_udata_set(self, 0, x);   /* spawn point */
    eng_node_udata_set(self, 1, y);
    eng_node_udata_set(self, 2, z);
    eng_node_udata_set(self, 3, 0);   /* coyote timer */
    eng_node_udata_set(self, 4, 0);   /* jump buffer */
    player_prev_space = 0;
}

static void player_respawn(EngNode *self) {
    eng_node3d_set_position(self, eng_node_udata_get(self, 0),
                            eng_node_udata_get(self, 1),
                            eng_node_udata_get(self, 2));
    eng_character_set_velocity(self, 0, 0, 0);
    eng_node_udata_set(self, 3, 0);
    eng_node_udata_set(self, 4, 0);
}

static void player_process(EngNode *self, float dt) {
    float move = 0;
    if (eng_input_is_pressed(ENG_KEY_A)) move -= 1;
    if (eng_input_is_pressed(ENG_KEY_D)) move += 1;
    if (eng_input_is_pressed(ENG_KEY_LEFT)) move -= 1;
    if (eng_input_is_pressed(ENG_KEY_RIGHT)) move += 1;

    float vx, vy, vz;
    eng_character_get_velocity(self, &vx, &vy, &vz);

    float target = move * PLAYER_MOVE, rate = PLAYER_ACCEL * dt;
    if (vx < target) vx = vx + rate < target ? vx + rate : target;
    else if (vx > target) vx = vx - rate > target ? vx - rate : target;

    float coyote = eng_node_udata_get(self, 3);
    if (eng_character_is_grounded(self)) coyote = PLAYER_COYOTE;
    else if (coyote > 0) coyote -= dt;
    eng_node_udata_set(self, 3, coyote);

    float buffer = eng_node_udata_get(self, 4);
    int space = eng_input_is_pressed(ENG_KEY_SPACE);
    if (space && !player_prev_space) buffer = PLAYER_BUFFER;
    else if (buffer > 0) buffer -= dt;
    player_prev_space = space;

    if (buffer > 0 && coyote > 0) {
        vy = PLAYER_JUMP;
        coyote = 0; buffer = 0;
        eng_node_udata_set(self, 3, coyote);
        eng_play_sound("jump.wav");
    }
    eng_node_udata_set(self, 4, buffer);

    vy -= PLAYER_GRAVITY * dt;
    if (vy < -PLAYER_MAX_FALL) vy = -PLAYER_MAX_FALL;
    if (vy > 0 && !space) vy -= PLAYER_GRAVITY * 1.5f * dt;   /* shorter hop on a tap */

    eng_character_set_velocity(self, vx, vy, vz);
    eng_character_move_and_slide(self, dt);

    /* collect coins under Main/Coins by freeing them on touch */
    EngNode *coins = eng_node_find("Main/Coins");
    if (coins) {
        float px, py, pz;
        eng_node3d_get_position(self, &px, &py, &pz);
        EngNode *c = eng_node_first_child(coins);
        while (c) {
            EngNode *next = eng_node_next_sibling(c);
            float cx, cy, cz;
            eng_node3d_get_position(c, &cx, &cy, &cz);
            float dx = px - cx, dy = py - cy, dz = pz - cz;
            if (dx * dx + dy * dy + dz * dz < 1.4f * 1.4f) {
                eng_node_free(c);
                eng_play_sound("break.wav");
                eng_print("coin collected!");
            }
            c = next;
        }
    }

    /* reaching the goal (or falling off the world) returns to the start */
    EngNode *goal = eng_node_find("Main/Goal");
    float px, py, pz;
    eng_node3d_get_position(self, &px, &py, &pz);
    int at_goal = 0;
    if (goal) {
        float gx, gy, gz;
        eng_node3d_get_position(goal, &gx, &gy, &gz);
        float dx = px - gx, dy = py - gy, dz = pz - gz;
        if (dx * dx + dy * dy + dz * dz < 2.4f * 2.4f) at_goal = 1;
    }
    if (at_goal) { eng_print("GOAL! back to the start."); player_respawn(self); }
    else if (py < -12.0f) { eng_print("fell off — respawn."); player_respawn(self); }
}

/* ── coin: spin and bob in place (per-node state in udata) ─────────────── */
static void coin_ready(EngNode *self) {
    float x, y, z;
    eng_node3d_get_position(self, &x, &y, &z);
    eng_node_udata_set(self, 0, y);                 /* rest height */
    eng_node_udata_set(self, 1, x * 0.71f + z * 1.37f); /* stable phase offset */
}

static void coin_process(EngNode *self, float dt) {
    float yaw, pitch, roll;
    eng_node3d_get_rotation(self, &yaw, &pitch, &roll);
    eng_node3d_set_rotation(self, yaw + dt * 2.2f, pitch, roll);
    float base = eng_node_udata_get(self, 0);
    float phase = eng_node_udata_get(self, 1);
    float x, y, z;
    eng_node3d_get_position(self, &x, &y, &z);
    eng_node3d_set_position(self, x, base + 0.12f * sinf((float)eng_time() * 3.0f + phase), z);
}

/* ── follow: side-view camera that tracks a target on the X axis ───────── */
static void follow_ready(EngNode *self) {
    float x, y, z;
    eng_node3d_get_position(self, &x, &y, &z);
    eng_node_udata_set(self, 0, y);   /* authored height */
    eng_node_udata_set(self, 1, z);   /* authored distance */
    (void)x;
}

static void follow_process(EngNode *self, float dt) {
    EngNode *target = eng_node_find("Main/Hero");
    if (!target) return;
    float tx, ty, tz;
    eng_node3d_get_position(target, &tx, &ty, &tz);
    (void)ty;
    float cx, cy, cz;
    eng_node3d_get_position(self, &cx, &cy, &cz);
    float k = 1.0f - expf(-6.0f * dt);               /* framerate-independent lerp */
    eng_node3d_set_position(self, cx + (tx - cx) * k,
                            eng_node_udata_get(self, 0),
                            eng_node_udata_get(self, 1));
    (void)cz;
}

static const EngBuiltinScript builtins[] = {
    { "spin", NULL, spin_process },
    { "orbit", orbit_ready, orbit_process },
    { "roller", roller_ready, roller_process },
    { "player", player_ready, player_process },
    { "coin", coin_ready, coin_process },
    { "follow", follow_ready, follow_process }
};

const EngBuiltinScript *eng_script_builtin(const char *name) {
    if (!name || !name[0]) return NULL;
    for (unsigned i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++)
        if (!strcmp(builtins[i].name, name)) return &builtins[i];
    return NULL;
}
