/* Physics: StaticBody3D colliders and RigidBody3D dynamics for engine scenes.
 *
 * This is a compact, deterministic impulse-integrator tuned to a blocky voxel
 * aesthetic, not a general rigid-body solver. A RigidBody3D is a dynamic
 * sphere whose radius matches its unit sphere mesh (size * gscale / 2), so the
 * drawn ball and its physics body coincide. A StaticBody3D is a solid box by
 * default but can also be a sphere. Rigid bodies collide with static bodies
 * (the main way they are used); rigid/rigid contact is left out on purpose so
 * the solver stays predictable. Bodies are simulated in world space, so a body
 * must hang from the root or a plain container Node (no transform parent).
 *
 * The step applies gravity, integrates velocity, then resolves penetration
 * against static bodies, marking the body `grounded` when it rests on a
 * support. A still, grounded body falls asleep and is skipped until a script
 * wakes it with an impulse, keeping resting bodies rock-stable and cheap.
 */
#include "eng_internal.h"
#include <math.h>
#include <string.h>

#define ENG_GRAVITY 20.0f        /* world units / s^2 (snappy, blocky feel) */
#define ENG_SLEEP_EPS 1.5f       /* squared speed below which a grounded body rests */
#define ENG_SLEEP_FRAMES 6.0f
#define ENG_GROUND_DOT 0.6f

int eng_body_dynamic(const EngNode *n) { return n && n->type == ENG_RIGID_BODY; }

float eng_body_world_size(const EngNode *n) { return n->size * n->gscale; }
static float sphere_radius(const EngNode *n) { return 0.5f * n->size * n->gscale; }
static float box_half(const EngNode *n) { return 0.5f * n->size * n->gscale; }

/* A body is free only when its whole parent chain is a plain container at the
 * origin, so local `pos` == world `gpos`. */
static int body_free(const EngNode *n) {
    const EngNode *p = n->parent;
    if (!p) return 1;
    if (p->type != ENG_NODE) return 0;
    return p->gpos.x == 0 && p->gpos.y == 0 && p->gpos.z == 0 &&
           p->gscale == 1 && p->basis.m[0] == 1 && p->basis.m[4] == 1 && p->basis.m[8] == 1;
}

static void wake(EngNode *n) { if (n->type == ENG_RIGID_BODY) { n->sleep_t = 0; n->asleep = 0; } }

void eng_body_apply_impulse(EngNode *n, float x, float y, float z) {
    if (!eng_body_dynamic(n) || n->mass <= 0) return;
    n->vel[0] += x / n->mass;
    n->vel[1] += y / n->mass;
    n->vel[2] += z / n->mass;
    wake(n);
}
void eng_body_set_linear_velocity(EngNode *n, float x, float y, float z) {
    if (!eng_body_dynamic(n)) return;
    n->vel[0] = x; n->vel[1] = y; n->vel[2] = z;
    wake(n);
}
void eng_body_get_linear_velocity(EngNode *n, float *x, float *y, float *z) {
    if (x) *x = eng_body_dynamic(n) ? n->vel[0] : 0;
    if (y) *y = eng_body_dynamic(n) ? n->vel[1] : 0;
    if (z) *z = eng_body_dynamic(n) ? n->vel[2] : 0;
}
int eng_body_is_grounded(EngNode *n) {
    return n && n->type == ENG_RIGID_BODY && n->grounded;
}
void eng_body_set_mass(EngNode *n, float m) {
    if (eng_body_dynamic(n) && m > 0) n->mass = m;
}
void eng_body_set_gravity_scale(EngNode *n, float s) {
    if (n && (n->type == ENG_RIGID_BODY || n->type == ENG_STATIC_BODY)) n->gravity_scale = s;
}
void eng_body_set_shape(EngNode *n, const char *shape) {
    if (!n || (n->type != ENG_RIGID_BODY && n->type != ENG_STATIC_BODY &&
               n->type != ENG_CHARACTER_BODY)) return;
    int s = eng_shape_from_name(shape);
    if (s != ENG_SHAPE_NONE) n->shape = s;
}

void eng_physics_init(void) {}

/* Scene load: wake every rigid body and clear character floor state so a
 * freshly loaded scene starts from the poses authored in its scene file. */
static void fresh_body(EngNode *n) {
    if (n->type == ENG_RIGID_BODY) { n->grounded = 0; n->sleep_t = 0; n->asleep = 0; }
    else if (n->type == ENG_CHARACTER_BODY) n->grounded = 0;
    for (EngNode *c = n->child; c; c = c->next) fresh_body(c);
}
void eng_physics_reset(void) { if (eng_node_root()) fresh_body(eng_node_root()); }
void eng_body_sync_scene(void) {}

/* ── collision primitive: sphere vs axis-aligned box ───────────────────── */

/* Returns contact normal (pointing from the box toward the sphere centre)
 * and penetration depth; 0 = no contact. */
static int box_contact(const EngNode *dyn, const EngNode *st,
                       float *nx, float *ny, float *nz, float *pen) {
    float h = box_half(st);
    float px = dyn->gpos.x, py = dyn->gpos.y, pz = dyn->gpos.z;
    float cx = st->gpos.x, cy = st->gpos.y, cz = st->gpos.z;
    float r = sphere_radius(dyn);
    float qx = fminf(fmaxf(px, cx - h), cx + h);   /* closest point on the box */
    float qy = fminf(fmaxf(py, cy - h), cy + h);
    float qz = fminf(fmaxf(pz, cz - h), cz + h);
    float dx = px - qx, dy = py - qy, dz = pz - qz;
    float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 >= r * r) return 0;
    if (d2 > 1e-9f) {
        float d = sqrtf(d2);
        *pen = r - d; *nx = dx / d; *ny = dy / d; *nz = dz / d;
    } else {                     /* centre inside the box: exit the nearest face */
        float ex = fminf(px - (cx - h), (cx + h) - px);
        float ey = fminf(py - (cy - h), (cy + h) - py);
        float ez = fminf(pz - (cz - h), (cz + h) - pz);
        if (ex <= ey && ex <= ez) { *nx = px < cx ? -1 : 1; *ny = 0; *nz = 0; *pen = r + ex; }
        else if (ey <= ez)         { *nx = 0; *ny = py < cy ? -1 : 1; *nz = 0; *pen = r + ey; }
        else                       { *nx = 0; *ny = 0; *nz = pz < cz ? -1 : 1; *pen = r + ez; }
    }
    return 1;
}

/* Collect static colliders (boxes and spheres) into `st` (fixed capacity). */
static int collect_statics(EngNode *stack_out[256], EngNode *st[128]) {
    int count = 0, sp = 0;
    if (eng_node_root()) stack_out[sp++] = eng_node_root();
    while (sp) {
        EngNode *n = stack_out[--sp];
        if (n->type == ENG_STATIC_BODY && n->shape != ENG_SHAPE_NONE && count < 128)
            st[count++] = n;
        for (EngNode *c = n->child; c; c = c->next) if (sp < 256) stack_out[sp++] = c;
    }
    return count;
}

static void resolve_static(EngNode *dyn, const EngNode *st) {
    float nx, ny, nz, pen;
    if (st->shape == ENG_SHAPE_SPHERE) {
        float dx = dyn->gpos.x - st->gpos.x, dy = dyn->gpos.y - st->gpos.y, dz = dyn->gpos.z - st->gpos.z;
        float rr = sphere_radius(dyn) + sphere_radius(st);
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 >= rr * rr) return;
        if (d2 > 1e-9f) { float d = sqrtf(d2); pen = rr - d; nx = dx / d; ny = dy / d; nz = dz / d; }
        else { pen = rr; nx = 0; ny = 1; nz = 0; }
    } else {
        if (!box_contact(dyn, st, &nx, &ny, &nz, &pen)) return;
    }
    dyn->gpos.x += nx * pen; dyn->gpos.y += ny * pen; dyn->gpos.z += nz * pen;
    float vn = dyn->vel[0] * nx + dyn->vel[1] * ny + dyn->vel[2] * nz;
    if (vn < 0) {
        float rest = fminf(1.0f, fmaxf(0.0f, st->restitution * dyn->restitution));
        float j = -(1 + rest) * vn;
        dyn->vel[0] += j * nx; dyn->vel[1] += j * ny; dyn->vel[2] += j * nz;
    }
    /* surface classification for grounding / ceiling */
    if (ny > ENG_GROUND_DOT && vn < 0) { dyn->grounded = 1; if (dyn->vel[1] < 0) dyn->vel[1] = 0; }
    else if (ny < -ENG_GROUND_DOT && vn < 0 && dyn->vel[1] > 0) dyn->vel[1] = 0;
}

void eng_physics_step(float dt) {
    if (!eng_node_root() || dt <= 0) return;
    eng_node_update_transforms();

    /* Gather dynamic bodies that are free to move. */
    EngNode *stack[256]; int sp = 0, nb = 0;
    EngNode *bodies[128];
    if (eng_node_root()) stack[sp++] = eng_node_root();
    while (sp && nb < 128) {
        EngNode *n = stack[--sp];
        if (n->type == ENG_RIGID_BODY && body_free(n)) bodies[nb++] = n;
        for (EngNode *c = n->child; c; c = c->next) if (sp < 256) stack[sp++] = c;
    }
    if (!nb) return;

    EngNode *statics[128];
    int ns = collect_statics(stack, statics);

    /* gravity + integrate the velocity of every awake body */
    for (int i = 0; i < nb; i++) {
        EngNode *b = bodies[i];
        if (b->asleep) continue;            /* fully resting: never move again */
        b->grounded = 0;
        b->vel[1] -= ENG_GRAVITY * b->gravity_scale * dt;
        b->gpos.x += b->vel[0] * dt;
        b->gpos.y += b->vel[1] * dt;
        b->gpos.z += b->vel[2] * dt;
    }

    /* two solver passes for stability on a body resting across box seams */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < nb; i++) {
            EngNode *b = bodies[i];
            if (b->asleep) continue;
            for (int s = 0; s < ns; s++) resolve_static(b, statics[s]);
        }
        for (int i = 0; i < nb; i++) {
            bodies[i]->pos = bodies[i]->gpos;   /* keep local pos in sync */
            bodies[i]->dirty = 1;
        }
        eng_node_update_transforms();
    }

    /* put still, grounded bodies to sleep so they never jitter */
    for (int i = 0; i < nb; i++) {
        EngNode *b = bodies[i];
        if (b->asleep) continue;
        float sq = b->vel[0] * b->vel[0] + b->vel[1] * b->vel[1] + b->vel[2] * b->vel[2];
        if (b->grounded && sq < ENG_SLEEP_EPS) {
            b->sleep_t += 1;
            if (b->sleep_t >= ENG_SLEEP_FRAMES) {
                b->asleep = 1;
                b->vel[0] = b->vel[1] = b->vel[2] = 0;
            }
        } else b->sleep_t = 0;
        b->pos = b->gpos; b->dirty = 1;
    }
    eng_node_update_transforms();
}

/* ── CharacterBody3D: kinematic collide-and-slide ───────────────────────── */
/* A CharacterBody3D is a script-driven kinematic body (Godot-style): the
 * script owns gravity and sets `velocity`, then asks the engine to move one
 * frame with eng_character_move_and_slide. The body collides with
 * StaticBody3D boxes and spheres, slides along walls and lands on floors;
 * `one_way = true` on a box StaticBody3D turns it into a pass-through
 * platform that only blocks from above. Like rigid bodies, a character is
 * simulated in world space, so it must hang from the root or a plain Node
 * container. Box colliders are axis-aligned (the same convention the rigid
 * solver already uses). */

static float char_half(const EngNode *n) { return 0.5f * n->size * n->gscale; }

/* Minimum-translation-vector between a character and a static body.
 * Returns 1 with a unit normal pointing from the static toward the character
 * and the penetration depth. */
static int char_overlap(const EngNode *c, const EngNode *s,
                        float *nx, float *ny, float *nz, float *pen) {
    int csphere = c->shape == ENG_SHAPE_SPHERE;
    int ssphere = s->shape == ENG_SHAPE_SPHERE;
    float cx = c->gpos.x, cy = c->gpos.y, cz = c->gpos.z;
    float sx = s->gpos.x, sy = s->gpos.y, sz = s->gpos.z;

    if (csphere && ssphere) {
        float dx = cx - sx, dy = cy - sy, dz = cz - sz;
        float rr = char_half(c) + char_half(s);
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 >= rr * rr) return 0;
        if (d2 > 1e-9f) { float d = sqrtf(d2); *nx = dx / d; *ny = dy / d; *nz = dz / d; *pen = rr - d; }
        else { *nx = 0; *ny = 1; *nz = 0; *pen = rr; }
        return 1;
    }
    if (!csphere && !ssphere) {
        float hc = char_half(c), hs = char_half(s);
        float ox = hc + hs - fabsf(cx - sx);
        float oy = hc + hs - fabsf(cy - sy);
        float oz = hc + hs - fabsf(cz - sz);
        if (ox <= 0 || oy <= 0 || oz <= 0) return 0;
        if (ox <= oy && ox <= oz) { *pen = ox; *nx = cx < sx ? -1 : 1; *ny = *nz = 0; }
        else if (oy <= oz)         { *pen = oy; *ny = cy < sy ? -1 : 1; *nx = *nz = 0; }
        else                       { *pen = oz; *nz = cz < sz ? -1 : 1; *nx = *ny = 0; }
        return 1;
    }

    /* one box (centre b, half h) and one sphere (centre p, radius r) */
    const EngNode *box = csphere ? s : c;
    const EngNode *sph = csphere ? c : s;
    float h = char_half(box), r = char_half(sph);
    float px = sph->gpos.x, py = sph->gpos.y, pz = sph->gpos.z;
    float bx = box->gpos.x, by = box->gpos.y, bz = box->gpos.z;
    float qx = fminf(fmaxf(px, bx - h), bx + h);
    float qy = fminf(fmaxf(py, by - h), by + h);
    float qz = fminf(fmaxf(pz, bz - h), bz + h);
    float dx = px - qx, dy = py - qy, dz = pz - qz;
    float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 >= r * r) return 0;
    if (d2 > 1e-9f) { float d = sqrtf(d2); *nx = dx / d; *ny = dy / d; *nz = dz / d; *pen = r - d; }
    else {                         /* sphere centre inside the box: nearest face */
        float ex = fminf(px - (bx - h), (bx + h) - px);
        float ey = fminf(py - (by - h), (by + h) - py);
        float ez = fminf(pz - (bz - h), (bz + h) - pz);
        if (ex <= ey && ex <= ez) { *nx = px < bx ? -1 : 1; *ny = *nz = 0; *pen = r + ex; }
        else if (ey <= ez)         { *ny = py < by ? -1 : 1; *nx = *nz = 0; *pen = r + ey; }
        else                       { *nz = pz < bz ? -1 : 1; *nx = *ny = 0; *pen = r + ez; }
    }
    if (!csphere) { *nx = -*nx; *ny = -*ny; *nz = -*nz; }  /* point static → character */
    return 1;
}

/* A one-way platform blocks only a downward-moving character whose bottom was
 * already at or above the platform's top face. */
static int one_way_active(const EngNode *s, float vy, float prev_bottom) {
    if (!s->one_way || s->shape != ENG_SHAPE_BOX) return 1;
    if (vy >= 0) return 0;
    float top = s->gpos.y + char_half(s);
    return prev_bottom >= top - 1e-4f;
}

static float char_floor_probe(const EngNode *c, EngNode *statics[128], int ns, float prev_bottom) {
    /* deepest contact whose normal points up — a floor below the character.
     * A one-way platform only counts when the body was resting above its top
     * face, so a body passing up through one is never snapped back onto it. */
    float best = 0;
    for (int i = 0; i < ns; i++) {
        EngNode *s = statics[i];
        if (s->one_way && s->shape == ENG_SHAPE_BOX) {
            float top = s->gpos.y + char_half(s);
            if (prev_bottom < top - 1e-4f) continue;
        }
        float nx, ny, nz, pen;
        if (char_overlap(c, s, &nx, &ny, &nz, &pen) && ny > ENG_GROUND_DOT && pen > best) best = pen;
    }
    return best;
}

void eng_character_set_velocity(EngNode *n, float x, float y, float z) {
    if (!n || n->type != ENG_CHARACTER_BODY) return;
    n->vel[0] = x; n->vel[1] = y; n->vel[2] = z;
}
void eng_character_get_velocity(EngNode *n, float *x, float *y, float *z) {
    if (x) *x = n && n->type == ENG_CHARACTER_BODY ? n->vel[0] : 0;
    if (y) *y = n && n->type == ENG_CHARACTER_BODY ? n->vel[1] : 0;
    if (z) *z = n && n->type == ENG_CHARACTER_BODY ? n->vel[2] : 0;
}
int eng_character_is_grounded(EngNode *n) {
    return n && n->type == ENG_CHARACTER_BODY && n->grounded;
}

int eng_character_move_and_slide(EngNode *n, float dt) {
    if (!n || n->type != ENG_CHARACTER_BODY) return 0;
    if (dt <= 0) return n->grounded;
    if (dt > 0.25f) dt = 0.25f;          /* bound one frame's motion through stalls */

    eng_node_update_transforms();        /* pick up any teleports the script made */
    int was_grounded = n->grounded;
    n->grounded = 0;

    float vx = n->vel[0], vy = n->vel[1], vz = n->vel[2];
    float vy_in = vy;
    float half = char_half(n);
    float prev_bottom = n->gpos.y - half;   /* for one-way platform tests */

    EngNode *stack[256];
    EngNode *statics[128];
    int ns = collect_statics(stack, statics);

    /* sub-step the displacement so a fast body cannot tunnel through thin
     * platforms; the slide correction runs up to 4 passes per sub-step. */
    float dist = sqrtf(vx * vx + vy * vy + vz * vz) * dt;
    float step_limit = half > 1e-4f ? half * 0.5f : 0.05f;
    int steps = (int)ceilf(dist / step_limit);
    if (steps < 1) steps = 1;
    if (steps > 64) steps = 64;          /* keep the worst-case cost bounded */
    float sdt = dt / steps;

    for (int step = 0; step < steps; step++) {
        n->gpos.x += vx * sdt;          /* move once per sub-step */
        n->gpos.y += vy * sdt;
        n->gpos.z += vz * sdt;

        /* resolve the deepest contact, slide the velocity along it and repeat
         * (up to 4 times) so a corner contact settles every face at once. */
        for (int iter = 0; iter < 4; iter++) {
            float best = 0, nx = 0, ny = 0, nz = 0;
            int hit = 0;
            for (int i = 0; i < ns; i++) {
                EngNode *s = statics[i];
                if (!one_way_active(s, vy, prev_bottom)) continue;
                float tnx, tny, tnz, tpen;
                if (!char_overlap(n, s, &tnx, &tny, &tnz, &tpen)) continue;
                if (s->one_way && tny <= ENG_GROUND_DOT) continue;   /* top face only */
                if (tpen > best) { best = tpen; nx = tnx; ny = tny; nz = tnz; hit = 1; }
            }
            if (!hit) break;

            n->gpos.x += nx * best;     /* push out along the contact normal */
            n->gpos.y += ny * best;
            n->gpos.z += nz * best;

            float vdot = vx * nx + vy * ny + vz * nz;  /* slide the velocity */
            if (vdot < 0) { vx -= vdot * nx; vy -= vdot * ny; vz -= vdot * nz; }

            if (ny > ENG_GROUND_DOT && vy_in <= 0) n->grounded = 1;
        }
    }

    /* Floor snapping keeps `grounded` true while a body rests with zero
     * vertical velocity (scripts that only add gravity in the air), without
     * sticking a body that has walked off a ledge. */
    if (!n->grounded && was_grounded && vy_in <= 0) {
        float snap = half > 1e-4f ? half * 0.25f : 0.05f;
        n->gpos.y -= snap;
        float depth = char_floor_probe(n, statics, ns, prev_bottom);
        if (depth > 0) { n->gpos.y += depth; n->grounded = 1; }
        else n->gpos.y += snap;
    }

    n->vel[0] = vx; n->vel[1] = vy; n->vel[2] = vz;   /* report the slid velocity */
    n->pos = n->gpos;                                  /* keep local pos in sync */
    n->dirty = 1;
    return n->grounded;
}
