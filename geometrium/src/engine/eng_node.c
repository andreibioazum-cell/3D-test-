/* Typed scene tree with Godot-style node names and hierarchical transforms. */
#include "eng_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static EngNode *root;

int eng_type_from_name(const char *t) {
    if (!t) return -1;
    if (!strcmp(t, "Node")) return ENG_NODE;
    if (!strcmp(t, "Node3D")) return ENG_NODE3D;
    if (!strcmp(t, "MeshInstance3D")) return ENG_MESH;
    if (!strcmp(t, "Camera3D")) return ENG_CAMERA;
    if (!strcmp(t, "DirectionalLight3D")) return ENG_DIR_LIGHT;
    if (!strcmp(t, "OmniLight3D")) return ENG_OMNI_LIGHT;
    if (!strcmp(t, "VoxelWorld3D")) return ENG_VOXEL_WORLD;
    if (!strcmp(t, "StaticBody3D")) return ENG_STATIC_BODY;
    if (!strcmp(t, "RigidBody3D")) return ENG_RIGID_BODY;
    if (!strcmp(t, "CharacterBody3D")) return ENG_CHARACTER_BODY;
    return -1;
}
const char *eng_type_name(int type) {
    switch (type) {
        case ENG_NODE3D: return "Node3D";
        case ENG_MESH: return "MeshInstance3D";
        case ENG_CAMERA: return "Camera3D";
        case ENG_DIR_LIGHT: return "DirectionalLight3D";
        case ENG_OMNI_LIGHT: return "OmniLight3D";
        case ENG_VOXEL_WORLD: return "VoxelWorld3D";
        case ENG_STATIC_BODY: return "StaticBody3D";
        case ENG_RIGID_BODY: return "RigidBody3D";
        case ENG_CHARACTER_BODY: return "CharacterBody3D";
        default: return "Node";
    }
}
int eng_shape_from_name(const char *s) {
    if (!s) return ENG_SHAPE_NONE;
    if (!strcmp(s, "box")) return ENG_SHAPE_BOX;
    if (!strcmp(s, "sphere")) return ENG_SHAPE_SPHERE;
    return ENG_SHAPE_NONE;
}
const char *eng_shape_name(int shape) {
    switch (shape) {
        case ENG_SHAPE_BOX: return "box";
        case ENG_SHAPE_SPHERE: return "sphere";
        default: return "none";
    }
}
EngNode *eng_node_create(int type, const char *name) {
    EngNode *n = calloc(1, sizeof(*n));
    if (!n) return NULL;
    snprintf(n->name, sizeof(n->name), "%s", name && name[0] ? name : eng_type_name(type));
    n->type = type;
    n->scale = 1;
    n->fov = 66;
    n->energy = 1;
    n->range = 10;
    n->lcol[0] = n->lcol[1] = n->lcol[2] = 1;
    n->size = 1;
    n->dirty = 1;
    n->mass = 1;
    n->gravity_scale = 1;
    n->restitution = 0.1f;
    n->friction = 0.6f;
    /* sensible default shapes: static and character bodies are solid boxes, a
     * rigid body is a dynamic sphere whose radius matches its sphere mesh. */
    n->shape = (type == ENG_STATIC_BODY || type == ENG_CHARACTER_BODY) ? ENG_SHAPE_BOX
             : (type == ENG_RIGID_BODY) ? ENG_SHAPE_SPHERE : ENG_SHAPE_NONE;
    return n;
}
void eng_node_attach(EngNode *parent, EngNode *child) {
    if (!parent || !child || parent == child) return;
    if (child->parent) { /* detach first */
        EngNode **link = &child->parent->child;
        while (*link && *link != child) link = &(*link)->next;
        if (*link) *link = child->next;
    }
    child->parent = parent;
    child->next = NULL;
    if (!parent->child) parent->child = child;
    else { EngNode *s = parent->child; while (s->next) s = s->next; s->next = child; }
    child->dirty = 1;
}
void eng_node_destroy(EngNode *node) {
    if (!node) return;
    while (node->child) eng_node_destroy(node->child);
    if (node->parent) { /* unlink from the parent's child list */
        EngNode **link = &node->parent->child;
        while (*link && *link != node) link = &(*link)->next;
        if (*link) *link = node->next;
    } else if (root == node) root = NULL;
    free(node);
}
EngNode *eng_node_root(void) { return root; }
void eng_node_set_root(EngNode *r) { root = r; }

void eng_basis_euler(EngBasis *b, float yaw, float pitch, float roll) {
    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    float cr = cosf(roll), sr = sinf(roll);
    /* Ry * Rx * Rz, row-major */
    b->m[0] = cy * cr + sy * sp * sr; b->m[1] = -cy * sr + sy * sp * cr; b->m[2] = sy * cp;
    b->m[3] = cp * sr;               b->m[4] = cp * cr;                b->m[5] = -sp;
    b->m[6] = -sy * cr + cy * sp * sr; b->m[7] = sy * sr + cy * sp * cr; b->m[8] = cy * cp;
}
static void basis_mul(EngBasis *out, const EngBasis *a, const EngBasis *b) {
    EngBasis r;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++)
        r.m[i * 3 + j] = a->m[i * 3] * b->m[j] + a->m[i * 3 + 1] * b->m[3 + j] + a->m[i * 3 + 2] * b->m[6 + j];
    *out = r;
}
static EngVec basis_xform(const EngBasis *b, EngVec v) {
    return (EngVec){b->m[0] * v.x + b->m[1] * v.y + b->m[2] * v.z,
                    b->m[3] * v.x + b->m[4] * v.y + b->m[5] * v.z,
                    b->m[6] * v.x + b->m[7] * v.y + b->m[8] * v.z};
}
static void update_one(EngNode *n) {
    EngBasis local;
    eng_basis_euler(&local, n->yaw, n->pitch, n->roll);
    if (n->parent) {
        const EngNode *p = n->parent;
        basis_mul(&n->basis, &p->basis, &local);
        EngVec off = basis_xform(&p->basis, (EngVec){n->pos.x * p->gscale, n->pos.y * p->gscale, n->pos.z * p->gscale});
        n->gpos = (EngVec){p->gpos.x + off.x, p->gpos.y + off.y, p->gpos.z + off.z};
        n->gscale = p->gscale * n->scale;
    } else {
        n->basis = local;
        n->gpos = n->pos;
        n->gscale = n->scale;
    }
    n->dirty = 0;
}
static void update_tree(EngNode *n) {
    update_one(n);
    for (EngNode *c = n->child; c; c = c->next) update_tree(c);
}
void eng_node_update_transforms(void) { if (root) update_tree(root); }
const EngBasis *eng_node_basis(const EngNode *n) { return &n->basis; }

static EngNode *find_child(EngNode *n, const char *name, size_t len) {
    for (EngNode *c = n->child; c; c = c->next)
        if (!strncmp(c->name, name, len) && !c->name[len]) return c;
    return NULL;
}
EngNode *eng_node_find(const char *path) {
    if (!root || !path) return NULL;
    if (!path[0] || !strcmp(path, "/")) return root;
    EngNode *n = root;
    const char *p = path[0] == '/' ? path + 1 : path;
    size_t len = strcspn(p, "/");
    /* first component may name the root itself (Godot-style absolute paths) */
    if (!strncmp(root->name, p, len) && !root->name[len]) {
        if (!p[len]) return root;
        p += len + 1;
    }
    while (*p) {
        len = strcspn(p, "/");
        if (!len) { p++; continue; }
        n = find_child(n, p, len);
        if (!n) return NULL;
        if (!p[len]) return n;
        p += len + 1;
    }
    return n;
}

/* ── public scripting-facing wrappers ── */
EngNode *eng_node_new(const char *type, const char *name) {
    int t = eng_type_from_name(type);
    return t < 0 ? NULL : eng_node_create(t, name);
}
void eng_node_add_child(EngNode *parent, EngNode *child) { eng_node_attach(parent, child); }
void eng_node_free(EngNode *node) { eng_node_destroy(node); }
EngNode *eng_node_first_child(const EngNode *n) { return n ? n->child : NULL; }
EngNode *eng_node_next_sibling(const EngNode *n) { return n ? n->next : NULL; }
const char *eng_node_name(const EngNode *n) { return n ? n->name : ""; }
const char *eng_node_type(const EngNode *n) { return n ? eng_type_name(n->type) : ""; }
void eng_node_udata_set(EngNode *n, int slot, float value) {
    if (n && slot >= 0 && slot < 8) n->udata[slot] = value;
}
float eng_node_udata_get(const EngNode *n, int slot) {
    return n && slot >= 0 && slot < 8 ? n->udata[slot] : 0.0f;
}
void eng_node3d_set_position(EngNode *n, float x, float y, float z) {
    if (!n) return;
    n->pos = (EngVec){x, y, z};
    n->dirty = 1;
}
void eng_node3d_get_position(EngNode *n, float *x, float *y, float *z) {
    if (!n) return;
    if (x) *x = n->gpos.x;
    if (y) *y = n->gpos.y;
    if (z) *z = n->gpos.z;
}
void eng_node3d_set_rotation(EngNode *n, float yaw, float pitch, float roll) {
    if (!n) return;
    n->yaw = yaw;
    n->pitch = pitch;
    n->roll = roll;
    n->dirty = 1;
}
void eng_node3d_get_rotation(EngNode *n, float *yaw, float *pitch, float *roll) {
    if (!n) return;
    if (yaw) *yaw = n->yaw;
    if (pitch) *pitch = n->pitch;
    if (roll) *roll = n->roll;
}
void eng_node3d_set_scale(EngNode *n, float s) { if (n && s > 0) { n->scale = s; n->dirty = 1; } }
void eng_camera_make_current(EngNode *n) { if (n && n->type == ENG_CAMERA) n->current = 1; }
void eng_light_set_energy(EngNode *n, float e) { if (n) n->energy = e; }
void eng_light_set_color(EngNode *n, float r, float g, float b) {
    if (!n) return;
    n->lcol[0] = r;
    n->lcol[1] = g;
    n->lcol[2] = b;
}
void eng_light_set_range(EngNode *n, float r) { if (n && n->type == ENG_OMNI_LIGHT && r > 0) n->range = r; }
static int can_draw_mesh(const EngNode *n) {
    return n && (n->type == ENG_MESH || n->type == ENG_STATIC_BODY ||
                 n->type == ENG_RIGID_BODY || n->type == ENG_CHARACTER_BODY);
}
void eng_mesh_set(EngNode *n, const char *mesh) {
    if (!can_draw_mesh(n) || !mesh) return;
    if (!strcmp(mesh, "cube")) n->mesh_kind = ENG_MESH_CUBE;
    else if (!strcmp(mesh, "plane")) n->mesh_kind = ENG_MESH_PLANE;
    else if (!strcmp(mesh, "sphere")) n->mesh_kind = ENG_MESH_SPHERE;
    else if (!strcmp(mesh, "cylinder")) n->mesh_kind = ENG_MESH_CYLINDER;
    else n->mesh_kind = ENG_MESH_NONE;
}
void eng_mesh_set_color(EngNode *n, float r, float g, float b) {
    if (!n) return;
    int ir = (int)(r * 255 + .5f), ig = (int)(g * 255 + .5f), ib = (int)(b * 255 + .5f);
    n->color = 0xff000000u | ((uint32_t)ir << 16) | ((uint32_t)ig << 8) | (uint32_t)ib;
    n->has_color = 1;
}
void eng_mesh_set_size(EngNode *n, float s) { if (n && s > 0) n->size = s; }
