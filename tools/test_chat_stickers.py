#!/usr/bin/env python3
"""Regression checks for the chat sticker menu, without Android or Firebase.

Compile the real DimScript sources, then run the chat sticker functions with
mock rendering and networking. Requires a host C compiler (CC). Temporary
Android header stubs only satisfy runtime.h; this is not a PC build.

Covered:
  * /sticker <имя> parsing (known textures only, no prefix abuse);
  * sticker tap sends the message and instantly closes the chat;
  * sticker button toggles the small menu, outside tap closes it;
  * sticker messages render as images in the chat list and head bubbles.
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
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.c"

int screen_w = 720, screen_h = 1280;
double dt = 0.1;
Joy joy;

/* ---------- string / misc native stubs ---------- */
double str_len(const char *s) { return (double)(int)strlen(s); }
int str_eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
int str_contains(const char *hay, const char *needle) { return hay && needle && strstr(hay, needle) != NULL; }
double str_index_of(const char *hay, const char *needle) { const char *p = hay && needle ? strstr(hay, needle) : NULL; return p ? (double)(p - hay) : -1; }
int str_starts_with(const char *s, const char *pref) { return s && pref && strncmp(s, pref, strlen(pref)) == 0; }
int str_ends_with(const char *s, const char *suf) { size_t ls = s ? strlen(s) : 0, lf = suf ? strlen(suf) : 0; return ls >= lf && suf && strcmp(s + ls - lf, suf) == 0; }
const char *str_sub(const char *s, double start, double len) {
    /* Как в нативе: каждый вызов — отдельная выделенная строка. */
    size_t sl = s ? strlen(s) : 0, st = (size_t)start, ln = (size_t)len;
    if (st > sl) st = sl;
    if (st + ln > sl) ln = sl - st;
    char *out = malloc(ln + 1);
    assert(out);
    if (s && ln > 0) memcpy(out, s + st, ln);
    out[ln] = 0;
    return out;
}
char *ds_concat(const char *left, const char *right) {
    static char buf[1024];
    snprintf(buf, sizeof(buf), "%s%s", left ? left : "", right ? right : "");
    return buf;
}
double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
double dist(double x1, double y1, double x2, double y2) { return hypot(x1 - x2, y1 - y2); }
void ds_log(const char *format, ...) { (void)format; }
void ds_runtime_error(const char *format, ...) { fputs(format, stderr); abort(); }

/* ---------- array stubs (ds_main инициализирует глобальные массивы) ---------- */
struct DSArray { int len; double values[4096]; };
DSArray *arr_new(void) { DSArray *a = calloc(1, sizeof(*a)); assert(a); return a; }
void arr_free(DSArray *a) { free(a); }
void arr_clear(DSArray *a) { a->len = 0; }
void arr_push(DSArray *a, double v) { assert(a->len < 4096); a->values[a->len++] = v; }
double arr_get(DSArray *a, double i) { return i >= 0 && i < a->len ? a->values[(int)i] : 0; }
void arr_set(DSArray *a, double i, double v) { assert(i >= 0 && i < 4096); while (a->len <= (int)i) arr_push(a, 0); a->values[(int)i] = v; }
double arr_len(DSArray *a) { return a->len; }

/* ---------- rendering mocks ---------- */
typedef struct { const char *name; float x, y, angle, scale; } TexCall;
static TexCall tex_calls[64];
static int tex_count, ring_calls, circle_calls, rect_calls, roundrect_calls;
void rect(float x, float y, float w, float h, uint32_t color) { (void)x; (void)y; (void)w; (void)h; (void)color; rect_calls++; }
void roundrect(float x, float y, float w, float h, float r, uint32_t color) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)color; roundrect_calls++; }
void circle(float x, float y, float r, uint32_t color) { (void)x; (void)y; (void)r; (void)color; circle_calls++; }
void ring(float x, float y, float r, float t, uint32_t color) { (void)x; (void)y; (void)r; (void)t; (void)color; ring_calls++; }
void line(float x1, float y1, float x2, float y2, float t, uint32_t color) { (void)x1; (void)y1; (void)x2; (void)y2; (void)t; (void)color; }
int png_load(const char *name) { (void)name; return 1; }
void tex(float x, float y, const char *name, float a, float sc) {
    if (!(isfinite(x + y + a + sc) && sc > 0) || tex_count >= 64) {
        fprintf(stderr, "bad tex: %s x=%g y=%g a=%g sc=%g (count=%d)\n", name, x, y, a, sc, tex_count);
        abort();
    }
    tex_calls[tex_count++] = (TexCall){name, x, y, a, sc};
}
void tex_tint(float x, float y, const char *name, float a, float sc, uint32_t c) { (void)c; tex(x, y, name, a, sc); }
static int ink_width(const char *s) { return s ? (int)strlen(s) * 10 : 0; }
void text(const char *s, float x, float y, uint32_t c) { (void)s; (void)x; (void)y; (void)c; }
typedef struct { char s[96]; float x, y, scale; } TextCall;
static TextCall text_calls[128];
static int text_count;
void text_scaled(const char *s, float x, float y, uint32_t c, float sc) {
    (void)y; (void)c;
    if (text_count >= 128) return;
    snprintf(text_calls[text_count].s, sizeof text_calls[0].s, "%s", s ? s : "");
    text_calls[text_count].x = x;
    text_calls[text_count].y = y;
    text_calls[text_count].scale = sc;
    text_count++;
}
int text_width(const char *s) { return ink_width(s); }
int text_height(const char *s) { (void)s; return 20; }
int text_ink_width(const char *s) { return ink_width(s); }
/* Подпись как в runtime.h: надписи вида «Уровень 3» и «HP: 20» собираются из
 * числа (ds_concat в харнессе уже есть), а карточки классов их рисуют. */
char *ds_num_to_string(double v) {
    static char buf[32];
    snprintf(buf, sizeof buf, "%g", v);
    return buf;
}
int text_ink_height(const char *s) { (void)s; return 20; }
int text_ink_top(const char *s) { (void)s; return 0; }

/* ---------- keyboard mock ---------- */
static int keyboard_up;
static char keyboard_buf[64] = "";
void keyboard_show(void) { keyboard_up = 1; }
void keyboard_hide(void) { keyboard_up = 0; }
const char *keyboard_get_text(void) { return keyboard_buf; }
const char *keyboard_get_raw(void) { return keyboard_buf; }
void keyboard_clear(void) { keyboard_buf[0] = 0; }
int keyboard_visible(void) { return keyboard_up; }
int keyboard_enter_pressed(void) { return 0; }

/* ---------- network mock ---------- */
double net_slot(void) { return 0; }
double net_login_status(void) { return 0; }
const char *net_login_nick(void) { return "Tester"; }
const char *net_login_pass(void) { return ""; }
double net_event(void) { return 0; }
void net_event_set(double mode) { (void)mode; }
double net_banned(void) { return 0; }
void net_ban_set(const char *nick, double banned) { (void)nick; (void)banned; }
double net_chat_is_ban(const char *msg) { (void)msg; return 0; }
double net_chat_is_unban(const char *msg) { (void)msg; return 0; }
const char *net_chat_ban_target(const char *msg) { (void)msg; return ""; }
const char *net_chat_unban_target(const char *msg) { (void)msg; return ""; }
double net_chat_is_text_cmd(const char *msg) { (void)msg; return 0; }
const char *net_chat_text_cmd_text(const char *msg) { (void)msg; return ""; }
const char *net_chat_text_cmd_color(const char *msg) { (void)msg; return ""; }
void net_banner_send(const char *text, const char *color) { (void)text; (void)color; }
double net_banner_ts(void) { return 0; }
const char *net_banner_text(void) { return ""; }
const char *net_banner_color(void) { return ""; }
static const char *chat_msgs[16];
static int chat_msg_count;
static char sent_buf[16][128];
static int sent_count;
void net_chat_send(const char *text) {
    assert(sent_count < 16 && strlen(text) < 128);
    strcpy(sent_buf[sent_count++], text);
}
void net_chat_trim(double keep) { (void)keep; }
double net_chat_count(void) { return chat_msg_count; }
const char *net_chat_text(double idx) { return idx >= 0 && idx < chat_msg_count ? chat_msgs[(int)idx] : ""; }
const char *net_chat_uid(double idx) { (void)idx; return "Tester"; }
const char *net_chat_key(double idx) { (void)idx; return "key"; }

static void near(double a, double b) { assert(fabs(a - b) < 0.001); }

static void test_parsing(void) {
    assert(strcmp(ds_fn_chat_sticker_tex("/sticker like.png"), "like.png") == 0);
    assert(strcmp(ds_fn_chat_sticker_tex("/sticker dislike.png"), "dislike.png") == 0);
    assert(strcmp(ds_fn_chat_sticker_tex("/sticker 75.png"), "75.png") == 0);
    /* Неизвестные текстуры и обход префикса — обычный текст. */
    assert(strcmp(ds_fn_chat_sticker_tex("/sticker hacked.png"), "") == 0);
    assert(strcmp(ds_fn_chat_sticker_tex("/sticker like.pngx"), "") == 0);
    assert(strcmp(ds_fn_chat_sticker_tex("/sticker"), "") == 0);
    assert(strcmp(ds_fn_chat_sticker_tex("/sticker "), "") == 0);
    assert(strcmp(ds_fn_chat_sticker_tex("/stickery"), "") == 0);
    assert(strcmp(ds_fn_chat_sticker_tex("hi /sticker like.png"), "") == 0);
    assert(strcmp(ds_fn_chat_sticker_tex(""), "") == 0);
    puts("parsing: only the three known sticker messages are recognized OK");
}

static void test_send_and_touch(void) {
    /* Отправка: сообщение уходит в чат, чат закрывается мгновенно. */
    sent_count = 0;
    ds_fn_chat_set_open(1);
    assert(chat_open == 1 && sticker_menu_open == 0);
    ds_fn_chat_send_sticker(STICKER_DISLIKE);
    assert(sent_count == 1 && strcmp(sent_buf[0], "/sticker dislike.png") == 0);
    assert(chat_open == 0 && sticker_menu_open == 0 && keyboard_up == 0);

    /* Кнопка: тап открывает меню, второй тап — закрывает. */
    ds_fn_chat_set_open(1);
    double bx = ds_fn_sticker_btn_x(), by = ds_fn_chat_input_y();
    near(bx, 16 + (screen_w - 32) - 112 - 8 - STICKER_BTN);
    assert(ds_fn_touch_chat(bx + 26, by + 26, 0) == 1);
    assert(sticker_menu_open == 1);
    assert(ds_fn_touch_chat(bx + 26, by + 26, 0) == 1);
    assert(sticker_menu_open == 0);

    /* Тап по плитке: стикер отправлен, чат закрыт, меню сброшено. */
    sent_count = 0;
    assert(ds_fn_touch_chat(bx + 26, by + 26, 0) == 1);  /* open menu */
    near(ds_fn_sticker_menu_x(), 16 + (screen_w - 32) - 240);
    near(ds_fn_sticker_menu_y(), by - 88 - 10);
    double tx = ds_fn_sticker_tile_x(2), ty = ds_fn_sticker_tile_y();
    assert(ds_fn_touch_chat(tx + 32, ty + 32, 0) == 1);
    assert(sent_count == 1 && strcmp(sent_buf[0], "/sticker 75.png") == 0);
    assert(chat_open == 0 && sticker_menu_open == 0);

    /* Тап в подложке меню ничего не шлёт и меню не закрывает; тап мимо — закрывает. */
    ds_fn_chat_set_open(1);
    assert(ds_fn_touch_chat(bx + 26, by + 26, 0) == 1);  /* open menu */
    sent_count = 0;
    assert(ds_fn_touch_chat(ds_fn_sticker_menu_x() + 2, ds_fn_sticker_menu_y() + 40, 0) == 1);
    assert(sent_count == 0 && sticker_menu_open == 1 && chat_open == 1);
    assert(ds_fn_touch_chat(60, 600, 0) == 1);
    assert(sticker_menu_open == 0 && chat_open == 1);

    /* Кнопка «закрыть» чат сбрасывает меню. */
    assert(ds_fn_touch_chat(bx + 26, by + 26, 0) == 1);  /* open menu */
    assert(ds_fn_touch_chat(ds_fn_chat_close_x() + 140, ds_fn_chat_top_y() + 28, 0) == 1);
    assert(chat_open == 0 && sticker_menu_open == 0);
    puts("touch: button toggles menu, tile sends + closes instantly, outside tap closes menu OK");
}

static void test_video_settings(void) {
    /* Апскейла и лимита FPS в настройках больше нет: строк стало 8, последняя -
     * индекс 7, а функций переключения их значений не существует вовсе. */
    assert(SETTINGS_ROWS == 8);
    /* Все 8 строк настроек помещаются на низком (landscape) экране: на
     * экранах ниже нужного шаг строк сжимается (но не меньше высоты кнопки). */
    screen_h = 720;
    assert(ds_fn_settings_row_y(7) + 56 <= screen_h - 4);
    screen_h = 640;
    assert(ds_fn_settings_row_y(7) + 56 <= screen_h - 4);
    screen_h = 1280;
    assert(ds_fn_settings_row_y(7) + 56 <= screen_h - 4);
    puts("video settings: no fps cap / upscale rows, 8 settings rows fit OK");
}

static void test_class_mottos(void) {
    /* Девизы классов: Азум и буК говорят свои фразы, а длинная фраза буК
     * вписывается в карточку и в экран характеристик подбором масштаба. */
    assert(strcmp(ds_fn_tr_class_azum_desc(), "Lived once, buried twice...") == 0);
    const char *ebuc = ds_fn_tr_class_ebuc_desc();
    assert(strcmp(ebuc, "You thought I was a regular cube, but it was me, buC!") == 0);
    double cw = ds_fn_classes_card_w();
    near(ds_fn_fit_text_scale("I was the first", cw - 16, 0.5), 0.5);
    double sc = ds_fn_fit_text_scale(ebuc, cw - 16, 0.5);
    assert(sc > 0.1 && sc <= 0.5);
    near(sc * ink_width(ebuc), cw - 16);          /* ровно в доступную ширину */
    int saved_w = screen_w;
    screen_w = 1600;
    near(ds_fn_fit_text_scale(ebuc, screen_w - 2 * screen_margin, 0.8), 0.8);
    screen_w = 400;   /* узкое окно: девиз обязан ужаться под поля экрана */
    double ss = ds_fn_fit_text_scale(ebuc, screen_w - 2 * screen_margin, 0.8);
    assert(ss < 0.8 && ss * ink_width(ebuc) <= screen_w - 2 * screen_margin + 0.001);
    screen_w = saved_w;

    /* Карточки классов: девиз действительно рисуется и остаётся внутри карточки. */
    text_count = 0;
    ds_fn_draw_classes();
    const char *azum = ds_fn_tr_class_azum_desc();
    double cw2 = ds_fn_classes_card_w();
    int seen_azum = 0, seen_ebuc = 0;
    for (int i = 0; i < text_count; i++) {
        const char *drawn = text_calls[i].s;
        int cls = -1;
        if (strcmp(drawn, azum) == 0) { cls = CLASS_AZUM; seen_azum = 1; }
        else if (strcmp(drawn, ebuc) == 0) { cls = CLASS_EBUC; seen_ebuc = 1; }
        if (cls < 0) continue;
        double cx = ds_fn_classes_card_x(ds_fn_class_visible_index(cls));
        double sc2 = text_calls[i].scale;
        near(sc2, ds_fn_fit_text_scale(drawn, cw2 - 16, 0.5));
        assert(sc2 <= 0.5 + 1e-9);
        assert(text_calls[i].x >= cx - 0.001);                      /* левый край внутри */
        assert(text_calls[i].x + sc2 * ink_width(drawn) <= cx + cw2 + 0.001);  /* и правый */
    }
    assert(seen_azum == 1 && seen_ebuc == 1);
    /* Девиз выбранного класса дублируется на экране характеристик — тоже вписан. */
    player_class = CLASS_EBUC;
    text_count = 0;
    ds_fn_draw_class_stats();
    int seen_stat = 0;
    for (int i = 0; i < text_count; i++) {
        if (strcmp(text_calls[i].s, ebuc) != 0) continue;
        seen_stat = 1;
        near(text_calls[i].scale, ds_fn_fit_text_scale(ebuc, screen_w - 2 * screen_margin, 0.8));
        assert(text_calls[i].x >= screen_margin - 0.001);
        assert(text_calls[i].x + text_calls[i].scale * ink_width(ebuc) <= screen_w - screen_margin + 0.001);
    }
    assert(seen_stat == 1);
    puts("class mottos: Azum/buC text and fit-to-width scaling OK");
}

static void test_render(void) {
    /* История: стикеры рисуются картинкой в своей строке, текст — текстом. */
    game_state = ST_ONLINE;
    chat_msg_count = 3;
    chat_msgs[0] = "hello";
    chat_msgs[1] = "/sticker like.png";
    chat_msgs[2] = "/sticker 75.png";
    ds_fn_chat_set_open(1);
    tex_count = 0; ring_calls = 0; circle_calls = 0;
    ds_fn_draw_chat();
    assert(tex_count == 2);  /* только две строки-стикера */
    assert(strcmp(tex_calls[0].name, "like.png") == 0);
    assert(strcmp(tex_calls[1].name, "75.png") == 0);
    near(tex_calls[0].scale, 24.0 / 100.0);  /* стикеры 100x100, строка 24px */
    near(tex_calls[1].scale, 24.0 / 100.0);
    near(tex_calls[0].x, 16 + 8);
    near(tex_calls[0].y, 84 + 28 + 2);   /* вторая строка, по центру её высоты */
    near(tex_calls[1].y, 84 + 56 + 2);   /* третья строка */
    assert(ring_calls == 1 && circle_calls == 3);  /* смайлик на стикер-кнопке */

    /* Пузырь над головой: стикер — картинкой, обычный текст — без tex. */
    tex_count = 0; roundrect_calls = 0;
    ds_fn_draw_chat_bubble_at(360, 640, "/sticker dislike.png", 2.0);
    assert(tex_count == 1 && strcmp(tex_calls[0].name, "dislike.png") == 0);
    near(tex_calls[0].scale, 40.0 / 100.0);
    near(tex_calls[0].x, 360 - 56 / 2 + 8);
    near(tex_calls[0].y, 640 - 63 - 56 - 30 + 8);  /* hw = 25*1.4*1.8 = 63 */
    assert(roundrect_calls == 1);
    tex_count = 0;
    ds_fn_draw_chat_bubble_at(360, 640, "привет", 2.0);
    assert(tex_count == 0);
    puts("render: sticker rows in the chat list and sticker head bubbles OK");
}

int main(void) {
    setbuf(stdout, NULL);
    ds_main();
    test_parsing();
    test_send_and_touch();
    test_video_settings();
    test_class_mottos();
    test_render();
    return 0;
}
'''


def main():
    # Заглушки стикеров должны лежать в ассетах 100x100.
    for name in ("like.png", "dislike.png", "75.png"):
        data = (ROOT / "game/assets" / name).read_bytes()
        assert data[:8] == b"\x89PNG\r\n\x1a\n", f"{name} is not a PNG"
        assert int.from_bytes(data[16:20], "big") == 100, f"{name} width"
        assert int.from_bytes(data[20:24], "big") == 100, f"{name} height"
    with tempfile.TemporaryDirectory(prefix="cubic-sticker-") as directory:
        temp = Path(directory)
        compiler = DimScriptCompiler()
        assert compiler.compile(find_ds_files(str(ROOT / "game/scripts")), str(temp / "game.c"))
        assert not compiler.errors and not compiler.warnings
        # Wiring: в открытом чате рисуется стикер-кнопка и меню, в истории — картинки.
        draw_body = "".join(compiler.functions["draw_chat"][2])
        assert "draw_sticker_face(" in draw_body and "draw_sticker_tile(" in draw_body
        assert "sticker_menu_open == 1" in draw_body
        touch_body = "".join(compiler.functions["touch_chat"][2])
        assert "chat_send_sticker(" in touch_body
        # Настройки: лимита FPS и апскейла больше нет - ни строк, ни подписей,
        # ни вызовов ds_set_*; последние две строки - зимняя тема и счётчик FPS.
        settings_body = "".join(compiler.functions["draw_settings"][2])
        assert "tr_fps_label()" not in settings_body
        assert "tr_scale_label()" not in settings_body
        assert "tr_winter_toggle()" in settings_body and "tr_fps_meter_toggle()" in settings_body
        assert "settings_row_y(6)" in settings_body and "settings_row_y(7)" in settings_body
        assert "settings_row_y(8)" not in settings_body and "settings_row_y(9)" not in settings_body
        touch_settings_body = "".join(compiler.functions["touch_settings"][2])
        assert "ds_set_fps_cap(" not in touch_settings_body
        assert "ds_set_render_scale(" not in touch_settings_body
        assert "next_fps_cap" not in touch_settings_body
        assert "next_render_scale" not in touch_settings_body
        assert "winter_theme = 1 - winter_theme" in touch_settings_body
        assert "show_fps = 1 - show_fps" in touch_settings_body
        bubble_body = "".join(compiler.functions["draw_chat_bubble_at"][2])
        assert "chat_sticker_tex(" in bubble_body
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
