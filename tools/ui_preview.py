#!/usr/bin/env python3
"""Предпросмотр экранов игры на хосте: PNG без Android и Vulkan.

Игра (game/game.c) рисуется настоящими функциями отрисовки, но поверх копии
прежнего CPU-растеризатора (tools/host_test/legacy_raster.inc) и настоящего
разбора TTF (ttf_font.c): текст выходит реальным шрифтом, текстуры читаются
прямо из game/assets, раскладка считается боевыми функциями layout-скриптов.
Зачем: правки меню/карточек/настроек можно увидеть и проверить на хосте, не
собирая APK (например, что длинный девиз не вылезает за карточку).

Запуск из корня репозитория (нужен cc и сгенерированный game/game.c):

    python3 gen.py
    python3 tools/ui_preview.py preview classes settings lobby

Аргументы: каталог для PNG (создаётся) и имена экранов: classes (карточки
классов с девизами), settings (настройки, 10 строк), lobby (главное меню).
Без имён экранов рисуются classes и settings. Сетевые и звуковые вызовы
заглушены, поэтому прогресс выставляется в main() руками.

Недостающие заглушки догенерируются автоматически по прототипам из
runtime.h/net.h (сигнатуры настоящие, тела возвращают 0/""/ничего), поэтому
инструмент не ломается при добавлении новых net_* функций.
"""
from __future__ import annotations

import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CC = shlex.split(__import__("os").environ.get("CC", "cc"))
SCREENS = ("classes", "settings")

STUBS = r"""
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
/* Конкатенация и числа - через пул буферов, как ds_string_pool в рантайме:
 * вложенные ds_concat(ds_concat(a, b), c) не должны затирать левый операнд
 * (одна статическая строка на все вызовы режет надписи на куски). */
#define PREV_POOL 16
static char prev_pool[PREV_POOL][1024];
static int prev_pool_i;
static char *prev_pool_next(void) {
    prev_pool_i = (prev_pool_i + 1) % PREV_POOL;
    return prev_pool[prev_pool_i];
}
char *ds_concat(const char *left, const char *right) {
    char *out = prev_pool_next();
    snprintf(out, 1024, "%s%s", left ? left : "", right ? right : "");
    return out;
}
char *ds_num_to_string(double v) {
    char *out = prev_pool_next();
    snprintf(out, 1024, "%g", v);
    return out;
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
/* Переменные ввода из runtime.h (на хосте их никто не двигает). */
int mouse_clicked = 0;
double ds_mouse_x = 0, ds_mouse_y = 0;
"""

LAYER = r"""
/* ================== настоящий растеризатор вместо моков ==================
 * legacy_raster.inc - дословная копия прежнего CPU-растеризатора игры,
 * ttf_font.c - настоящий разбор TTF и запекание атласа. Вместе они рисуют
 * кадр ровно так, как его видел бы игрок (текст - настоящим шрифтом). */
#include "tools/host_test/legacy_raster.inc"
#include "ttf_font.c"

/* Ассеты читаем прямо из game/assets - как AAssetManager на устройстве. */
struct AAsset { FILE *fp; long len; };
static int dummy_amgr_storage;
AAsset *AAssetManager_open(AAssetManager *mgr, const char *name, int mode) {
    (void)mgr; (void)mode;
    char path[512];
    snprintf(path, sizeof path, "game/assets/%s", name);
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    AAsset *a = (AAsset *)calloc(1, sizeof *a);
    if (!a) { fclose(fp); return NULL; }
    a->fp = fp;
    fseek(fp, 0, SEEK_END); a->len = ftell(fp); fseek(fp, 0, SEEK_SET);
    return a;
}
off_t AAsset_getLength(AAsset *a) { return a ? (off_t)a->len : 0; }
int AAsset_read(AAsset *a, void *buf, size_t n) { return a ? (int)fread(buf, 1, n, a->fp) : -1; }
int AAsset_close(AAsset *a) { if (!a) return 0; fclose(a->fp); free(a); return 0; }

static Buffer g_buf;
void ds_set_asset_manager(AAssetManager *a) { amgr = a ? a : (AAssetManager *)&dummy_amgr_storage; }

/* Весь текст в игре рисуется белым (кроме ников админов и тёмных чернил чата) -
 * правило дословно из native/graphics/lifecycle.inc. */
static uint32_t prev_text_force_white(uint32_t c) {
    if (c == 0xFFFF4444u || c == 0xFF4FC3F7u || c == 0xFFFF3333u || c == 0xFF33A8FFu) return c;
    if ((c & 0x00ffffffu) == 0x00202020u) return c;
    return (c & 0xff000000u) | 0x00ffffffu;
}

void rect(float x, float y, float w, float h, uint32_t c) { render_rect(&g_buf, x, y, w, h, pack_c(c)); }
void roundrect(float x, float y, float w, float h, float r, uint32_t c) { render_roundrect(&g_buf, x, y, w, h, r, pack_c(c)); }
void circle(float x, float y, float r, uint32_t c) { render_circle(&g_buf, x, y, r, pack_c(c)); }
void ring(float x, float y, float r, float t, uint32_t c) { render_ring(&g_buf, x, y, r, t, pack_c(c)); }
void line(float x1, float y1, float x2, float y2, float t, uint32_t c) { render_line(&g_buf, x1, y1, x2, y2, t, pack_c(c)); }
void clear_screen(uint32_t c) { clear_buf(&g_buf, pack_c(c)); }
int png_load(const char *name) { return load_png(name) != NULL; }
void tex(float x, float y, const char *name, float a, float sc) {
    Texture *t = load_png(name);
    if (t) draw_tx(&g_buf, t, x, y, a, sc);
}
void tex_tint(float x, float y, const char *name, float a, float sc, uint32_t c) {
    Texture *t = load_png(name);
    if (t) draw_tx_tint(&g_buf, t, x, y, a, sc, pack_c(c));
}
void text_scaled(const char *s, float x, float y, uint32_t c, float sc) {
    if (!s || !ensure_font()) return;
    render_text_now(&g_buf, s, x, y, pack_c(prev_text_force_white(c)), sc);
}
void text(const char *s, float x, float y, uint32_t c) { text_scaled(s, x, y, c, 1.0f); }

/* Метрики текста - дословно из native/graphics/lifecycle.inc. */
int text_ink_width(const char *s) {
    if (!s || !ensure_font()) return 0;
    const DSFontGlyph *ref = ds_font_glyph(font, 'S');
    float pen = -(ref ? ref->bearing_x : 0);
    int first = 1; float minL = 0, maxR = 0;
    for (const char *c = s; *c;) {
        int cp = utf8_dec(&c);
        const DSFontGlyph *g = ds_font_glyph(font, (uint32_t)cp);
        if (!g) continue;
        float dl = pen + g->bearing_x, dr = dl + g->width;
        if (first || dl < minL) minL = dl;
        if (first || dr > maxR) maxR = dr;
        first = 0; pen += g->advance;
    }
    return first ? 0 : (int)(maxR - minL + 0.5f);
}
int text_ink_height(const char *s) {
    if (!s || !ensure_font()) return 0;
    const DSFontGlyph *ref = ds_font_glyph(font, 'S');
    float base = ref ? ref->bearing_top : 0;
    int first = 1; float minT = 0, maxB = 0;
    for (const char *c = s; *c;) {
        int cp = utf8_dec(&c);
        const DSFontGlyph *g = ds_font_glyph(font, (uint32_t)cp);
        if (!g) continue;
        float dt = base - g->bearing_top, db = dt + g->height;
        if (db > base) db = base;
        if (first || dt < minT) minT = dt;
        if (first || db > maxB) maxB = db;
        first = 0;
    }
    return first ? 0 : (int)(maxB - minT + 0.5f);
}
int text_ink_top(const char *s) {
    if (!s || !ensure_font()) return 0;
    const DSFontGlyph *ref = ds_font_glyph(font, 'S');
    float base = ref ? ref->bearing_top : 0;
    int first = 1; float minT = 0;
    for (const char *c = s; *c;) {
        int cp = utf8_dec(&c);
        const DSFontGlyph *g = ds_font_glyph(font, (uint32_t)cp);
        if (!g) continue;
        float dt = base - g->bearing_top;
        if (first || dt < minT) minT = dt;
        first = 0;
    }
    return first ? 0 : (int)floorf(minT);
}
int text_width(const char *s) { return text_ink_width(s); }
int text_height(const char *s) { return text_ink_height(s); }

/* --------- запись PNG без zlib: deflate «stored»-блоками --------- */
static uint32_t png_crc(const uint8_t *p, size_t n) {
    static uint32_t tab[256]; static int ready = 0;
    if (!ready) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1; tab[i] = c; } ready = 1; }
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) c = tab[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}
static void png_chunk(FILE *f, const char *type, const uint8_t *data, size_t n) {
    uint8_t len[4] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n };
    fwrite(len, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (n) fwrite(data, 1, n, f);
    uint8_t *buf = (uint8_t *)malloc(n + 4);
    memcpy(buf, type, 4);
    if (n) memcpy(buf + 4, data, n);
    uint32_t c = png_crc(buf, n + 4);
    free(buf);
    uint8_t cb[4] = { (uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
    fwrite(cb, 1, 4, f);
}
static int write_png(const char *path, const Buffer *b) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    uint32_t w = (uint32_t)b->width, h = (uint32_t)b->height;
    ihdr[0] = w >> 24; ihdr[1] = w >> 16; ihdr[2] = w >> 8; ihdr[3] = w;
    ihdr[4] = h >> 24; ihdr[5] = h >> 16; ihdr[6] = h >> 8; ihdr[7] = h;
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);
    size_t row = (size_t)w * 4;
    size_t raw_sz = (row + 1) * h;
    uint8_t *raw = (uint8_t *)malloc(raw_sz);
    for (uint32_t y = 0; y < h; y++) {
        raw[y * (row + 1)] = 0;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t p = b->pixels[y * b->stride + x];
            uint8_t *d = &raw[y * (row + 1) + 1 + x * 4];
            d[0] = p & 0xff; d[1] = (p >> 8) & 0xff; d[2] = (p >> 16) & 0xff; d[3] = (p >> 24) & 0xff;
        }
    }
    uint32_t a = 1, bb = 0;
    for (size_t i = 0; i < raw_sz; i++) { a = (a + raw[i]) % 65521u; bb = (bb + a) % 65521u; }
    uint32_t adler = (bb << 16) | a;
    size_t max_blk = 65535;
    size_t nblk = (raw_sz + max_blk - 1) / max_blk; if (!nblk) nblk = 1;
    size_t idat_sz = 2 + nblk * 5 + raw_sz + 4;
    uint8_t *idat = (uint8_t *)malloc(idat_sz);
    size_t o = 0;
    idat[o++] = 0x78; idat[o++] = 0x01;
    for (size_t k = 0; k < nblk; k++) {
        size_t off = k * max_blk, n = raw_sz - off; if (n > max_blk) n = max_blk;
        idat[o++] = (k + 1 == nblk) ? 0x01 : 0x00;
        idat[o++] = n & 0xff; idat[o++] = (n >> 8) & 0xff;
        idat[o++] = ~n & 0xff; idat[o++] = (~n >> 8) & 0xff;
        memcpy(idat + o, raw + off, n); o += n;
    }
    idat[o++] = adler >> 24; idat[o++] = (adler >> 16) & 0xff;
    idat[o++] = (adler >> 8) & 0xff; idat[o++] = adler & 0xff;
    png_chunk(f, "IDAT", idat, o);
    free(idat); free(raw);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    return 1;
}"""

MAIN = r"""
/* ================================ сцена ================================
 * Экраны рисуются настоящими функциями игры (game.c), поэтому PNG показывает
 * ровно то, что увидел бы игрок: реальная раскладка, реальный шрифт, реальные
 * текстуры из game/assets. Прогресс выставляется руками, чтобы карточки были
 * открыты, а настройки соответствуют значениям по умолчанию из settings.dat. */
int main(int argc, char **argv) {
    const char *screen = argc > 1 ? argv[1] : "classes";
    const char *out = argc > 2 ? argv[2] : "ui_preview.png";
    int W = 1280, H = 720;
    screen_w = W; screen_h = H;
    uint32_t *px = (uint32_t *)calloc((size_t)W * H, sizeof *px);
    if (!px) { fprintf(stderr, "out of memory for framebuffer\n"); return 1; }
    g_buf.width = W; g_buf.height = H; g_buf.stride = W; g_buf.pixels = px;
    amgr = (AAssetManager *)&dummy_amgr_storage;   /* ассеты читаются с диска */
    ds_main();                                     /* глобальные массивы игры */
    ds_fn_init();                                  /* текстуры, шрифт, настройки */
    /* Значения по умолчанию из settings.dat (сетевые заглушки вернули нули). */
    language = 1; show_hitboxes = 1; music_volume = 70;
    winter_theme = 1; show_fps = 1;
    render_scale = 1; /* дефолт чистой установки (config.ds): net_load_render_scale вернул 0 */
    ds_fn_apply_winter_theme();
    /* Прогресс: все классы куплены, выбран Азум - как у играющего человека. */
    ds_fn_set_class_owned(CLASS_AZUM, 1);
    ds_fn_set_class_owned(CLASS_SANTA, 1);
    ds_fn_set_class_owned(CLASS_EBUC, 1);
    candies = 320; cups = 1200;
    player_class = CLASS_AZUM;
    ds_fn_sync_selected_class();
    if (strcmp(screen, "classes") == 0) { game_state = ST_CLASSES; ds_fn_draw_classes(); }
    else if (strcmp(screen, "settings") == 0) { game_state = ST_SETTINGS; ds_fn_draw_settings(); }
    else if (strcmp(screen, "lobby") == 0) { game_state = ST_LOBBY; ds_fn_draw_lobby(); }
    else { fprintf(stderr, "неизвестный экран: %s (классы: classes/settings/lobby)\n", screen); return 2; }
    if (!write_png(out, &g_buf)) { fprintf(stderr, "не удалось записать %s\n", out); return 1; }
    printf("%s -> %s (%dx%d)\n", screen, out, W, H);
    return 0;
}
"""

HEADER_PROTOS = (ROOT / "runtime.h", ROOT / "net.h")
VAR_DECLS = {  # переменные, которые компилятор не найдёт по прототипу функции
    "mouse_clicked": "int mouse_clicked = 0;",
    "ds_mouse_x": "double ds_mouse_x = 0;",
    "ds_mouse_y": "double ds_mouse_y = 0;",
}


def prototypes() -> dict[str, tuple[str, str]]:
    """Имя функции -> (тип возврата, аргументы) из заголовков рантайма/сети."""
    protos: dict[str, tuple[str, str]] = {}
    for header in HEADER_PROTOS:
        text = header.read_text(encoding="utf-8", errors="ignore")
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
        text = re.sub(r"//[^\n]*", " ", text)
        for m in re.finditer(r"([A-Za-z_][\w \t*]*?)\b([a-z_]\w*)\s*\(([^;{)]*)\)\s*;", text):
            ret, name, args = m.group(1).strip(), m.group(2), m.group(3).strip()
            protos.setdefault(name, (ret, args or "void"))
    return protos


def stub_source(name: str, ret: str, args: str) -> str:
    if name == "lerp":  # математика, а не заглушка: от неё зависят координаты
        return "double lerp(double a, double b, double t) { return a + (b - a) * t; }"
    if name == "ds_log_err":  # ошибки загрузки ассетов полезно видеть в консоли
        return ("void ds_log_err(const char *f, ...) { va_list ap; va_start(ap, f); "
                "vfprintf(stderr, f, ap); va_end(ap); fputc(10, stderr); }")
    ret = ret.replace("static", "").strip()
    if ret == "void":
        return f"{ret} {name}({args}) {{ }}"
    if "*" in ret:
        if "char" in ret:
            return f'{ret} {name}({args}) {{ static char e[1] = ""; return {"e" if "const" in ret else "(char *) e"}; }}'
        return f"{ret} {name}({args}) {{ return 0; }}"
    return f"{ret} {name}({args}) {{ return 0; }}"


def compile_preview(temp: Path) -> Path:
    preview = temp / "preview.c"
    stubs = temp / "stubs.c"
    binary = temp / "ui_preview"
    preview.write_text(STUBS + LAYER + MAIN, encoding="utf-8")
    stubs.write_text("#include <stdarg.h>\n#include <stdio.h>\n#include <string.h>\n", encoding="utf-8")
    protos = prototypes()
    cmd = [*CC, "-std=gnu99", "-O1", "-I", str(ROOT), "-I", str(ROOT / "game"),
           "-I", str(ROOT / "tools" / "host_test" / "stub"),
           str(preview), str(stubs), "-lm", "-o", str(binary)]
    done: set[str] = set()
    for _ in range(10):
        run = subprocess.run(cmd, capture_output=True, text=True)
        missing = sorted(set(re.findall(r"undefined reference to `(\w+)'", run.stderr)))
        if not missing:
            if run.returncode == 0:
                return binary
            sys.exit(f"сборка предпросмотра не удалась:\n{run.stderr}")
        lines = ["/* Автозаглушки: сигнатуры из runtime.h/net.h, тела нейтральные. */",
                 "#include <stdarg.h>", "#include <stdio.h>"]
        unknown = []
        for name in missing:
            if name in done:
                continue
            if name in VAR_DECLS:
                lines.append(VAR_DECLS[name])
            elif name in protos:
                lines.append(stub_source(name, *protos[name]))
            else:
                unknown.append(name)
                continue
            done.add(name)
        if unknown:
            sys.exit("нет прототипов для заглушек: " + ", ".join(unknown))
        stubs.write_text("\n".join(lines) + "\n", encoding="utf-8")
    sys.exit("не удалось слинковать предпросмотр за 10 итераций автозаглушек")


def main(argv: list[str]) -> int:
    args = argv[1:]
    known = SCREENS + ("lobby",)
    # первый аргумент - каталог для PNG, если он не имя экрана
    if args and args[0] not in known:
        out_dir = Path(args[0])
        screens = [a for a in args[1:] if a in known] or list(SCREENS)
    else:
        out_dir = Path(tempfile.mkdtemp(prefix="ui-preview-"))
        screens = [a for a in args if a in known] or list(SCREENS)
    out_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ui-preview-build-") as td:
        binary = compile_preview(Path(td))
        failed = 0
        for screen in screens:
            target = out_dir / f"{screen}.png"
            run = subprocess.run([str(binary), screen, str(target)], capture_output=True, text=True)
            sys.stderr.write(run.stderr)
            if run.returncode != 0:
                failed = 1
                print(f"{screen}: ОШИБКА рендера", file=sys.stderr)
            else:
                print(run.stdout.strip())
    return failed


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
