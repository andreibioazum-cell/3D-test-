#ifndef RUNTIME_H
#define RUNTIME_H
#include <math.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_window.h>
typedef struct {
    uint32_t *pixels; /* больше не используется: рендер идёт через Vulkan */
    int width;
    int height;
    int stride;
} Buffer;
extern int screen_w, screen_h;
extern double dt;
extern int mouse_clicked;
extern double ds_mouse_x, ds_mouse_y;
typedef struct { float x, y, dx, dy, ox, oy, r; } Joy;
extern Joy joy;
void ds_log(const char *format, ...);
void ds_log_err(const char *format, ...);
void ds_console_log(int is_error, const char *format, ...);
typedef void (*DSProtectedFunction)(void *userdata);
int ds_call_protected(DSProtectedFunction function, void *userdata, const char *label);
void ds_runtime_error(const char *format, ...);
const char *ds_runtime_error_message(void);
int ds_script_has_error(void);
void ds_clear_runtime_error(void);
void ds_request_script_restart(void);
int ds_script_restart_requested(void);
void ds_clear_script_restart(void);
char *ds_concat(const char *left, const char *right);
char *ds_num_to_string(double number);
void ds_string_pool_reset(void);
int console_count(void);
const char *console_line(int index);
int console_type(int index);
void console_clear(void);
typedef struct DSArray DSArray;
DSArray* arr_new(void);
void arr_push(DSArray* a, double v);
double arr_get(DSArray* a, double idx);
void arr_set(DSArray* a, double idx, double v);
double arr_len(DSArray* a);
void arr_clear(DSArray* a);
void arr_free(DSArray* a);
double clamp(double v, double lo, double hi);
double lerp(double a, double b, double t);
double dist(double x1, double y1, double x2, double y2);
/* Математика для скриптов (реализация в native/runtime/core.inc): минимум,
 * максимум, модуль, округление до целого, знак, остаток от деления и отбрасывание
 * дробной части. В DimScript они пишутся без префикса ds_: min, max, abs, round,
 * sign, mod, trunc (см. BUILTINS в ds_compiler.py и LANGUAGE.md). */
double ds_min(double a, double b);
double ds_max(double a, double b);
double ds_abs(double v);
double ds_round(double v);
double ds_sign(double v);
double ds_mod(double a, double b);
double ds_trunc(double v);
double str_len(const char *s);
int str_eq(const char *a, const char *b);
int str_contains(const char *hay, const char *needle);
double str_index_of(const char *hay, const char *needle);
int str_starts_with(const char *s, const char *pref);
int str_ends_with(const char *s, const char *suf);
const char *str_sub(const char *s, double start, double len);
double str_to_num(const char *s);
const char *str_trim(const char *s);
const char *str_lower(const char *s);
const char *str_upper(const char *s);
void ds_set_activity(void *activity);
void keyboard_show(void);
void keyboard_hide(void);
const char* keyboard_get_text(void);
const char* keyboard_get_raw(void);
void keyboard_clear(void);
int keyboard_visible(void);
int keyboard_enter_pressed(void);
int keyboard_handle_key(int keycode, int action, int meta);
int keyboard_uses_editor(void);
void keyboard_type(const char *text);
void keyboard_backspace(void);
void keyboard_commit_utf8(const char *utf8);
void rect(float x, float y, float w, float h, uint32_t color);
void roundrect(float x, float y, float w, float h, float r, uint32_t color);
/* Повёрнутый прямоугольник: x,y - левый верхний угол НЕ повёрнутой фигуры,
 * angle - поворот вокруг её центра. Рисуется с альфа-смешиванием (как rect),
 * поэтому годится для полупрозрачных зон хитбоксов с острыми углами. */
void rect_rot(float x, float y, float w, float h, float angle, uint32_t color);
void circle(float x, float y, float r, uint32_t color);
void ring(float x, float y, float r, float t, uint32_t color);
void line(float x1, float y1, float x2, float y2, float thickness, uint32_t color);
void clear_screen(uint32_t color);
void ds_set_asset_manager(AAssetManager *assets);
void ds_release_assets(void);
int png_load(const char *name);
void tex(float x, float y, const char *name, float angle, float scale);
void tex_tint(float x, float y, const char *name, float angle, float scale, uint32_t color);
/* Звуки: WAV-файлы из game/sounds (см. sound.c). snd_load возвращает 1/0,
 * snd_playing — 1/0; snd_volume задаёт громкость конкретного звука (0..1). */
int snd_load(const char *name);
int snd_play(const char *name);
/* короткий синоним snd_play(), чтобы скрипты могли писать sound_play(...) */
int sound_play(const char *name);
int snd_loop(const char *name);
void snd_stop(const char *name);
int snd_playing(const char *name);
void snd_volume(const char *name, double volume);
void snd_stop_all(void);
int ds_sound_init(AAssetManager *assets);
void ds_sound_shutdown(void);
void ds_sound_pause(void);
void ds_sound_resume(void);
void ds_sound_set_java_vm(void *vm);
/* Игровой шрифт: Comic Relief — метрически эквивалентная Comic Sans MS гарнитура
 * под свободной лицензией SIL OFL 1.1 (текст лицензии лежит рядом с файлом,
 * game/assets/fonts/ComicRelief-OFL.txt). Кириллица и латиница в нём есть,
 * поэтому оба языка интерфейса рисуются одной гарнитурой. */
#define DS_FONT_ASSET "fonts/ComicRelief-Regular.ttf"
#define DS_FONT_PIXEL_HEIGHT 32
void text(const char *string, float x, float y, uint32_t color);
void text_scaled(const char *string, float x, float y, uint32_t color, float scale);
/* Алиасы text_ink_* для более наглядного API из примера "Кликер". */
int text_width(const char *string);
int text_height(const char *string);
int text_ink_width(const char *string);
int text_ink_height(const char *string);
int text_ink_top(const char *string);
/* Автоматическое внутреннее разрешение (без кнопки и без настройки): главный
 * цикл сообщает фактический интервал кадров, а графика при накоплении промахов
 * по vsync рисует кадр в оффскрин в 2..3 раза меньше окна и растягивает его
 * (LINEAR), возвращаясь к 1:1, как только появляется запас. ds_graphics_pixel_scale
 * нужен логам и хост-тестам. */
void ds_graphics_report_frame_interval(double seconds);
int ds_graphics_pixel_scale(void);
/* Создание Vulkan-рендера: менеджер активов плюс окно (для поверхности
 * VK_KHR_android_surface). Возвращает 1 при успехе; при 0 кадры не рисуются. */
int ds_graphics_init(AAssetManager *assets, ANativeWindow *window);
int ds_graphics_begin_frame(Buffer *buffer);
void ds_graphics_end_frame(void);
void ds_graphics_cancel_frame(void);
void ds_graphics_shutdown(void);
void ds_graphics_error_screen(const char *message);
void init(AAssetManager *assets);
void update(void);
void draw(Buffer *buffer);
void touch(float x, float y, int action, int pointer_id);
/* Системная кнопка «Назад»: 1 - скрипт обработал (закрыл чат/вернулся на
 * прошлый экран), 0 - пусть Android закрывает активити. */
int back_pressed(void);
void reset(void);
#endif
