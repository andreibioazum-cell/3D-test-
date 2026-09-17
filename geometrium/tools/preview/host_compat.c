/* Host implementations of the Android dependencies for the preview build:
 * a file-based asset manager, system logging and audio stubs
 * (the browser preview needs no sound). */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "engine.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ── logging ───────────────────────────────────────────────────────────── */
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap) {
    fprintf(stderr, "[%s] ", tag ? tag : "?");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    (void)prio;
    return 0;
}
int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = __android_log_vprint(prio, tag, fmt, ap);
    va_end(ap);
    return r;
}

/* ── file-based asset manager ──────────────────────────────────────────── */
struct AAssetManager { char root[512]; };
struct AAsset {
    unsigned char *data;
    size_t size;
    size_t pos;
};
static struct AAssetManager host_am;

AAssetManager *host_asset_manager(const char *root) {
    snprintf(host_am.root, sizeof(host_am.root), "%s", root ? root : "assets");
    return &host_am;
}

AAsset *AAssetManager_open(AAssetManager *mgr, const char *filename, int mode) {
    (void)mode;
    if (!mgr || !filename) return NULL;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", mgr->root, filename);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    AAsset *a = (AAsset *)calloc(1, sizeof(*a));
    if (!a) { fclose(f); return NULL; }
    a->data = (unsigned char *)malloc((size_t)len);
    if (!a->data) { free(a); fclose(f); return NULL; }
    a->size = fread(a->data, 1, (size_t)len, f);
    fclose(f);
    return a;
}

off_t AAsset_getLength(AAsset *a) { return a ? (off_t)a->size : 0; }

int AAsset_read(AAsset *a, void *buf, size_t count) {
    if (!a || a->pos >= a->size) return 0;
    size_t left = a->size - a->pos;
    if (count > left) count = left;
    memcpy(buf, a->data + a->pos, count);
    a->pos += count;
    return (int)count;
}

void AAsset_close(AAsset *a) {
    if (!a) return;
    free(a->data);
    free(a);
}

/* Preview has no audio output; Android uses AudioTrack. */
#ifndef PREVIEW_EXTERNAL_AUDIO
int snd_load(const char *name) { (void)name; return 1; }
int snd_play(const char *name) { (void)name; return 1; }
void snd_set_volume(int percent) { (void)percent; }
int audio_init(AAssetManager *assets) { (void)assets; return 1; }
void audio_shutdown(void) {}
void audio_pause(void) {}
void audio_resume(void) {}
void audio_set_java_vm(void *vm) { (void)vm; }

#endif
