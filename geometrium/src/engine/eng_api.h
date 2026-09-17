/* Public C scripting API for Enjoer engine projects.
 *
 * A project script is a regular C file compiled to a shared object; the engine
 * loads it with dlopen and calls the two optional entry points:
 *
 *   void eng_script_ready(EngNode *self);     once, after the scene exists
 *   void eng_script_process(EngNode *self, float dt);   every frame
 *
 * The script links against the functions below (the host binary exports them;
 * build the .so with `cc -shared -fPIC script.c -o script.so` and -I pointing
 * at src/engine). */
#ifndef ENG_API_H
#define ENG_API_H

typedef struct EngNode EngNode;

/* ── key codes for eng_input_is_pressed ─────────────────────────────────── */
enum {
    ENG_KEY_LEFT = 1000, ENG_KEY_RIGHT, ENG_KEY_UP, ENG_KEY_DOWN,
    ENG_KEY_A, ENG_KEY_D, ENG_KEY_W, ENG_KEY_S, ENG_KEY_SPACE, ENG_KEY_SHIFT
};

/* ── logging ────────────────────────────────────────────────────────────── */
void eng_print(const char *message);

/* ── node tree ──────────────────────────────────────────────────────────── */
/* Create a node of the given type ("Node3D", "MeshInstance3D", "Camera3D",
 * "DirectionalLight3D", "OmniLight3D", "VoxelWorld3D", "StaticBody3D",
 * "RigidBody3D", "CharacterBody3D"). Not attached yet. */
EngNode *eng_node_new(const char *type, const char *name);
void eng_node_add_child(EngNode *parent, EngNode *child);
void eng_node_free(EngNode *node);              /* detaches and frees subtree */
EngNode *eng_node_find(const char *path);       /* "Root/Cube", from the root */
EngNode *eng_node_first_child(const EngNode *node);   /* NULL when no children */
EngNode *eng_node_next_sibling(const EngNode *node);  /* NULL at the list end */
const char *eng_node_name(const EngNode *node);
const char *eng_node_type(const EngNode *node);

/* Per-node scratch floats a script can use to remember state between frames
 * (eight slots, all zero until written). Useful for per-instance data such as
 * a coin's rest height or the player's spawn point. */
void eng_node_udata_set(EngNode *node, int slot, float value);
float eng_node_udata_get(const EngNode *node, int slot);

/* ── Node3D transform ───────────────────────────────────────────────────── */
void eng_node3d_set_position(EngNode *node, float x, float y, float z);
void eng_node3d_get_position(EngNode *node, float *x, float *y, float *z);
/* Euler radians: yaw (Y), pitch (X), roll (Z), applied Ry*Rx*Rz. */
void eng_node3d_set_rotation(EngNode *node, float yaw, float pitch, float roll);
void eng_node3d_get_rotation(EngNode *node, float *yaw, float *pitch, float *roll);
void eng_node3d_set_scale(EngNode *node, float scale);

/* ── Camera3D ───────────────────────────────────────────────────────────── */
void eng_camera_make_current(EngNode *camera);

/* ── input (polled every frame from a script) ───────────────────────────── */
/* key: one of the ENG_KEY_* codes above. Returns 1 while held. */
int eng_input_is_pressed(int key);
/* pointer: the last pointer position in window pixels and whether it is
 * currently pressed. Returns 0 until a pointer has been seen. */
int eng_input_pointer(float *x, float *y, int *down);

/* ── StaticBody3D / RigidBody3D ─────────────────────────────────────────── */
/* Set which collision shape a body uses: "box" or "sphere" (default box for
 * StaticBody3D and CharacterBody3D, sphere for RigidBody3D). Only meaningful
 * on body node types; others are ignored. */
void eng_body_set_shape(EngNode *node, const char *shape);
void eng_body_set_mass(EngNode *body, float mass);
void eng_body_set_gravity_scale(EngNode *body, float scale);
/* Impulse = instantaneous change of momentum; velocity += impulse / mass. */
void eng_body_apply_impulse(EngNode *body, float x, float y, float z);
void eng_body_set_linear_velocity(EngNode *body, float x, float y, float z);
void eng_body_get_linear_velocity(EngNode *body, float *x, float *y, float *z);
int eng_body_is_grounded(EngNode *body);

/* ── CharacterBody3D (kinematic platformer character) ───────────────────── */
/* Godot-style move-and-collide. The script writes `velocity` (world units per
 * second — gravity is the script's job, see projects/platformer), then calls
 * eng_character_move_and_slide once a frame. The node slides along walls and
 * lands on floors, reporting `grounded` (also returned by the call). The
 * stored velocity is updated to the slid result. A CharacterBody3D must hang
 * from the root or a plain Node container. */
void eng_character_set_velocity(EngNode *body, float x, float y, float z);
void eng_character_get_velocity(EngNode *body, float *x, float *y, float *z);
int eng_character_move_and_slide(EngNode *body, float dt);
int eng_character_is_grounded(EngNode *body);

/* ── lights ─────────────────────────────────────────────────────────────── */
void eng_light_set_energy(EngNode *light, float energy);
void eng_light_set_color(EngNode *light, float r, float g, float b);
void eng_light_set_range(EngNode *omni, float range); /* OmniLight3D only */

/* ── MeshInstance3D ─────────────────────────────────────────────────────── */
/* mesh: "cube", "plane", "sphere", "cylinder". Also applies to
 * StaticBody3D / RigidBody3D so a body can carry a visible mesh. */
void eng_mesh_set(EngNode *node, const char *mesh);
void eng_mesh_set_color(EngNode *node, float r, float g, float b);
void eng_mesh_set_size(EngNode *node, float size);

/* ── VoxelWorld3D ── */
/* Read and write single cells of the streamed voxel world. Coordinates are
 * half-block cells (two cells per block); material is "grass", "dirt",
 * "stone", "sand", "water", "log", "leaves" or "air". Writes only land inside
 * chunks that are currently streamed in, and return 0 otherwise. */
int eng_voxel_get_cell(int x, int y, int z);
int eng_voxel_set_cell(int x, int y, int z, const char *material);

/* ── audio ──────────────────────────────────────────────────────────────── */
/* Play a short effect from assets/sounds/ (e.g. "jump.wav"). Loads the file
 * on first use. Silently does nothing when the platform has no audio. */
void eng_play_sound(const char *name);

/* ── misc ───────────────────────────────────────────────────────────────── */
double eng_time(void);        /* seconds since the engine started */
float eng_delta(void);        /* last frame interval */

#endif
