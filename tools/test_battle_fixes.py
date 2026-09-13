#!/usr/bin/env python3
"""Regression checks for the battle fixes batch, without Android or Firebase.

Covers: real dash hitbox (traveled length), turret shield of the buk absorbing
all damage, snowflakes piercing turrets, smooth hitbox fades, filled snowflake
hitbox, poison drawn green next to blue freeze, and the warning/studio splash
timing (black screen never jumps). Compiles the real DimScript sources with a
host C harness and asserts on the actual logic/draw calls.
"""
from pathlib import Path
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from ds_compiler import DimScriptCompiler  # noqa: E402
from gen import find_ds_files  # noqa: E402

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "game.c"

int screen_w = 1280, screen_h = 720;
double dt = 0.05;
int mouse_clicked = 0;
Joy joy;
double ds_mouse_x = 0, ds_mouse_y = 0;

void ds_log(const char *format, ...) { (void)format; }
void ds_log_err(const char *format, ...) { (void)format; }
void ds_console_log(int is_error, const char *format, ...) { (void)is_error; (void)format; }
void ds_runtime_error(const char *format, ...) { fputs(format, stderr); abort(); }

struct DSArray { int len; double values[8192]; };
DSArray *arr_new(void) { DSArray *a = calloc(1, sizeof(*a)); assert(a); return a; }
void arr_free(DSArray *a) { free(a); }
void arr_clear(DSArray *a) { if (a) a->len = 0; }
void arr_push(DSArray *a, double v) { assert(a && a->len < 8192); a->values[a->len++] = v; }
double arr_get(DSArray *a, double i) { return a && i >= 0 && i < a->len ? a->values[(int)i] : 0; }
void arr_set(DSArray *a, double i, double v) {
    assert(a && i >= 0 && i < 8192);
    while (a->len <= i) arr_push(a, 0);
    a->values[(int)i] = v;
}
double arr_len(DSArray *a) { return a ? a->len : 0; }
double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
double dist(double x, double y, double a, double b) { return hypot(x - a, y - b); }
double lerp(double a, double b, double t) { return a + (b - a) * t; }
/* Строковые хелперы рантайма: нужны экранам боя (подписи кнопок, «+N кубков»). */
static char s_concat[512];
char *ds_concat(const char *left, const char *right) {
    snprintf(s_concat, sizeof(s_concat), "%s%s", left ? left : "", right ? right : "");
    return s_concat;
}
char *ds_num_to_string(double value) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "%g", value);
    return buf;
}
int png_load(const char *name) { (void)name; return 1; }
void tex(float x, float y, const char *name, float a, float sc) { (void)x; (void)y; (void)name; (void)a; (void)sc; }
/* Тени рисуются tex_tint: считаем вызовы и запоминаем текстуру. */
static int tint_calls = 0;
static char tint_last[64] = "";
void tex_tint(float x, float y, const char *name, float a, float sc, uint32_t c) {
    (void)x; (void)y; (void)a; (void)sc; (void)c;
    tint_calls++;
    snprintf(tint_last, sizeof(tint_last), "%s", name ? name : "");
}
void roundrect(float x, float y, float w, float h, float r, uint32_t color) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)color; }
int text_ink_width(const char *s) { (void)s; return 10; }
int text_ink_height(const char *s) { (void)s; return 10; }
int text_ink_top(const char *s) { (void)s; return 2; }
void text_scaled(const char *s, float x, float y, uint32_t c, float scale) { (void)s; (void)x; (void)y; (void)c; (void)scale; }

/* Net stubs (same prototypes as net.h): purchases/saves never reach the cloud
 * in these tests, they only need to not crash. */
void net_save_progress_all(double, double, double, double, double, double,
    double, double, double, double, double, double,
    double, double, double, double, double, double) {}
void net_save_progress(double, double, double, double, double, double, double, double) {}
double net_slot(void) { return -1; }
double net_event(void) { return 0; }
void net_set_class(double v) { (void)v; }
void net_set_level(double v) { (void)v; }
void net_set_skin(double v) { (void)v; }
double net_load_cups(void) { return 0; }
double net_load_candies(void) { return 0; }
double net_load_class(void) { return 0; }
double net_load_azum(void) { return 0; }
double net_load_santa(void) { return 0; }
double net_load_ebuc(void) { return 0; }
double net_load_level(void) { return 0; }
double net_load_levels_unlocked(void) { return 0; }
double net_load_ordinary_level(void) { return 0; }
double net_load_ordinary_levels_unlocked(void) { return 0; }
double net_load_azum_level(void) { return 0; }
double net_load_azum_levels_unlocked(void) { return 0; }
double net_load_santa_level(void) { return 0; }
double net_load_santa_levels_unlocked(void) { return 0; }
double net_load_ebuc_level(void) { return 0; }
double net_load_ebuc_levels_unlocked(void) { return 0; }
double net_load_bp_level(void) { return 0; }
double net_load_azum_skin(void) { return 0; }
void net_mark_achievement_flag(double) {}
void net_save_achievement_flags(double) {}
double net_has_achievement_flag(double) { return 0; }
double net_load_achievement_flags(void) { return 0; }
void net_save_azum_revives(double) {}
double net_load_azum_revives(void) { return 0; }
/* Промокоды (promo.inc): в тестах код детерминированно фиксированный,
 * флаг активации и стрик матчей не меняются. */
const char *net_promo_code(void) { return "CB4-0000-0000-0000"; }
void net_promo_register(void) {}
double net_promo_used(void) { return 0; }
void net_promo_mark_used(void) {}
double net_promo_streak(void) { return 0; }
void net_promo_bump_streak(void) {}
void net_promo_reset_streak(void) {}
void net_set_mode(double v) { (void)v; }
void net_set_room(double v) { (void)v; }
/* Публикации в сеть: в соло-тестах не нужны, но update_game тянет их в линк. */
void net_publish(double a, double b, double c, double d, double e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
}
void net_publish_punch(double a, double b, double c, double d, double e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
}
void net_publish_snow(double a, double b, double c, double d, double e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
}
void net_publish_turrets(double a, double b, double c, double d, double e,
                         double f, double g, double h, double i, double j) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    (void)f; (void)g; (void)h; (void)i; (void)j;
}
void net_publish_dash(double a, double b, double c, double d, double e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
}
void net_publish_universe(double a, double b, double c) { (void)a; (void)b; (void)c; }
void net_publish_thud(double a) { (void)a; }
void net_open(double v) { (void)v; }
void net_connect(const char *a, const char *b) { (void)a; (void)b; }
double net_login_status(void) { return 0; }
void keyboard_hide(void) {}
void keyboard_show(void) {}
const char *keyboard_get_text(void) { return ""; }
int keyboard_visible(void) { return 0; }
int keyboard_enter_pressed(void) { return 0; }
/* Строковые/клавиатурные хелперы для экрана промокодов (promo.ds). */
double str_len(const char *s) { return s ? (double)strlen(s) : 0.0; }
int str_eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
int str_starts_with(const char *s, const char *pref) {
    return s && pref && strncmp(s, pref, strlen(pref)) == 0;
}
double str_index_of(const char *hay, const char *needle) {
    if (!hay || !needle) return -1;
    const char *p = strstr(hay, needle);
    return p ? (double)(p - hay) : -1;
}
const char *str_sub(const char *s, double start, double len) {
    static char buf[24];
    if (!s) return "";
    size_t st = (size_t)start, ln = (size_t)len, sl = strlen(s);
    if (st > sl) st = sl;
    if (st + ln > sl) ln = sl - st;
    memcpy(buf, s + st, ln);
    buf[ln] = 0;
    return buf;
}
const char *str_trim(const char *s) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "%s", s ? s : "");
    return buf;
}
const char *str_upper(const char *s) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "%s", s ? s : "");
    for (size_t i = 0; buf[i]; i++)
        if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] = (char)(buf[i] - 'a' + 'A');
    return buf;
}
void net_event_set(double mode) { (void)mode; }
double net_status(void) { return 0; }
double net_count(void) { return 0; }
double net_player_online(double s) { (void)s; return 0; }
double net_player_x(double s) { (void)s; return 0; }
double net_player_y(double s) { (void)s; return 0; }
double net_player_angle(double s) { (void)s; return 0; }
double net_player_hp(double s) { (void)s; return 0; }
double net_player_alive(double s) { (void)s; return 0; }
const char *net_player_nick(double s) { (void)s; return ""; }
double net_player_punch_x(double s) { (void)s; return 0; }
double net_player_punch_y(double s) { (void)s; return 0; }
double net_player_punch_dx(double s) { (void)s; return 0; }
double net_player_punch_dy(double s) { (void)s; return 0; }
double net_player_punch(double s) { (void)s; return 0; }
double net_player_snow_x(double s) { (void)s; return 0; }
double net_player_snow_y(double s) { (void)s; return 0; }
double net_player_snow_dx(double s) { (void)s; return 0; }
double net_player_snow_dy(double s) { (void)s; return 0; }
double net_player_snow(double s) { (void)s; return 0; }
double net_player_station_x(double s) { (void)s; return 0; }
double net_player_station_y(double s) { (void)s; return 0; }
double net_player_station_hp(double s) { (void)s; return 0; }
double net_player_station2_x(double s) { (void)s; return 0; }
double net_player_station2_y(double s) { (void)s; return 0; }
double net_player_station2_hp(double s) { (void)s; return 0; }
double net_player_station3_x(double s) { (void)s; return 0; }
double net_player_station3_y(double s) { (void)s; return 0; }
double net_player_station3_hp(double s) { (void)s; return 0; }
double net_player_station(double s) { (void)s; return 0; }
double net_player_universe_x(double s) { (void)s; return 0; }
double net_player_universe_y(double s) { (void)s; return 0; }
double net_player_universe(double s) { (void)s; return 0; }
double net_player_thud(double s) { (void)s; return 0; }
double net_player_dash(double s) { (void)s; return 0; }
double net_player_dash_x(double s) { (void)s; return 0; }
double net_player_dash_y(double s) { (void)s; return 0; }
double net_player_dash_dx(double s) { (void)s; return 0; }
double net_player_dash_dy(double s) { (void)s; return 0; }
double net_player_class(double s) { (void)s; return 0; }
double net_player_level(double s) { (void)s; return 0; }

/* Draw capture: remember every primitive call. */
typedef struct { char kind; float x, y, w, h, r, t; uint32_t color; } DrawCall;
static DrawCall calls[2048];
static int call_count;
static void record(char kind, float x, float y, float w, float h, uint32_t color) {
    assert(call_count < 2048);
    calls[call_count].kind = kind;
    calls[call_count].x = x; calls[call_count].y = y;
    calls[call_count].w = w; calls[call_count].h = h;
    calls[call_count].color = color;
    call_count++;
}
void circle(float x, float y, float r, uint32_t c) { record('c', x, y, r, 0, c); }
void ring(float x, float y, float r, float t, uint32_t c) { record('r', x, y, r, t, c); }
void line(float x1, float y1, float x2, float y2, float t, uint32_t c) {
    record('l', x1, y1, x2, y2, c);
    calls[call_count - 1].t = t;   /* толщина полосы — она и есть ширина зоны */
}
void rect(float x, float y, float w, float h, uint32_t c) { record('q', x, y, w, h, c); }

static int count_kind_color(char kind, uint32_t color) {
    int n = 0;
    for (int i = 0; i < call_count; i++) if (calls[i].kind == kind && calls[i].color == color) n++;
    return n;
}
static void near(double a, double b) {
    if (fabs(a - b) >= 1e-6) { fprintf(stderr, "near fail: %.9f vs %.9f\n", a, b); abort(); }
}

static void own_turret(int i, double x, double y, double hp) {
    arr_set(turret_x, i, x); arr_set(turret_y, i, y);
    arr_set(turret_hp, i, hp); arr_set(turret_maxhp, i, hp);
    arr_set(turret_spawn_t, i, 0);
}
static void foe_turret(int i, double x, double y, double hp) {
    arr_set(enemy_turret_x, i, x); arr_set(enemy_turret_y, i, y);
    arr_set(enemy_turret_hp, i, hp); arr_set(enemy_turret_maxhp, i, hp);
    arr_set(enemy_turret_spawn_t, i, 0);
}

static void test_shield_player(void) {
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    player_class = CLASS_EBUC;
    enemy_class = CLASS_ORDINARY;
    player->x = 500; player->y = 400; player->size = 25;
    enemy->x = 900; enemy->y = 400; enemy->size = 25;
    player->hp = 20; player->max_hp = 20;
    enemy->hp = 999; enemy->max_hp = 999;
    own_turret(0, 500, 460, 10);
    ds_fn_take_damage(4);
    near(arr_get(turret_hp, 0), 6);
    near(player->hp, 20);           /* турель забрала весь урон */
    ds_fn_take_damage(8);
    near(arr_get(turret_hp, 0), 0); /* турель разбита */
    near(player->hp, 18);           /* остаток урона дошёл до бука */
    ds_fn_take_damage(3);
    near(player->hp, 15);           /* без турелей урон идёт напрямую */
    assert(arr_len(station_boom_ts) == 1);
    /* Урон в онлайне (вселенная соперника) идёт через тот же щит. */
    own_turret(0, 500, 460, 9);
    ds_fn_universe_collapse_remote(5);
    near(arr_get(turret_hp, 0), 4);
    near(player->hp, 15);
    puts("shield: buk turret absorbs player damage (solo+remote), overflow hits buk");
}

static void test_shield_enemy(void) {
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    enemy_class = CLASS_EBUC;
    player_class = CLASS_ORDINARY;
    player->x = 500; player->y = 500; player->size = 25;
    enemy->x = 900; enemy->y = 400; enemy->size = 25;
    player->hp = 999; player->max_hp = 999;
    enemy->hp = 50; enemy->max_hp = 50;
    foe_turret(0, 900, 340, 10);
    ds_fn_enemy_apply_damage(6);
    near(arr_get(enemy_turret_hp, 0), 4);
    near(enemy->hp, 50);
    ds_fn_enemy_apply_damage(5);
    near(arr_get(enemy_turret_hp, 0), 0);
    near(enemy->hp, 49);
    ds_fn_enemy_apply_damage(10);
    near(enemy->hp, 39);
    /* Ульта бука в соло: урон уходит в турели врага и свои. */
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    enemy_class = CLASS_EBUC;
    player_class = CLASS_EBUC;
    player->x = 500; player->y = 500; player->size = 25;
    enemy->x = 900; enemy->y = 400; enemy->size = 25;
    player->hp = 30; player->max_hp = 30;
    enemy->hp = 30; enemy->max_hp = 30;
    own_turret(0, 500, 460, 8);
    foe_turret(0, 900, 340, 8);
    ds_fn_universe_collapse(6);
    near(arr_get(turret_hp, 0), 2);      /* своя вселенная бьёт и свой щит */
    near(player->hp, 30);                /* урон поглощён полностью */
    near(arr_get(enemy_turret_hp, 0), 2);
    near(enemy->hp, 30);
    ds_fn_universe_collapse(10);
    near(arr_get(turret_hp, 0), 0);
    near(player->hp, 22);                /* остаток (10-2) дошёл до бука */
    near(arr_get(enemy_turret_hp, 0), 0);
    near(enemy->hp, 22);
    puts("shield: enemy buk turret absorbs damage, own universe absorbed by own shield");
}

static void test_snow_pierce_enemy_turrets(void) {
    /* Снежинка игрока летит к врагу-буку и проходит сквозь его турель. */
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    player_class = CLASS_SANTA;
    enemy_class = CLASS_EBUC;
    player->x = 300; player->y = 400; player->size = 25;
    enemy->x = 900; enemy->y = 400; enemy->size = 25;
    player->hp = 999; player->max_hp = 999;
    enemy->hp = 500; enemy->max_hp = 500;
    foe_turret(0, 600, 400, 100);  /* прямо на траектории, HP больше урона */
    gift->x = player->x; gift->y = player->y;
    gift->dx = 1; gift->dy = 0;
    gift->active = 1; gift->t = 0; gift->pierce = 0;
    double before = arr_get(enemy_turret_hp, 0);
    int exploded = 0;
    for (int i = 0; i < 2000 && gift->active == 1; i++) {
        double px = gift->x;
        ds_fn_update_gift();
        if (!gift->active) exploded = 1;
        (void)px;
    }
    assert(exploded == 1);                 /* в итоге долетела и взорвалась */
    /* Турель чинится ровно один раз за полёт (pierce-бит), не каждый кадр. */
    near(before - arr_get(enemy_turret_hp, 0), santa_super_damage);
    puts("snow: pierces enemy turret (single chip) and keeps flying to the buk");
}

static void test_snow_pierce_player_turrets(void) {
    /* Снежинка врага летит к игроку и проходит сквозь турель игрока. */
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    enemy_class = CLASS_SANTA;
    player_class = CLASS_ORDINARY;
    player->x = 900; player->y = 400; player->size = 25;
    enemy->x = 300; enemy->y = 400; enemy->size = 25;
    player->hp = 999; player->max_hp = 999;
    enemy->hp = 500; enemy->max_hp = 500;
    own_turret(0, 600, 400, 100);
    ds_fn_enemy_start_snow();
    double before = arr_get(turret_hp, 0);
    int exploded = 0;
    for (int i = 0; i < 2000 && enemy_gift->active == 1; i++) {
        ds_fn_tick_enemy_gift();
        if (!enemy_gift->active) exploded = 1;
    }
    assert(exploded == 1);
    near(before - arr_get(turret_hp, 0), santa_super_damage);
    puts("snow: enemy snow pierces player turret once and flies on");
}

static void test_dash_real_length(void) {
    /* Онлайн-рывок бьёт только по уже проеханному отрезку. */
    ds_fn_reset_battle();
    game_state = ST_ONLINE;
    player_class = CLASS_ORDINARY;
    player->y = 500; player->size = 25;
    player->hp = 999; player->max_hp = 999;
    enemy->x = 100; enemy->y = 100; enemy->size = 25;
    double total = azum_dash_speed * azum_dash_time;  /* весь путь рывка */
    assert(total > 0);
    /* Турель на полпути, игрок чуть раньше конца пути. */
    player->x = 200 + total - 40;
    own_turret(0, 200 + total * 0.5, 500, 50);
    assert(ds_fn_dash_resolve_target(200, 500, 1, 0, total * 0.2) == -1);
    double got = ds_fn_dash_resolve_target(200, 500, 1, 0, total * 0.7);
    assert(got == 1);                                  /* турель на пути */
    got = ds_fn_dash_resolve_target(200, 500, 1, 0, total);
    assert(got == 1);                                  /* щит впереди игрока */
    arr_set(turret_hp, 0, 0);                          /* турель мертва */
    assert(ds_fn_dash_resolve_target(200, 500, 1, 0, total) == 0);
    puts("dash: damage only on the traveled segment, turret shield priority");
}

static void test_hitbox_fades(void) {
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    hitbox_fade_in = 0.1; hitbox_fade_out = 0.2;
    player->x = 500; player->y = 400; player->size = 25;
    enemy->x = 900; enemy->y = 400; enemy->size = 25;
    dt = 0.05;
    /* Рывок: альфа плавно набирается. */
    dash_active = 1; dash_box_a = 0;
    for (int i = 0; i < 4; i++) ds_fn_tick_hitbox_fades();
    near(dash_box_a, 1);
    /* После конца — плавно гаснет (0.05/0.2 за кадр). */
    dash_active = 0;
    ds_fn_tick_hitbox_fades();
    near(dash_box_a, 0.75);
    ds_fn_tick_hitbox_fades();
    near(dash_box_a, 0.5);
    for (int i = 0; i < 20; i++) ds_fn_tick_hitbox_fades();
    near(dash_box_a, 0);
    /* Снежинка: снаряд и взрыв набираются и гаснут. */
    gift->active = 1; snow_ball_a = 0;
    boom_t = 1; boom_x = 300; boom_y = 300; snow_boom_a = 0;
    for (int i = 0; i < 4; i++) ds_fn_tick_hitbox_fades();
    near(snow_ball_a, 1); near(snow_boom_a, 1);
    gift->active = 0; boom_t = 0;
    for (int i = 0; i < 20; i++) ds_fn_tick_hitbox_fades();
    near(snow_ball_a, 0); near(snow_boom_a, 0);
    /* Турель набирает альфу, после смерти — гаснет. */
    own_turret(0, 500, 460, 10);
    for (int i = 0; i < 4; i++) ds_fn_tick_hitbox_fades();
    near(arr_get(turret_box_a, 0), 1);
    arr_set(turret_hp, 0, 0);
    for (int i = 0; i < 20; i++) ds_fn_tick_hitbox_fades();
    near(arr_get(turret_box_a, 0), 0);
    puts("hitboxes: ability alphas fade in and out smoothly like the punch box");
}

static void test_hitbox_drawing(void) {
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    show_hitboxes = 1;
    player_class = CLASS_SANTA;
    enemy_class = CLASS_ORDINARY;
    player->x = 400; player->y = 400; player->size = 25;
    enemy->x = 1000; enemy->y = 400; enemy->size = 25;
    enemy->hp = 999; enemy->max_hp = 999;
    player->hp = 999; player->max_hp = 999;
    uint32_t gray = 0x00808080;
    uint32_t solid = (140u << 24) | gray;   /* hitbox_solid_alpha=140 */
    uint32_t line_c = (170u << 24) | gray;  /* hitbox_line_alpha=170 */
    /* Снежинка: вся зона залита (сплошной круг), линия пути и взрыв тоже. */
    gift->active = 1; snow_ball_a = 1;
    gift->x = 400; gift->y = 400; gift->dx = 1; gift->dy = 0; gift->t = 0;
    boom_t = 1; boom_x = 700; boom_y = 400; snow_boom_a = 1;
    call_count = 0;
    ds_fn_draw_ability_hitboxes();
    assert(count_kind_color('c', solid) >= 2);   /* снаряд + взрыв залиты */
    assert(count_kind_color('r', line_c) >= 2);  /* контуры поверх заливки */
    assert(count_kind_color('l', line_c) >= 1);  /* линия пути */
    /* При альфе 0.5 заливка полупрозрачная (плавное появление работает). */
    snow_ball_a = 0.5;
    call_count = 0;
    ds_fn_draw_ability_hitboxes();
    assert(count_kind_color('c', (70u << 24) | gray) >= 1);
    gift->active = 0; boom_t = 0;
    /* Зоны рисуются, пока жива их альфа: после полного затухания — ничего. */
    snow_ball_a = 0; snow_boom_a = 0;
    /* Рывок: зона урона — ОДИН слой квадратных больших хитбоксов по уже
     * проеханному отрезку (клетки мира размером со сторону квадрата).
     * Второго слоя нет: ни мелких кубиков следом, ни полосы, ни пятна. */
    dash_active = 1; dash_box_a = 1;
    dash_x0 = 300; dash_y0 = 400; dash_dx = 1; dash_dy = 0;
    player->x = 700; player->y = 400;
    call_count = 0;
    ds_fn_draw_ability_hitboxes();
    uint32_t aim_c = (102u << 24) | gray;        /* aim_max_alpha=102 */
    double pr = ds_fn_dash_hit_radius_solo();    /* тот же радиус, что и урон */
    double side = pr * dash_hitbox_zone_scale;   /* сторона большого квадрата */
    double travel = azum_dash_speed * azum_dash_time;   /* 382.5 из 400 пути */
    int squares = count_kind_color('q', aim_c);
    assert(squares >= (int)(travel / side));
    assert(squares >= 8);
    for (int i = 0; i < call_count; i++) {
        if (calls[i].kind != 'q' || calls[i].color != aim_c) continue;
        /* Все квадраты одного большого размера и сидят на сетке мира
         * (координаты хранятся во float, поэтому допуск 1e-3). */
        assert(fabs(calls[i].w - side) < 1e-3);
        assert(fabs(calls[i].h - side) < 1e-3);
        double gx = calls[i].x / side, gy = calls[i].y / side;
        assert(fabs(gx - floor(gx + 0.5)) < 1e-3);   /* центр клетки сетки */
        assert(fabs(gy - floor(gy + 0.5)) < 1e-3);
    }
    /* Ни полосы, ни кругов: слой ровно один. */
    assert(count_kind_color('l', aim_c) == 0);
    assert(count_kind_color('c', aim_c) == 0);
    assert(count_kind_color('c', solid) == 0);
    /* Рывок бота в соло рисуется тем же радиусом, которым он бьёт. */
    dash_active = 0; dash_box_a = 0;
    enemy_dash_active = 1; edash_box_a = 1;
    enemy_dash_x0 = 1100; enemy_dash_y0 = 400; enemy_dash_dx = -1; enemy_dash_dy = 0;
    enemy->x = 800; enemy->y = 400;
    call_count = 0;
    ds_fn_draw_ability_hitboxes();
    assert(count_kind_color('q', aim_c) >= 5);
    assert(count_kind_color('l', aim_c) == 0);
    assert(count_kind_color('c', aim_c) == 0);
    enemy_dash_active = 0;
    /* Квадрат зоны вокруг деспенсера больше не рисуется: ни заливки, ни
     * контура — у турели остаётся только тень-спрайт. */
    dash_active = 0; enemy_dash_active = 0; edash_box_a = 0;
    own_turret(0, 500, 460, 10);
    arr_set(turret_box_a, 0, 1);
    call_count = 0;
    ds_fn_draw_ability_hitboxes();
    assert(count_kind_color('q', (36u << 24) | gray) == 0);  /* без заливки квадрата */
    assert(count_kind_color('l', line_c) == 0);              /* без контура */
    assert(count_kind_color('r', line_c) == 0);              /* без круглого контура */
    assert(count_kind_color('c', (36u << 24) | gray) == 0);  /* без круглой заливки */
    puts("hitboxes: real geometry drawn, snowflake solid-filled, dash is big squares only");
}

static void test_poison_green(void) {
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    enemy->x = 100; enemy->y = 100; enemy->size = 25;
    player->x = 300; player->y = 100; player->size = 25;
    /* Яд зелёный — не сливается с голубой заморозкой. */
    poison_a = 1;
    call_count = 0;
    ds_fn_draw_poison();
    assert(count_kind_color('c', (76u << 24) | 0xC853) == 1);     /* 0x00C853 */
    assert(count_kind_color('r', (178u << 24) | 0x69F0AE) == 1);
    ppoison_a = 1;
    call_count = 0;
    ds_fn_draw_player_poison();
    assert(count_kind_color('c', (76u << 24) | 0xC853) == 1);
    assert(count_kind_color('r', (178u << 24) | 0x69F0AE) == 1);
    /* Заморозка осталась голубой. */
    freeze_a = 1;
    call_count = 0;
    ds_fn_draw_freeze();
    assert(count_kind_color('c', (76u << 24) | 0x1E88E5) == 1);
    assert(count_kind_color('r', (178u << 24) | 0x4FC3F7) == 1);
    puts("poison: green vs blue freeze — the two effects no longer look doubled");
}

static void test_splash_screens(void) {
    /* Предупреждение: текст тает, чёрный экран остаётся и сразу же на нём
     * проявляется заставка студии. */
    ds_fn_reset_battle();
    warn_open = 1; warn_t = 0; warn_a = 1; warn_hold = 2.5; warn_fade = 0.6;
    studio_open = 0;
    dt = 0.1;
    for (int i = 0; i < 40; i++) ds_fn_update_warning();  /* 4.0 c */
    assert(warn_open == 0 && studio_open == 1 && studio_bg_a == 1);
    /* Заставка: логотип проявляется и висит на цельном чёрном фоне... */
    studio_t = 0; studio_a = 0; studio_in = 0.5; studio_hold = 1.6; studio_fade = 0.7;
    for (int i = 0; i < 10; i++) ds_fn_update_studio();   /* 1.0 c */
    near(studio_a, 1); near(studio_bg_a, 1);
    for (int i = 0; i < 12; i++) ds_fn_update_studio();   /* +1.2 c = 2.2 c */
    /* Логотип висел на цельном чёрном фоне до 2.1 c; после 2.1 c чёрный фон
     * начал гаснуть вместе с логотипом — сейчас середина затухания. */
    assert(studio_a > 0 && studio_a < 1);
    near(studio_bg_a, studio_a);
    for (int i = 0; i < 20; i++) ds_fn_update_studio();   /* +2.0 c */
    assert(studio_open == 0 && studio_bg_a == 0);
    /* Отрисовка: фон предупреждения непрозрачно-чёрный даже при тающем
     * тексте; фон заставки прозрачнеет только вместе с логотипом. */
    warn_open = 1; warn_a = 0.5;
    call_count = 0;
    ds_fn_draw_warning();
    assert(calls[0].kind == 'q' && calls[0].color == 0xFF000000);
    studio_open = 1; studio_a = 0.4; studio_bg_a = 0.4;
    call_count = 0;
    ds_fn_draw_studio();
    assert(calls[0].kind == 'q' && calls[0].color == 0x66000000);
    studio_a = 1; studio_bg_a = 1;
    call_count = 0;
    ds_fn_draw_studio();
    assert(calls[0].kind == 'q' && calls[0].color == 0xFF000000);
    puts("splash: warning keeps black screen, studio logo fades out with it");
}

static void test_punch_hitbox_fades(void) {
    /* Полоса удара бота больше не вспыхивает на один кадр: у неё своя альфа. */
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    dt = 0.05;
    aim_fade_in = 0.09; aim_fade_out = 0.1;
    show_hitboxes = 1;
    enemy->state = 2;                       /* удар бота */
    ds_fn_tick_hitbox_fades();
    assert(enemy_punch_a > 0 && enemy_punch_a < 1);
    for (int i = 0; i < 10; i++) ds_fn_tick_hitbox_fades();
    near(enemy_punch_a, 1);
    enemy->state = 0;
    ds_fn_tick_hitbox_fades();
    assert(enemy_punch_a > 0 && enemy_punch_a < 1);
    for (int i = 0; i < 10; i++) ds_fn_tick_hitbox_fades();
    near(enemy_punch_a, 0);
    /* Рисуется она той же альфой, а не единицей. */
    enemy_punch_a = 0.5;
    enemy->x = 400; enemy->y = 300; enemy->angle = 0;
    call_count = 0;
    ds_fn_draw_enemy_hitbox();
    assert(count_kind_color('l', (51u << 24) | 0x00808080) == 1);  /* floor(0.5*102) */
    /* Конец боя: все хитбоксы гаснут плавно, а не исчезают разом. */
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    dash_active = 1; dash_box_a = 1;
    enemy->state = 1; enemy_punch_a = 1;
    own_turret(0, 500, 460, 10);
    arr_set(turret_box_a, 0, 1);
    finished = 1;
    ds_fn_tick_hitbox_fades();
    assert(dash_box_a > 0 && dash_box_a < 1);
    assert(enemy_punch_a > 0 && enemy_punch_a < 1);
    assert(arr_get(turret_box_a, 0) > 0 && arr_get(turret_box_a, 0) < 1);
    for (int i = 0; i < 20; i++) ds_fn_tick_hitbox_fades();
    near(dash_box_a, 0); near(enemy_punch_a, 0); near(arr_get(turret_box_a, 0), 0);
    puts("hitboxes: bot punch box fades, everything fades out at the end of battle");
}

static void test_status_circles(void) {
    /* Эффекты, которые враг наложил на бойца, видны тем же ровным кругом и
     * так же плавно гаснут. */
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    dt = 0.05;
    freeze_fade_in = 0.12; freeze_fade_out = 0.35;
    player->size = 45; enemy->size = 45;
    player->x = 300; player->y = 300;
    enemy->x = 900; enemy->y = 300;
    player_freeze = 1; player_poison = 1; player_stun = 1;
    enemy->freeze = 1; enemy->poison = 1; enemy->stun = 1;
    for (int i = 0; i < 10; i++) ds_fn_tick_status_fades();
    near(pfreeze_a, 1); near(ppoison_a, 1); near(pstun_a, 1);
    near(freeze_a, 1); near(poison_a, 1); near(stun_a, 1);
    player_freeze = 0; player_poison = 0; player_stun = 0;
    enemy->freeze = 0; enemy->poison = 0; enemy->stun = 0;
    ds_fn_tick_status_fades();
    assert(pfreeze_a > 0 && pfreeze_a < 1);
    assert(ppoison_a > 0 && ppoison_a < 1);
    assert(pstun_a > 0 && pstun_a < 1);
    for (int i = 0; i < 20; i++) ds_fn_tick_status_fades();
    near(pfreeze_a, 0); near(ppoison_a, 0); near(pstun_a, 0);
    near(freeze_a, 0); near(poison_a, 0); near(stun_a, 0);
    /* Круг вокруг бойца рисуется в соло (draw_game), тем же цветом, что у врага. */
    pfreeze_a = 1; ppoison_a = 1; pstun_a = 1;
    call_count = 0;
    ds_fn_draw_game();
    assert(count_kind_color('c', (76u << 24) | 0x1E88E5) == 1);   /* заморозка */
    assert(count_kind_color('c', (76u << 24) | 0xC853) == 1);     /* яд */
    assert(count_kind_color('c', (76u << 24) | 0xF9A825) == 1);   /* оглушение */
    /* В конце боя таймеры состояний уже не тикают, но круги всё равно гаснут. */
    player_freeze = 2; pfreeze_a = 1;
    finished = 2;
    ds_fn_tick_status_fades();
    assert(pfreeze_a > 0 && pfreeze_a < 1);
    for (int i = 0; i < 20; i++) ds_fn_tick_status_fades();
    near(pfreeze_a, 0);
    puts("status: enemy-applied effects circle the fighter and fade out smoothly");
}

static void test_dash_zone_narrow(void) {
    /* Зона рывка в уроне и в рисунке одна: радиус задаётся масштабом.
     * Масштаб чуть меньше единицы (рывок всё ещё прицельный), но заметно
     * больше прежних 0.75 - рывок Азума должен попадать чаще. */
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    player->size = 45; enemy->size = 45;
    double old_r = player->size * 0.65 + enemy->size * 0.65 + 12;
    double pr = ds_fn_dash_hit_radius_solo();
    near(pr, old_r * dash_hit_radius_scale);
    assert(dash_hit_radius_scale < 1.0);
    assert(dash_hit_radius_scale >= 0.85);
    assert(pr * dash_hitbox_zone_scale < old_r * 2);   /* полоса уже прежнего диаметра */
    /* Рывок, прошедший в стороне между новым и старым радиусом, больше не бьёт. */
    game_state = ST_ONLINE;
    double remote_old = player->size * 0.65 + 14;
    double remote_pr = ds_fn_dash_hit_radius_remote();
    near(remote_pr, remote_old * dash_hit_radius_scale);
    player->x = 500; player->y = 500 + (remote_old + remote_pr) / 2;
    assert(ds_fn_dash_resolve_target(200, 500, 1, 0, 600) == -1);
    player->y = 500 + remote_pr - 1;
    assert(ds_fn_dash_resolve_target(200, 500, 1, 0, 600) == 0);
    puts("dash: narrower damage zone, the drawn squares follow it");
}

static void test_own_punch_spares_own_turret(void) {
    /* Свой деспенсер не перехватывает свой удар: турель стоит прямо на буке,
     * удар уходит в пустоту (турель цела, враг далеко — не ранен), а когда
     * враг в полосе — урон доходит до врага, турель не теряет HP. */
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    player_class = CLASS_EBUC;
    enemy_class = CLASS_ORDINARY;
    dt = 0.05;
    player->x = 400; player->y = 400; player->size = 25; player->angle = 0;
    enemy->x = 900; enemy->y = 400; enemy->size = 25;
    player->hp = 20; player->max_hp = 20;
    enemy->hp = 999; enemy->max_hp = 999;
    own_turret(0, 400, 400, 9);
    enemy->cooldown = 5; enemy->think = 5;      /* бот не вмешивается */
    punch->active = 1; punch->hit = 0; punch_left = punch_time;
    ds_fn_update_game();
    near(arr_get(turret_hp, 0), 9);             /* свой удар не грызёт деспенсер */
    near(enemy->hp, 999);                       /* и не бьёт врага сквозь пустоту */
    assert(punch->hit == 0);
    enemy->x = 460; enemy->y = 400;             /* враг в полосе удара */
    punch->active = 1; punch->hit = 0; punch_left = punch_time;
    ds_fn_update_game();
    near(enemy->hp, 999 - ebuc_damage);         /* урон дошёл до врага */
    near(arr_get(turret_hp, 0), 9);             /* деспенсер цел */
    assert(punch->hit == 1);
    puts("punch: buk's own despenser never eats his punch, damage reaches the foe");
}

static void test_enemy_shield_front_only(void) {
    /* Деспенсер врага-буКа прикрывает только спереди: турель за спиной не
     * подставляется под удар в лицо, и враг честно получает урон. */
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    player_class = CLASS_ORDINARY;
    enemy_class = CLASS_EBUC;
    player->x = 500; player->y = 400; player->size = 25; player->angle = 0;
    enemy->x = 600; enemy->y = 400; enemy->size = 25;
    player->hp = 999; player->max_hp = 999;
    enemy->hp = 50; enemy->max_hp = 50;
    /* Турель между игроком и врагом — щит принимает удар. */
    foe_turret(0, 560, 400, 10);
    assert(ds_fn_enemy_punch_turret_target(500, 400, 1, 0) == 1);
    /* Турель в полосе удара, но позади тела (дальше допуска 30px) — не щит:
     * прямого чипа и контрудара нет, удар проходит в лицо врага (входящий
     * урон бука по правилу класса принимает щит через absorb). */
    arr_set(enemy_turret_x, 0, 632);
    assert(ds_fn_enemy_punch_turret_target(500, 400, 1, 0) == -1);
    enemy->cooldown = 5; enemy->think = 5;
    punch->x = 500; punch->y = 400; punch->dx = 1; punch->dy = 0;
    punch->active = 1; punch->hit = 0; punch_left = punch_time;
    ds_fn_update_game();
    assert(punch->hit == 1);                    /* удар в лицо прошёл */
    near(arr_get(enemy_turret_hp, 0), 9);       /* absorb принял урон */
    near(player->hp, 999);                      /* контрудара нет */
    /* Турель спереди: прямой чип в щит плюс контрудар по игроку. */
    arr_set(enemy_turret_x, 0, 560);
    punch->active = 1; punch->hit = 0; punch_left = punch_time;
    ds_fn_update_game();
    near(arr_get(enemy_turret_hp, 0), 8);
    near(player->hp, 999 - turret_counter_damage);
    puts("shield: enemy despenser covers only from the front, face punches land");
}

static void test_online_turret_punch_death(void) {
    /* Онлайн-буК: удар соперника бьёт по турели напрямую (damage_turret), а
     * удар по самому игроку поглощается щитом из турелей — в обоих случаях
     * турель теряет HP и в итоге умирает (взрыв попадает в очередь). */
    ds_fn_reset_battle();
    game_state = ST_ONLINE;
    player_class = CLASS_EBUC;
    dt = 0.05;
    player->x = 500; player->y = 400; player->size = 25;
    player->hp = 20; player->max_hp = 20;
    /* Слот 1 — живой соперник (обычный класс, урон 1). */
    arr_set(remotes, 1*remote_fields+5, 1);
    arr_set(remotes, 1*remote_fields+10, CLASS_ORDINARY);
    double rb = 1*punch_fields;
    /* 1) Удар точно по игроку (турель далеко от полосы): щит поглощает урон. */
    own_turret(0, 500, 560, 9);
    arr_set(remote_punches, rb, 1);
    arr_set(remote_punches, rb+1, 420);
    arr_set(remote_punches, rb+2, 400);
    arr_set(remote_punches, rb+3, 1);
    arr_set(remote_punches, rb+4, 0);
    ds_fn_update_remote_punches();
    near(player->hp, 20);                      /* игрок цел */
    near(arr_get(turret_hp, 0), 8);            /* щит забрал удар */
    assert(arr_get(remote_punches, rb) == 0);  /* удар обработан */
    /* 2) Прямое попадание по турели: чип за чипом, после девяти ударов смерть. */
    arr_set(turret_x, 0, 500); arr_set(turret_y, 0, 460); arr_set(turret_hp, 0, 9);
    arr_set(remote_punches, rb, 1);
    ds_fn_update_remote_punches();
    near(arr_get(turret_hp, 0), 8);
    for (int i = 0; i < 8; i++) {
        arr_set(remote_punches, rb, 1);
        ds_fn_update_remote_punches();
    }
    near(arr_get(turret_hp, 0), 0);
    assert(arr_len(station_boom_ts) == 1);
    puts("online: friend punch chips and kills buk turret (shield absorb + direct hit)");
}

static void test_turret_shadow_square(void) {
    /* Тень деспенсера — тем же спрайтом (tex_tint), что и сам деспенсер:
     * ни кругов, ни колец под турелью. Хитбокс турели — квадрат. */
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    show_hitboxes = 1;
    own_turret(0, 500, 460, 10);
    call_count = 0; tint_calls = 0;
    ds_fn_draw_turret_shadows();
    assert(tint_calls == 1);
    assert(strcmp(tint_last, "despenser.png") == 0);
    assert(call_count == 0);                    /* тень — спрайт, не круг */
    arr_set(turret_box_a, 0, 1);
    call_count = 0;
    ds_fn_draw_ability_hitboxes();
    assert(call_count == 0);   /* квадрат зоны вокруг деспенсера убран */
    puts("turret: square sprite shadow stays, no hit square around the despenser");
}

static void test_universe_fade(void) {
    /* Вспышка вселенной не обрывается в момент схлопывания. */
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    dt = 0.05;
    universe_active = 1; universe_t = 0.5; universe_x = 400; universe_y = 300;
    for (int i = 0; i < 10; i++) ds_fn_tick_hitbox_fades();
    near(universe_fx_a, 1);
    universe_active = 0; universe_t = ebuc_universe_collapse_time;
    ds_fn_tick_hitbox_fades();
    assert(universe_fx_a > 0 && universe_fx_a < 1);
    call_count = 0;
    ds_fn_draw_universe();
    assert(call_count >= 1);                  /* точка и кольца ещё доигрывают */
    for (int i = 0; i < 40; i++) ds_fn_tick_hitbox_fades();
    near(universe_fx_a, 0);
    call_count = 0;
    ds_fn_draw_universe();
    assert(call_count == 0);
    puts("universe: the collapse flash fades out instead of snapping away");
}

static void test_enemy_class_chances(void) {
    /* буК выпадает врагом редко — 8%; Азум 20%, Дед Мороз 30%. */
    ds_fn_reset_battle();
    game_state = ST_SOLO;
    near(enemy_class_ebuc_chance, 8);
    int counts[4] = {0, 0, 0, 0};
    const int n = 40000;
    for (int i = 0; i < n; i++) {
        ds_fn_enemy_pick_random_class();
        counts[(int)enemy_class]++;
    }
    double ebuc = (double)counts[(int)CLASS_EBUC] / n;
    double azum = (double)counts[(int)CLASS_AZUM] / n;
    double santa = (double)counts[(int)CLASS_SANTA] / n;
    assert(ebuc > 0.065 && ebuc < 0.095);
    assert(azum > 0.18 && azum < 0.22);
    assert(santa > 0.28 && santa < 0.32);
    puts("enemy: buk shows up in 8% of solo battles");
}

int main(void) {
    setbuf(stdout, NULL);
    ds_main();
    test_shield_player();
    test_shield_enemy();
    test_snow_pierce_enemy_turrets();
    test_snow_pierce_player_turrets();
    test_dash_real_length();
    test_hitbox_fades();
    test_hitbox_drawing();
    test_punch_hitbox_fades();
    test_status_circles();
    test_dash_zone_narrow();
    test_own_punch_spares_own_turret();
    test_enemy_shield_front_only();
    test_online_turret_punch_death();
    test_turret_shadow_square();
    test_universe_fade();
    test_enemy_class_chances();
    test_poison_green();
    test_splash_screens();
    return 0;
}
'''


def check_firebase_rules_cover_request_bodies():
    """Правила RTDB должны объявлять КАЖДЫЙ ключ тел запросов клиента.

    У узлов правил есть "$other": {".validate": false}, поэтому одно новое
    поле в PUT/PATCH без правила отклоняет всю запись: именно так онлайн
    когда-то завис на вечном «подключении» (claim слота не проходил
    валидацию). Проверка ловит рассинхрон firebase.rules.json и native/net.
    """
    rules = json.loads((ROOT / "firebase.rules.json").read_text(encoding="utf-8"))["rules"]

    def declared(node):
        return {k for k in node if not k.startswith((".", "$"))}

    slot = declared(rules["rooms"]["$room"]["players"]["$slot"])
    user = declared(rules["users"]["$nick"])
    banner = declared(rules["banner"])
    msg = declared(rules["rooms"]["$room"]["chat"]["$msg"])

    def body_keys(name):
        text = (ROOT / "native/net" / name).read_text(encoding="utf-8")
        return set(re.findall(r'\\"([a-z0-9_]+)\\":', text))

    missing = body_keys("room_sync.inc") - slot
    assert not missing, f"rooms/$room/players/$slot rules miss: {sorted(missing)}"
    # auth_session.inc также шлёт тела логина в identitytoolkit — их ключи
    # не относятся к базе и в проверке не участвуют.
    auth_only = {"email", "password", "grant_type", "refresh_token"}
    profile = (body_keys("state_storage.inc") | body_keys("settings_storage.inc")
               | body_keys("promo.inc")
               | (body_keys("auth_session.inc") - auth_only))
    missing = profile - user
    assert not missing, f"users/$nick rules miss: {sorted(missing)}"
    missing = body_keys("player_api.inc") - msg
    assert not missing, f"rooms/$room/chat/$msg rules miss: {sorted(missing)}"
    missing = body_keys("room_control.inc") - banner
    assert not missing, f"banner rules miss: {sorted(missing)}"


def main():
    check_firebase_rules_cover_request_bodies()
    with tempfile.TemporaryDirectory(prefix="cubic-fixes-") as directory:
        temp = Path(directory)
        compiler = DimScriptCompiler()
        assert compiler.compile(find_ds_files(str(ROOT / "game/scripts")), str(temp / "game.c"))
        assert not compiler.errors and not compiler.warnings

        # ── Wiring checks inside the compiled script modules ──
        fns = compiler.functions
        for name in ("update_game", "update_online"):
            body = fns[name][1]
            for hook in ("tick_hitbox_fades()", "tick_status_fades()"):
                assert body.count(hook) == 1, f"{name} must call {hook} once"
        # Эффекты, наложенные врагом, рисуются и вокруг бойца (соло).
        solo_body = "".join(fns["draw_game"][1])
        for fn in ("draw_player_freeze()", "draw_player_poison()", "draw_player_stun()"):
            assert solo_body.count(fn) == 1, f"draw_game must call {fn}"
        # Шансы классов врага берутся из конфига, а не зашиты числами.
        pick_body = "".join(fns["enemy_pick_random_class"][1])
        for name in ("enemy_class_azum_chance", "enemy_class_ebuc_chance",
                     "enemy_class_santa_chance"):
            assert name in pick_body, f"enemy_pick_random_class must use {name}"
        for name in ("draw_game", "draw_online"):
            body = fns[name][1]
            assert body.count("draw_ability_hitboxes()") == 1, f"{name} must draw ability hitboxes"
        # Snow pierce helpers are wired into both flight paths.
        assert "snow_chip_enemy_turrets()" in "".join(fns["update_gift"][1])
        assert "snow_chip_player_turrets()" in "".join(fns["tick_enemy_gift"][1])
        # Pierce mask resets on every throw (launch happens at windup end).
        assert "gift.pierce=0" in "".join(fns["tick_super_windup"][1])
        assert "enemy_gift.pierce=0" in "".join(fns["enemy_start_snow"][1])
        # Shield funnels sit in the damage entry points.
        assert "player_shield_absorb(dmg)" in "".join(fns["take_damage"][1])
        assert "enemy_shield_absorb(dmg)" in "".join(fns["enemy_apply_damage"][1])
        assert "enemy_shield_absorb(dmg)" in "".join(fns["universe_collapse"][1])
        assert "player_shield_absorb(dmg)" in "".join(fns["universe_collapse_remote"][1])
        # Dash resolution takes the traveled length.
        params = [p[1] for p in fns["dash_resolve_target"][0]]
        assert params == ["sx", "sy", "dx", "dy", "len"]
        # Dash hitbox: one layer of big squares, no small trailing cube chain.
        assert "draw_hit_dash_cube" not in fns and "draw_hit_dash_zone" not in fns
        dash_body = "".join(fns["draw_hit_dash_path"][1])
        assert "rect(" in dash_body and "line(" not in dash_body
        # Own turret can never intercept the owner's punch.
        assert "punch_turret_target" not in fns
        # Turret shadow is the despenser sprite itself (square, tinted).
        shadow_body = "".join(fns["draw_turret_shadow_at"][1])
        assert "tex_tint(" in shadow_body and "DESPENSER_TEX" in shadow_body
        for name in ("draw_game", "draw_online"):
            assert "".join(fns[name][1]).count("draw_turret_shadows()") == 1, \
                f"{name} must draw turret shadows in the shadow layer"
        # Enemy shield covers from the front only (turret behind the body
        # never substitutes for it).
        assert "tf<=bw+30" in "".join(fns["enemy_punch_turret_target"][1])
        # Poison is green (fill 0x00C853), freeze stays blue.
        assert "0x0000C853" in "".join(fns["draw_poison"][1])
        assert "0x0000C853" in "".join(fns["draw_player_poison"][1])
        # Splash: warning keeps its black screen; studio bg fades with the logo.
        assert "0xFF000000" in "".join(fns["draw_warning"][1])
        assert "studio_bg_rgb(0)" in "".join(fns["draw_studio"][1])
        studio_body = "".join(fns["update_studio"][1]).replace(" ", "")
        assert "studio_bg_a=studio_a" in studio_body

        # ── Чат: пузыри сообщений над бойцами и кнопка закрытия по центру ──
        online_body = "".join(fns["draw_online"][1])
        assert online_body.count("draw_chat_bubbles()") == 1, \
            "draw_online must draw chat bubbles once"
        upd_body = "".join(fns["update_online"][1])
        assert upd_body.count("chat_bubbles_watch()") == 1, \
            "update_online must tick chat bubbles once"
        assert "chat_bubbles_reset()" in "".join(fns["init_online"][1])
        # Закрытие чата — там же, где draw_back: вверху по центру.
        close_x = "".join(fns["chat_close_x"][1]).replace(" ", "")
        assert "(screen_w-btn_w)/2" in close_x, \
            "chat close button must sit centered like every other close button"
        assert "back_y" in "".join(fns["chat_top_y"][1])
        # Новые сообщения находятся по серверному ключу (устойчиво к обрезке).
        watch_body = "".join(fns["chat_bubbles_watch"][1])
        assert "net_chat_key(" in watch_body and "chat_seen_key" in watch_body

        # ── Настройки: громкость музыки ──
        settings_body = "".join(fns["draw_settings"][1])
        assert "tr_music_vol()" in settings_body and "music_half_w()" in settings_body
        touch_body = "".join(fns["touch_settings"][1])
        assert touch_body.count("music_volume_step(") == 2, \
            "touch_settings must handle both - and + buttons"
        assert "net_load_music_volume()" in "".join(fns["settings_from_storage"][1])
        assert "net_save_music_volume(" in "".join(fns["music_volume_step"][1])

        # ── Карточки и промокоды: дроп только в соло, код персональный ──
        solo_upd = "".join(fns["update_game"][1])
        assert solo_upd.count("card_update()") == 1, \
            "update_game must tick cards once"
        draw_game_body = "".join(fns["draw_game"][1])
        assert draw_game_body.count("draw_card_item()") == 1, \
            "draw_game must draw the field card once"
        assert draw_game_body.count("draw_card_open()") == 1, \
            "draw_game must draw the card overlay once"
        assert "card_roll()" in "".join(fns["init_game"][1]), \
            "init_game must roll the card chance"
        reset_body = "".join(fns["reset_battle"][1])
        assert "card_active=0" in reset_body and "card_open=0" in reset_body, \
            "reset_battle must clear the card state"
        # Онлайн карточки не видит: тик и ролл только в соло.
        assert "card_update()" not in "".join(fns["update_online"][1])
        assert "card_roll()" not in "".join(fns["init_online"][1])
        assert "card_open!=0" in "".join(fns["touch_game"][1]), \
            "touch_game must be blocked while the card overlay is open"
        # Экран промокодов: кнопка в меню, маршрутизация ввода.
        assert "tr_promo()" in "".join(fns["draw_modes"][1])
        assert "start_transition(ST_PROMO)" in "".join(fns["touch_modes"][1])
        assert "touch_promo(" in "".join(fns["touch_menu"][1])
        # Код детерминированно выводится из ника в C (не в конфиге и не на клиенте).
        promo_c = (ROOT / "native/net/promo.inc").read_text(encoding="utf-8")
        assert "CB4-%04X-%04X-%04X" in promo_c
        assert "promo_sync_with_cloud(resp)" in (
            ROOT / "native/net/profile_apply.inc").read_text(encoding="utf-8")

        # ── Рывок Азума: быстрее при той же дистанции (300 * 1.275 = 382.5) ──
        config_text = (ROOT / "game/scripts/core/config.ds").read_text(encoding="utf-8")
        assert "azum_dash_time=1.275" in config_text and "azum_dash_speed=300" in config_text

        android = temp / "android"
        android.mkdir()
        (android / "asset_manager.h").write_text("typedef struct AAssetManager AAssetManager;\n")
        (android / "log.h").touch()
        (android / "native_window.h").touch()
        (temp / "test.c").write_text(HARNESS)
        subprocess.run([
            *shlex.split(os.environ.get("CC", "cc")), "-std=c99", "-O0",
            "-Werror=implicit-function-declaration", "-Werror=incompatible-pointer-types",
            "-ffunction-sections", "-fdata-sections", "-I", str(temp), "-I", str(ROOT),
            str(temp / "test.c"), "-Wl,--gc-sections", "-lm", "-o", str(temp / "test"),
        ], check=True)
        subprocess.run([str(temp / "test")], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
