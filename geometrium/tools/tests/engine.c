/* Engine regression: node tree math, scene parsing, C scripting, rendering. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "engine/eng_internal.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "engine:%d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-4f)

static void test_types(void) {
    CHECK(eng_type_from_name("MeshInstance3D") == ENG_MESH);
    CHECK(eng_type_from_name("Camera3D") == ENG_CAMERA);
    CHECK(eng_type_from_name("DirectionalLight3D") == ENG_DIR_LIGHT);
    CHECK(eng_type_from_name("OmniLight3D") == ENG_OMNI_LIGHT);
    CHECK(eng_type_from_name("VoxelWorld3D") == ENG_VOXEL_WORLD);
    CHECK(eng_type_from_name("Node3D") == ENG_NODE3D);
    CHECK(eng_type_from_name("Bogus") < 0);
    CHECK(!strcmp(eng_type_name(ENG_MESH), "MeshInstance3D"));
}

static void test_tree(void) {
    EngNode *root = eng_node_create(ENG_NODE, "Root");
    eng_node_set_root(root);
    EngNode *a = eng_node_create(ENG_NODE3D, "A");
    EngNode *b = eng_node_create(ENG_NODE3D, "B");
    eng_node_attach(root, a);
    eng_node_attach(a, b);
    CHECK(eng_node_find("A") == a);
    CHECK(eng_node_find("Root/A/B") == b);
    CHECK(eng_node_find("A/B") == b);
    CHECK(eng_node_find("A/C") == NULL);
    eng_node_destroy(a);
    CHECK(eng_node_find("A") == NULL);
    CHECK(eng_node_root() == root);
    eng_node_destroy(root);
    CHECK(eng_node_root() == NULL);
}

static void test_transforms(void) {
    EngNode *root = eng_node_create(ENG_NODE, "Root");
    eng_node_set_root(root);
    EngNode *p = eng_node_create(ENG_NODE3D, "P");
    EngNode *c = eng_node_create(ENG_NODE3D, "C");
    eng_node_attach(root, p);
    eng_node_attach(p, c);
    eng_node3d_set_rotation(p, (float)(M_PI / 2), 0, 0);
    eng_node3d_set_position(c, 1, 0, 0);
    eng_node3d_set_scale(p, 2);
    eng_node_update_transforms();
    float x, y, z;
    eng_node3d_get_position(c, &x, &y, &z);
    /* parent yaw 90 deg maps +X to -Z, and the parent scale doubles the offset */
    CHECK(NEAR(x, 0) && NEAR(y, 0) && NEAR(z, -2));
    eng_node3d_set_rotation(c, 0, 0, 0);
    eng_node_update_transforms();
    float yaw, pitch, roll;
    eng_node3d_get_rotation(p, &yaw, &pitch, &roll);
    CHECK(NEAR(yaw, (float)(M_PI / 2)));
    eng_node_destroy(root);
    eng_node_set_root(NULL);
}

static void make_dirs(const char *in) { /* mkdir -p for the given directory path */
    char path[256];
    snprintf(path, sizeof(path), "%s", in);
    for (char *s = path + 1; *s; s++) {
        if (*s == '/') {
            *s = 0;
            mkdir(path, 0777);
            *s = '/';
        }
    }
    mkdir(path, 0777);
}

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    CHECK(f);
    fputs(text, f);
    fclose(f);
}

static void test_scene_parse(void) {
    make_dirs("build-tests/fixtures/engproj/scenes");
    make_dirs("build-tests/fixtures/engproj/scripts");
    write_file("build-tests/fixtures/engproj/scenes/main.escn",
               "# test scene\n"
               "[node name=\"Root\" type=\"Node\"]\n"
               "\n"
               "[node name=\"Cam\" type=\"Camera3D\" parent=\".\"]\n"
               "position = 1 2 3\n"
               "fov = 70\n"
               "current = true\n"
               "\n"
               "[node name=\"Sun\" type=\"DirectionalLight3D\" parent=\".\"]\n"
               "energy = 0.75\n"
               "light_color = 1 0.5 0.25\n"
               "\n"
               "[node name=\"Cube\" type=\"MeshInstance3D\" parent=\".\"]\n"
               "mesh = cube\n"
               "color = 0.5 0.5 1\n"
               "size = 2.5\n"
               "script = \"tick\"\n");
    write_file("build-tests/fixtures/engproj/project.eng",
               "name = Engine Test\nmain_scene = scenes/main.escn\n");
    write_file("build-tests/fixtures/engproj/scripts/tick.c",
               "#include \"eng_api.h\"\n"
               "void eng_script_ready(EngNode *self) {\n"
               "    eng_node3d_set_position(self, 0, 5, 0);\n"
               "}\n"
               "void eng_script_process(EngNode *self, float dt) {\n"
               "    float x, y, z;\n"
               "    eng_node3d_get_position(self, &x, &y, &z);\n"
               "    eng_node3d_set_position(self, x, y + dt, z);\n"
               "}\n");
    CHECK(eng_project_load("build-tests/fixtures/engproj"));
    EngNode *cam = eng_node_find("Cam");
    CHECK(cam && cam->type == ENG_CAMERA && cam->current);
    float x, y, z;
    eng_node3d_get_position(cam, &x, &y, &z);
    CHECK(NEAR(x, 1) && NEAR(y, 2) && NEAR(z, 3));
    EngNode *sun = eng_node_find("Sun");
    CHECK(sun && sun->type == ENG_DIR_LIGHT && NEAR(sun->energy, 0.75f));
    EngNode *cube = eng_node_find("Cube");
    CHECK(cube && cube->type == ENG_MESH && cube->mesh_kind == ENG_MESH_CUBE);
    CHECK(NEAR(cube->size, 2.5f) && cube->has_color);
    CHECK(cube->script && cube->script->ready && cube->script->process);
    /* ready lifted the cube to y=5, process then integrates */
    eng_update(0.5f);
    eng_node3d_get_position(cube, &x, &y, &z);
    CHECK(NEAR(y, 5.5f));

    /* rendering: the scene camera must produce a frame */
    static uint32_t pixels[96 * 54];
    Buffer buffer = {pixels, 96, 54, 96};
    eng_node_update_transforms();
    CHECK(eng_draw(&buffer));
    eng_project_free();
    CHECK(eng_node_root() == NULL);
}

static void test_meshes(void) {
    const EngFace *faces;
    CHECK(eng_mesh_faces(ENG_MESH_CUBE, &faces) == 6);
    CHECK(eng_mesh_faces(ENG_MESH_PLANE, &faces) == 1);
    CHECK(eng_mesh_faces(ENG_MESH_SPHERE, &faces) > 10);
    CHECK(eng_mesh_faces(ENG_MESH_CYLINDER, &faces) == 10);
    RendMaterial *m = eng_flat_material(0xff112233u);
    CHECK(m && m->palette[0] == 0xff112233u && m->colors == 1);
    CHECK(eng_flat_material(0xff112233u) == m); /* cached */
}

/* ── physics & input pillars ────────────────────────────────────────────── */

/* Build a scene of one static ground + bodies under a plain container root.
 * The ground box has half-extent `L` in every axis and its top face at y=0
 * (box centre placed at y=-L), giving L of reach in x/z for rolling bodies. */
static EngNode *scene_floor_root(float L) {
    EngNode *root = eng_node_create(ENG_NODE, "Root");
    eng_node_set_root(root);
    EngNode *floor = eng_node_create(ENG_STATIC_BODY, "Floor");
    eng_mesh_set_size(floor, 2 * L);            /* full side = 2*L → half = L */
    eng_node3d_set_position(floor, 0, -L, 0);
    eng_mesh_set(floor, "cube");
    eng_node_attach(root, floor);
    return root;
}
static EngNode *add_ball(EngNode *root, const char *name, float x, float y, float z) {
    EngNode *b = eng_node_create(ENG_RIGID_BODY, name);
    eng_node3d_set_position(b, x, y, z);
    eng_mesh_set(b, "sphere");
    eng_node_attach(root, b);
    return b;
}

static void test_physics_fall(void) {
    EngNode *root = scene_floor_root(6.0f);            /* ground top at y=0 */
    EngNode *ball = add_ball(root, "Ball", 0, 5, 0);   /* radius 0.5, size 1 */
    eng_node_update_transforms();
    for (int i = 0; i < 400; i++) eng_physics_step(1.0f / 60.0f);
    /* rests with its centre ~radius above the floor top (y=0) */
    CHECK(ball->grounded);
    CHECK(fabsf(ball->gpos.y - 0.5f) < 0.02f);
    CHECK(fabsf(ball->vel[1]) < 0.5f);
    /* an upward impulse makes it leave the ground, gravity brings it back */
    eng_body_apply_impulse(ball, 0, 8, 0);
    eng_physics_step(1.0f / 60.0f);
    CHECK(ball->gpos.y > 0.55f);
    eng_node_destroy(root);
    eng_node_set_root(NULL);
}

static void test_physics_obstacle(void) {
    EngNode *root = scene_floor_root(6.0f);
    /* a tall wall the ball cannot cross (box: half 1, from y 0..2) */
    EngNode *wall = eng_node_create(ENG_STATIC_BODY, "Wall");
    eng_node3d_set_position(wall, 6, 1, 0);
    eng_mesh_set_size(wall, 2);
    eng_node_attach(root, wall);
    EngNode *ball = add_ball(root, "Ball", 0, 1, 0);
    eng_body_set_linear_velocity(ball, 6, 0, 0);   /* roll toward the wall */
    eng_node_update_transforms();
    for (int i = 0; i < 300; i++) eng_physics_step(1.0f / 60.0f);
    /* blocked by the wall face at x=5 (centre 6, half 1, ball radius 0.5) */
    CHECK(ball->gpos.x < 5.6f);
    CHECK(ball->gpos.x > 3.0f);                    /* and actually moved */
    eng_node_destroy(root);
    eng_node_set_root(NULL);
}

static void test_input_api(void) {
    eng_input_reset();
    CHECK(!eng_input_is_pressed(ENG_KEY_SPACE));
    eng_input_feed_key(ENG_KEY_SPACE, 1);
    CHECK(eng_input_is_pressed(ENG_KEY_SPACE));
    eng_input_feed_key(ENG_KEY_SPACE, 0);
    CHECK(!eng_input_is_pressed(ENG_KEY_SPACE));
    eng_input_feed_key(ENG_KEY_A, 1);
    CHECK(eng_input_is_pressed(ENG_KEY_A));
    CHECK(!eng_input_is_pressed(ENG_KEY_D));
    float x = 0, y = 0; int down = 0;
    CHECK(!eng_input_pointer(&x, &y, &down));
    eng_input_feed_pointer(123, 456, 1);
    CHECK(eng_input_pointer(&x, &y, &down));
    CHECK(NEAR(x, 123) && NEAR(y, 456) && down == 1);
    eng_input_reset();
    CHECK(!eng_input_is_pressed(ENG_KEY_A));
}

static void test_body_scene_parse(void) {
    make_dirs("build-tests/fixtures/bodyproj/scenes");
    write_file("build-tests/fixtures/bodyproj/project.eng",
               "name = Physics Test\nmain_scene = scenes/main.escn\n");
    write_file("build-tests/fixtures/bodyproj/scenes/main.escn",
               "[node name=\"Root\" type=\"Node\"]\n"
               "[node name=\"Floor\" type=\"StaticBody3D\" parent=\".\"]\n"
               "mesh = cube\n"
               "shape = box\n"
               "size = 2\n"
               "position = 0 -1 0\n"
               "[node name=\"Ball\" type=\"RigidBody3D\" parent=\".\"]\n"
               "mesh = sphere\n"
               "mass = 2\n"
               "gravity_scale = 1\n"
               "position = 0 5 0\n");

    CHECK(eng_project_load("build-tests/fixtures/bodyproj"));
    EngNode *ball = eng_node_find("Ball");
    CHECK(ball && ball->type == ENG_RIGID_BODY);
    CHECK(NEAR(ball->mass, 2.0f));
    EngNode *floor = eng_node_find("Floor");
    CHECK(floor && floor->type == ENG_STATIC_BODY && floor->shape == ENG_SHAPE_BOX);
    /* falling with mass 2 still settles on the floor */
    for (int i = 0; i < 400; i++) eng_update(1.0f / 60.0f);
    CHECK(ball->grounded);
    CHECK(fabsf(ball->gpos.y - 0.5f) < 0.02f);
    eng_project_free();
    CHECK(eng_node_root() == NULL);
}

/* ── CharacterBody3D: kinematic collide-and-slide ──────────────────────── */

static void hero_reset(EngNode *hero) {
    eng_node3d_set_position(hero, 0, 1, 0);
    eng_character_set_velocity(hero, 0, 0, 0);
    hero->grounded = 0;
    eng_node_update_transforms();
}

static void test_character_body(void) {
    EngNode *root = eng_node_create(ENG_NODE, "Root");
    eng_node_set_root(root);
    EngNode *floor = eng_node_create(ENG_STATIC_BODY, "Floor");
    eng_mesh_set_size(floor, 20);              /* top face at y=0 */
    eng_node3d_set_position(floor, 0, -10, 0);
    eng_node_attach(root, floor);

    EngNode *hero = eng_node_create(ENG_CHARACTER_BODY, "Hero");
    eng_mesh_set(hero, "cube");
    eng_mesh_set_size(hero, 2);                /* box half-extent 1 */
    eng_node3d_set_position(hero, 0, 3, 0);
    eng_node_attach(root, hero);
    eng_node_update_transforms();

    /* gravity (script-style) pulls the hero onto the floor: centre at y=1 */
    for (int i = 0; i < 180; i++) {
        float vx, vy, vz;
        eng_character_get_velocity(hero, &vx, &vy, &vz);
        vy -= 22.0f / 60.0f;
        eng_character_set_velocity(hero, vx, vy, vz);
        eng_character_move_and_slide(hero, 1.0f / 60.0f);
    }
    float x, y, z;
    CHECK(eng_character_is_grounded(hero));
    eng_node3d_get_position(hero, &x, &y, &z);
    CHECK(NEAR(y, 1.0f) && NEAR(x, 0.0f) && NEAR(z, 0.0f));

    /* a diagonal down-forward push slides along the floor, not into it */
    hero_reset(hero);
    for (int i = 0; i < 60; i++) {
        eng_character_set_velocity(hero, 3, -10, 0);
        eng_character_move_and_slide(hero, 1.0f / 60.0f);
    }
    eng_node3d_get_position(hero, &x, &y, &z);
    CHECK(NEAR(y, 1.0f));                      /* stayed on top of the floor */
    CHECK(x > 2.0f);                           /* and slid forward */
    CHECK(eng_character_is_grounded(hero));

    /* a head-on wall blocks the hero but leaves it standing */
    EngNode *wall = eng_node_create(ENG_STATIC_BODY, "Wall");
    eng_mesh_set_size(wall, 2);                /* x in [3,5], y in [0,2] */
    eng_node3d_set_position(wall, 4, 1, 0);
    eng_node_attach(root, wall);
    hero_reset(hero);
    for (int i = 0; i < 120; i++) {
        eng_character_set_velocity(hero, 3, -1, 0);
        eng_character_move_and_slide(hero, 1.0f / 60.0f);
    }
    eng_node3d_get_position(hero, &x, &y, &z);
    CHECK(x < 2.0f + 0.02f);                   /* face at x=3: hero half 1 */
    CHECK(eng_character_is_grounded(hero));
    CHECK(NEAR(y, 1.0f) && NEAR(z, 0.0f));

    /* one-way platform spanning y in [2,6]: up through, then land on top */
    EngNode *plank = eng_node_create(ENG_STATIC_BODY, "Plank");
    eng_mesh_set_size(plank, 4);
    eng_node3d_set_position(plank, 0, 4, 0);
    plank->one_way = 1;
    eng_node_attach(root, plank);

    hero_reset(hero);
    for (int i = 0; i < 30; i++) {             /* rise straight up through it */
        eng_character_set_velocity(hero, 0, 12, 0);
        eng_character_move_and_slide(hero, 1.0f / 60.0f);
    }
    eng_node3d_get_position(hero, &x, &y, &z);
    CHECK(y > 6.0f);                           /* passed cleanly through */
    CHECK(!eng_character_is_grounded(hero));

    for (int i = 0; i < 180; i++) {            /* gravity drops it onto the top */
        float vx, vy, vz;
        eng_character_get_velocity(hero, &vx, &vy, &vz);
        vy -= 22.0f / 60.0f;
        eng_character_set_velocity(hero, vx, vy, vz);
        eng_character_move_and_slide(hero, 1.0f / 60.0f);
    }
    CHECK(eng_character_is_grounded(hero));
    eng_node3d_get_position(hero, &x, &y, &z);
    CHECK(NEAR(y, 7.0f));                      /* resting on the top face (y=6) */
    eng_node_destroy(root);
    eng_node_set_root(NULL);
}

/* The whole pipeline: a .escn with a CharacterBody3D + one-way platform and a
 * C script compiled via dlopen drive the character down onto the floor. */
static void test_character_scene_parse(void) {
    make_dirs("build-tests/fixtures/charproj/scenes");
    make_dirs("build-tests/fixtures/charproj/scripts");
    write_file("build-tests/fixtures/charproj/project.eng",
               "name = Character Test\nmain_scene = scenes/main.escn\n");
    write_file("build-tests/fixtures/charproj/scenes/main.escn",
               "[node name=\"Root\" type=\"Node\"]\n"
               "[node name=\"Floor\" type=\"StaticBody3D\" parent=\".\"]\n"
               "mesh = cube\nshape = box\nsize = 12\nposition = 0 -6 0\n"
               "[node name=\"Plank\" type=\"StaticBody3D\" parent=\".\"]\n"
               "mesh = cube\nshape = box\nsize = 4\none_way = true\nposition = 0 2 0\n"
               "[node name=\"Hero\" type=\"CharacterBody3D\" parent=\".\"]\n"
               "mesh = cube\nshape = box\nsize = 2\nvelocity = 1 0 0\nposition = 0 3 0\n"
               "script = \"hero\"\n");
    write_file("build-tests/fixtures/charproj/scripts/hero.c",
               "#include \"eng_api.h\"\n"
               "void eng_script_process(EngNode *self, float dt) {\n"
               "    float vx, vy, vz;\n"
               "    eng_character_get_velocity(self, &vx, &vy, &vz);\n"
               "    vy -= 22.0f * dt;\n"
               "    eng_character_set_velocity(self, vx, vy, vz);\n"
               "    eng_character_move_and_slide(self, dt);\n"
               "}\n");

    CHECK(eng_project_load("build-tests/fixtures/charproj"));
    EngNode *hero = eng_node_find("Hero");
    CHECK(hero && hero->type == ENG_CHARACTER_BODY);
    CHECK(hero->shape == ENG_SHAPE_BOX);
    CHECK(NEAR(hero->vel[0], 1.0f));           /* scene `velocity` parsed */
    EngNode *plank = eng_node_find("Plank");
    CHECK(plank && plank->one_way);            /* scene `one_way = true` parsed */

    for (int i = 0; i < 240; i++) eng_update(1.0f / 60.0f);
    CHECK(eng_character_is_grounded(hero));    /* script fell through the plank */
    float x, y, z;
    eng_node3d_get_position(hero, &x, &y, &z);
    CHECK(fabsf(y - 1.0f) < 0.02f);            /* settled on the floor (top y=0) */
    CHECK(x > 0.5f);                           /* and kept drifting +X */
    eng_project_free();
    CHECK(eng_node_root() == NULL);
}

int main(void) {
    test_types();
    test_tree();
    test_transforms();
    test_scene_parse();
    test_meshes();
    test_input_api();
    test_physics_fall();
    test_physics_obstacle();
    test_body_scene_parse();
    test_character_body();
    test_character_scene_parse();
    printf("engine: %d groups ok\n", 11);
    return 0;
}
