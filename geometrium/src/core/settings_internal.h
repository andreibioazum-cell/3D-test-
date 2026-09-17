/* Internal settings API (src/core/settings.c). */
#ifndef SETTINGS_INTERNAL_H
#define SETTINGS_INTERNAL_H
#include "engine/render/rend_internal.h"

void settings_load(void);
void settings_save(void);
void settings_apply(void);
int settings_volume(void);
void settings_set_volume(int percent);
int settings_quality(void);
void settings_set_quality(int quality);
#endif
