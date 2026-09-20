/* Хост-тест 3D-слоя (native/graphics/render3d.inc): проекция, отбор задних
 * граней, обрезка по ближней плоскости, порядок художника, встраивание в
 * 2D-список команд. Собирается из корня репозитория:
 *   gcc -std=gnu99 -O1 -o /tmp/test_3d tools/host_test/test_3d.c \
 *       -I tools/host_test/stub -I . -lm && /tmp/test_3d [out.bmp]
 * Vulkan тут не нужен: как и graphics.c, всё живёт в одном трансляционном
 * юните, только вместо vulkan_backend.inc тест подставляет заглушки ds_vk_*. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "runtime.h"

void ds_log(const char *format, ...) { (void)format; }
void ds_log_err(const char *format, ...) { (void)format; }
void ds_console_log(int is_error, const char *format, ...) { (void)is_error; (void)format; }
void ds_runtime_error(const char *format, ...) { (void)format; }
const char *ds_runtime_error_message(void) { return ""; }
int console_count(void) { return 0; }
const char *console_line(int i) { (void)i; return ""; }
int console_type(int i) { (void)i; return 0; }

int screen_w = 1280, screen_h = 720;
double dt = 0.016;
Joy joy;
int mouse_clicked = 0;
double ds_mouse_x, ds_mouse_y;

/* Прототипы GPU-заглушек: lifecycle.inc зовёт их до определения. */
typedef struct Texture Texture;
void ds_vk_pending_texture(Texture *t);
void ds_vk_texture_gpu_release(Texture *t);
void ds_vk_pending_reset(void);
void ds_vk_font_gpu_release(void);
int ds_vk_init_backend(ANativeWindow *w);
int ds_vk_begin_frame_backend(uint32_t w, uint32_t h, uint32_t ow, uint32_t oh);
void ds_vk_end_frame_backend(void);
void ds_vk_shutdown_backend(void);
/* Флаг «шрифт ждёт загрузки в GPU» обычно живёт в vulkan_state.inc. */
static int vk_pending_font;

#include "native/graphics/types.inc"
#include "native/graphics/geometry.inc"
#include "native/graphics/lifecycle.inc"
#include "native/graphics/render3d.inc"

/* Заглушки GPU-части Vulkan-бэкенда: командам всё равно, кто их рисует. */
void ds_vk_pending_texture(Texture *t) { (void)t; }
void ds_vk_texture_gpu_release(Texture *t) { (void)t; }
void ds_vk_pending_reset(void) {}
void ds_vk_font_gpu_release(void) {}
int ds_vk_init_backend(ANativeWindow *w) { (void)w; return 1; }
int ds_vk_begin_frame_backend(uint32_t w, uint32_t h, uint32_t ow, uint32_t oh) {
    (void)w; (void)h; (void)ow; (void)oh; return 1;
}
void ds_vk_end_frame_backend(void) {}
void ds_vk_shutdown_backend(void) {}

/* Активы в тесте недоступны (asset manager NULL), но open_asset линкуется —
 * добавляем тела заглушек вместо NDK. */
AAsset *AAssetManager_open(AAssetManager *m, const char *n, int mode) { (void)m; (void)n; (void)mode; return NULL; }
off_t AAsset_getLength(AAsset *a) { (void)a; return 0; }
int AAsset_read(AAsset *a, void *buf, size_t c) { (void)a; (void)buf; (void)c; return 0; }
int AAsset_close(AAsset *a) { (void)a; return 0; }

/* --- мини-растеризатор проверки: треугольники + линии в BMP --- */

static uint32_t fb[1280 * 720];

static uint32_t blend(uint32_t dst, uint32_t src) {
    /* Цвет команды — байтовый порядок pack_c (см. types.inc): r в младшем
     * байте. Фреймбуфер хранит 0xRRGGBBAA, поэтому байты нормализуем здесь —
     * и для непрозрачных, и для смешиваемых цветов одинаково. */
    uint32_t sr = src & 0xff, sg = (src >> 8) & 0xff, sb = (src >> 16) & 0xff;
    uint32_t a = (src >> 24) & 0xff;
    uint32_t dr = (dst >> 16) & 0xff, dg = (dst >> 8) & 0xff, db = dst & 0xff;
    if (a >= 255) return 0xff000000u | (sr << 16) | (sg << 8) | sb;
    uint32_t ia = 255 - a;
    uint32_t r = (sr * a + dr * ia) / 255;
    uint32_t g = (sg * a + dg * ia) / 255;
    uint32_t b = (sb * a + db * ia) / 255;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

static void fb_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) {
    float minx = fminf(fminf(x0, x1), x2), maxx = fmaxf(fmaxf(x0, x1), x2);
    float miny = fminf(fminf(y0, y1), y2), maxy = fmaxf(fmaxf(y0, y1), y2);
    if (minx < 0) minx = 0; if (miny < 0) miny = 0;
    if (maxx > 1279) maxx = 1279; if (maxy > 719) maxy = 719;
    float d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (fabsf(d) < 1e-6f) return;
    for (int y = (int)miny; y <= (int)maxy; y++) {
        for (int x = (int)minx; x <= (int)maxx; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / d;
            float w1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / d;
            float w2 = 1 - w0 - w1;
            if (w0 >= -0.001f && w1 >= -0.001f && w2 >= -0.001f)
                fb[y * 1280 + x] = blend(fb[y * 1280 + x], c);
        }
    }
}

static void fb_line(float x1, float y1, float x2, float y2, float th, uint32_t c) {
    float len = sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    int n = (int)(len * 2) + 1;
    float r = th * 0.5f;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n;
        float cx = x1 + (x2 - x1) * t, cy = y1 + (y2 - y1) * t;
        for (int y = (int)(cy - r); y <= (int)(cy + r); y++)
            for (int x = (int)(cx - r); x <= (int)(cx + r); x++) {
                if (x < 0 || y < 0 || x > 1279 || y > 719) continue;
                float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                if (dx * dx + dy * dy <= r * r) fb[y * 1280 + x] = blend(fb[y * 1280 + x], c);
            }
    }
}

static void fb_rect(float x, float y, float w, float h, uint32_t c) {
    fb_tri(x, y, x + w, y, x + w, y + h, c);
    fb_tri(x, y, x + w, y + h, x, y + h, c);
}

static void fb_render_cmds(void) {
    for (size_t i = 0; i < cmd_n; i++) {
        DSCmd *c = &cmds[i];
        switch (c->t) {
            case DS_CMD_RECT: fb_rect(c->v.rc.x, c->v.rc.y, c->v.rc.w, c->v.rc.h, c->v.rc.c); break;
            case DS_CMD_CIRCLE: case DS_CMD_RING: case DS_CMD_ROUND: case DS_CMD_RECT_ROT: break;
            case DS_CMD_LINE:
                fb_line(c->v.ln.x1, c->v.ln.y1, c->v.ln.x2, c->v.ln.y2, c->v.ln.th, c->v.ln.c);
                break;
            case DS_CMD_TRI:
                fb_tri(c->v.tri.x0, c->v.tri.y0, c->v.tri.x1, c->v.tri.y1,
                       c->v.tri.x2, c->v.tri.y2, c->v.tri.c);
                break;
            default: break;
        }
    }
}

static void fb_write_bmp(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint8_t hdr[54] = {0};
    uint32_t fsz = 54 + 1280 * 720 * 3;
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2, &fsz, 4);
    hdr[10] = 54; hdr[14] = 40;
    int32_t w = 1280, h = -720;
    memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4);
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    for (int y = 0; y < 720; y++)
        for (int x = 0; x < 1280; x++) {
            uint32_t p = fb[y * 1280 + x];
            uint8_t bgr[3] = { (uint8_t)(p & 0xff), (uint8_t)((p >> 8) & 0xff), (uint8_t)((p >> 16) & 0xff) };
            fwrite(bgr, 1, 3, f);
        }
    fclose(f);
}

/* --- проверки --- */

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (строка %d)\n", msg, __LINE__); failures++; } \
    else { printf("ok: %s\n", msg); } \
} while (0)

static int count_cmds(DSCommand t) {
    int n = 0;
    for (size_t i = 0; i < cmd_n; i++) if (cmds[i].t == t) n++;
    return n;
}

static void begin(void) {
    Buffer b; b.pixels = NULL; b.width = 1280; b.height = 720; b.stride = 1280;
    screen_w = 1280; screen_h = 720;
    if (!ds_graphics_begin_frame(&b)) { printf("FAIL: begin_frame\n"); exit(1); }
    memset(fb, 0, sizeof(fb));
}

static void end(void) { ds_graphics_end_frame(); }

/* Независимый пересчёт проекции лицевой грани куба (углы (±1,0..2,-1)) для
 * камеры cam3d(0,4,-6 -> 0,1,0, 60): базис считается здесь заново, теми же
 * формулами, но отдельным кодом — опечатка в render3d.inc даст расхождение. */
static void expect_front_corners(float *sx, float *sy) {
    double ex = 0, ey = 4, ez = -6, tx = 0, ty = 1, tz = 0;
    double fx = tx - ex, fy = ty - ey, fz = tz - ez;
    double l = sqrt(fx * fx + fy * fy + fz * fz); fx /= l; fy /= l; fz /= l;
    /* r = normalize(f x (0,1,0)), u = r x f */
    double rx = fy * 0 - fz * 1, ry = fz * 0 - fx * 0, rz = fx * 1 - fy * 0;
    l = sqrt(rx * rx + ry * ry + rz * rz); rx /= l; ry /= l; rz /= l;
    double ux = ry * fz - rz * fy, uy = rz * fx - rx * fz, uz = rx * fy - ry * fx;
    double fl = 0.5 * 720.0 / tan(30.0 * M_PI / 180.0);
    static const double cxs[4] = { -1, 1, 1, -1 }, cys[4] = { 0, 0, 2, 2 };
    for (int i = 0; i < 4; i++) {
        double dx = cxs[i] - ex, dy = cys[i] - ey, dz = -1.0 - ez;
        double vx = dx * rx + dy * ry + dz * rz;
        double vy = dx * ux + dy * uy + dz * uz;
        double vz = dx * fx + dy * fy + dz * fz;
        sx[i] = (float)(640 + vx * fl / vz);
        sy[i] = (float)(360 - vy * fl / vz);
    }
}

int main(int argc, char **argv) {
    /* 1. Проекция лицевой грани: куб 2x2x2 в (0,1,0), глаз (0,4,-6). */
    begin();
    cam3d(0, 4, -6, 0, 1, 0, 60);
    cube3d(0, 1, 0, 2, 2, 2, 0xFF808080);
    flush3d();
    /* Видны только лицевая (z=-1) и верхняя (y=2) грани. Большие грани
     * делятся на плитки (см. DS3D_TILE_SPAN), поэтому треугольников больше
     * четырёх, но их объединение — те же две грани с теми же углами. */
    CHECK(count_cmds(DS_CMD_TRI) >= 4, "видимы грани куба (2 грани, с плитками >= 4 треугольника)");
    float ex_sx[4], ex_sy[4];
    expect_front_corners(ex_sx, ex_sy);
    DSCmd *front = NULL, *top = NULL;
    for (size_t i = 0; i < cmd_n; i++) {
        if (cmds[i].t != DS_CMD_TRI) continue;
        DSCmd *c = &cmds[i];
        float ymax = fmaxf(c->v.tri.y0, fmaxf(c->v.tri.y1, c->v.tri.y2));
        if (!top && ymax < 360) top = c;
        if (ymax > 360) front = c;
    }
    CHECK(top && front, "верхняя и лицевая грани найдены по экранной высоте");
    if (front) {
        /* Лицевая грань — трапеция (камера наклонена вниз), поэтому сравниваем
         * все четыре расчётных угла с вершинами всех её плиток. */
        float vx[256], vy[256];
        int vn = 0, matched = 0;
        float front_ymax = 0;
        for (size_t i = 0; i < cmd_n && vn < 252; i++) {
            if (cmds[i].t != DS_CMD_TRI) continue;
            DSCmd *c = &cmds[i];
            float ymax = fmaxf(c->v.tri.y0, fmaxf(c->v.tri.y1, c->v.tri.y2));
            if (ymax <= 360) continue;
            front_ymax = fmaxf(front_ymax, ymax);
            vx[vn] = c->v.tri.x0; vy[vn] = c->v.tri.y0; vn++;
            vx[vn] = c->v.tri.x1; vy[vn] = c->v.tri.y1; vn++;
            vx[vn] = c->v.tri.x2; vy[vn] = c->v.tri.y2; vn++;
        }
        for (int k = 0; k < 4; k++)
            for (int i = 0; i < vn; i++)
                if (fabsf(vx[i] - ex_sx[k]) < 0.6f && fabsf(vy[i] - ex_sy[k]) < 0.6f) { matched++; break; }
        CHECK(matched == 4, "все 4 угла лицевой грани совпадают с расчётом");
        CHECK(fabsf(front_ymax - ex_sy[0]) < 0.6f, "нижний угол лицевой грани совпадает с расчётом");
    }
    end();

    /* 2. Порядок художника: дальний куб раньше ближнего. */
    begin();
    cam3d(0, 1.5, -10, 0, 0, 0, 60);
    cube3d(0, 0, 4, 1, 1, 1, 0xFF0000FF);   /* дальний: рисуется первым */
    cube3d(0, 0, 0, 1, 1, 1, 0xFF00FF00);   /* ближний: рисуется последним */
    flush3d();
    {
        /* Грани могут быть разбиты на плитки — проверяем не число, а
         * строгий порядок: все треугольники дальнего (красного) куба идут
         * раньше всех треугольников ближнего (зелёного). Глубины кубов не
         * пересекаются, поэтому порядок полный. */
        int n = 0, last_red = -1, first_green = -1, has_green = 0, has_red = 0;
        for (size_t i = 0; i < cmd_n; i++) {
            if (cmds[i].t != DS_CMD_TRI) continue;
            DSCmd *c = &cmds[i];
            n++;
            /* pack_c: r в младшем байте; у дальнего куба g==0, у ближнего g!=0. */
            int green = (c->v.tri.c & 0x00ff00u) != 0;
            if (green) { has_green = 1; if (first_green < 0) first_green = (int)i; }
            else { has_red = 1; last_red = (int)i; }
        }
        CHECK(n >= 8, "два куба дали грани (>= 8 треугольников)");
        CHECK(has_red && has_green, "оба цвета кубов на месте");
        CHECK(last_red >= 0 && first_green > last_red, "дальний куб нарисован раньше ближнего");
    }
    end();

    /* 3. Куб позади направления взгляда: ни одной команды, без падений. */
    begin();
    cam3d(0, 3, -10, 0, 1, -20, 60);
    cube3d(0, 1, 0, 2, 2, 2, 0xFF808080);
    flush3d();
    CHECK(cmd_n == 0, "куб за глазом не рисуется");
    end();

    /* 4. Куб пересекает ближнюю плоскость: обрезка без NaN. */
    begin();
    cam3d(0, 1.35, -1.05, 0, 1, 0, 60);
    cube3d(0, 1, 0, 2, 2, 2, 0xFF808080);
    flush3d();
    {
        int bad = 0, tris = 0;
        for (size_t i = 0; i < cmd_n; i++)
            if (cmds[i].t == DS_CMD_TRI) {
                tris++;
                DSCmd *c = &cmds[i];
                if (!isfinite(c->v.tri.x0 + c->v.tri.y0 + c->v.tri.x1 + c->v.tri.y1 +
                              c->v.tri.x2 + c->v.tri.y2)) bad++;
            }
        CHECK(tris > 0, "грань у глаза всё ещё видна");
        CHECK(bad == 0, "обрезка по ближней плоскости без NaN");
    }
    end();

    /* 5. 3D рисуется до последующей 2D-команды (порядок слоёв скрипта). */
    begin();
    cam3d(0, 0, -8, 0, 0, 0, 60);
    cube3d(0, 0, 0, 1, 1, 1, 0xFF808080);
    rect(0, 0, 10, 10, 0xFFFF0000);
    flush3d();
    {
        int tri_at = -1, rect_at = -1;
        for (size_t i = 0; i < cmd_n; i++) {
            if (cmds[i].t == DS_CMD_TRI && tri_at < 0) tri_at = (int)i;
            if (cmds[i].t == DS_CMD_RECT && rect_at < 0) rect_at = (int)i;
        }
        CHECK(tri_at >= 0 && rect_at > tri_at, "3D уходит под последующие 2D-команды");
    }
    end();

    /* 6. line3d: отрезок мира превращается в 2D-линию с толщиной. */
    begin();
    cam3d(0, 0, -8, 0, 0, 0, 60);
    line3d(-1, 0, 0, 1, 0, 0, 5, 0xFF00FFFF);
    flush3d();
    {
        DSCmd *ln = NULL;
        for (size_t i = 0; i < cmd_n; i++) if (cmds[i].t == DS_CMD_LINE) ln = &cmds[i];
        CHECK(ln != NULL, "line3d даёт 2D-линию");
        if (ln) {
            float lxmin = fminf(ln->v.ln.x1, ln->v.ln.x2);
            float lxmax = fmaxf(ln->v.ln.x1, ln->v.ln.x2);
            CHECK(ln->v.ln.th == 5.0f, "толщина line3d сохраняется");
            CHECK(lxmin < 640 && lxmax > 640, "отрезок симметричен относительно центра");
            CHECK(ln->v.ln.y1 > 360 - 0.6f && ln->v.ln.y1 < 360 + 0.6f, "горизонталь мира на уровне глаз — центр экрана");
        }
    }
    end();

    /* 7. Повёрнутый куб (yaw): без NaN, больше одной грани. */
    begin();
    cam3d(0, 0, -8, 0, 0, 0, 60);
    cube3d_yaw(0, 0, 0, 2, 2, 2, 0.7853981633974483, 0xFF808080);
    flush3d();
    CHECK(count_cmds(DS_CMD_TRI) >= 4, "куб с yaw рисует несколько граней");
    {
        int bad = 0;
        for (size_t i = 0; i < cmd_n; i++)
            if (cmds[i].t == DS_CMD_TRI) {
                DSCmd *c = &cmds[i];
                if (!isfinite(c->v.tri.x0 + c->v.tri.y0 + c->v.tri.x1 + c->v.tri.y1 +
                              c->v.tri.x2 + c->v.tri.y2)) bad++;
            }
        CHECK(bad == 0, "повёрнутый куб без NaN");
    }
    end();

    /* 8. Освещение: верхняя грань ярче лицевой при том же базовом цвете. */
    begin();
    cam3d(0, 4, -6, 0, 1, 0, 60);
    cube3d(0, 1, 0, 2, 2, 2, 0xFF808080);
    flush3d();
    {
        DSCmd *front = NULL, *top = NULL;
        for (size_t i = 0; i < cmd_n; i++) {
            if (cmds[i].t != DS_CMD_TRI) continue;
            DSCmd *c = &cmds[i];
            float ymax = fmaxf(c->v.tri.y0, fmaxf(c->v.tri.y1, c->v.tri.y2));
            if (!top && ymax < 360) top = c;
            if (ymax > 360) front = c;
        }
        CHECK(top && front, "грани найдены для проверки света");
        if (top && front) {
            uint32_t tr = (top->v.tri.c >> 16) & 0xff;
            uint32_t fr = (front->v.tri.c >> 16) & 0xff;
            CHECK(tr > fr, "верхняя грань освещена ярче лицевой");
            CHECK(tr <= 0x80 && fr <= 0x80, "свет не пересвечивает базовый цвет");
        }
    }
    end();

    /* 9. Демонстрационная сцена (картинка для глаз): «островки» платформера. */
    begin();
    {
        fb_rect(0, 0, 1280, 720, 0xFF2E3440);
        cam3d(0, 7.0, -13.0, 0, 2.2, 0, 58);
        cube3d(0, 0.5, 0, 9, 1, 9, 0xFF3DA05A);       /* стартовая площадка */
        cube3d(6.5, 1.2, -2, 3, 1, 3, 0xFFC9A227);    /* ступени */
        cube3d(11.0, 2.0, 1, 3, 1, 3, 0xFFC9A227);
        cube3d(15.5, 3.0, -1, 3, 1, 3, 0xFF3A7BD5);   /* контрольная точка */
        cube3d(21.0, 3.6, 0, 2.4, 0.6, 2.4, 0xFF8A5A44); /* движущаяся платформа */
        cube3d(26.0, 4.2, 0, 5, 1, 5, 0xFF3A7BD5);    /* финиш */
        cube3d_yaw(6.5, 2.6, -2, 0.55, 0.55, 0.55, 0.6, 0xFFFFD34D);  /* монета */
        cube3d_yaw(11.0, 3.4, 1, 0.55, 0.55, 0.55, 1.9, 0xFFFFD34D);
        cube3d(0, 1.45, 0, 0.9, 0.9, 0.9, 0xFFE94560); /* игрок-куб */
        line3d(26, 5.4, -2.2, 26, 5.4, 2.2, 3, 0xFF4FC3F7); /* флажок финиша */
        flush3d();
        fb_render_cmds();
        end();
        fb_write_bmp(argc > 1 ? argv[1] : "/tmp/test_3d_scene.bmp");
        printf("сцена записана: %s\n", argc > 1 ? argv[1] : "/tmp/test_3d_scene.bmp");
    }

    printf(failures ? "\nЕСТЬ ОШИБКИ: %d\n" : "\nвсе проверки пройдены\n", failures);
    return failures ? 1 : 0;
}
