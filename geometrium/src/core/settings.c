/* Player settings (sound volume, render quality) persisted across runs.
 * Plain key=value lines in the storage directory; nothing else reads them. */
#define _POSIX_C_SOURCE 200809L
#include "settings_internal.h"
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>

static int volume = 80;      /* percent, 0..100 */
static int quality = 0;      /* REND_QUALITY_* */

void settings_apply(void) {
    snd_set_volume(volume);
    rend_set_quality(quality);
}

void settings_set_volume(int value) {
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    volume = value;
    snd_set_volume(volume);
}

void settings_set_quality(int value) {
    if (value < REND_QUALITY_AUTO || value > REND_QUALITY_LOW) value = REND_QUALITY_AUTO;
    quality = value;
    rend_set_quality(quality);
}

int settings_volume(void) { return volume; }
int settings_quality(void) { return quality; }

void settings_load(void) {
    char path[640], buf[256];
    volume = 80; quality = REND_QUALITY_AUTO;
    if (!app_save_path(path, sizeof(path), "enjoer.settings")) { settings_apply(); return; }
    FILE *f = fopen(path, "r");
    if (!f) { settings_apply(); return; }
    while (fgets(buf, sizeof(buf), f)) {
        char key[32]; int value;
        if (sscanf(buf, "%31[^=]=%d", key, &value) != 2) continue;
        if (!strcmp(key, "volume")) volume = value;
        else if (!strcmp(key, "quality")) quality = value;
    }
    fclose(f);
    if (volume < 0 || volume > 100) volume = 80;
    if (quality < REND_QUALITY_AUTO || quality > REND_QUALITY_LOW) quality = REND_QUALITY_AUTO;
    settings_apply();
}

void settings_save(void) {
    char path[640];
    if (!app_save_path(path, sizeof(path), "enjoer.settings")) return;
    char tmp[640];
    if (!app_save_path(tmp, sizeof(tmp), "enjoer.settings.tmp")) return;
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "ENJOERSET1\nvolume=%d\nquality=%d\n", volume, quality);
    fclose(f);
    if (rename(tmp, path) != 0) remove(tmp);
}
