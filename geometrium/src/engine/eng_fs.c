/* Engine file IO.
 *
 * Projects, manifests and scenes are read by relative path ("projects/demo/
 * project.eng"). On the host those are plain files next to the binary; inside
 * the APK the very same paths resolve against the asset tree staged by
 * stage_assets.py. Keeping the two behind one call means a project directory
 * is byte-identical on the PC and on the phone.
 */
#include "eng_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ANDROID__

static AAssetManager *fs_assets;

void eng_fs_set_assets(AAssetManager *assets) { fs_assets = assets; }

int eng_fs_read(const char *path, char **out, size_t *len) {
    uint8_t *data = NULL;
    size_t size = 0;
    if (!out || !len) return 0;
    *out = NULL;
    *len = 0;
    if (!path || !asset_read(fs_assets, path, &data, &size) || !data) return 0;
    char *text = (char *)malloc(size + 1);
    if (!text) { free(data); return 0; }
    memcpy(text, data, size);
    text[size] = 0;              /* scene parsing works on NUL-terminated text */
    free(data);
    *out = text;
    *len = size;
    return 1;
}

/* APK assets cannot be enumerated for subdirectories, so staging writes an
 * index file ("projects/index.txt", one entry per line) and the phone reads
 * that instead of walking the tree. */
int eng_fs_list(const char *dir, char names[][ENG_FS_NAME_MAX], int max) {
    char index[512];
    char *text = NULL;
    size_t len = 0;
    if (!dir || max <= 0) return 0;
    snprintf(index, sizeof(index), "%s/index.txt", dir);
    if (!eng_fs_read(index, &text, &len)) return 0;
    int count = 0;
    char *line = text;
    while (line && *line && count < max) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        char *e = s + strlen(s);
        while (e > s && (e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        if (*s && *s != '#') snprintf(names[count++], ENG_FS_NAME_MAX, "%.63s", s);
        line = nl ? nl + 1 : NULL;
    }
    free(text);
    return count;
}

#else /* host: plain files, real directory listing */

#include <dirent.h>

static AAssetManager *fs_assets;

void eng_fs_set_assets(AAssetManager *assets) { fs_assets = assets; (void)fs_assets; }

int eng_fs_read(const char *path, char **out, size_t *len) {
    FILE *f;
    long size;
    char *text;
    if (!out || !len) return 0;
    *out = NULL;
    *len = 0;
    if (!path) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > 16 * 1024 * 1024) { fclose(f); return 0; }
    text = (char *)malloc((size_t)size + 1);
    if (!text) { fclose(f); return 0; }
    if (size > 0 && fread(text, 1, (size_t)size, f) != (size_t)size) {
        free(text);
        fclose(f);
        return 0;
    }
    text[size] = 0;
    fclose(f);
    *out = text;
    *len = (size_t)size;
    return 1;
}

int eng_fs_list(const char *dir, char names[][ENG_FS_NAME_MAX], int max) {
    DIR *d;
    struct dirent *entry;
    int count = 0;
    if (!dir || max <= 0) return 0;
    d = opendir(dir);
    if (!d) return 0;
    while (count < max && (entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        snprintf(names[count++], ENG_FS_NAME_MAX, "%.63s", entry->d_name);
    }
    closedir(d);
    /* readdir order is filesystem order: sort so the launcher is stable. */
    for (int i = 1; i < count; i++)
        for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char tmp[ENG_FS_NAME_MAX];
            memcpy(tmp, names[j - 1], ENG_FS_NAME_MAX);
            memcpy(names[j - 1], names[j], ENG_FS_NAME_MAX);
            memcpy(names[j], tmp, ENG_FS_NAME_MAX);
        }
    return count;
}

#endif
