/* Text scene files (.escn): node tree, C scripts, lights, voxel worlds.
 *
 *   [node name="Main" type="Node"]
 *   [node name="Camera" type="Camera3D" parent="."]
 *   position = 0 4 9
 *   current = true
 */
#include "eng_internal.h"
#include "eng_api.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static char project_dir[4096];
static int world_built;
/* Last pointer position of an active camera drag (see freecam_update). */
static float drag_x, drag_y;
static int drag_valid;
static uint32_t world_seed = 1234;
static int seed_set;

/* ── parsing helpers ── */
static const char *attr(const char *line, const char *key, char *out, int cap) {
    char pat[32];
    snprintf(pat, sizeof(pat), "%s=\"", key);
    const char *p = strstr(line, pat);
    if (!p) return NULL;
    p += strlen(pat);
    int i = 0;
    while (*p && *p != '"' && i < cap - 1) out[i++] = *p++;
    out[i] = 0;
    return out;
}
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}
static int parse_floats(const char *s, float *out, int max) {
    int n = 0;
    while (n < max) {
        char *end;
        float v = strtof(s, &end);
        if (end == s) break;
        out[n++] = v;
        s = end;
    }
    return n;
}
static void unquote(char *s) {
    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        memmove(s, s + 1, len - 2);
        s[len - 2] = 0;
    }
}
static void apply_property(EngNode *n, const char *key, char *value) {
    float f[3];
    if (!strcmp(key, "position")) {
        if (parse_floats(value, f, 3) == 3) n->pos = (EngVec){f[0], f[1], f[2]};
    } else if (!strcmp(key, "rotation")) {
        int c = parse_floats(value, f, 3);
        if (c >= 1) n->yaw = f[0];
        if (c >= 2) n->pitch = f[1];
        if (c >= 3) n->roll = f[2];
    } else if (!strcmp(key, "yaw") && parse_floats(value, f, 1) == 1) n->yaw = f[0];
    else if (!strcmp(key, "pitch") && parse_floats(value, f, 1) == 1) n->pitch = f[0];
    else if (!strcmp(key, "roll") && parse_floats(value, f, 1) == 1) n->roll = f[0];
    else if (!strcmp(key, "scale") && parse_floats(value, f, 1) == 1) n->scale = f[0] > 0 ? f[0] : 1;
    else if (!strcmp(key, "mesh")) eng_mesh_set(n, trim(value));
    else if (!strcmp(key, "size") && parse_floats(value, f, 1) == 1) n->size = f[0] > 0 ? f[0] : 1;
    else if (!strcmp(key, "color") && parse_floats(value, f, 3) == 3) {
        n->color = 0xff000000u | ((uint32_t)(f[0] * 255 + .5f) << 16) |
                   ((uint32_t)(f[1] * 255 + .5f) << 8) | (uint32_t)(f[2] * 255 + .5f);
        n->has_color = 1;
    } else if (!strcmp(key, "material")) n->block = eng_block_from_name(trim(value));
    else if (!strcmp(key, "fov") && parse_floats(value, f, 1) == 1) n->fov = f[0];
    else if (!strcmp(key, "current")) n->current = !strcmp(trim(value), "true");
    else if (!strcmp(key, "energy") && parse_floats(value, f, 1) == 1) n->energy = f[0];
    else if (!strcmp(key, "range") && parse_floats(value, f, 1) == 1) n->range = f[0];
    else if (!strcmp(key, "light_color") && parse_floats(value, f, 3) == 3) {
        n->lcol[0] = f[0]; n->lcol[1] = f[1]; n->lcol[2] = f[2];
    } else if (!strcmp(key, "script")) {
        unquote(value);
        n->script = eng_script_load(trim(value), project_dir);
    } else if (!strcmp(key, "seed") && parse_floats(value, f, 1) == 1) {
        world_seed = (uint32_t)f[0];
        seed_set = 1;
    } else if (!strcmp(key, "shape")) {
        eng_body_set_shape(n, trim(value));
    } else if (!strcmp(key, "mass") && parse_floats(value, f, 1) == 1) {
        eng_body_set_mass(n, f[0]);
    } else if (!strcmp(key, "gravity_scale") && parse_floats(value, f, 1) == 1) {
        eng_body_set_gravity_scale(n, f[0]);
    } else if (!strcmp(key, "velocity") && parse_floats(value, f, 3) == 3) {
        if (n->type == ENG_CHARACTER_BODY) eng_character_set_velocity(n, f[0], f[1], f[2]);
        else eng_body_set_linear_velocity(n, f[0], f[1], f[2]);
    } else if (!strcmp(key, "one_way")) {
        n->one_way = !strcmp(trim(value), "true");
    } else if (!strcmp(key, "restitution") && parse_floats(value, f, 1) == 1) {
        n->restitution = f[0] > 0 ? f[0] : 0;
    } else if (!strcmp(key, "friction") && parse_floats(value, f, 1) == 1) {
        n->friction = f[0] >= 0 ? f[0] : 0;
    }
}

static int has_type(EngNode *n, int type) {
    if (n->type == type) return 1;
    for (EngNode *c = n->child; c; c = c->next)
        if (has_type(c, type)) return 1;
    return 0;
}

/* The project loader sets this so scripts resolve against <project>/scripts. */
void eng_scene_set_project_dir(const char *dir) {
    snprintf(project_dir, sizeof(project_dir), "%s", dir ? dir : "");
}

int eng_scene_load(const char *path) {
    char *text = NULL;
    size_t text_len = 0;
    if (!eng_fs_read(path, &text, &text_len)) {
        app_log("engine: cannot open scene '%s'", path);
        return 0;
    }
    if (!project_dir[0]) { /* stand-alone scene: fall back to its directory */
        snprintf(project_dir, sizeof(project_dir), "%s", path);
        char *slash = strrchr(project_dir, '/');
        if (slash) *slash = 0;
    }

    eng_scene_free();
    char line[1024];
    EngNode *last = NULL;
    const char *cursor = text;
    while (cursor && *cursor) {
        const char *nl = strchr(cursor, '\n');
        size_t n = nl ? (size_t)(nl - cursor) : strlen(cursor);
        if (n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, cursor, n);
        line[n] = 0;
        cursor = nl ? nl + 1 : NULL;
        char *s = trim(line);
        if (!s[0] || s[0] == '#') continue;
        if (s[0] == '[') {
            char name[64] = "", type[32] = "", parent[64] = "";
            attr(s, "name", name, sizeof(name));
            if (!attr(s, "type", type, sizeof(type))) continue;
            int t = eng_type_from_name(type);
            if (t < 0) { app_log("engine: unknown node type '%s'", type); continue; }
            EngNode *n = eng_node_create(t, name);
            attr(s, "parent", parent, sizeof(parent));
            if (!eng_node_root()) {
                eng_node_set_root(n);
            } else if (!parent[0] || !strcmp(parent, ".")) {
                eng_node_attach(eng_node_root(), n);
            } else {
                char pathbuf[128];
                snprintf(pathbuf, sizeof(pathbuf), "%s", parent);
                EngNode *p = eng_node_find(pathbuf);
                if (!p) p = eng_node_root();
                eng_node_attach(p, n);
            }
            last = n;
        } else if (last) {
            char *eq = strchr(s, '=');
            if (!eq) continue;
            *eq = 0;
            char *key = trim(s), *value = trim(eq + 1);
            apply_property(last, key, value);
        }
    }
    free(text);
    if (!eng_node_root()) { app_log("engine: scene '%s' has no root node", path); return 0; }
    eng_node_update_transforms();
    if (has_type(eng_node_root(), ENG_VOXEL_WORLD) && !world_built) {
        voxel_terrain_seed(seed_set ? world_seed : 1234);
        voxel_world_build(seed_set ? world_seed : 1234);
        world_built = 1;
    }
    eng_physics_reset();       /* fresh scene: wake every rigid body */
    eng_node_update_transforms();
    eng_script_ready_run();
    eng_node_update_transforms();
    return 1;
}

static int count_nodes(const EngNode *n) {
    int total = 1;
    for (const EngNode *c = n->child; c; c = c->next) total += count_nodes(c);
    return total;
}
int eng_scene_node_count(void) {
    const EngNode *root = eng_node_root();
    return root ? count_nodes(root) : 0;
}

void eng_scene_free(void) {
    if (eng_node_root()) {
        EngNode *r = eng_node_root();
        eng_node_set_root(NULL);
        eng_node_destroy(r);
    }
    world_built = 0;
    drag_valid = 0;
    eng_input_reset();
}

/* ── per-frame update ── */
static EngNode *find_camera(EngNode *n, int require_current);

/* Inspectable camera: when the current Camera3D carries no script of its own
 * the engine drives it, so every scene can be looked around in — drag orbits,
 * WASD moves, Space/Shift change altitude. A scripted camera keeps full
 * control and is never touched here. */
static void freecam_update(EngNode *cam, float dt) {
    float px, py;
    int down = 0;
    if (eng_input_pointer(&px, &py, &down) && down) {
        if (drag_valid) {
            cam->yaw += (px - drag_x) * 0.005f;
            cam->pitch += (py - drag_y) * 0.005f;
            if (cam->pitch > 1.5f) cam->pitch = 1.5f;
            if (cam->pitch < -1.5f) cam->pitch = -1.5f;
            cam->dirty = 1;
        }
        drag_x = px;
        drag_y = py;
        drag_valid = 1;
    } else {
        drag_valid = 0;
    }

    float fwd = 0, side = 0, lift = 0;
    if (eng_input_is_pressed(ENG_KEY_UP)) fwd += 1;
    if (eng_input_is_pressed(ENG_KEY_DOWN)) fwd -= 1;
    if (eng_input_is_pressed(ENG_KEY_RIGHT)) side += 1;
    if (eng_input_is_pressed(ENG_KEY_LEFT)) side -= 1;
    if (eng_input_is_pressed(ENG_KEY_SPACE)) lift += 1;
    if (eng_input_is_pressed(ENG_KEY_SHIFT)) lift -= 1;
    if (fwd == 0 && side == 0 && lift == 0) return;
    float step = 6.0f * (dt > 0 ? dt : 0);
    float sy = sinf(cam->yaw), cy = cosf(cam->yaw);
    cam->pos.x += (sy * fwd + cy * side) * step;
    cam->pos.z += (cy * fwd - sy * side) * step;
    cam->pos.y += lift * step;
    cam->dirty = 1;
}

static EngNode *first_of_type(EngNode *n, int type) {
    if (n->type == type) return n;
    for (EngNode *c = n->child; c; c = c->next) {
        EngNode *r = first_of_type(c, type);
        if (r) return r;
    }
    return NULL;
}
void eng_update(float dt) {
    EngNode *root = eng_node_root();
    if (!root) return;
    eng_node_update_transforms(); /* scripts see fresh global transforms */
    eng_script_process_all(dt);
    {
        EngNode *cam = find_camera(root, 1);
        if (!cam) cam = find_camera(root, 0);
        if (cam && !cam->script) freecam_update(cam, dt);
    }
    eng_node_update_transforms();
    /* physics: split large frame gaps into <= 1/30 s substeps for stability */
    {
        float remain = dt;
        while (remain > 1e-5f) {
            float step = remain < 1.0f / 30.0f ? remain : 1.0f / 30.0f;
            eng_physics_step(step);
            remain -= step;
        }
    }
    if (world_built) {
        voxel_water_update(dt);
        EngNode *cam = first_of_type(root, ENG_CAMERA);
        voxel_world_update(cam ? cam->gpos.x : 0, cam ? cam->gpos.z : 0);
    }
}

/* ── rendering ── */
typedef struct { EngVec dir; float energy, luma; } DirLight;
typedef struct { EngVec pos; float energy, range, luma; } OmniLight;

static float luma(const float c[3]) { return .299f * c[0] + .587f * c[1] + .114f * c[2]; }

static void collect_lights(EngNode *n, DirLight *d, int *nd, OmniLight *o, int *no) {
    if (n->type == ENG_DIR_LIGHT && *nd < 8) {
        const EngBasis *b = eng_node_basis(n);
        d[*nd] = (DirLight){{-b->m[2], -b->m[5], -b->m[8]}, n->energy, luma(n->lcol)};
        (*nd)++;
    } else if (n->type == ENG_OMNI_LIGHT && *no < 8) {
        o[*no] = (OmniLight){n->gpos, n->energy, fmaxf(0.01f, n->range), luma(n->lcol)};
        (*no)++;
    }
    for (EngNode *c = n->child; c; c = c->next) collect_lights(c, d, nd, o, no);
}

static unsigned char face_light(EngVec fn, EngVec fc, const DirLight *d, int nd,
                                const OmniLight *o, int no) {
    if (!nd && !no) return 255;
    float sum = .4f;
    for (int i = 0; i < nd; i++) {
        float dot = -(fn.x * d[i].dir.x + fn.y * d[i].dir.y + fn.z * d[i].dir.z);
        if (dot > 0) sum += dot * d[i].energy * d[i].luma;
    }
    for (int i = 0; i < no; i++) {
        float dx = o[i].pos.x - fc.x, dy = o[i].pos.y - fc.y, dz = o[i].pos.z - fc.z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist >= o[i].range) continue;
        float dot = (fn.x * dx + fn.y * dy + fn.z * dz) / dist;
        if (dot > 0) sum += dot * (1 - dist / o[i].range) * o[i].energy * o[i].luma;
    }
    return (unsigned char)(fminf(1, sum) * 255);
}

static int face_index(EngVec n) {
    float ax = fabsf(n.x), ay = fabsf(n.y), az = fabsf(n.z);
    if (ay >= ax && ay >= az) return n.y > 0 ? 0 : 1;
    return 2;
}

static void draw_mesh(EngNode *n, const DirLight *d, int nd, const OmniLight *o, int no) {
    int visual = n->type == ENG_MESH || n->type == ENG_STATIC_BODY ||
                 n->type == ENG_RIGID_BODY || n->type == ENG_CHARACTER_BODY;
    if (!visual || n->mesh_kind == ENG_MESH_NONE) return;
    const EngFace *faces;
    int count = eng_mesh_faces(n->mesh_kind, &faces);
    const EngBasis *b = eng_node_basis(n);
    float s = n->size * n->gscale;
    for (int i = 0; i < count; i++) {
        const EngFace *f = &faces[i];
        RendVertex verts[8];
        EngVec fc = {0, 0, 0};
        for (int v = 0; v < f->count; v++) {
            EngVec lv = {f->v[v].x * s, f->v[v].y * s, f->v[v].z * s};
            EngVec gv = {b->m[0] * lv.x + b->m[1] * lv.y + b->m[2] * lv.z + n->gpos.x,
                         b->m[3] * lv.x + b->m[4] * lv.y + b->m[5] * lv.z + n->gpos.y,
                         b->m[6] * lv.x + b->m[7] * lv.y + b->m[8] * lv.z + n->gpos.z};
            verts[v] = (RendVertex){gv.x, gv.y, gv.z, f->uv[v][0], f->uv[v][1]};
            fc.x += gv.x; fc.y += gv.y; fc.z += gv.z;
        }
        fc.x /= f->count; fc.y /= f->count; fc.z /= f->count;
        EngVec gn = {b->m[0] * f->n.x + b->m[1] * f->n.y + b->m[2] * f->n.z,
                     b->m[3] * f->n.x + b->m[4] * f->n.y + b->m[5] * f->n.z,
                     b->m[6] * f->n.x + b->m[7] * f->n.y + b->m[8] * f->n.z};
        unsigned char lb = face_light(gn, fc, d, nd, o, no);
        unsigned char light[8];
        for (int v = 0; v < f->count; v++) light[v] = lb;
        if (n->block) {
            RendMaterial *m = rend_material(n->block, face_index(gn));
            if (m) rend3d_polygon(verts, f->count, gn.x, gn.y, gn.z, 0, m, light);
        } else {
            uint32_t color = n->has_color ? n->color : 0xffc8c8c8u;
            rend3d_polygon(verts, f->count, gn.x, gn.y, gn.z, color,
                                 eng_flat_material(color), light);
        }
    }
}

static void draw_tree(EngNode *n, const DirLight *d, int nd, const OmniLight *o, int no) {
    draw_mesh(n, d, nd, o, no);
    for (EngNode *c = n->child; c; c = c->next) draw_tree(c, d, nd, o, no);
}

static EngNode *find_camera(EngNode *n, int require_current) {
    if (n->type == ENG_CAMERA && (!require_current || n->current)) return n;
    for (EngNode *c = n->child; c; c = c->next) {
        EngNode *r = find_camera(c, require_current);
        if (r) return r;
    }
    return NULL;
}

int eng_draw(Buffer *buffer) {
    EngNode *root = eng_node_root();
    if (!root) return 0;
    EngNode *cam = find_camera(root, 1);
    if (!cam) cam = find_camera(root, 0); /* fall back to first camera */
    if (!cam) return 0;

    float yaw = cam->yaw, pitch = cam->pitch;
    if (pitch > 1.5f) pitch = 1.5f;
    if (pitch < -1.5f) pitch = -1.5f;
    int scale = rend_render_scale(buffer->width, buffer->height);
    if (!rend3d_begin(buffer, scale, cam->gpos.x, cam->gpos.y, cam->gpos.z, yaw, pitch, cam->fov))
        return 0;
    rend3d_sky(0xff78b8e8u, 0xffc7e5f5u);
    rend3d_fog(40, 90);

    DirLight d[8]; OmniLight o[8];
    int nd = 0, no = 0;
    collect_lights(root, d, &nd, o, &no);
    if (world_built) voxel_world_draw();
    draw_tree(root, d, nd, o, no);
    rend3d_end();
    return 1;
}


