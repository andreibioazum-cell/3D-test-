/* Project registry: find the engine projects, read their manifests, run one.
 *
 * A project is a directory holding a `project.eng` manifest:
 *
 *   name = Demo Scene
 *   main_scene = scenes/main.escn
 *
 * The registry scans a root directory for `project.eng` manifests (on the
 * host, plain folders; inside an APK, the staged asset folder plus an
 * index.txt) and opens whichever project the caller picks. The shipped app
 * no longer embeds the engine, but the API and its test suite remain for
 * host use.
 */
#include "eng_internal.h"
#include <stdio.h>
#include <string.h>

#define ENG_MAX_PROJECTS 16

typedef struct {
    char dir[256];
    char name[64];
} EngProject;

static EngProject projects[ENG_MAX_PROJECTS];
static int project_count;
static int project_index = -1;
static char project_root[512];
static int active;

/* ── manifest parsing ─────────────────────────────────────────────────── */

static void manifest_field(const char *text, const char *key, char *out, size_t outsz) {
    size_t klen = strlen(key);
    const char *p = text;
    out[0] = 0;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        const char *line = p;
        size_t llen = nl ? (size_t)(nl - p) : strlen(p);
        p = nl ? nl + 1 : NULL;
        if (llen > 0 && line[0] == '#') continue;
        const char *eq = memchr(line, '=', llen);
        if (!eq) continue;
        /* trim the key */
        const char *ks = line;
        const char *ke = eq;
        while (ks < ke && (*ks == ' ' || *ks == '\t')) ks++;
        while (ke > ks && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
        if ((size_t)(ke - ks) != klen || strncmp(ks, key, klen) != 0) continue;
        /* trim the value */
        const char *vs = eq + 1;
        const char *ve = line + llen;
        while (vs < ve && (*vs == ' ' || *vs == '\t')) vs++;
        while (ve > vs && (ve[-1] == '\r' || ve[-1] == '\n' || ve[-1] == ' ' || ve[-1] == '\t')) ve--;
        size_t n = (size_t)(ve - vs);
        if (n >= outsz) n = outsz - 1;
        memcpy(out, vs, n);
        out[n] = 0;
        return;
    }
}

/* ── discovery ────────────────────────────────────────────────────────── */

const char *eng_project_root(void) { return project_root; }

int eng_project_scan(const char *root) {
    char names[ENG_MAX_PROJECTS][ENG_FS_NAME_MAX];
    project_count = 0;
    project_index = -1;
    snprintf(project_root, sizeof(project_root), "%s", root && root[0] ? root : "projects");
    int found = eng_fs_list(project_root, names, ENG_MAX_PROJECTS);
    for (int i = 0; i < found && project_count < ENG_MAX_PROJECTS; i++) {
        char manifest[512], *text = NULL;
        size_t len = 0;
        snprintf(manifest, sizeof(manifest), "%.380s/%.63s/project.eng", project_root, names[i]);
        if (!eng_fs_read(manifest, &text, &len)) continue;   /* not a project */
        EngProject *p = &projects[project_count];
        snprintf(p->dir, sizeof(p->dir), "%.190s/%.63s", project_root, names[i]);
        manifest_field(text, "name", p->name, sizeof(p->name));
        free(text);
        if (!p->name[0]) snprintf(p->name, sizeof(p->name), "%.63s", names[i]);
        project_count++;
    }
    if (!project_count) app_log("engine: no projects found in '%s'", project_root);
    return project_count;
}

int eng_project_count(void) { return project_count; }

const char *eng_project_dir(int index) {
    return index >= 0 && index < project_count ? projects[index].dir : NULL;
}

const char *eng_project_title(int index) {
    return index >= 0 && index < project_count ? projects[index].name : NULL;
}

int eng_project_index(void) { return project_index; }

/* ── loading ──────────────────────────────────────────────────────────── */

int eng_project_load(const char *directory) {
    char manifest[512], scene_path[512], main_scene[256], name[64];
    char *text = NULL;
    size_t len = 0;
    if (!directory || !directory[0]) return 0;
    snprintf(manifest, sizeof(manifest), "%s/project.eng", directory);
    if (!eng_fs_read(manifest, &text, &len)) {
        app_log("engine: no project.eng in '%s'", directory);
        return 0;
    }
    manifest_field(text, "name", name, sizeof(name));
    manifest_field(text, "main_scene", main_scene, sizeof(main_scene));
    free(text);
    if (!main_scene[0]) {
        app_log("engine: project '%s' has no main_scene", directory);
        return 0;
    }
    if (main_scene[0] == '/') snprintf(scene_path, sizeof(scene_path), "%s", main_scene);
    else snprintf(scene_path, sizeof(scene_path), "%s/%s", directory, main_scene);
    eng_scene_set_project_dir(directory);
    if (!eng_scene_load(scene_path)) return 0;
    if (name[0]) app_log("engine: project '%s'", name);
    active = 1;
    /* Track which scanned project is running (index stays -1 when the loaded
     * directory is outside the scanned root, e.g. --project). */
    project_index = -1;
    for (int i = 0; i < project_count; i++)
        if (!strcmp(projects[i].dir, directory)) project_index = i;
    return 1;
}

int eng_project_open(int index) {
    const char *dir = eng_project_dir(index);
    if (!dir) return 0;
    eng_project_free();
    if (!eng_project_load(dir)) return 0;
    project_index = index;
    return 1;
}

void eng_project_free(void) {
    eng_scene_free();
    eng_script_discard_all();
    active = 0;
    project_index = -1;
}

int eng_project_active(void) { return active; }

const char *eng_project_name(void) {
    const char *t = eng_project_title(project_index);
    return t ? t : "";
}
