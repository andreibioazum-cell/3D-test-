/* Internal engine API: node tree, scene files, projects, scripting, rendering. */
#ifndef ENG_INTERNAL_H
#define ENG_INTERNAL_H
#include "engine.h"
#include "eng_api.h"
#include "engine/render/rend3d_internal.h"

enum {
    ENG_NODE, ENG_NODE3D, ENG_MESH, ENG_CAMERA,
    ENG_DIR_LIGHT, ENG_OMNI_LIGHT, ENG_VOXEL_WORLD,
    ENG_STATIC_BODY, ENG_RIGID_BODY, ENG_CHARACTER_BODY
};

/* Body shapes for StaticBody3D / RigidBody3D (see eng_node_shape_name). */
enum { ENG_SHAPE_NONE, ENG_SHAPE_BOX, ENG_SHAPE_SPHERE };
/* A box collider half-extent for the uniform `size` value. */
#define ENG_SIZE_HALF 0.5f

typedef struct { float m[9]; } EngBasis;   /* row-major 3x3, v' = M*v */
typedef struct { float x, y, z; } EngVec;
/* EngNode is forward-declared in eng_api.h. */

struct EngNode {
    char name[32];
    int type;
    struct EngNode *parent, *child, *next; /* child list + sibling link */
    EngVec pos; float yaw, pitch, roll, scale;
    /* cached global transform */
    EngBasis basis; EngVec gpos; float gscale; int dirty;
    /* MeshInstance3D */
    int mesh_kind;                 /* ENG_MESH_KIND_* */
    float size;
    uint32_t color; int has_color;
    int block;                     /* voxel texture material, 0 = none */
    /* Camera3D */
    float fov; int current;
    /* lights */
    float energy, range; float lcol[3];
    /* physics: StaticBody3D / RigidBody3D / CharacterBody3D */
    int shape;                     /* ENG_SHAPE_* */
    float mass, gravity_scale, restitution, friction;
    float vel[3];                  /* RigidBody3D / CharacterBody3D velocity */
    int grounded;                  /* RigidBody3D resting / CharacterBody3D on floor */
    float sleep_t;                 /* frames of near-zero motion */
    int asleep;                    /* RigidBody3D fully resting (not stepped) */
    int one_way;                   /* StaticBody3D: only collide from above */
    float udata[8];                /* per-node scratch floats for scripts */
    /* scripting */
    struct EngScript *script;
};
enum { ENG_MESH_NONE, ENG_MESH_CUBE, ENG_MESH_PLANE, ENG_MESH_SPHERE, ENG_MESH_CYLINDER };

typedef struct EngScript EngScript;
struct EngScript {
    char name[32];
    void *handle;
    void (*ready)(EngNode *);
    void (*process)(EngNode *, float);
    EngScript *next;
};

/* ── node.c ── */
EngNode *eng_node_create(int type, const char *name);
void eng_node_attach(EngNode *parent, EngNode *child);
void eng_node_destroy(EngNode *node);
EngNode *eng_node_root(void);
void eng_node_set_root(EngNode *root);
void eng_node_update_transforms(void);
const EngBasis *eng_node_basis(const EngNode *node);
int eng_type_from_name(const char *type);
const char *eng_type_name(int type);
int eng_shape_from_name(const char *shape);
const char *eng_shape_name(int shape);
void eng_basis_euler(EngBasis *out, float yaw, float pitch, float roll);

/* ── mesh.c ── */
typedef struct { int count; EngVec v[8]; EngVec n; float uv[8][2]; } EngFace;
int eng_mesh_faces(int kind, const EngFace **out); /* built-in primitives */
RendMaterial *eng_flat_material(uint32_t argb);

/* ── fs.c: project/scene IO, host files or APK assets ── */
#define ENG_FS_NAME_MAX 64
void eng_fs_set_assets(AAssetManager *assets);
int eng_fs_read(const char *path, char **out, size_t *len);   /* malloc'd text */
int eng_fs_list(const char *dir, char names[][ENG_FS_NAME_MAX], int max);

/* ── scene.c ── */
void eng_scene_set_project_dir(const char *dir);
int eng_scene_load(const char *path);              /* .escn text scene */
int eng_scene_node_count(void);                    /* nodes in the live tree */
void eng_scene_free(void);
void eng_update(float dt);                          /* scripts + voxel world */
int eng_draw(Buffer *buffer);                       /* camera, lights, meshes */

/* ── script.c ── */
/* Scripts that ship inside the engine binary: on a phone there is no compiler
 * and no writable .so, so the projects' own C files fall back to these. */
typedef struct {
    const char *name;
    void (*ready)(EngNode *);
    void (*process)(EngNode *, float);
} EngBuiltinScript;
const EngBuiltinScript *eng_script_builtin(const char *name);
EngScript *eng_script_load(const char *name, const char *project_dir);
void eng_script_ready_run(void);
void eng_script_process_all(float dt);
void eng_script_discard_all(void);

/* ── physics.c ── */
/* Fixed-timestep accumulator driven from eng_update(). */
void eng_physics_init(void);
void eng_physics_reset(void);            /* scene load: wake every body */
void eng_physics_step(float dt);         /* integrate + resolve once at dt */
int eng_body_dynamic(const EngNode *node);
float eng_body_world_size(const EngNode *node);
void eng_body_apply_impulse(EngNode *body, float x, float y, float z);
void eng_body_set_linear_velocity(EngNode *body, float x, float y, float z);
void eng_body_sync_scene(void);          /* push body transforms to the tree */

/* ── physics.c: CharacterBody3D (kinematic, script-driven) ── */
void eng_character_set_velocity(EngNode *body, float x, float y, float z);
void eng_character_get_velocity(EngNode *body, float *x, float *y, float *z);
int eng_character_move_and_slide(EngNode *body, float dt); /* returns grounded */
int eng_character_is_grounded(EngNode *body);

/* ── input.c ── */
void eng_input_reset(void);
void eng_input_feed_key(int key, int down);
void eng_input_feed_pointer(float x, float y, int down);

/* ── project.c: the launcher's project list ── */
int eng_project_scan(const char *root);        /* returns the project count */
int eng_project_count(void);
const char *eng_project_dir(int index);
const char *eng_project_title(int index);
const char *eng_project_root(void);
int eng_project_index(void);                   /* running project, -1 = none */
int eng_project_open(int index);
int eng_project_load(const char *directory);
void eng_project_free(void);
int eng_project_active(void);
const char *eng_project_name(void);

/* ── api.c ── */
int eng_block_from_name(const char *name);   /* "grass" -> BLOCK_GRASS, 0 air */
int eng_init(AAssetManager *assets);
void eng_time_internal(double now, double dt);

#endif
