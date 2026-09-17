/* Built-in mesh primitives (unit-sized, centred) and flat colour materials. */
#include "eng_internal.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void face4(EngFace *f, EngVec a, EngVec b, EngVec c, EngVec d, EngVec n) {
    f->count = 4;
    f->v[0] = a; f->v[1] = b; f->v[2] = c; f->v[3] = d;
    f->n = n;
    f->uv[0][0] = 0; f->uv[0][1] = 0;
    f->uv[1][0] = 1; f->uv[1][1] = 0;
    f->uv[2][0] = 1; f->uv[2][1] = 1;
    f->uv[3][0] = 0; f->uv[3][1] = 1;
}

/* ── cube ── */
static EngFace cube_faces[6];
static void build_cube(void) {
    const float h = .5f;
    face4(&cube_faces[0], (EngVec){-h, -h, -h}, (EngVec){ h, -h, -h}, (EngVec){ h,  h, -h}, (EngVec){-h,  h, -h}, (EngVec){0, 0, -1});
    face4(&cube_faces[1], (EngVec){ h, -h, -h}, (EngVec){ h, -h,  h}, (EngVec){ h,  h,  h}, (EngVec){ h,  h, -h}, (EngVec){1, 0, 0});
    face4(&cube_faces[2], (EngVec){ h, -h,  h}, (EngVec){-h, -h,  h}, (EngVec){-h,  h,  h}, (EngVec){ h,  h,  h}, (EngVec){0, 0, 1});
    face4(&cube_faces[3], (EngVec){-h, -h,  h}, (EngVec){-h, -h, -h}, (EngVec){-h,  h, -h}, (EngVec){-h,  h,  h}, (EngVec){-1, 0, 0});
    face4(&cube_faces[4], (EngVec){-h,  h, -h}, (EngVec){ h,  h, -h}, (EngVec){ h,  h,  h}, (EngVec){-h,  h,  h}, (EngVec){0, 1, 0});
    face4(&cube_faces[5], (EngVec){-h, -h,  h}, (EngVec){ h, -h,  h}, (EngVec){ h, -h, -h}, (EngVec){-h, -h, -h}, (EngVec){0, -1, 0});
    /* The grass_side texture keeps the green band in its first row: v=0 is up. */
    for (int i = 0; i < 4; i++)
        for (int q = 0; q < 4; q++) cube_faces[i].uv[q][1] = 1 - cube_faces[i].uv[q][1];
}

/* ── plane (XZ, normal +Y) ── */
static EngFace plane_faces[1];
static void build_plane(void) {
    const float h = .5f;
    face4(&plane_faces[0], (EngVec){-h, 0, -h}, (EngVec){ h, 0, -h}, (EngVec){ h, 0,  h}, (EngVec){-h, 0,  h}, (EngVec){0, 1, 0});
}

/* ── sphere: lat/long bands ── */
#define SPH_LON 10
#define SPH_LAT 7
static EngFace sph_faces[SPH_LON * SPH_LAT];
static EngVec sph_pt(float u, float v) {
    float th = u * 2 * (float)M_PI, ph = (v - .5f) * (float)M_PI;
    float r = cosf(ph);
    return (EngVec){r * cosf(th) * .5f, sinf(ph) * .5f, r * sinf(th) * .5f};
}
static void build_sphere(void) {
    int k = 0;
    for (int i = 0; i < SPH_LAT; i++) {
        float v0 = (float)i / SPH_LAT, v1 = (float)(i + 1) / SPH_LAT;
        for (int j = 0; j < SPH_LON; j++) {
            float u0 = (float)j / SPH_LON, u1 = (float)(j + 1) / SPH_LON;
            EngVec a = sph_pt(u0, v0), b = sph_pt(u1, v0), c = sph_pt(u1, v1), d = sph_pt(u0, v1);
            EngVec n = {a.x + b.x + c.x + d.x, a.y + b.y + c.y + d.y, a.z + b.z + c.z + d.z};
            float il = 1 / sqrtf(n.x * n.x + n.y * n.y + n.z * n.z + 1e-9f);
            EngFace *f = &sph_faces[k++];
            f->count = 4;
            f->v[0] = a; f->v[1] = b; f->v[2] = c; f->v[3] = d;
            f->n = (EngVec){n.x * il, n.y * il, n.z * il};
            for (int q = 0; q < 4; q++) { f->uv[q][0] = q < 2 ? u0 : u1; f->uv[q][1] = (q == 0 || q == 3) ? v0 : v1; }
        }
    }
}

/* ── cylinder: octagonal prism ── */
#define CYL_N 8
static EngFace cyl_faces[CYL_N + 2];
static void build_cylinder(void) {
    const float h = .5f, r = .5f;
    for (int j = 0; j < CYL_N; j++) {
        float a0 = j * 2 * (float)M_PI / CYL_N, a1 = (j + 1) * 2 * (float)M_PI / CYL_N;
        float x0 = cosf(a0) * r, z0 = sinf(a0) * r, x1 = cosf(a1) * r, z1 = sinf(a1) * r;
        float nx = cosf((a0 + a1) / 2), nz = sinf((a0 + a1) / 2);
        face4(&cyl_faces[j], (EngVec){x0, -h, z0}, (EngVec){x1, -h, z1}, (EngVec){x1, h, z1}, (EngVec){x0, h, z0}, (EngVec){nx, 0, nz});
    }
    EngFace *top = &cyl_faces[CYL_N], *bot = &cyl_faces[CYL_N + 1];
    top->count = bot->count = CYL_N;
    for (int j = 0; j < CYL_N; j++) {
        float a = j * 2 * (float)M_PI / CYL_N;
        top->v[j] = (EngVec){cosf(a) * r, h, sinf(a) * r};
        bot->v[CYL_N - 1 - j] = (EngVec){cosf(a) * r, -h, sinf(a) * r};
    }
    top->n = (EngVec){0, 1, 0};
    bot->n = (EngVec){0, -1, 0};
    memset(top->uv, 0, sizeof(top->uv));
    memset(bot->uv, 0, sizeof(bot->uv));
}

static int built = 0;
int eng_mesh_faces(int kind, const EngFace **out) {
    if (!built) { build_cube(); build_plane(); build_sphere(); build_cylinder(); built = 1; }
    switch (kind) {
        case ENG_MESH_CUBE: *out = cube_faces; return 6;
        case ENG_MESH_PLANE: *out = plane_faces; return 1;
        case ENG_MESH_SPHERE: *out = sph_faces; return SPH_LON * SPH_LAT;
        case ENG_MESH_CYLINDER: *out = cyl_faces; return CYL_N + 2;
    }
    *out = NULL;
    return 0;
}

/* ── flat colour materials (palette slot 0 = colour, mip 0 only) ── */
#define ENG_MAX_FLAT 64
static RendMaterial flat_materials[ENG_MAX_FLAT];
static uint32_t flat_keys[ENG_MAX_FLAT];
static int flat_count;

RendMaterial *eng_flat_material(uint32_t argb) {
    for (int i = 0; i < flat_count; i++)
        if (flat_keys[i] == argb) return &flat_materials[i];
    int i = flat_count < ENG_MAX_FLAT ? flat_count++ : 0;
    RendMaterial *m = &flat_materials[i];
    memset(m, 0, sizeof(*m));
    m->palette[0] = argb | 0xff000000u; /* every mip texel indexes palette 0 */
    m->colors = 1;
    flat_keys[i] = argb;
    return m;
}
