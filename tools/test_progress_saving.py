#!/usr/bin/env python3
"""Saving checks: everything the player earns must reach the device AND the cloud.

The regression this guards: `save_progress()` in ui/progress_rewards.ds used to
be a multi-line `net_save_progress_all(...)` call, the DimScript compiler had no
line continuation and silently dropped it — so nothing (cups, candies, the buk,
levels, skins) was ever written to progress.dat or to Firebase.

The test compiles the real DimScript sources, links them against a fake network
layer that plays the role of net.c (progress.dat / settings.dat / cloud), and
then drives the real script functions: buy the buk, restart, check it is still
there. Requires a host C compiler (CC). No Android, no Firebase.
"""
from pathlib import Path
import os
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
void keyboard_hide(void) {}
void net_disconnect(void) {}

/* Строковые хелперы рантайма: моды приходят из поля ввода, поэтому str_trim
 * здесь настоящий — скрипт сохраняет уже обрезанное имя. */
double str_len(const char *s) { return s ? (double)strlen(s) : 0; }
int str_eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
const char *str_trim(const char *s) {
    static char buf[128];
    const char *end;
    size_t n;
    if (!s) return "";
    while (*s == ' ' || *s == '\t') s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t')) end--;
    n = (size_t)(end - s);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, s, n);
    buf[n] = '\0';
    return buf;
}

struct DSArray { int len; double values[512]; };
DSArray *arr_new(void) { DSArray *a = calloc(1, sizeof(*a)); assert(a); return a; }
void arr_free(DSArray *a) { free(a); }
void arr_clear(DSArray *a) { if (a) a->len = 0; }
void arr_push(DSArray *a, double v) { assert(a && a->len < 512); a->values[a->len++] = v; }
double arr_get(DSArray *a, double i) { return a && i >= 0 && i < a->len ? a->values[(int)i] : 0; }
void arr_set(DSArray *a, double i, double v) {
    assert(a && i >= 0 && i < 512);
    while (a->len <= i) arr_push(a, 0);
    a->values[(int)i] = v;
}
double arr_len(DSArray *a) { return a ? a->len : 0; }
double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
double lerp(double a, double b, double t) { return a + (b - a) * t; }
double dist(double x, double y, double a, double b) { return hypot(x - a, y - b); }
int png_load(const char *name) { (void)name; return 1; }
int snd_load(const char *name) { (void)name; return 0; }
void snd_volume(const char *name, double v) { (void)name; (void)v; }
int snd_loop(const char *name) { (void)name; return 1; }
void snd_stop(const char *name) { (void)name; }
int snd_playing(const char *name) { (void)name; return 0; }
void net_set_firebase_key(const char *key) { (void)key; }
void net_autologin(const char *url) { (void)url; }

/* ── «Устройство и облако»: net.c в миниатюре ─────────────────────────────
 * Каждый net_save_* пишет сюда, каждый net_load_* читает отсюда — ровно как
 * progress.dat/settings.dat на телефоне. Счётчики save_* показывают, что игра
 * вообще пыталась сохраниться. */
static struct {
    int progress_saves, settings_saves, cloud_pushes;
    double cups, candies, cls, azum, santa, ebuc, level, levels;
    double ol, ou, al, au, sl, su, el, eu, bp, skin;
    double flags, revives;
    double language, hitboxes, musicvol, winter, showfps;
} store;

void net_save_progress_all(double cups, double candies, double cls, double azum, double santa,
    double ebuc, double level, double levels, double ol, double ou, double al, double au,
    double sl, double su, double el, double eu, double bp, double skin) {
    store.progress_saves++;
    store.cups = cups; store.candies = candies; store.cls = cls;
    store.azum = azum; store.santa = santa; store.ebuc = ebuc;
    store.level = level; store.levels = levels;
    store.ol = ol; store.ou = ou; store.al = al; store.au = au;
    store.sl = sl; store.su = su; store.el = el; store.eu = eu;
    store.bp = bp; store.skin = skin;
}
double net_load_cups(void) { return store.cups; }
double net_load_candies(void) { return store.candies; }
double net_load_class(void) { return store.cls; }
double net_load_azum(void) { return store.azum; }
double net_load_santa(void) { return store.santa; }
double net_load_ebuc(void) { return store.ebuc; }
double net_load_level(void) { return store.level; }
double net_load_levels_unlocked(void) { return store.levels; }
double net_load_ordinary_level(void) { return store.ol; }
double net_load_ordinary_levels_unlocked(void) { return store.ou; }
double net_load_azum_level(void) { return store.al; }
double net_load_azum_levels_unlocked(void) { return store.au; }
double net_load_santa_level(void) { return store.sl; }
double net_load_santa_levels_unlocked(void) { return store.su; }
double net_load_ebuc_level(void) { return store.el; }
double net_load_ebuc_levels_unlocked(void) { return store.eu; }
double net_load_bp_level(void) { return store.bp; }
double net_load_azum_skin(void) { return store.skin; }
double net_load_achievement_flags(void) { return store.flags; }
void net_save_achievement_flags(double f) { store.flags = f; }
double net_load_azum_revives(void) { return store.revives; }
void net_save_azum_revives(double r) { store.revives = r; }
void net_mark_achievement_flag(double bit) { store.flags += bit; store.cloud_pushes++; }

void net_save_settings(double language, double hitboxes) {
    store.settings_saves++;
    store.language = language;
    store.hitboxes = hitboxes;
}
double net_load_language(void) { return store.language; }
double net_load_hitboxes(void) { return store.hitboxes; }
double net_load_music_volume(void) { return store.musicvol; }
void net_save_music_volume(double v) { store.musicvol = v; }
double net_load_fps_cap(void) { return 0; }
void net_save_fps_cap(double v) { (void)v; }
double net_load_render_scale(void) { return 1; }
void net_save_render_scale(double v) { (void)v; }
double net_load_winter_theme(void) { return store.winter; }
void net_save_winter_theme(double v) { store.winter = v; }
double net_load_fps_meter(void) { return store.showfps; }
void net_save_fps_meter(double v) { store.showfps = v; }
void ds_set_fps_cap(int v) { (void)v; }
void ds_set_render_scale(int v) { (void)v; }

/* Снимок сети (класс/уровень/скин своего бойца) — просто считаем вызовы. */
void net_set_class(double v) { (void)v; store.cloud_pushes++; }
void net_set_level(double v) { (void)v; }
void net_set_skin(double v) { (void)v; }

static void near(const char *what, double a, double b) {
    if (fabs(a - b) >= 1e-6) {
        fprintf(stderr, "near fail (%s): %g vs %g\n", what, a, b);
        abort();
    }
}
static void fresh_device(void) {
    memset(&store, 0, sizeof(store));
    store.hitboxes = 1;  /* как в settings.dat по умолчанию */
    store.musicvol = 70; /* громкость музыки по умолчанию */
    store.winter = 1;    /* зимняя тема по умолчанию включена */
    store.showfps = 0;   /* счётчик FPS в бою по умолчанию выключен */
    reset();
}

/* Покупка бука обязана уйти в сохранение: валюта, владелец класса, выбор. */
static void test_buy_buk_saves(void) {
    fresh_device();
    candies = 500;
    ds_fn_pick_class(CLASS_EBUC);
    assert(store.progress_saves >= 1);
    near("store.ebuc, 1", store.ebuc, 1);
    near("store.cls, CLASS_EBUC", store.cls, CLASS_EBUC);
    near("store.candies, 500 - ebuc_candy_cost", store.candies, 500 - ebuc_candy_cost);
    near("ds_fn_class_owned_of(CLASS_EBUC), 1", ds_fn_class_owned_of(CLASS_EBUC), 1);
    /* Уровень ветки и скин уходят тем же сохранением. */
    cups = 100;
    assert(ds_fn_buy_level(CLASS_EBUC, 1) == 1);
    near("store.el, 1", store.el, 1);
    near("store.eu, 1", store.eu, 1);
    near("store.cls, CLASS_EBUC", store.cls, CLASS_EBUC);
    /* Скины есть только у Азума: покупаем его и берём «Зомби». */
    achievement_mask = ACH_LEGENDS;
    cups = 500;
    ds_fn_pick_class(CLASS_AZUM);
    near("store.azum, 1", store.azum, 1);
    ds_fn_pick_skin(SKIN_ZOMBIE);
    near("store.skin, SKIN_ZOMBIE", store.skin, SKIN_ZOMBIE);
    puts("save: buk, its level and skin reach net_save_progress_all");
}

/* Перезапуск игры: то, что сохранилось, обязано читаться обратно. */
static void test_restart_keeps_buk(void) {
    fresh_device();
    candies = 400;
    cups = 60;
    ds_fn_pick_class(CLASS_EBUC);
    ds_fn_buy_level(CLASS_EBUC, 1);
    int before = store.progress_saves;
    /* «Перезаход»: память скрипта обнуляется, файл на устройстве остаётся. */
    reset();
    assert(ds_fn_class_owned_of(CLASS_EBUC) == 0);
    ds_fn_progress_from_storage();
    assert(store.progress_saves == before);  /* чтение не должно перезаписывать файл */
    near("ds_fn_class_owned_of(CLASS_EBUC), 1", ds_fn_class_owned_of(CLASS_EBUC), 1);
    near("player_class, CLASS_EBUC", player_class, CLASS_EBUC);
    near("candies, 400 - ebuc_candy_cost", candies, 400 - ebuc_candy_cost);
    near("cups, 60 - ds_fn_level_cost(1)", cups, 60 - ds_fn_level_cost(1));
    near("ds_fn_class_level_of(CLASS_EBUC), 1", ds_fn_class_level_of(CLASS_EBUC), 1);
    near("player_level, 1", player_level, 1);
    puts("restart: buk, currencies and its level survive a full script reset");
}

/* Настройки: свой файл на устройстве и свои вызовы сохранения. */
static void test_settings_save(void) {
    fresh_device();
    language = 1;
    show_hitboxes = 0;
    ds_fn_save_settings();
    assert(store.settings_saves == 1);
    near("store.language, 1", store.language, 1);
    near("store.hitboxes, 0", store.hitboxes, 0);

    /* Перезапуск: язык и хитбоксы читаются из файла. */
    reset();
    near("language, 0", language, 0);
    near("show_hitboxes, 1", show_hitboxes, 1);
    ds_fn_settings_from_storage();
    near("language, 1", language, 1);
    near("show_hitboxes, 0", show_hitboxes, 0);

    /* Громкость музыки: читается из «файла», шаг жмётся в 0..100 и сразу сохраняется. */
    store.musicvol = 45;
    reset();
    ds_fn_settings_from_storage();
    near("music_volume, 45", music_volume, 45);
    ds_fn_music_volume_step(-10);
    near("music_volume, 35", music_volume, 35);
    near("store.musicvol, 35", store.musicvol, 35);
    ds_fn_music_volume_step(-100);
    near("music_volume, 0", music_volume, 0);
    ds_fn_music_volume_step(150);
    near("music_volume, 100", music_volume, 100);
    near("store.musicvol, 100", store.musicvol, 100);

    /* Зимняя тема и счётчик FPS: свои ключи файла, тот же путь чтения/записи. */
    reset();
    ds_fn_settings_from_storage();
    near("winter_theme, 1", winter_theme, 1);
    near("show_fps, 0", show_fps, 0);
    winter_theme = 0;
    show_fps = 1;
    ds_fn_save_settings();
    near("store.winter, 0", store.winter, 0);
    near("store.showfps, 1", store.showfps, 1);
    /* Перезапуск: тема и счётчик читаются обратно из сохранённого. */
    reset();
    near("winter_theme, 1", winter_theme, 1);
    near("show_fps, 0", show_fps, 0);
    ds_fn_settings_from_storage();
    near("winter_theme, 0", winter_theme, 0);
    near("show_fps, 1", show_fps, 1);
    puts("settings: language, hitboxes, music, winter theme and FPS meter survive a restart");
}

/* Стартовый путь целиком: init() обязан поднять и прогресс, и настройки. */
static void test_init_loads_everything(void) {
    fresh_device();
    store.ebuc = 1;
    store.cls = CLASS_EBUC;
    store.candies = 77;
    store.cups = 12;
    store.el = 2;
    store.eu = 2;
    store.skin = 1;
    store.flags = ACH_LEGENDS;
    store.language = 1;
    store.winter = 1;
    ds_fn_init();
    near("winter_theme, 1", winter_theme, 1);
    near("snow_tex_ok, 1", snow_tex_ok, 1);
    near("candies, 77", candies, 77);
    near("cups, 12", cups, 12);
    near("player_class, CLASS_EBUC", player_class, CLASS_EBUC);
    near("ds_fn_class_owned_of(CLASS_EBUC), 1", ds_fn_class_owned_of(CLASS_EBUC), 1);
    near("ds_fn_class_level_of(CLASS_EBUC), 2", ds_fn_class_level_of(CLASS_EBUC), 2);
    near("achievement_mask, ACH_LEGENDS", achievement_mask, ACH_LEGENDS);
    near("language, 1", language, 1);
    /* Тема выключена — снежная плитка не грузится, арена остаётся в траве. */
    store.winter = 0;
    reset();
    ds_fn_init();
    near("winter_theme, 0", winter_theme, 0);
    near("snow_tex_ok, 0", snow_tex_ok, 0);
    puts("init: startup reads progress and settings from the device");
}

/* Выход из боя сохраняет прогресс: подобранные леденцы не теряются. */
static void test_leaving_battle_saves(void) {
    fresh_device();
    game_state = ST_SOLO;
    candies = 5;
    int before = store.progress_saves;
    ds_fn_leave_screen(ST_SOLO);
    assert(store.progress_saves == before + 1);
    near("store.candies, 5", store.candies, 5);
    before = store.progress_saves;
    ds_fn_leave_screen(ST_LOBBY);
    assert(store.progress_saves == before);  /* уход из меню ничего не пишет */
    puts("battle exit: progress is saved when leaving solo/online");
}

int main(void) {
    test_buy_buk_saves();
    test_restart_keeps_buk();
    test_settings_save();
    test_init_loads_everything();
    test_leaving_battle_saves();
    puts("all saving checks passed");
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="cubic-saving-") as directory:
        temp = Path(directory)
        compiler = DimScriptCompiler()
        assert compiler.compile(find_ds_files(str(ROOT / "game/scripts")), str(temp / "game.c"))
        assert not compiler.errors and not compiler.warnings

        # ── Проводка: сохранение действительно вызывается из игры ──
        fns = compiler.functions
        save_body = "".join(fns["save_progress"][1])
        assert "net_save_progress_all(" in save_body, (
            "save_progress() must call net_save_progress_all — the whole "
            "progress write (device + cloud) lives in that one call"
        )
        assert save_body.count("net_save_progress_all(") == 1
        # 18 positional fields: cups, candies, cls, azum, santa, ebuc, level,
        # levels, 4x (level, levels_unlocked) per class, bp, skin.
        call = save_body[save_body.index("net_save_progress_all("):]
        assert call.count(",") == 17, f"expected 18 fields, got {call.count(',') + 1}"
        for fn, hook in (("touch_settings", "save_settings()"),
                         ("leave_screen", "save_progress()"),
                         ("login_do", "settings_from_storage()"),
                         ("init", "settings_from_storage()")):
            assert hook in "".join(fns[fn][1]), f"{fn} must call {hook}"

        android = temp / "android"
        android.mkdir()
        (android / "asset_manager.h").write_text("typedef struct AAssetManager AAssetManager;\n")
        (android / "log.h").touch()
        (android / "native_window.h").write_text("typedef struct ANativeWindow ANativeWindow;\n")
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
