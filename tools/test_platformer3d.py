#!/usr/bin/env python3
"""Сквозной тест 3D-платформера (game3d/platformer.ds) без Android.

Компилирует настоящие скрипты игры (как их собирает gen.py) вместе с заглушками
рантайма и проверяет на живом коде: приземление и гравитацию, прыжок и второй
прыжок, ходьбу вперёд и камеру от третьего лица (cam3d звался, глаз позади),
гибель при падении и возврат на чекпоинт, сбор монет, активацию чекпоинта,
финиш с оверлеем, кнопку в лобби и порядок слоёв отрисовки (небо -> 3D -> HUD).

Заглушки рантайма берутся из HARNESS в tools/test_battle_fixes.py (общая
часть до main), чтобы список net_*/sound*/keyboard* не разъезжался между
тестами; сверху добавляются регистраторы cam3d/cube3d/line3d.
"""
from pathlib import Path
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
from test_battle_fixes import HARNESS as BATTLE_HARNESS  # noqa: E402


def build_harness() -> str:
    """Общая часть боевой заглушки (до main) + 3D-регистраторы + свои тесты."""
    marker = "int main(void) {"
    cut = BATTLE_HARNESS.find(marker)
    assert cut > 0, "в HARNESS battle-теста не найден main"
    base = BATTLE_HARNESS[:cut]
    tests = r'''


/* Математика FUNCTION_MAP (abs в скриптах -> ds_abs) и ещё несколько
 * сетевых/текстовых функций, которые тянут draw_online и админ-чат. */

double ds_mod(double a, double b) { return fmod(a, b); }
double net_auth(const char *url, const char *nick, const char *pass) {
    (void)url; (void)nick; (void)pass; return 0;
}
double net_banned(void) { return 0; }

double ds_abs(double v) { return v < 0 ? -v : v; }
void text(const char *s, float x, float y, uint32_t c) { text_scaled(s, x, y, c, 1.0f); }
void net_ban_set(const char *n, double b) { (void)n; (void)b; }
double net_player_skin(double s) { (void)s; return 0; }
double net_leaderboard_cups(double i) { (void)i; return 0; }
double net_player_prime_level(double s) { (void)s; return 0; }

/* ---- Заглушки чата/звука/сети: тест гоняет update() в состоянии
 * ST_PLATFORM3D, поэтому линкер тянет и «онлайновые» ветки update(). ---- */
void ds_set_asset_manager(AAssetManager *a) { (void)a; }
static int snd_stub(void) { return 0; }
int snd_load(const char *n) { (void)n; return snd_stub(); }
int snd_play(const char *n) { (void)n; return snd_stub(); }
int snd_loop(const char *n) { (void)n; return snd_stub(); }
void snd_stop(const char *n) { (void)n; }
int snd_playing(const char *n) { (void)n; return 0; }
void snd_volume(const char *n, double v) { (void)n; (void)v; }
void snd_stop_all(void) {}
const char *net_login_nick(void) { return ""; }
const char *net_login_pass(void) { return ""; }
void net_autologin(const char *url) { (void)url; }
void net_set_firebase_key(const char *k) { (void)k; }
void net_disconnect(void) {}
double net_is_banned(const char *n) { (void)n; return 0; }
void net_leaderboard_fetch(const char *url) { (void)url; }
double net_leaderboard_status(void) { return 2; }
double net_leaderboard_count(void) { return 0; }
const char *net_leaderboard_nick(double i) { (void)i; return ""; }
void net_chat_send(const char *text) { (void)text; }
double net_chat_count(void) { return 0; }
const char *net_chat_text(double i) { (void)i; return ""; }
const char *net_chat_uid(double i) { (void)i; return ""; }
const char *net_chat_key(double i) { (void)i; return ""; }
void net_chat_trim(double keep) { (void)keep; }
double net_chat_is_ban(const char *msg) { (void)msg; return 0; }
double net_chat_is_unban(const char *msg) { (void)msg; return 0; }
double net_chat_is_text_cmd(const char *msg) { (void)msg; return 0; }
const char *net_chat_ban_target(const char *msg) { (void)msg; return ""; }
const char *net_chat_unban_target(const char *msg) { (void)msg; return ""; }
const char *net_chat_text_cmd_text(const char *msg) { (void)msg; return ""; }
const char *net_chat_text_cmd_color(const char *msg) { (void)msg; return ""; }
void net_banner_send(const char *t, const char *c) { (void)t; (void)c; }
double net_banner_ts(void) { return 0; }
const char *net_banner_text(void) { return ""; }
const char *net_banner_color(void) { return ""; }
double net_load_language(void) { return 0; }
double net_load_hitboxes(void) { return 1; }
double net_load_music_volume(void) { return 60; }
double net_load_winter_theme(void) { return 1; }
double net_load_fps_meter(void) { return 0; }
const char *keyboard_get_raw(void) { return ""; }
void keyboard_clear(void) {}
void keyboard_type(const char *t) { (void)t; }
int keyboard_uses_editor(void) { return 0; }

/* ---- регистраторы 3D-слоя (реализация в native/graphics/render3d.inc) ---- */
static int cam_calls;
static double last_eye[3], last_target[3], last_fov;
void cam3d(double ex, double ey, double ez, double tx, double ty, double tz, double fov) {
    cam_calls++;
    last_eye[0] = ex; last_eye[1] = ey; last_eye[2] = ez;
    last_target[0] = tx; last_target[1] = ty; last_target[2] = tz;
    last_fov = fov;
}
static int cube_calls, yaw_calls, line3_calls, flush_calls;
static double last_cube[6];
static uint32_t last_cube_color;
void cube3d(double x, double y, double z, double sx, double sy, double sz, uint32_t c) {
    cube_calls++;
    last_cube[0] = x; last_cube[1] = y; last_cube[2] = z;
    last_cube[3] = sx; last_cube[4] = sy; last_cube[5] = sz;
    last_cube_color = c;
}
void cube3d_yaw(double x, double y, double z, double sx, double sy, double sz,
                double yaw, uint32_t c) {
    (void)yaw;
    yaw_calls++;
    cube_calls++;
    last_cube[0] = x; last_cube[1] = y; last_cube[2] = z;
    last_cube[3] = sx; last_cube[4] = sy; last_cube[5] = sz;
    last_cube_color = c;
}
void line3d(double x1, double y1, double z1, double x2, double y2, double z2,
            double th, uint32_t c) {
    (void)x1; (void)y1; (void)z1; (void)x2; (void)y2; (void)z2; (void)th; (void)c;
    line3_calls++;
}
void flush3d(void) { flush_calls++; }

static void nearp(double a, double b, double eps) {
    assert(fabs(a - b) <= eps);
}

static void frames(int n) {
    for (int i = 0; i < n; i++) ds_fn_update();
}

static void test_spawn_and_camera(void) {
    ds_fn_p3d_start();
    game_state = ST_PLATFORM3D;
    frames(40);
    assert(p3d_ground == 1);
    nearp(p3d_py, 0.47, 0.02);            /* стоит на стартовой площадке */
    assert(cam_calls > 0);               /* камера задаётся в update */
    /* Третье лицо: глаз позади игрока по Z (курс идёт вдоль +Z). */
    assert(last_eye[2] < p3d_pz - 4);
    assert(last_eye[1] > p3d_py);
    puts("p3d: spawn lands on the start pad, camera follows from behind");
}

static void test_move_forward(void) {
    ds_fn_p3d_spawn(0);
    frames(10);
    double z0 = p3d_pz;
    joy.dy = -1;                          /* джойстик вверх = вперёд */
    frames(30);
    joy.dy = 0;
    assert(p3d_pz > z0 + 1.5);            /* куб действительно уехал вперёд */
    /* Игрок смотрит по движению: угол около нуля (курс вдоль +Z). */
    assert(fabs(sin(p3d_angle)) < 0.2);
    puts("p3d: joystick up walks the cube forward along the course");
}

static void test_jump_and_double_jump(void) {
    ds_fn_p3d_spawn(0);
    frames(10);
    assert(p3d_ground == 1);
    p3d_buf_t = p3d_t;                    /* нажатие кнопки прыжка */
    frames(1);
    assert(p3d_vy > 5.0);
    frames(8);                            /* ещё в воздухе (полёт ~0.8 c) */
    assert(p3d_ground == 0);
    p3d_buf_t = p3d_t;
    frames(1);
    assert(p3d_jumps == 2);               /* второй прыжок в воздухе */
    frames(90);
    assert(p3d_ground == 1);              /* и приземлились обратно */
    puts("p3d: jump plus one air jump, landing back on the pad");
}

static void test_fall_death_and_checkpoint(void) {
    ds_fn_p3d_start();
    frames(10);
    /* Сбор монеты: она висит над краем стартовой площадки. */
    p3d_px = 0; p3d_py = 1.25; p3d_pz = 3.5;
    frames(2);
    assert(p3d_coins == 1);
    /* Прыжок вниз в пустоту (между 57 и 64 платформ нет):
     * счётчик падений растёт, возврат на стартовый чекпоинт. */
    p3d_px = 0; p3d_py = 1.0; p3d_pz = 59;
    frames(150);
    assert(p3d_deaths == 1);
    assert(p3d_ground == 1);
    nearp(p3d_pz, 0, 0.5);
    /* Чекпоинт: встаём на синюю плиту (индекс 5, z=27). */
    p3d_px = 0; p3d_pz = 27; p3d_py = 2.15;
    frames(30);
    assert(p3d_cp == 5);
    /* Снова падаем — возврат уже на чекпоинт. */
    p3d_px = 0; p3d_py = 1.0; p3d_pz = 59;
    frames(150);
    assert(p3d_deaths == 2);
    nearp(p3d_pz, 27, 0.5);
    puts("p3d: coins collect, deaths respawn at the last checkpoint");
}

static void test_finish_and_overlay(void) {
    ds_fn_p3d_start();
    frames(5);
    p3d_px = 0; p3d_pz = 67; p3d_py = 6.6;   /* финишная площадка */
    frames(30);
    assert(p3d_finished == 1);
    double t0 = p3d_finish_t;
    frames(10);
    assert(p3d_finish_t > t0);
    /* Тап после задержки — переход обратно в лобби. */
    ds_fn_p3d_touch(100, 100, 0, 1);
    assert(t_target == ST_LOBBY);
    puts("p3d: goal plate finishes the level, tap returns to the lobby");
}

static void test_draw_layers(void) {
    t_dir = 0; t_fade = 0;                  /* прошлый переход не мешает */
    game_state = ST_PLATFORM3D;
    p3d_finished = 0;
    ds_fn_p3d_start();
    frames(3);
    cam_calls = 0; cube_calls = 0; yaw_calls = 0; line3_calls = 0;
    call_count = 0;
    ds_fn_draw();
    assert(cam_calls == 0);               /* камера не трогается в draw */
    assert(cube_calls >= 14);             /* 14 платформ курса */
    assert(yaw_calls >= 1);               /* игрок-куб (и монеты) через yaw */
    assert(line3_calls == 1);             /* флаг финиша */
    assert(flush_calls >= 1);             /* 3D выдан до HUD */
    assert(call_count > 0);               /* 2D-слой (небо, HUD) рисуется */
    puts("p3d: draw stacks sky, 3D world and HUD in one frame");
}

static void test_lobby_button(void) {
    game_state = ST_LOBBY;
    t_dir = 0; t_fade = 0;
    double px = 640, py = ds_fn_menu_row(5) + btn_h / 2;
    ds_fn_touch_lobby(px, py, 0);
    assert(t_dir == 1 && t_target == ST_PLATFORM3D);
    assert(p3d_ready == 1);
    puts("p3d: lobby row 6 opens the 3D platformer");
}

int main(void) {
    setbuf(stdout, NULL);
    ds_main();
    test_spawn_and_camera();
    test_move_forward();
    test_jump_and_double_jump();
    test_fall_death_and_checkpoint();
    test_finish_and_overlay();
    test_draw_layers();
    test_lobby_button();
    return 0;
}
'''
    return base + tests


def main():
    with tempfile.TemporaryDirectory(prefix="cubic-p3d-") as directory:
        temp = Path(directory)
        compiler = DimScriptCompiler()
        assert compiler.compile(find_ds_files(str(ROOT / "game/scripts")), str(temp / "game.c"))

        android = temp / "android"
        android.mkdir()
        (android / "asset_manager.h").write_text("typedef struct AAssetManager AAssetManager;\n")
        (android / "log.h").touch()
        (android / "native_window.h").write_text("typedef struct ANativeWindow ANativeWindow;\n")
        (temp / "test.c").write_text(build_harness())
        subprocess.run([
            *shlex.split(os.environ.get("CC", "cc")), "-std=c99", "-O0",
            "-Werror=implicit-function-declaration", "-Werror=incompatible-pointer-types",
            "-ffunction-sections", "-fdata-sections",
            "-I", str(temp), "-I", str(ROOT),
            str(temp / "test.c"), "-Wl,--gc-sections", "-lm", "-o", str(temp / "test"),
        ], check=True)
        subprocess.run([str(temp / "test")], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
