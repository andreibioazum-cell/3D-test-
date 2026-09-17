/* C scripting: shared objects compiled from project script sources, dlopen'd.
 * Entry points: void eng_script_ready(EngNode*), void eng_script_process(EngNode*, float dt).
 *
 * A device build has neither a compiler nor a writable .so path, so there the
 * engine's built-in scripts (eng_script_builtin.c) answer instead. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "eng_internal.h"
#include "eng_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __ANDROID__
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#endif

static EngScript *scripts;

#ifndef __ANDROID__
static long mtime_of(const char *path) {
    struct stat st;
    if (stat(path, &st)) return -1;
    return (long)st.st_mtime;
}
static void compile_if_needed(const char *c_path, const char *so_path, const char *project_dir) {
    long cs = mtime_of(c_path), so = mtime_of(so_path);
    if (cs <= so) return;
    char cmd[16384];
    snprintf(cmd, sizeof(cmd),
             "cc -shared -fPIC -O2 -Isrc/engine -I\"%s/scripts\" -o \"%s\" \"%s\" 2>&1",
             project_dir, so_path, c_path);
    FILE *f = popen(cmd, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) fputs(line, stderr);
    pclose(f);
}
#endif

EngScript *eng_script_load(const char *name, const char *project_dir) {
    if (!name || !name[0]) return NULL;
    for (EngScript *s = scripts; s; s = s->next)
        if (!strcmp(s->name, name)) return s;

    EngScript *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    snprintf(s->name, sizeof(s->name), "%s", name);

#ifndef __ANDROID__
    char c_path[4096], so_path[4096];
    snprintf(c_path, sizeof(c_path), "%s/scripts/%s.c", project_dir, name);
    snprintf(so_path, sizeof(so_path), "%s/scripts/%s.so", project_dir, name);
    compile_if_needed(c_path, so_path, project_dir);
    s->handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (s->handle) {
        s->ready = (void (*)(EngNode *))dlsym(s->handle, "eng_script_ready");
        s->process = (void (*)(EngNode *, float))dlsym(s->handle, "eng_script_process");
    }
#else
    (void)project_dir;
#endif

    if (!s->handle) {
        /* No compiled script (device build, or the .c is missing): fall back to
         * the engine's built-in script of the same name, if there is one. */
        const EngBuiltinScript *b = eng_script_builtin(name);
        if (!b) {
            app_log("engine: script '%s' not found", name);
            free(s);
            return NULL;
        }
        s->ready = b->ready;
        s->process = b->process;
    }
    s->next = scripts;
    scripts = s;
    return s;
}

static void ready_tree(EngScript *s, EngNode *n) {
    if (n->script == s && s->ready) s->ready(n);
    for (EngNode *c = n->child; c; c = c->next) ready_tree(s, c);
}
static void process_tree(EngScript *s, EngNode *n, float dt) {
    if (n->script == s && s->process) s->process(n, dt);
    for (EngNode *c = n->child; c; c = c->next) process_tree(s, c, dt);
}
void eng_script_process_all(float dt) {
    for (EngScript *s = scripts; s; s = s->next) {
        EngNode *root = eng_node_root();
        if (root) process_tree(s, root, dt);
    }
}
void eng_script_discard_all(void) {
    EngScript *s = scripts;
    while (s) {
        EngScript *next = s->next;
#ifndef __ANDROID__
        if (s->handle) dlclose(s->handle);
#endif
        free(s);
        s = next;
    }
    scripts = NULL;
}

void eng_script_ready_run(void) {
    for (EngScript *s = scripts; s; s = s->next) {
        EngNode *root = eng_node_root();
        if (root) ready_tree(s, root);
    }
}
