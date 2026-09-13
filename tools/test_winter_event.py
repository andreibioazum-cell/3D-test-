#!/usr/bin/env python3
"""Regression checks for winter weather and event 3, without Android or Firebase.

Compile the real DimScript sources, then run their weather/plates functions
with mock rendering, assets and networking. Requires a host C compiler (CC).
Temporary Android header stubs only satisfy runtime.h; this is not a PC build.
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

int screen_w = 1273, screen_h = 713;
double dt = 0.1;
Joy joy;
static int snow_available, snow_loads, progress_updates, event_reads;
static double cloud_event;
struct DSArray { int len; double values[4096]; };
DSArray *arr_new(void) { DSArray *a = calloc(1, sizeof(*a)); assert(a); return a; }
void arr_free(DSArray *a) { free(a); }
void arr_clear(DSArray *a) { a->len = 0; }
void arr_push(DSArray *a, double v) { assert(a->len < 4096); a->values[a->len++] = v; }
double arr_get(DSArray *a, double i) { return i >= 0 && i < a->len ? a->values[(int)i] : 0; }
void arr_set(DSArray *a, double i, double v) {
    assert(i >= 0 && i < 4096);
    while (a->len <= i) arr_push(a, 0);
    a->values[(int)i] = v;
}
double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
double dist(double x, double y, double a, double b) { return hypot(x-a, y-b); }
void ds_log(const char *format, ...) { (void)format; }
void ds_runtime_error(const char *format, ...) { fputs(format, stderr); abort(); }
double net_slot(void) { return 0; }
double net_event(void) { event_reads++; return cloud_event; }
void net_set_class(double v) { (void)v; progress_updates++; }
void net_set_level(double v) { (void)v; }
void net_set_skin(double v) { (void)v; }
void net_save_progress_all(double a, double b, double c, double d, double e, double f,
    double g, double h, double i, double j, double k, double l, double m, double n,
    double o, double p, double q, double r) {}
double net_load_bp_level(void) { return 0; }

int png_load(const char *name) {
    assert(strcmp(name, "dice.png") != 0);
    if (strcmp(name, "snow.png") == 0) { snow_loads++; return snow_available; }
    return 1;
}
typedef struct { const char *name; float x, y, angle, scale; uint32_t tint; } TextureCall;
static TextureCall calls[128];
static int call_count, plate_rects, ring_calls;
typedef struct { float x1, y1, x2, y2, t; uint32_t color; } LineCall;
static LineCall lines[64];
static int line_count;
void line(float x1, float y1, float x2, float y2, float t, uint32_t color) {
    assert(line_count < 64 && isfinite(x1+y1+x2+y2+t));
    lines[line_count++] = (LineCall){x1, y1, x2, y2, t, color};
}
void tex_tint(float x, float y, const char *name, float a, float sc, uint32_t tint) {
    assert(strcmp(name, "dice.png") != 0);
    assert(call_count < 128 && isfinite(x+y+a+sc) && sc > 0);
    calls[call_count++] = (TextureCall){name, x, y, a, sc, tint};
}
void tex(float x, float y, const char *name, float a, float sc) {
    tex_tint(x, y, name, a, sc, 0);
}
void ring(float x, float y, float r, float th, uint32_t color) { (void)color; (void)x; (void)y; (void)r; (void)th; ring_calls++; }
void roundrect(float x, float y, float w, float h, float r, uint32_t color) { plate_rects++; }
static void near(double a, double b) { assert(fabs(a-b) < 0.001); }

static void test_background(void) {
    for (int online = 0; online < 2; online++) {
        game_state = online ? ST_ONLINE : ST_SOLO;
        for (int enabled = 0; enabled < 2; enabled++) {
            for (int available = 0; available < 2; available++) {
                candy_enabled = enabled;
                snow_available = available;
                snow_loads = 0;
                ds_fn_load_textures();
                assert(snow_loads == enabled);
                assert(snow_tex_ok == (enabled && available));
                call_count = 0;
                ds_fn_draw_arena_background();
                int columns = (int)ceil(screen_w / arena_tile_size);
                int rows = (int)ceil(screen_h / arena_tile_size);
                assert(call_count == columns * rows);
                for (int i = 0; i < call_count; i++) {
                    assert(strcmp(calls[i].name, enabled && available ? "snow.png" : "grass.png") == 0);
                    near(calls[i].x, (i % columns) * arena_tile_size);
                    near(calls[i].y, (i / columns) * arena_tile_size);
                    near(calls[i].scale, 1);
                }
            }
        }
    }
    puts("background: candy toggle, missing PNG fallback, solo/online tiling OK");
}

static void test_weather(void) {
    int reads_before = event_reads;
    for (int state = 0; state <= 14; state++) {
        for (int enabled = 0; enabled < 2; enabled++) {
            for (int mode = 0; mode < 4; mode++) {
                for (int result = 0; result < 3; result++) {
                    game_state = state; candy_enabled = enabled; event_mode = mode;
                    finished = result; snow_t = 10; event_t = 37;
                    int active = (state == ST_SOLO || state == ST_ONLINE) && enabled;
                    assert(ds_fn_snow_active() == active);
                    ds_fn_update_weather();
                    near(snow_t, active ? 10.1 : 10);
                    assert(event_t == 37 && event_mode == mode);
                    call_count = 0;
                    ds_fn_draw_snow();
                    assert(active ? call_count > 0 : call_count == 0);
                    assert(call_count <= event_flakes);
                    for (int i = 0; i < call_count; i++) {
                        assert(strcmp(calls[i].name, "snowflake.png") == 0);
                        assert(calls[i].scale <= 1);
                    }
                }
            }
        }
    }
    game_state = ST_SOLO; candy_enabled = 1; event_mode = 0; snow_t = 0;
    for (int i = 0; i < 36000; i++) ds_fn_update_weather();
    near(snow_t, 3600);  // A full hour in solo, with no network reads or expiry.
    call_count = 0;
    ds_fn_draw_snow();
    assert(call_count > 0);
    snow_t = 100000;
    ds_fn_update_weather();
    assert(snow_t == 0);
    ds_fn_update_weather();
    near(snow_t, dt);
    call_count = 0;
    ds_fn_draw_snow();
    assert(call_count > 0);
    TextureCall first = calls[0];
    ds_fn_update_weather();
    call_count = 0;
    ds_fn_draw_snow();
    assert(call_count > 0);
    assert(calls[0].x != first.x || calls[0].y != first.y || calls[0].scale != first.scale);
    TextureCall solo[128];
    int count = call_count;
    memcpy(solo, calls, sizeof(calls));
    game_state = ST_ONLINE; candy_enabled = 1; event_mode = 2;
    call_count = 0;
    ds_fn_draw_snow();
    assert(call_count == count && count > 0);
    for (int i = 0; i < count; i++) {
        near(calls[i].x, solo[i].x); near(calls[i].y, solo[i].y);
        near(calls[i].scale, solo[i].scale); near(calls[i].angle, solo[i].angle);
    }
    assert(event_reads == reads_before);
    puts("snow: all mode/flag/result combinations, continuous timer, shared effect OK");
}

static void test_candies(void) {
    ds_fn_reset_battle();
    game_state = ST_SOLO; candy_enabled = 1;
    player->x = -1000; player->y = -1000;   /* не подбирать случайно */
    ds_fn_spawn_all_candies();
    /* Спавн: в пределах экрана с отступом, таймеры появления/сбора обнулены. */
    for (int i = 0; i < candy_count; i++) {
        double x = arr_get(candy_x, i), y = arr_get(candy_y, i);
        assert(x >= candy_margin && x < screen_w - candy_margin);
        assert(y >= candy_margin && y < screen_h - candy_margin);
        assert(arr_get(candy_t, i) == 0 && arr_get(candy_pick, i) == 0);
    }
    /* Появление как у Деда Мороза: за candy_pop_time дорастает до размера. */
    dt = 0.1;
    ds_fn_tick_candies();
    call_count = 0;
    ds_fn_draw_candies();
    assert(call_count == candy_count);
    double pop = 1 - (1 - 0.1/candy_pop_time)*(1 - 0.1/candy_pop_time);
    for (int i = 0; i < call_count; i++) near(calls[i].scale, candy_scale*pop);
    ds_fn_tick_candies(); ds_fn_tick_candies(); ds_fn_tick_candies();
    call_count = 0;
    ds_fn_draw_candies();
    assert(call_count == candy_count);
    for (int i = 0; i < call_count; i++) near(calls[i].scale, candy_scale);
    /* Сбор: +1 леденец и pop-out вместо мгновенного телепорта в другую точку.
     * Таймер запускается полным в самом тике подбора, тикает со следующего. */
    arr_set(candy_x, 0, player->x);
    arr_set(candy_y, 0, player->y);
    double before = candies;
    ds_fn_tick_candies();
    assert(candies == before + candy_give);
    near(arr_get(candy_pick, 0), candy_pick_time);
    assert(arr_get(candy_x, 0) == player->x);   /* ещё не переспавнился */
    call_count = 0;
    ds_fn_draw_candies();
    assert(call_count == candy_count);
    for (int i = 0; i < call_count; i++) near(calls[i].scale, candy_scale);
    ds_fn_tick_candies();
    near(arr_get(candy_pick, 0), candy_pick_time - dt);
    call_count = 0;
    ds_fn_draw_candies();
    assert(call_count == candy_count);
    for (int i = 0; i < call_count; i++) {
        if (fabs(calls[i].x + 25*calls[i].scale - player->x) < 1)
            near(calls[i].scale, candy_scale*(candy_pick_time - dt)/candy_pick_time);
        else
            near(calls[i].scale, candy_scale);
    }
    /* Pop-out доигран -> возрождение в новой точке: t=0, пока не виден. */
    dt = 0.2;
    ds_fn_tick_candies();
    assert(arr_get(candy_pick, 0) == 0);
    assert(arr_get(candy_t, 0) == 0);
    call_count = 0;
    ds_fn_draw_candies();
    assert(call_count == candy_count - 1);
    candy_enabled = 0;
    call_count = 0;
    ds_fn_draw_candies();
    assert(call_count == 0);
    candy_enabled = 1;
    puts("candies: pop-in/pop-out like Santa, respawn after pickup OK");
}

static void test_plates(void) {
    ds_fn_reset_battle();
    game_state = ST_ONLINE; cloud_event = 3; candy_enabled = 0;
    ds_fn_update_event();
    assert(event_mode == 0);
    candy_enabled = 1;
    player->x = ds_fn_plate_ax(); player->y = ds_fn_plate_ay();
    ds_fn_update_event();
    assert(plates_phase == 0 && plate_a_lit == 1 && plate_b_lit == 0);
    call_count = 0; plate_rects = 0;
    ds_fn_draw_event_plates(); ds_fn_draw_event_santa();
    assert(plate_rects == 4 && call_count == 0);
    arr_set(remotes, remote_fields+1, ds_fn_plate_bx());
    arr_set(remotes, remote_fields+2, ds_fn_plate_by());
    arr_set(remotes, remote_fields+5, 1);
    ds_fn_update_event();
    assert(plates_phase == 1);
    /* Луча больше нет: фаза «заряда» не рисует ни одной линии, плиты просто
     * подсвечены и держат бойцов plates_charge_time секунд. */
    dt = 0.1;
    player->x = ds_fn_plate_ax() + 18; player->y = ds_fn_plate_ay() - 12;
    arr_set(remotes, remote_fields+1, ds_fn_plate_bx() - 20);
    arr_set(remotes, remote_fields+2, ds_fn_plate_by() + 9);
    ds_fn_update_event();
    line_count = 0; ring_calls = 0;
    ds_fn_draw_event_plates(); ds_fn_draw_event_candies(); ds_fn_draw_event_santa();
    assert(line_count == 0);
    /* Фаза 1: заряд, потом сжатие, потом Санта (фаза 3). */
    dt = plates_charge_time;
    ds_fn_update_event();
    assert(plates_phase == 2);
    dt = plates_shrink_time;
    ds_fn_update_event();
    assert(plates_phase == 3);
    dt = 0.1;
    player->x = -1000; player->y = -1000; // Don't collect until explicitly tested.
    for (int i = 0; i < 5; i++) ds_fn_update_event();
    assert(plates_next_candy == 1 && arr_get(plates_candies, 0) == 1);
    call_count = 0; plate_rects = 0; ring_calls = 0;
    ds_fn_draw_event_plates(); ds_fn_draw_event_santa();
    assert(plate_rects == 0 && call_count == 2);
    assert(ring_calls == 0);  /* появление без серого кольца */
    assert(strcmp(calls[0].name, "santa.png") == 0 && calls[0].tint == 0x55000000);
    assert(strcmp(calls[1].name, "santa.png") == 0 && calls[1].tint == 0);
    near(calls[1].scale * 50, plates_santa_size);
    near(calls[1].x + 25*calls[1].scale, screen_w/2);
    near(calls[1].y + 25*calls[1].scale, screen_h/2 + 6*sin(plates_santa_t*3));
    near(calls[0].x - calls[1].x, 10); near(calls[0].y - calls[1].y, 12);
    /* Случайное направление: цель броска в пределах отступов экрана, а
     * второй бросок (seed детерминирован srand в main) летит не туда же. */
    double m = candy_margin + 16;
    double tx0 = arr_get(plates_candies, 4), ty0 = arr_get(plates_candies, 5);
    assert(tx0 >= m && tx0 < screen_w - m);
    assert(ty0 >= m && ty0 < screen_h - m);
    ds_fn_plates_throw_candy();
    assert(arr_get(plates_candies, plates_candy_fields+4) != tx0 ||
           arr_get(plates_candies, plates_candy_fields+5) != ty0);
    arr_set(plates_candies, plates_candy_fields, 0);   /* второй леденец убрали */
    /* Появление как у Деда Мороза: пока t < pop_time, масштаб докручивается
     * с оборотом; после — полный, с пульсацией полёта. */
    call_count = 0;
    ds_fn_draw_event_candies();
    assert(call_count == 1 && strcmp(calls[0].name, "candy.png") == 0);
    double t_now = arr_get(plates_candies, 1);
    double k_now = t_now/plates_candy_flight;
    double p_now = t_now/plates_candy_pop_time;
    if (p_now > 1) p_now = 1;
    double pop_now = 1-(1-p_now)*(1-p_now);
    near(calls[0].scale, candy_scale*pop_now*(1+0.45*sin(pi*k_now)));
    arr_set(plates_candies, 1, 0.6);   /* t >= pop_time: полный масштаб */
    call_count = 0;
    ds_fn_draw_event_candies();
    k_now = 0.6/plates_candy_flight;
    near(calls[0].scale, candy_scale*(1+0.45*sin(pi*k_now)));
    /* Сбор: не телепорт, а pop-out на месте; леденец снимается только когда
     * анимация доиграет. Второй вызов collect повторно не подбирает. */
    player->x = arr_get(plates_candies, 4); player->y = arr_get(plates_candies, 5);
    double before = candies;
    arr_set(plates_candies, 1, plates_candy_flight);
    ds_fn_plates_collect_candies();
    ds_fn_plates_collect_candies();
    assert(candies == before+candy_give);
    assert(arr_get(plates_candies, 0) == 1);
    near(arr_get(plates_candies, 7), plates_candy_pick_time-dt);
    call_count = 0;
    ds_fn_draw_event_candies();
    assert(call_count == 1);
    near(calls[0].scale, candy_scale*(plates_candy_pick_time-dt)/plates_candy_pick_time);
    /* Сохранение теперь с троттлингом: подбор помечает dirty, а
     * tick_event_santa пишет прогресс и сбрасывает флаг. */
    assert(plates_dirty == 1);
    ds_fn_update_event();
    assert(plates_dirty == 0);
    near(arr_get(plates_candies, 7), plates_candy_pick_time-2*dt);
    ds_fn_plates_collect_candies();
    /* Pop-out доигран: леденец снят с поля. */
    assert(arr_get(plates_candies, 0) == 0);
    call_count = 0;
    ds_fn_draw_event_candies();
    assert(call_count == 0);
    /* Уход Деда Мороза: не уменьшается, а поворачивается вверх и улетает за
     * верх экрана; колец вокруг него нет ни при появлении, ни при уходе. */
    plates_thrown = plates_candy_total;   /* все леденцы розданы -> начало ухода */
    plates_santa_out = 0;
    ds_fn_tick_event_santa();
    assert(plates_santa_out > 0 && plates_santa_gone == 0 && plates_phase == 3);
    double min_scale = 1e18, min_cy = 1e18, last_angle = 1e18, exit_time = 0;
    int left_up = 0, steps = 0;
    while (plates_phase == 3 && steps < 600) {
        call_count = 0; ring_calls = 0;
        ds_fn_draw_event_santa();
        assert(call_count == 2 && ring_calls == 0);
        double sc = calls[1].scale;
        if (sc < min_scale) min_scale = sc;
        double cy = calls[1].y + 25 * sc;
        if (cy < min_cy) min_cy = cy;
        last_angle = calls[1].angle;
        double dy_before = plates_santa_dy;
        ds_fn_tick_event_santa();
        exit_time += dt;
        if (plates_phase == 3) {
            assert(plates_santa_dy < dy_before);   /* движение только вверх */
        } else {
            left_up = 1;                           /* улетел: ивент сбросился */
        }
        steps++;
    }
    assert(left_up == 1);
    near(min_scale * 50, plates_santa_size);       /* масштаб не уменьшался */
    assert(min_cy < screen_h / 2);                 /* поднялся выше центра */
    assert(exit_time < plates_santa_out_time);     /* ушёл полётом, не таймаутом */
    assert(last_angle == 0);                       /* повернулся вверх */
    cloud_event = 2;
    ds_fn_update_event();
    assert(plates_phase == 0 && plates_santa_t == 0 && arr_get(plates_candies, 0) == 0);
    call_count = 0;
    ds_fn_draw_event_santa(); ds_fn_draw_event_candies();
    assert(call_count == 0);
    cloud_event = 3; candy_enabled = 0;
    ds_fn_update_event();
    assert(event_mode == 0);
    snow_t = 53; event_t = 9; disco_t = 2; plates_prev_mode = 3;
    game_state = ST_SOLO;
    ds_fn_reset_battle();
    assert(snow_t == 0 && event_mode == 0 && event_t == 0 && disco_t == 0);
    assert(plates_prev_mode == 0 && plates_phase == 0);
    puts("plates: two-player trigger, Santa sprite/size/shadow, candies and resets OK");
}

int main(void) {
    setbuf(stdout, NULL);
    srand(20260913);   /* как в main.c: rand() сидится до первого броска */
    ds_main();
    test_background(); test_weather(); test_candies(); test_plates();
    return 0;
}
'''


def main():
    for name, size in (("grass.png", 256), ("santa.png", 50)):
        data = (ROOT / "game/assets" / name).read_bytes()
        assert int.from_bytes(data[16:20], "big") == size
        assert int.from_bytes(data[20:24], "big") == size
    with tempfile.TemporaryDirectory(prefix="cubic-winter-") as directory:
        temp = Path(directory)
        compiler = DimScriptCompiler()
        assert compiler.compile(find_ds_files(str(ROOT / "game/scripts")), str(temp / "game.c"))
        assert not compiler.errors and not compiler.warnings
        # Wiring checks: weather must tick outside the battle's early returns,
        # and both modes must draw snow below their HUD, using the same ground.
        frame = compiler.functions["update"][1]
        assert frame[-1] == "update_weather()" and frame.count("update_weather()") == 1
        for name in ("draw_game", "draw_online"):
            body = compiler.functions[name][1]
            # В draw_online первым может стоять чат-гард: пока чат открыт,
            # рисуется только чат, без арены позади.
            if body[:4] == ["if chat_open==1", "draw_chat()", "return", "end"]:
                body = body[4:]
            assert body[0] == "draw_arena_background()"
            assert body.count("draw_snow()") == 1
            assert body.index("draw_snow()") < next(i for i, line in enumerate(body) if line.startswith("hud_bar("))
        assert "update_event()" not in compiler.functions["update_game"][1]
        # Луча ивента больше нет: фаза «заряда» держит бойцов, но линии не рисует.
        assert "draw_event_beam" not in compiler.functions, \
            "event beam must be removed from the plates event"
        assert "draw_event_beam()" not in "".join(compiler.functions["draw_online"][1])
        assert "plates_charge_time" in "".join(compiler.functions["tick_plates_event"][1])
        # Леденцы (соло и ивент) появляются/собираются с анимацией, как Дед Мороз.
        solo_draw = "".join(compiler.functions["draw_candies"][1])
        assert "candy_pop_time" in solo_draw and "candy_pick" in solo_draw, \
            "solo candies must pop in/out like Santa"
        solo_tick = "".join(compiler.functions["tick_candies"][1])
        assert "candy_pick_time" in solo_tick and "candy_rand_pos(" in solo_tick, \
            "candy respawn must wait for the pop-out to finish"
        event_candy_draw = "".join(compiler.functions["draw_event_candies"][1])
        assert "plates_candy_pop_time" in event_candy_draw and "plates_candy_pick_time" in event_candy_draw
        event_candy_tick = "".join(compiler.functions["plates_collect_candies"][1])
        assert "plates_candy_pick_time" in event_candy_tick
        # Дед Мороз: без серого кольца при появлении, уход — полётом вверх.
        santa_body = "".join(compiler.functions["draw_event_santa"][1])
        assert "ring(" not in santa_body, "Santa must appear and leave without rings"
        assert "plates_santa_dy" in santa_body, "Santa must move up on exit"
        assert "plates_santa_out/plates_santa_out_time" not in santa_body, \
            "Santa must not shrink away on exit"
        santa_tick = "".join(compiler.functions["tick_event_santa"][1])
        assert "plates_santa_fly_acc" in santa_tick and "plates_santa_up" in santa_tick
        # Луч бука: без резкого обреза по дистанции и без мигающей альфы.
        station_beam = "".join(compiler.functions["draw_station_beam"][1])
        assert "d<40" not in station_beam and "station_beam_min" in station_beam
        assert "station_beam_alpha" in station_beam and "floor(250)" not in station_beam
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
