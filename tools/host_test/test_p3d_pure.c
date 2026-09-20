/* Хост-тест 3D-платформера на чистом C (game/game_pure.c) без Android.
 *
 * Собирается из корня репозитория (тот же набор инклудов, что у test_3d.c —
 * настоящий рендер 2D/3D, без Vulkan):
 *   gcc -std=gnu99 -O1 -o /tmp/test_p3d_pure tools/host_test/test_p3d_pure.c \
 *       -I tools/host_test/stub -I . -lm && /tmp/test_p3d_pure
 *
 * Проверяет живую игру: запуск из лобби одной кнопкой, спавн на стартовой
 * площадке и камеру от третьего лица, ходьбу джойстиком В ПРАВО НА ЭКРАНЕ
 * (старый баг «поворачиваю влево — иду вправо»), направление поворота
 * камеры (взгляд следует за пальцем) и новый pitch вверх/вниз, прыжок и
 * двойной прыжок, монеты, чекпоинты и возвращение на них после падения,
 * финиш и возврат в лобби, слои кадра (небо-градиент, 3D, HUD) и
 * физическую защиту «игрок не под полом» при скачках dt.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

#include "runtime.h"

void ds_log(const char *format, ...) { (void)format; }
void ds_log_err(const char *format, ...) {
    va_list ap; va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}
void ds_console_log(int is_error, const char *format, ...) { (void)is_error; (void)format; }
void ds_runtime_error(const char *format, ...) { (void)format; }
const char *ds_runtime_error_message(void) { return ""; }
int console_count(void) { return 0; }
const char *console_line(int i) { (void)i; return ""; }
int console_type(int i) { (void)i; return 0; }
double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

int screen_w = 1280, screen_h = 720;
double dt = 0.016;
int mouse_clicked = 0;
double ds_mouse_x = 0, ds_mouse_y = 0;
Joy joy;

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

AAsset *AAssetManager_open(AAssetManager *m, const char *n, int mode) { (void)m; (void)n; (void)mode; return NULL; }
off_t AAsset_getLength(AAsset *a) { (void)a; return 0; }
int AAsset_read(AAsset *m, void *buf, size_t c) { (void)m; (void)buf; (void)c; return 0; }
int AAsset_close(AAsset *a) { (void)a; return 0; }

/* Сама игра (одна точка входа в translation unit — статические состояния
 * доступны тестам напрямую). */
#include "../../game/game_pure.c"

/* --- проверки --- */

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (строка %d)\n", msg, __LINE__); failures++; } \
    else { printf("ok: %s\n", msg); } \
} while (0)
#define NEAR(a, b, eps) (fabs((a) - (b)) <= (eps))

static void begin(void) {
    Buffer b; b.pixels = NULL; b.width = 1280; b.height = 720; b.stride = 1280;
    if (!ds_graphics_begin_frame(&b)) { printf("FAIL: begin_frame\n"); exit(1); }
}
static void end(void) { ds_graphics_end_frame(); }

static int count_cmds(DSCommand t) {
    int n = 0;
    for (size_t i = 0; i < cmd_n; i++) if (cmds[i].t == t) n++;
    return n;
}

static void frames(int n) {
    for (int i = 0; i < n; i++) update();
}

/* --- тесты --- */

static void test_lobby_button(void) {
    reset();
    game_state = ST_LOBBY;
    t_dir = 0; t_fade = 0;
    /* Одна кнопка — в центре экрана: нажатие запускает платформер. */
    double bx = (screen_w - UI_BTN_W) / 2, by = screen_h / 2 - UI_BTN_H / 2;
    touch((float)(bx + UI_BTN_W / 2), (float)(by + UI_BTN_H / 2), 0, 1);
    CHECK(t_dir == 1 && t_target == ST_GAME, "лобби: одна кнопка запускает 3D-платформер");
    /* До перехода кадр рисуется без ошибок: фон-прямоугольник + кнопка. */
    begin();
    draw(NULL);
    CHECK(count_cmds(DS_CMD_RECT) >= 1 && count_cmds(DS_CMD_ROUND) >= 1,
          "лобби: фон и кнопка рисуются");
    end();
}

static void test_spawn_and_camera(void) {
    reset();
    touch((screen_w - UI_BTN_W) / 2 + UI_BTN_W / 2, screen_h / 2, 0, 1);
    frames(20); /* догнать переход и приземлиться */
    CHECK(game_state == ST_GAME, "перешли в игру");
    CHECK(p3d_ground == 1, "спавн: игрок стоит на стартовой площадке");
    CHECK(NEAR(p3d_py, 0.47, 0.03), "спавн: игрок на крышке площадки, не под ней");
    CHECK(p3d_px == 0 && p3d_pz == 0, "спавн: в центре стартовой площадки");
    /* Камера от третьего лица задаётся в update, глаз позади по Z и выше. */
    CHECK(ds3d_cam.ready, "камера задана в update");
    CHECK(ds3d_cam.ez < p3d_pz - 4.0, "камера: глаз позади игрока по курсу");
    CHECK(ds3d_cam.ey > p3d_py, "камера: глаз выше игрока");
}

static void test_move_forward(void) {
    reset();
    touch((screen_w - UI_BTN_W) / 2 + UI_BTN_W / 2, screen_h / 2, 0, 1);
    frames(10);
    double z0 = p3d_pz;
    /* Джойстик: вниз-вверх по экрану = вперёд по курсу. */
    touch(100, 600, 0, 2);
    touch(100, 460, 2, 2);
    frames(30);
    touch(100, 600, 1, 2);
    CHECK(p3d_pz > z0 + 1.5, "джойстик вверх идёт вперёд по курсу");
    CHECK(fabs(sin(p3d_angle)) < 0.2, "персонаж смотрит по направлению движения");
}

static void test_move_screen_right(void) {
    reset();
    touch((screen_w - UI_BTN_W) / 2 + UI_BTN_W / 2, screen_h / 2, 0, 1);
    frames(10);
    /* При yaw=0 правая сторона экрана — это мир -X (базис камеры
     * f x up). Джойстик вправо обязан двигать игрока вправо на экране:
     * старый баг двигал в левую сторону мира. */
    touch(100, 600, 0, 3);
    touch(260, 600, 2, 3);
    frames(20);
    touch(100, 600, 1, 3);
    CHECK(p3d_vx < -1.0, "джойстик вправо = вправо на экране (vx в -X)");
}

static void test_camera_yaw_direction(void) {
    reset();
    touch((screen_w - UI_BTN_W) / 2 + UI_BTN_W / 2, screen_h / 2, 0, 1);
    frames(10);
    double y0 = p3d_cam_yaw;
    /* Тянем влево: взгляд должен повернуться влево (yaw растёт,
     * fw = (sin, cos) уходит в +X — левую сторону мира на старте). */
    touch(900, 300, 0, 4);
    touch(700, 300, 2, 4);
    touch(900, 300, 1, 4);
    CHECK(p3d_cam_yaw > y0 + 1.0, "тянем влево — камера поворачивается влево");
    /* И дальше вправо: yaw вернётся обратно. */
    touch(700, 300, 0, 5);
    touch(900, 300, 2, 5);
    touch(700, 300, 1, 5);
    CHECK(p3d_cam_yaw < y0 + 1.0, "тянем вправо — камера поворачивается вправо");
}

static void test_camera_pitch(void) {
    reset();
    touch((screen_w - UI_BTN_W) / 2 + UI_BTN_W / 2, screen_h / 2, 0, 1);
    frames(10);
    double p0 = p3d_cam_pitch;
    update();
    double ey0 = ds3d_cam.ey;
    /* Тянем вниз: камера наклоняется вниз (pitch убывает, глаз поднимается
     * — взгляд из-под... нет, сверху: сильный уклон вниз = глаз высоко). */
    touch(900, 200, 0, 6);
    touch(900, 440, 2, 6);
    update();
    CHECK(p3d_cam_pitch < p0 - 0.5, "тянем вниз — камера смотрит вниз");
    CHECK(NEAR(p3d_cam_pitch, P3D_PITCH_MIN, 1e-6), "pitch ограничен снизу");
    CHECK(ds3d_cam.ey > ey0 + 1.0, "тянем вниз — глаз поднялся над игроком");
    touch(900, 440, 1, 6); /* отпустить палец */
    /* Тянем вверх: pitch растёт, но ограничен; глаз не уходит ниже
     * крыши платформы под игроком (иначе камера «под полом»). */
    touch(900, 440, 0, 7);
    touch(900, 250, 2, 7);
    touch(900, 60, 2, 7);
    update();
    CHECK(p3d_cam_pitch > p0 + 1.0, "тянем вверх — камера смотрит вверх");
    CHECK(NEAR(p3d_cam_pitch, P3D_PITCH_MAX, 1e-6), "pitch ограничен сверху");
    double top = p3d_plat[0].by + p3d_plat[0].hy;
    CHECK(ds3d_cam.ey >= top - 0.01, "глаз камеры не проваливается под пол");
    touch(900, 200, 1, 7);
}

static void test_jump_and_double_jump(void) {
    reset();
    touch((screen_w - UI_BTN_W) / 2 + UI_BTN_W / 2, screen_h / 2, 0, 1);
    frames(10);
    CHECK(p3d_ground == 1, "перед прыжком стоим");
    touch((float)p3d_atk_x, (float)p3d_atk_y, 0, 8);
    touch((float)p3d_atk_x, (float)p3d_atk_y, 1, 8);
    update();
    CHECK(p3d_vy > 5.0, "прыжок: скорость вверх");
    frames(8);
    CHECK(p3d_ground == 0, "в воздухе");
    touch((float)p3d_atk_x, (float)p3d_atk_y, 0, 9);
    touch((float)p3d_atk_x, (float)p3d_atk_y, 1, 9);
    update();
    CHECK(p3d_jumps == 2, "второй прыжок в воздухе");
    frames(90);
    CHECK(p3d_ground == 1, "приземлились обратно на площадку");
}

static void test_fall_death_and_checkpoint(void) {
    reset();
    touch((screen_w - UI_BTN_W) / 2 + UI_BTN_W / 2, screen_h / 2, 0, 1);
    frames(10);
    /* Сбор монеты: она висит над краем стартовой площадки. */
    p3d_px = 0; p3d_py = 1.25; p3d_pz = 3.5;
    frames(2);
    CHECK(p3d_coins == 1, "монета собрана");
    /* Падение в пустоту (между z=57 и 64 платформ нет): счётчик падений,
     * возврат на стартовый чекпоинт. */
    p3d_px = 0; p3d_py = 1.0; p3d_pz = 59;
    frames(150);
    CHECK(p3d_deaths == 1, "падение засчитано");
    CHECK(p3d_ground == 1 && NEAR(p3d_pz, 0, 0.5), "возврат на стартовый чекпоинт");
    /* Чекпоинт: встаём на синюю плиту (индекс 5, z=27). */
    p3d_px = 0; p3d_pz = 27; p3d_py = 2.15;
    frames(30);
    CHECK(p3d_cp == 5, "чекпоинт активирован");
    /* Снова падаем — возврат уже на чекпоинт. */
    p3d_px = 0; p3d_py = 1.0; p3d_pz = 59;
    frames(150);
    CHECK(p3d_deaths == 2 && NEAR(p3d_pz, 27, 0.5), "возврат на чекпоинт 27");
}

static void test_no_floor_tunnel(void) {
    reset();
    touch((screen_w - UI_BTN_W) / 2 + UI_BTN_W / 2, screen_h / 2, 0, 1);
    frames(10);
    /* Скачок dt: сильный провал fps в воздухе над площадкой. После
     * кадра игрок обязан быть либо над крышкой, либо (не может быть)
     * внутри/под платформой. */
    p3d_px = 0; p3d_pz = 7; p3d_py = 6.0; p3d_vy = -22.0;
    dt = 0.09;
    update();
    dt = 0.016;
    double top = p3d_plat[1].by + p3d_plat[1].hy;
    CHECK(p3d_py >= top + P3D_HALF - 0.01, "скачок dt: игрок не провалился под пол");
    /* Жёсткий случай: игрок уже внутри бокса платформы — выталкивание. */
    p3d_px = 0; p3d_pz = 7; p3d_py = p3d_plat[1].by + 0.1; p3d_vy = 0;
    update();
    CHECK(p3d_py > p3d_plat[1].by + p3d_plat[1].hy + P3D_HALF - 0.01,
          "игрок внутри платформы выталкивается на крышку");
}

static void test_finish_and_overlay(void) {
    reset();
    touch((screen_w - UI_BTN_W) / 2 + UI_BTN_W / 2, screen_h / 2, 0, 1);
    frames(10);
    p3d_px = 0; p3d_pz = 67; p3d_py = 6.6; /* финишная площадка */
    frames(30);
    CHECK(p3d_finished == 1, "финишная площадка завершила уровень");
    double t0 = p3d_finish_t;
    frames(10);
    CHECK(p3d_finish_t > t0, "таймер оверлея побежал");
    frames(40); /* тап принимается только после задержки 0.9 с */
    touch(100, 100, 0, 10); /* тап после задержки — в лобби */
    CHECK(t_dir == 1 && t_target == ST_LOBBY, "тап после финиша ведёт в лобби");
    frames(20);
    CHECK(game_state == ST_LOBBY, "снова в лобби");
}

static void test_draw_layers(void) {
    reset();
    touch((screen_w - UI_BTN_W) / 2 + UI_BTN_W / 2, screen_h / 2, 0, 1);
    frames(25); /* дождаться конца перехода и приземления */
    t_dir = 0; t_fade = 0;
    begin();
    draw(NULL);
    /* Камера не трогается в draw (задаётся в update). */
    int grad = 0, tri = 0, lines = 0;
    for (size_t i = 0; i < cmd_n; i++) {
        if (cmds[i].t == DS_CMD_GRAD) grad++;
        else if (cmds[i].t == DS_CMD_TRI) tri++;
        else if (cmds[i].t == DS_CMD_LINE) lines++;
    }
    CHECK(grad == 1, "небо — один градиентный quad");
    CHECK(tri >= 14 * 2, "мире есть минимум 14 платформ (по 2+ видимых грани)");
    CHECK(lines == 1, "флаг финиша — линия");
    /* 2D-слой: HUD поверх 3D (первая 2D-команда после 3D выдала очередь).
     * В HUD есть круги: солнце, иконка монеты, кольцо джойстика, прыжок. */
    CHECK(count_cmds(DS_CMD_CIRCLE) >= 3, "HUD-слой нарисован поверх мира");
    end();
}

/* --- мини-растеризатор (как в test_3d.c): проверка «что реально видно» --- */
static uint32_t tfb[1280 * 720];

static void tfb_blend(uint32_t *dst, uint32_t src) {
    uint32_t sr = src & 0xff, sg = (src >> 8) & 0xff, sb = (src >> 16) & 0xff;
    uint32_t a = (src >> 24) & 0xff;
    uint32_t dr = (*dst >> 16) & 0xff, dg = (*dst >> 8) & 0xff, db = *dst & 0xff;
    if (a >= 255) { *dst = 0xff000000u | (sr << 16) | (sg << 8) | sb; return; }
    uint32_t ia = 255 - a;
    *dst = 0xff000000u | (((sr * a + dr * ia) / 255) << 16)
         | (((sg * a + dg * ia) / 255) << 8) | ((sb * a + db * ia) / 255);
}

static void tfb_tri(float x0, float y0, float x1, float y1, float x2, float y2,
                    uint32_t c) {
    float minx = fminf(fminf(x0, x1), x2), maxx = fmaxf(fmaxf(x0, x1), x2);
    float miny = fminf(fminf(y0, y1), y2), maxy = fmaxf(fmaxf(y0, y1), y2);
    if (minx < 0) minx = 0; if (miny < 0) miny = 0;
    if (maxx > 1279) maxx = 1279; if (maxy > 719) maxy = 719;
    float d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (fabsf(d) < 1e-6f) return;
    for (int y = (int)miny; y <= (int)maxy; y++)
        for (int x = (int)minx; x <= (int)maxx; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / d;
            float w1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / d;
            float w2 = 1 - w0 - w1;
            if (w0 >= -0.001f && w1 >= -0.001f && w2 >= -0.001f)
                tfb_blend(&tfb[y * 1280 + x], c);
        }
}

static void tfb_render(void) {
    for (size_t i = 0; i < cmd_n; i++) {
        DSCmd *c = &cmds[i];
        if (c->t == DS_CMD_TRI)
            tfb_tri(c->v.tri.x0, c->v.tri.y0, c->v.tri.x1, c->v.tri.y1,
                    c->v.tri.x2, c->v.tri.y2, c->v.tri.c);
        /* 2D-команды не нужны: в проверках используется только 3D-слой. */
    }
}

/* rbox3d: замкнутый скруглённый бокс виден с любой стороны и при вращении
 * (качании вокруг плеча) число видимых граней не обнуляется. */
static void test_rbox(void) {
    static const double dirs[8][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 },
        { 0, 0, 1 }, { 0, 0, -1 }, { 0.6, 0.6, 0.6 }, { 0.6, -0.6, 0.6 },
    };
    begin();
    for (int d = 0; d < 8; d++) {
        cam3d(dirs[d][0] * 6, dirs[d][1] * 6, dirs[d][2] * 6, 0, 0, 0, 60);
        rbox3d(0, 0, 0, 1.8, 1.8, 1.8, 0.6, 6, 0, 0, 0, 0, 0, 0, 0xFF9E9E9E);
        flush3d();
        int n = count_cmds(DS_CMD_TRI);
        CHECK(n > 100, "rbox: скруглённый бокс виден с этой стороны");
        if (n <= 100) { end(); begin(); }
    }
    int min_n = 1 << 30;
    for (double a = 0; a < 6.3; a += 0.5) {
        cam3d(4, 1, 4, 0, 0, 0, 60);
        rbox3d(0, 0, 0, 1.3, 2.8, 1.1, 0.02, 3, 0, a, 0, 1.4, 0, 0, 0xFF9E9E9E);
        flush3d();
        int n = count_cmds(DS_CMD_TRI);
        if (n < min_n) min_n = n;
    }
    CHECK(min_n > 20, "rbox: при качании вокруг плеча грани не пропадают");
    end();
}

/* Большая плоская грань делится на плитки: без этого средняя глубина
 * грани — плохой ключ алгоритма художника. */
static void test_tile_big_face(void) {
    begin();
    /* Крыша 8×8 видна скошенно: разброс глубин по грани ~8 — без тилайнинга
     * это была бы одна грань со средней глубиной в её центре. */
    cam3d(0, 2, 10, 0, 0, 0, 50);
    cube3d(0, 0, 0, 8, 1, 8, 0xFF9C6B3F);
    flush3d();
    int n = count_cmds(DS_CMD_TRI);
    /* Без тилайнинга это было бы ~10 треугольников (6 граней × 2);
     * с тилайнингом — сотни (крыша 8×8 с наклоненной камеры). */
    CHECK(n > 100, "большая грань делится на плитки для алгоритма художника");
    end();
}

/* Регрессия «пропадают руки на движущихся плитах»: игрок у дальнего края
 * небольшой движущейся платформы, камера под стандартным углом. Без
 * тилайнинга крыша платформы (одна большая грань, средняя глубина ближе
 * к камере) рисовалась ПОСЛЕ персонажа и перекрывала его конечности. */
/* Регрессия «пропадают руки на движущихся плитах»: раньше монета №3
 * висела в центре пути платформы 4 (0, 2.5, 22) — прямо между камерой
 * и игроком; с невысокого ракурса её проекция накрывала руки персонажа
 * и те «исчезали». Теперь монета смещена в сторону (2.2, 2.3, 22), и в
 * кадре над плечом игрока — серая рука персонажа, а не монета/пол. */
static void test_limb_over_platform(void) {
    reset();
    game_state = ST_GAME;
    t_dir = 0; t_fade = 0;
    p3d_init_world();
    p3d_t = 0; /* позиции движущихся платформ считаются от p3d_t */
    /* Платформа 4 (2.6×2.6, едет по Z): игрок в её дальней части — там
     * монета (раньше 0, 2.5, 22) попадает между камерой и игроком, и её
     * проекция накрывает руки. */
    p3d_px = p3d_plat[4].cx + 0.0;
    p3d_pz = p3d_plat[4].cz + 0.85;
    p3d_py = p3d_plat[4].by + p3d_plat[4].hy + P3D_HALF;
    p3d_vx = p3d_vy = p3d_vz = 0;
    p3d_ground = 1; p3d_stand = 4;
    p3d_angle = 0.0; p3d_swing = 0.4;
    p3d_cam_yaw = 0.0;
    /* Камера пониже обычного: с такого ракурса проекции накладываются. */
    p3d_cam_pitch = -0.5;
    frames(2); /* платформа едет, игрок едет с ней, камера пересчитана */
    CHECK(fabs(p3d_pz - p3d_plat[4].cz) > 0.8, "игрок едет вместе с платформой");
    /* Центр правой руки с учётом качания вокруг плеча (pivot 1.4·RIG_K). */
    double sw = p3d_swing;
    double ax = p3d_px + 1.95 * RIG_K;
    double ay = p3d_py + RIG_OY + RIG_K - 1.4 * RIG_K * cos(sw);
    double az = p3d_pz + 1.4 * RIG_K * sin(sw);
    float vx, vy, vz, sx, sy;
    ds3d_view(ax, ay, az, &vx, &vy, &vz);
    ds3d_screen_xy(vx, vy, vz, &sx, &sy);
    /* Кадр рисуем и растрируем ДО end(): end() сбрасывает список команд. */
    begin();
    draw(NULL);
    memset(tfb, 0, sizeof tfb);
    tfb_render();
    end();
    int gray = 0, total = 0;
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++) {
            int px = (int)sx + dx, py = (int)sy + dy;
            if (px < 0 || py < 0 || px >= 1280 || py >= 720) continue;
            uint32_t c = tfb[py * 1280 + px];
            int r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
            total++;
            /* Рука — серый 0x9E9E9E; под ней ещё и тень (полупрозрачные
             * слои ~45%) — цвет может быть заметно темнее базы. Монета —
             * жёлтая (r>>g), пол — коричневый: оба не пройдут проверку. */
            if (r > 30 && r < 230 && abs(r - g) < 25 && abs(g - b) < 25) gray++;
        }
    CHECK(total > 50, "рука попала в кадр");
    CHECK(gray >= total / 2, "рука не перекрыта монетой: в кадре серый риг");
}

/* «Назад» сверху и системная кнопка — оба возвращают в лобби. */
static void test_back(void) {
    reset();
    CHECK(back_pressed() == 0, "в лобби системный «Назад» не перехватывается");
    touch((screen_w - UI_BTN_W) / 2 + UI_BTN_W / 2, screen_h / 2, 0, 1);
    frames(25);
    CHECK(back_pressed() == 1, "в игре системный «Назад» перехватывается");
    frames(25);
    CHECK(game_state == ST_LOBBY, "системный «Назад» ведёт в лобби");
    touch((screen_w - UI_BTN_W) / 2 + UI_BTN_W / 2, screen_h / 2, 0, 2);
    frames(25);
    CHECK(game_state == ST_GAME, "второй запуск игры");
    touch((screen_w - UI_BTN_W) / 2 + UI_BTN_W / 2, UI_BACK_Y, 0, 3);
    frames(25);
    CHECK(game_state == ST_LOBBY, "кнопка «Назад» в HUD ведёт в лобби");
    end();
}

int main(void) {
    setbuf(stdout, NULL);
    /* В заглушке AAssetManager — неполный тип: достаточно несвязанного
     * ненулевого указателя (open_asset в тесте ничего не читает). */
    static int dummy_assets;
    memset(&dummy_assets, 0, sizeof dummy_assets);
    init((AAssetManager *)&dummy_assets);
    test_lobby_button();
    test_spawn_and_camera();
    test_move_forward();
    test_move_screen_right();
    test_camera_yaw_direction();
    test_camera_pitch();
    test_jump_and_double_jump();
    test_fall_death_and_checkpoint();
    test_no_floor_tunnel();
    test_finish_and_overlay();
    test_back();
    test_rbox();
    test_tile_big_face();
    test_limb_over_platform();
    test_draw_layers();
    if (failures) {
        printf("\nFAILURES: %d\n", failures);
        return 1;
    }
    printf("\nall ok\n");
    return 0;
}
