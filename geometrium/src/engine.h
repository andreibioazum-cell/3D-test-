/* Public C engine API: platform state, assets, HUD and game lifecycle. */
#ifndef ENJOER_ENGINE_H
#define ENJOER_ENGINE_H
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <android/asset_manager.h>
#include <android/log.h>

typedef struct { uint32_t *pixels; int width, height, stride; } Buffer;
typedef struct { uint32_t *pixels; int width, height; } Image;
extern int screen_w, screen_h;
extern double dt; /* actual frame interval; physics clamps its own step */

typedef void (*AppCallback)(void *);
int app_call(AppCallback callback, void *arg, const char *label);
void app_fail(const char *format, ...);
const char *app_error(void);
int app_failed(void);
void app_clear_error(void);
void app_log(const char *format, ...);
void app_log_error(const char *format, ...);
double app_now(void);
void app_set_storage(const char *directory);
void app_quit(void);
void app_set_activity(void *activity);
void app_set_java_vm(void *vm);
int app_save_path(char *out, size_t size, const char *name);
int asset_read(AAssetManager *assets, const char *name, uint8_t **data, size_t *size);
int image_load(AAssetManager *assets, const char *name, Image *out);
void image_free(Image *image);

int gfx_init(AAssetManager *assets);
int gfx_begin_frame(Buffer *buffer);
void gfx_end_frame(void);
void gfx_cancel_frame(void);
void gfx_shutdown(void);
void gfx_error_screen(const char *message);
void rect(float x, float y, float w, float h, uint32_t color);
void roundrect(float x, float y, float w, float h, float radius, uint32_t color);
void circle(float x, float y, float radius, uint32_t color);
void ring(float x, float y, float radius, float thickness, uint32_t color);
void line(float x1, float y1, float x2, float y2, float thickness, uint32_t color);
void image_draw(const Image *image, float x, float y, float w, float h);
void text_scaled(const char *string, float x, float y, uint32_t color, float scale);
int text_width(const char *string);

int snd_load(const char *name);
int snd_play(const char *name);
void snd_set_volume(int percent);   /* 0..100, default 80 */
int audio_init(AAssetManager *assets);
void audio_shutdown(void);
void audio_pause(void);
void audio_resume(void);
void audio_set_java_vm(void *vm);

void game_init(AAssetManager *assets);
void game_update(void);
void game_draw(Buffer *buffer);
void game_touch(float x, float y, int action, int pointer_id);
void game_key(const char *name, int down);
void game_cancel_input(void);
void game_reset(void);
void game_save(void);
/*
 * Screens (src/core/game.c): a Minecraft-style main menu with flat beveled
 * buttons — Play / Servers / Options / Quit — plus a placeholder Servers
 * screen and an Options screen (sound volume, render quality). "Play"
 * starts the Geometrium block world; the round button, Escape, M or the
 * Android back key return to the menu.
 */
enum { GAME_SCREEN_MAIN = 0, GAME_SCREEN_SERVERS, GAME_SCREEN_OPTIONS, GAME_SCREEN_GAME };
int  game_menu_open(void);
int  game_in_geometrium(void);
int  game_screen(void);
const char *game_screen_name(void);
void game_menu_button_geom(int index, float *x, float *y, float *w, float *h);
void game_menu_button_geom2(float *x, float *y, float *r);  /* round in-game button */
int  game_settings_volume(void);
int  game_settings_quality(void);
#endif
