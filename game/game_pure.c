/* 3D-платформер на чистом C (без DimScript).
 *
 * Всё, что раньше жило в DimScript-скриптах и компилировалось gen.py в C,
 * теперь написано прямо на C: лобби с одной кнопкой, мир платформера,
 * физика, камера от третьего лица. Отрисовка строится теми же командами,
 * что и раньше, — 2D (rect/grad_rect/roundrect/circle/ring/text) и 3D
 * (cam3d/cube3d/cube3d_yaw/cube3d_part/line3d/flush3d), т.е. тем же
 * Vulkan-рендером (native/graphics), он не менялся.
 *
 * Точки входа для main.c: init/reset/update/draw/touch/back_pressed.
 * Глобальные screen_w, screen_h, dt, mouse_clicked, ds_mouse_x/y и joy
 * определены в native/runtime/core.inc (runtime.c), здесь только
 * используются.
 *
 * Управление:
 *   левая половина экрана  — плавающий джойстик движения (относительно камеры);
 *   правая половина экрана — поворот камеры перетаскиванием:
 *       влево-вправо  — горизонтальный обзор (взгляд следует за пальцем:
 *                       тянешь влево — смотришь влево),
 *       вверх-вниз    — наклон камеры (pitch), ограничен сверху и снизу;
 *   круглая кнопка справа  — прыжок (и двойной прыжок в воздухе).
 *
 * Игрок — блочный персонаж в духе Roblox (желтая голова и руки, синий торс,
 * зелёные ноги) с покачиванием рук и ног при ходьбе. Под ним — мягкая
 * многослойная тень, которая уменьшается и бледнеет в прыжке.
 */
#include "runtime.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ---------------- состояния и переходы ---------------- */

enum { ST_LOBBY = 0, ST_GAME = 1 };

static int game_state = ST_LOBBY;
static int t_dir = 0;             /* 1 — затемнение, -1 — осветление, 0 — нет */
static int t_target = ST_LOBBY;
static double t_fade = 0.0;
static const double TRANSITION_DUR = 0.15;

static void start_transition(int target) {
    if (t_dir == 0) {
        t_target = target;
        t_dir = 1;
    }
}

static void update_transition(void) {
    if (t_dir == 0) return;
    t_fade += t_dir * dt / TRANSITION_DUR;
    if (t_fade >= 1.0) {
        t_fade = 1.0;
        if (t_dir > 0) {
            game_state = t_target;
            t_dir = -1;
        }
    } else if (t_fade <= 0.0) {
        t_fade = 0.0;
        if (t_dir < 0) t_dir = 0;
    }
}

/* ---------------- метрики UI (как в core/config.ds) ---------------- */

#define UI_BTN_W 280.0
#define UI_BTN_H 56.0
#define UI_BTN_RADIUS 20.0
#define UI_BTN_PAD_X 12.0
#define UI_BTN_PAD_Y 8.0
#define UI_BACK_Y 24.0
#define UI_PURPLE 0xFF5F10A0u
#define UI_BG 0xFF1A1A2Eu
#define UI_TEXT 0xFFFFFFFFu
#define UI_DIM 0xFF8B98A5u

static void ui_button(double x, double y, const char *label, double scale) {
    roundrect((float)x, (float)y, (float)UI_BTN_W, (float)UI_BTN_H,
              (float)UI_BTN_RADIUS, UI_PURPLE);
    text_scaled(label, (float)(x + (UI_BTN_W - text_ink_width(label) * scale) / 2),
                (float)(y + (UI_BTN_H - text_ink_height(label) * scale) / 2
                        - text_ink_top(label) * scale + 1.5),
                UI_TEXT, (float)scale);
}

static int ui_button_hit(double px, double py, double x, double y) {
    if (px < x - UI_BTN_PAD_X || px > x + UI_BTN_W + UI_BTN_PAD_X) return 0;
    if (py < y - UI_BTN_PAD_Y || py > y + UI_BTN_H + UI_BTN_PAD_Y) return 0;
    return 1;
}

static void ui_ctext_scaled(const char *s, double y, uint32_t c, double scale) {
    text_scaled(s, (float)((screen_w - text_ink_width(s) * scale) / 2),
                (float)y, c, (float)scale);
}

static double lobby_button_y(void) { return screen_h / 2 - UI_BTN_H / 2; }

/* ---------------- мир: платформы и монеты ---------------- */

#define P3D_PLATS 14
#define P3D_COINS 7

typedef struct {
    double bx, by, bz;      /* базовый центр и половины размеров */
    double hx, hy, hz;
    int kind;               /* 0 обычная, 1 едет по X, 2 по Z, 3 чекпоинт, 4 финиш */
    double amp, spd;        /* параметры движения */
    double cx, cz;          /* текущий центр (после движения кадра) */
    double pdx, pdz;        /* дельта за кадр (игрок едет вместе с платформой) */
} P3dPlat;

static P3dPlat p3d_plat[P3D_PLATS];
static int p3d_coin_on[P3D_COINS];

typedef struct { double x, y, z, sx, sy, sz, kind, amp, spd; } P3dWorldBox;

/* (x, y, z, sx, sy, sz, kind, amp, spd) — тот же курс, что был в DimScript. */
static const P3dWorldBox P3D_WORLD[] = {
    { 0.0, -0.5,  0, 8.0, 1.0, 8.0, 0, 0.0, 0.0 },
    { 0.0, -0.25, 7, 3.0, 0.5, 3.0, 0, 0.0, 0.0 },
    { 2.5, 0.0,   12, 3.0, 0.5, 3.0, 1, 2.5, 0.9 },
    { -2.0, 0.4,  17, 3.0, 0.5, 3.0, 0, 0.0, 0.0 },
    { 0.0, 0.9,   22, 2.6, 0.5, 2.6, 2, 2.2, 1.1 },
    { 0.0, 1.4,   27, 4.0, 0.5, 4.0, 3, 0.0, 0.0 },
    { 3.2, 1.9,   32, 2.4, 0.5, 2.4, 0, 0.0, 0.0 },
    { -3.0, 2.4,  36, 2.4, 0.5, 2.4, 1, 3.0, 1.3 },
    { 0.0, 3.0,   41, 2.2, 0.5, 2.2, 0, 0.0, 0.0 },
    { 0.0, 3.5,   46, 4.0, 0.5, 4.0, 3, 0.0, 0.0 },
    { 2.0, 4.0,   51, 2.2, 0.5, 2.2, 2, 2.0, 1.4 },
    { -2.0, 4.6,  56, 2.2, 0.5, 2.2, 0, 0.0, 0.0 },
    { 0.0, 5.2,   61, 2.4, 0.5, 2.4, 1, 2.0, 1.6 },
    { 0.0, 5.8,   67, 6.0, 0.6, 6.0, 4, 0.0, 0.0 },
};
static const struct { double x, y, z; } P3D_COIN_POS[P3D_COINS] = {
    { 0.0, 1.2, 3.5 }, { 2.5, 1.6, 12 }, { -2.0, 2.0, 17 },
    /* Монета над платформой 4 со смещением вправо: в центре (0, …, 22)
     * она висела между камерой и игроком и перекрывала ему руки
     * («пропадающие руки» на движущейся плите). С края платформы её
     * собирается прыжком (радиус сбора 0.8). */
    { 2.2, 2.3, 22 },  { 3.2, 3.4, 32 }, { -3.0, 3.9, 36 },
    { 0.0, 6.8, 61 },
};

static uint32_t p3d_box_color(int kind) {
    if (kind == 0) return 0xFF9C6B3Fu;
    if (kind == 1 || kind == 2) return 0xFFB3543Fu;
    if (kind == 3) return 0xFF3A7BD5u;
    return 0xFF2E9E4Fu;
}

static void p3d_init_world(void) {
    for (int i = 0; i < P3D_PLATS; i++) {
        P3dPlat *p = &p3d_plat[i];
        const P3dWorldBox *w = &P3D_WORLD[i];
        p->bx = w->x; p->by = w->y; p->bz = w->z;
        p->hx = w->sx / 2; p->hy = w->sy / 2; p->hz = w->sz / 2;
        p->kind = (int)w->kind; p->amp = w->amp; p->spd = w->spd;
        p->cx = p->bx; p->cz = p->bz; p->pdx = 0; p->pdz = 0;
    }
    for (int i = 0; i < P3D_COINS; i++) p3d_coin_on[i] = 1;
}

/* ---------------- игрок ---------------- */

static double p3d_t = 0.0;
static double p3d_px, p3d_py, p3d_pz;
static double p3d_vx, p3d_vy, p3d_vz;
static double p3d_angle = 0.0;     /* куда смотрит персонаж (0 — по +Z) */
static int p3d_ground = 0;
static double p3d_coyote = 0.0;
static int p3d_jumps = 0;
static double p3d_buf_t = -9.0;
static int p3d_stand = -1;
static int p3d_cp = 0;
static int p3d_deaths = 0;
static int p3d_coins = 0;
static double p3d_time = 0.0;
static int p3d_finished = 0;
static double p3d_finish_t = 0.0;
static double p3d_flash = 0.0;
static double p3d_wph = 0.0;       /* фаза шага */
static double p3d_swing = 0.0;     /* текущее покачивание конечностей */

static const double P3D_HALF = 0.45;    /* половина бокса игрока */
static const double P3D_GRAV = 22.0;
static const double P3D_JUMP_V = 8.8;
static const double P3D_SPEED = 5.4;
static const double P3D_COYOTE_MAX = 0.12;
static const double P3D_BUF_MAX = 0.14;
static const double P3D_MAX_FALL = 22.0;
static const double P3D_CAM_DIST = 7.5;
static const double P3D_CAM_TGT_H = 1.1;   /* высота цели камеры над ногами */
static const double P3D_PITCH_DEF = -0.26; /* слегка вниз, как было раньше */
static const double P3D_PITCH_MIN = -1.05;
static const double P3D_PITCH_MAX = 0.85;
static const double P3D_FOV = 58.0;
static const double P3D_PI = 3.14159265358979;

/* Камера: yaw — поворот вокруг игрока, pitch — наклон вверх/вниз. */
static double p3d_cam_yaw = 0.0;
static double p3d_cam_pitch = P3D_PITCH_DEF;
static int p3d_drag_id = -1;
static double p3d_drag_x = 0.0, p3d_drag_y = 0.0;

/* Прыжок. */
static int p3d_jump_id = -1;
static double p3d_atk_x = 0.0, p3d_atk_y = 0.0;

/* Джойстик (плавающий, левая половина экрана). */
static int p3d_joy_id = -1;
static double p3d_joy_ox = 0.0, p3d_joy_oy = 0.0;
static double p3d_joy_dx = 0.0, p3d_joy_dy = 0.0;
static const double JOY_R = 70.0;

static double p3d_lerp_angle(double a, double b, double k) {
    double d = b - a;
    while (d > P3D_PI) d -= 2 * P3D_PI;
    while (d < -P3D_PI) d += 2 * P3D_PI;
    return a + d * k;
}

/* Прыжок: базовый + один в воздухе (p3d_jumps < 2). */
static void p3d_do_jump(void) {
    p3d_vy = P3D_JUMP_V;
    p3d_ground = 0;
    p3d_coyote = 0;
    p3d_stand = -1;
    p3d_jumps++;
    p3d_buf_t = -9;
}

static void p3d_spawn(int idx) {
    int i = p3d_cp;
    if (idx >= 0) i = idx;
    const P3dPlat *p = &p3d_plat[i];
    p3d_px = p->bx;
    p3d_pz = p->bz;
    p3d_py = p->by + p->hy + P3D_HALF + 0.02;
    p3d_vx = 0; p3d_vy = 0; p3d_vz = 0;
    p3d_stand = -1;
    p3d_ground = 0;
    p3d_jumps = 0;
    p3d_angle = 0;
    p3d_cam_yaw = 0;
    p3d_cam_pitch = P3D_PITCH_DEF;
}

static void p3d_die(void) {
    p3d_deaths++;
    p3d_flash = 1;
    p3d_spawn(-1);
}

static void p3d_start(void) {
    p3d_init_world();
    p3d_coins = 0;
    p3d_deaths = 0;
    p3d_time = 0;
    p3d_t = 0;
    p3d_flash = 0;
    p3d_finished = 0;
    p3d_finish_t = 0;
    p3d_cp = 0;
    p3d_buf_t = -9;
    p3d_wph = 0;
    p3d_swing = 0;
    p3d_joy_id = -1;
    p3d_jump_id = -1;
    p3d_drag_id = -1;
    p3d_joy_dx = 0; p3d_joy_dy = 0;
    p3d_atk_x = screen_w - 110;
    p3d_atk_y = screen_h - 150;
    p3d_spawn(0);
}

/* Столкновение бокса игрока (центр x,y,z) с платформой i. */
static int p3d_hit(int i, double x, double y, double z) {
    const P3dPlat *p = &p3d_plat[i];
    if (fabs(x - p->cx) >= p->hx + P3D_HALF) return 0;
    if (fabs(y - p->by) >= p->hy + P3D_HALF) return 0;
    if (fabs(z - p->cz) >= p->hz + P3D_HALF) return 0;
    return 1;
}

static void p3d_move_platforms(void) {
    for (int i = 0; i < P3D_PLATS; i++) {
        P3dPlat *p = &p3d_plat[i];
        double ox = 0, oz = 0, ox0 = 0, oz0 = 0;
        if (p->kind == 1) {
            ox = p->amp * sin(p3d_t * p->spd);
            ox0 = p->amp * sin((p3d_t - dt) * p->spd);
        } else if (p->kind == 2) {
            oz = p->amp * sin(p3d_t * p->spd);
            oz0 = p->amp * sin((p3d_t - dt) * p->spd);
        }
        p->cx = p->bx + ox;
        p->cz = p->bz + oz;
        p->pdx = ox - ox0;
        p->pdz = oz - oz0;
    }
}

/* Выше крыша платформы под игроком (для тени и «пола» для камеры);
 * -1, если под ногами пустота. */
static int p3d_floor_top(int *i_out) {
    int best = -1;
    double best_top = -1e9;
    for (int i = 0; i < P3D_PLATS; i++) {
        double top = p3d_plat[i].by + p3d_plat[i].hy;
        if (fabs(p3d_px - p3d_plat[i].cx) < p3d_plat[i].hx &&
            fabs(p3d_pz - p3d_plat[i].cz) < p3d_plat[i].hz &&
            top < p3d_py - P3D_HALF + 0.05 && top > best_top) {
            best_top = top;
            best = i;
        }
    }
    if (i_out) *i_out = best;
    return best;
}

static void p3d_update(void) {
    p3d_t += dt;
    if (p3d_finished) {
        p3d_finish_t += dt;
        return;
    }
    p3d_time += dt;
    p3d_flash = clamp(p3d_flash - dt * 2.2, 0, 1);
    p3d_move_platforms();

    /* Езда на платформе, на которой стояли в прошлом кадре. */
    if (p3d_ground && p3d_stand >= 0) {
        p3d_px += p3d_plat[p3d_stand].pdx;
        p3d_pz += p3d_plat[p3d_stand].pdz;
    }

    /* Управление: джойстик относительно камеры. fw — вперёд (от игрока по
     * курсу), rt — ИСТИННОЕ право камеры (вектор f x up; при yaw=0 это -X).
     * Тянешь джойстик вправо — персонаж идёт вправо на экране. */
    double fw_x = sin(p3d_cam_yaw), fw_z = cos(p3d_cam_yaw);
    double rt_x = -cos(p3d_cam_yaw), rt_z = sin(p3d_cam_yaw);
    double wish_x = fw_x * (-p3d_joy_dy) + rt_x * p3d_joy_dx;
    double wish_z = fw_z * (-p3d_joy_dy) + rt_z * p3d_joy_dx;
    double k = clamp(14 * dt, 0, 1);
    p3d_vx += (wish_x * P3D_SPEED - p3d_vx) * k;
    p3d_vz += (wish_z * P3D_SPEED - p3d_vz) * k;
    if (fabs(p3d_joy_dx) + fabs(p3d_joy_dy) > 0.25) {
        p3d_angle = p3d_lerp_angle(p3d_angle, atan2(p3d_vx, p3d_vz),
                                   clamp(12 * dt, 0, 1));
    }
    /* Фаза шага и покачивание рук/ног (в воздухе — плавно в нейтраль). */
    double hspeed = sqrt(p3d_vx * p3d_vx + p3d_vz * p3d_vz);
    p3d_wph += hspeed * dt * 1.7;
    double target_swing = p3d_ground
        ? 0.55 * sin(p3d_wph) * clamp(hspeed / P3D_SPEED, 0, 1)
        : 0.0;
    p3d_swing += (target_swing - p3d_swing) * clamp(10 * dt, 0, 1);

    /* Горизонталь: ось X, затем ось Z, с выталкиванием из стен. */
    p3d_px += p3d_vx * dt;
    for (int i = 0; i < P3D_PLATS; i++) {
        if (!p3d_hit(i, p3d_px, p3d_py, p3d_pz)) continue;
        if (p3d_vx > 0) p3d_px = p3d_plat[i].cx - p3d_plat[i].hx - P3D_HALF - 0.001;
        else if (p3d_vx < 0) p3d_px = p3d_plat[i].cx + p3d_plat[i].hx + P3D_HALF + 0.001;
        p3d_vx = 0;
    }
    p3d_pz += p3d_vz * dt;
    for (int i = 0; i < P3D_PLATS; i++) {
        if (!p3d_hit(i, p3d_px, p3d_py, p3d_pz)) continue;
        if (p3d_vz > 0) p3d_pz = p3d_plat[i].cz - p3d_plat[i].hz - P3D_HALF - 0.001;
        else if (p3d_vz < 0) p3d_pz = p3d_plat[i].cz + p3d_plat[i].hz + P3D_HALF + 0.001;
        p3d_vz = 0;
    }

    /* Вертикаль: гравитация с подшагами. Подшаг всегда меньше толщины
     * «зоны контакта» (2*hy + 2*half = 1.4 при div 0.35 и cap 12), поэтому
     * проскочить платформу целиком в один шаг уже не получается. */
    p3d_vy = clamp(p3d_vy - P3D_GRAV * dt, -P3D_MAX_FALL, P3D_MAX_FALL);
    double left = p3d_vy * dt;
    int steps = 1 + (int)(fabs(left) / 0.35);
    if (steps > 12) steps = 12;
    double sy = left / steps;
    int landed = 0;
    for (int s = 0; s < steps; s++) {
        p3d_py += sy;
        for (int j = 0; j < P3D_PLATS; j++) {
            if (!p3d_hit(j, p3d_px, p3d_py, p3d_pz)) continue;
            if (sy <= 0) {
                /* Пришли сверху — кладём на крышку платформы. */
                p3d_py = p3d_plat[j].by + p3d_plat[j].hy + P3D_HALF + 0.001;
                p3d_vy = 0;
                landed = 1;
                p3d_stand = j;
            } else {
                /* Удар головой — прижимаем к низу платформы. */
                p3d_py = p3d_plat[j].by - p3d_plat[j].hy - P3D_HALF - 0.001;
                p3d_vy = 0;
            }
        }
    }
    /* Страхушка против «игрока под полом»: если после всех шагов бокс всё
     * ещё внутри платформы (редкий скачок dt), выталкиваем по оси с
     * минимальным проникновением. */
    for (int j = 0; j < P3D_PLATS; j++) {
        if (!p3d_hit(j, p3d_px, p3d_py, p3d_pz)) continue;
        const P3dPlat *p = &p3d_plat[j];
        double ox = (p->hx + P3D_HALF) - fabs(p3d_px - p->cx);
        double oy = (p->hy + P3D_HALF) - fabs(p3d_py - p->by);
        double oz = (p->hz + P3D_HALF) - fabs(p3d_pz - p->cz);
        if (oy <= ox && oy <= oz) {
            if (p3d_py >= p->by) {
                p3d_py = p->by + p->hy + P3D_HALF + 0.001;
                landed = 1;
                p3d_stand = j;
            } else {
                p3d_py = p->by - p->hy - P3D_HALF - 0.001;
            }
            p3d_vy = 0;
        } else if (ox <= oz) {
            p3d_px = (p3d_px >= p->cx) ? p->cx + p->hx + P3D_HALF + 0.001
                                       : p->cx - p->hx - P3D_HALF - 0.001;
            p3d_vx = 0;
        } else {
            p3d_pz = (p3d_pz >= p->cz) ? p->cz + p->hz + P3D_HALF + 0.001
                                       : p->cz - p->hz - P3D_HALF - 0.001;
            p3d_vz = 0;
        }
    }

    /* Опора: стоим, если под ногами крышка какой-то платформы. */
    if (!landed) {
        p3d_stand = -1;
        for (int s = 0; s < P3D_PLATS; s++) {
            const P3dPlat *p = &p3d_plat[s];
            double sdx = fabs(p3d_px - p->cx);
            double sdz = fabs(p3d_pz - p->cz);
            double gap = p3d_py - P3D_HALF - (p->by + p->hy);
            if (sdx < p->hx + P3D_HALF - 0.06 && sdz < p->hz + P3D_HALF - 0.06 &&
                gap > -0.05 && gap < 0.12) {
                p3d_stand = s;
                break;
            }
        }
    }
    if (p3d_stand >= 0) {
        p3d_ground = 1;
        p3d_coyote = P3D_COYOTE_MAX;
        p3d_jumps = 0;
        int kind = p3d_plat[p3d_stand].kind;
        if (kind == 3 && p3d_cp != p3d_stand) p3d_cp = p3d_stand;
        if (kind == 4) {
            p3d_finished = 1;
            p3d_finish_t = 0;
        }
    } else {
        p3d_ground = 0;
        p3d_coyote -= dt;
    }

    /* Прыжок: буфер нажатия + койот-время + второй прыжок в воздухе. */
    if (p3d_t - p3d_buf_t < P3D_BUF_MAX) {
        if (p3d_ground || p3d_coyote > 0) {
            p3d_jumps = 0;
            p3d_do_jump();
        } else if (p3d_jumps < 2) {
            p3d_do_jump();
        }
    }

    /* Монеты. */
    for (int c = 0; c < P3D_COINS; c++) {
        if (!p3d_coin_on[c]) continue;
        double cdx = fabs(p3d_px - P3D_COIN_POS[c].x);
        double cdy = fabs(p3d_py - (P3D_COIN_POS[c].y + 0.15 * sin(p3d_t * 2.4 + c)));
        double cdz = fabs(p3d_pz - P3D_COIN_POS[c].z);
        if (cdx < 0.8 && cdy < 0.95 && cdz < 0.8) {
            p3d_coin_on[c] = 0;
            p3d_coins++;
        }
    }

    /* Падение в пустоту — возврат на чекпоинт. */
    if (p3d_py < -9) {
        p3d_die();
        return;
    }

    /* Камера от третьего лица: глаз позади и выше/ниже цели по pitch.
     * pitch > 0 — камера внизу и смотрит вверх, pitch < 0 — сверху вниз.
     * Глаз не уходит ниже крыши платформы под игроком: без этого сильного
     * «взгляда вверх» камера проваливалась бы под пол. */
    double cosp = cos(p3d_cam_pitch), sinp = sin(p3d_cam_pitch);
    double ex = p3d_px - fw_x * P3D_CAM_DIST * cosp;
    double ey = p3d_py + P3D_CAM_TGT_H - P3D_CAM_DIST * sinp;
    double ez = p3d_pz - fw_z * P3D_CAM_DIST * cosp;
    int floor_i = -1;
    if (p3d_floor_top(&floor_i) >= 0) {
        double floor_top = p3d_plat[floor_i].by + p3d_plat[floor_i].hy;
        if (ey < floor_top + 0.05) ey = floor_top + 0.05;
    }
    cam3d(ex, ey, ez, p3d_px, p3d_py + P3D_CAM_TGT_H, p3d_pz, P3D_FOV);
}

/* ---------------- отрисовка ---------------- */

/* Небо с градиентом: от глубокого синего сверху к светлому у горизонта.
 * grad_rect — один quad, цвета вершин интерполирует GPU. */
static void p3d_draw_sky(void) {
    grad_rect(0, 0, (float)screen_w, (float)screen_h,
              0xFF2F6FBF, 0xFFDCF1FF);
    /* Солнце. */
    circle((float)(screen_w * 0.82), (float)(screen_h * 0.18), 64, 0xFFFDF3B0);
    circle((float)(screen_w * 0.82), (float)(screen_h * 0.18), 44, 0xFFFFF9DA);
    /* Облака (плывут медленно). */
    for (int cl = 0; cl < 3; cl++) {
        double cx = fmod(p3d_t * 12 + cl * 430, screen_w + 340) - 170;
        double cy = screen_h * (0.12 + 0.09 * cl);
        circle((float)cx, (float)cy, 34, 0xCCFFFFFF);
        circle((float)(cx + 30), (float)(cy + 8), 26, 0xCCFFFFFF);
        circle((float)(cx - 30), (float)(cy + 10), 22, 0xCCFFFFFF);
    }
}

static void p3d_draw_world(void) {
    for (int i = 0; i < P3D_PLATS; i++) {
        const P3dPlat *p = &p3d_plat[i];
        cube3d(p->cx, p->by, p->cz, p->hx * 2, p->hy * 2, p->hz * 2,
               p3d_box_color(p->kind));
        if (p->kind == 3) {
            /* Плита чекпоинта: активный светится голубым. bias держит
             * плоскую плиту поверх соседних плиток крыши. */
            uint32_t mark = (p3d_cp == i) ? 0xFF4FC3F7 : 0xFF244E86;
            cube3d_part(p->cx, p->by + p->hy + 0.03, p->cz,
                        p->hx * 2 * 1.15, 0.05, p->hz * 2 * 1.15,
                        0.0, 0.0, 0.15, mark);
        }
        if (p->kind == 4) {
            /* Флаг на финишной площадке. bias — чтобы полотнище не
             * перекрывали плитки крыши с низкого ракурса. */
            double top = p->by + p->hy;
            line3d(p->cx, top, p->cz - 1.8, p->cx, top + 3.2, p->cz - 1.8,
                   4, 0xFFE8E8E8);
            cube3d_part(p->cx + 0.9, top + 2.8, p->cz - 1.8, 1.4, 0.8, 0.1,
                        0.0, 0.0, 0.55, 0xFFE94560);
        }
    }
    for (int c = 0; c < P3D_COINS; c++) {
        if (!p3d_coin_on[c]) continue;
        /* bias — держит монету поверх плиток пола даже с низкого ракурса,
         * когда игрок (и монета) у дальнего края платформы. */
        cube3d_part(P3D_COIN_POS[c].x,
                    P3D_COIN_POS[c].y + 0.15 * sin(p3d_t * 2.4 + c),
                    P3D_COIN_POS[c].z, 0.55, 0.55, 0.55,
                    p3d_t * 2.6 + c * 0.9, 0.0, 0.55, 0xFFFFD34D);
    }
}

/* ---------------- персонаж: собственный текстовый формат M3D -----------
 * Персонаж — набор частей (скруглённые боксы и шары), описанный в
 * game/assets/models/hero.txt и рисуемый в игре через rbox3d/ball3d.
 * Формат (одна директива на строку, # — комментарий):
 *   meta name <имя>
 *   part <id> <shape> x y z sx sy sz r seg R G B A [pivot px py pz] [sway s]
 *     shape: box — скруглённый бокс (r — радиус рёбер), ball — шар (sx = радиус)
 *     начало модели = центр игрока (0,0,0), низ ног — y −0.45 (пол)
 *     pivot — точка качания в локальных координатах части (0 0 0 = центр)
 *     sway  — радианы на единицу походки (вращение вокруг X)
 * Загрузка: APK → assets; если файла нет (например, в development-сборке) —
 * файловая копия рядом с исходниками; если и её нет — встроенный дефолт. */
#define M3D_MAX_PARTS 24
typedef struct {
    char id[16];
    int shape;              /* 0 — box, 1 — ball */
    float x, y, z;
    float sx, sy, sz, r, seg;
    uint8_t cr, cg, cb, ca;
    float pvx, pvy, pvz, sway;
} M3dPart;

static M3dPart p3d_model[M3D_MAX_PARTS];
static int p3d_model_n = 0;
static int p3d_model_loaded = 0;

static const char P3D_HERO_ASSET[] = "models/hero.txt";
static const char P3D_HERO_FILE[] = "game/assets/models/hero.txt";

/* Встроенная копия персонажа — игра работает даже без файла. */
static const char P3D_HERO_DEFAULT[] =
    "meta name hero\n"
    "part shoes_l box  -0.16 -0.37 0.03 0.26 0.16 0.36 0.06 2 100 106 114 255\n"
    "part shoes_r box   0.16 -0.37 0.03 0.26 0.16 0.36 0.06 2 100 106 114 255\n"
    "part leg_l   box  -0.16 -0.19 0    0.24 0.44 0.26 0.08 3 152 158 166 255 pivot 0 0.22 0 sway 0.55\n"
    "part leg_r   box   0.16 -0.19 0    0.24 0.44 0.26 0.08 3 152 158 166 255 pivot 0 0.22 0 sway -0.55\n"
    "part torso   box   0    0.12 0     0.58 0.46 0.38 0.12 3 180 186 194 255\n"
    "part arm_l   box  -0.40 -0.07 0    0.20 0.46 0.22 0.08 3 152 158 166 255 pivot 0 0.17 0 sway -0.55\n"
    "part arm_r   box   0.40 -0.07 0    0.20 0.46 0.22 0.08 3 152 158 166 255 pivot 0 0.17 0 sway 0.55\n"
    "part head    box   0    0.50 0     0.52 0.46 0.54 0.15 4 205 209 217 255\n"
    "part eye_l   ball -0.12 0.54 0.245 0.075 0 0 0 2 255 255 255 255\n"
    "part eye_r   ball  0.12 0.54 0.245 0.075 0 0 0 2 255 255 255 255\n"
    "part pupil_l ball -0.12 0.54 0.30  0.038 0 0 0 2 40 44 52 255\n"
    "part pupil_r ball  0.12 0.54 0.30  0.038 0 0 0 2 40 44 52 255\n";

static int m3d_clamp8(float v) {
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (int)v;
}

/* Разбор текста модели. Возвращает число частей (0 — модель не годится). */
static int p3d_model_parse(const char *text) {
    p3d_model_n = 0;
    const char *p = text;
    while (*p && p3d_model_n < M3D_MAX_PARTS) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[600];
        if (len > sizeof line - 1) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = 0;
        p = nl ? nl + 1 : p + len;
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' '))
            line[--len] = 0;
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#') continue;
        if (strncmp(s, "meta", 4) == 0) continue;      /* name — не нужно игре */
        if (strncmp(s, "part", 4) != 0 ||
            !isspace((unsigned char)s[4])) continue;
        M3dPart *m = &p3d_model[p3d_model_n];
        memset(m, 0, sizeof *m);
        s += 5;
        while (*s == ' ' || *s == '\t') s++;
        int ti = 0;
        while (*s && !isspace((unsigned char)*s)) {
            if (ti < (int)sizeof m->id - 1) m->id[ti++] = *s;
            s++;
        }
        m->id[ti] = 0;
        if (!*m->id) continue;
        while (*s == ' ' || *s == '\t') s++;
        int shape;
        if (strncmp(s, "box", 3) == 0) shape = 0;
        else if (strncmp(s, "ball", 4) == 0) shape = 1;
        else continue;
        s += (shape == 1) ? 4 : 3;
        /* Обязательные числа: x y z sx sy sz r seg R G B */
        float nums[16];
        int nn = 0;
        for (int i = 0; i < 11; i++) {
            char *e;
            float v = strtof(s, &e);
            if (e == s) goto bad_part;
            nums[nn++] = v;
            s = e;
        }
        m->shape = shape;
        m->x = nums[0]; m->y = nums[1]; m->z = nums[2];
        m->sx = nums[3]; m->sy = nums[4]; m->sz = nums[5];
        m->r = nums[6];
        m->seg = (float)(int)nums[7];
        m->cr = (uint8_t)m3d_clamp8(nums[8]);
        m->cg = (uint8_t)m3d_clamp8(nums[9]);
        m->cb = (uint8_t)m3d_clamp8(nums[10]);
        /* Опционально: A, затем ключевые слова pivot px py pz / sway s. */
        while (*s) {
            while (*s == ' ' || *s == '\t') s++;
            if (!*s) break;
            if (isdigit((unsigned char)*s)) {
                if (nn < 16) {
                    char *e;
                    float v = strtof(s, &e);
                    if (e == s) break;
                    nums[nn++] = v;
                    s = e;
                }
                continue;
            }
            if (strncmp(s, "pivot", 5) == 0) {
                s += 5;
                float a = 0, b = 0, c = 0;
                char *e;
                a = strtof(s, &e); if (e == s) goto bad_part; s = e;
                b = strtof(s, &e); if (e == s) goto bad_part; s = e;
                c = strtof(s, &e); if (e == s) goto bad_part; s = e;
                m->pvx = a; m->pvy = b; m->pvz = c;
                continue;
            }
            if (strncmp(s, "sway", 4) == 0) {
                s += 4;
                char *e;
                float a = strtof(s, &e);
                if (e == s) goto bad_part;
                m->sway = a;
                break;
            }
            break; /* неизвестное ключевое слово — игнорируем остаток */
        }
        if (nn > 11) m->ca = (uint8_t)m3d_clamp8(nums[11]);
        if (!m->ca) m->ca = 255;
        /* Проверка размеров: box с пустыми размерами или ball без радиуса — мимо. */
        if (shape == 0) {
            if (m->sx <= 0 || m->sy <= 0 || m->sz <= 0) goto bad_part;
        } else if (m->sx <= 0) {
            goto bad_part;
        }
        p3d_model_n++;
        continue;
    bad_part:
        continue;
    }
    return p3d_model_n;
}

static char *p3d_read_file(const char *path, size_t maxsz) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = NULL;
    if (l > 0 && (uint64_t)l < maxsz) {
        buf = (char *)malloc((size_t)l + 1);
        if (buf && fread(buf, 1, (size_t)l, f) == (size_t)l) {
            buf[l] = 0;
        } else {
            free(buf);
            buf = NULL;
        }
    }
    fclose(f);
    return buf;
}

static void p3d_model_load(void) {
    if (p3d_model_loaded) return;
    p3d_model_loaded = 1;
    char *text = NULL;
#ifdef __ANDROID__
    AAssetManager *am = ds_get_asset_manager();
    if (am) {
        AAsset *a = AAssetManager_open(am, P3D_HERO_ASSET, AASSET_MODE_BUFFER);
        if (a) {
            off_t l = AAsset_getLength(a);
            if (l > 0 && (uint64_t)l < 65536) {
                char *buf = (char *)malloc((size_t)l + 1);
                if (buf) {
                    off_t off = 0;
                    while (off < l) {
                        int nr = AAsset_read(a, buf + off, (size_t)(l - off));
                        if (nr <= 0) break;
                        off += nr;
                    }
                    if (off == l) { buf[off] = 0; text = buf; }
                    else { free(buf); }
                }
            }
            AAsset_close(a);
        }
    }
#endif
    if (!text) text = p3d_read_file(P3D_HERO_FILE, 65536);
    if (text) {
        if (p3d_model_parse(text) > 0) { free(text); return; }
        free(text);
        text = NULL;
        ds_log_err("model: '%s' is invalid, using built-in character", P3D_HERO_ASSET);
    }
    if (p3d_model_parse(P3D_HERO_DEFAULT) > 0) return;
    ds_log_err("model: built-in character is broken, player will be invisible");
}

int p3d_model_find(const char *id) {
    for (int i = 0; i < p3d_model_n; i++)
        if (strcmp(p3d_model[i].id, id) == 0) return i;
    return -1;
}

/* Мир-координаты центра части (с учётом покачивания и поворота персонажа)
 * — для пиксельных проверок в тестах. */
static void p3d_model_part_world(int i, double *wx, double *wy, double *wz) {
    M3dPart *m = &p3d_model[i];
    double th = m->sway * p3d_swing;
    double c = cos(th), s = sin(th);
    /* Центр части после поворота вокруг pivot: pv + R_x(th)·(−pv). */
    double ox = m->x + m->pvx;
    double oy = m->y + m->pvy * (1 - c) + m->pvz * s;
    double oz = m->z + m->pvz * (1 - c) - m->pvy * s;
    double ca = cos(p3d_angle), sa = sin(p3d_angle);
    *wx = p3d_px + ca * ox + sa * oz;
    *wy = p3d_py + oy;
    *wz = p3d_pz - sa * ox + ca * oz;
}

static void p3d_draw_player(void) {
    double s = p3d_swing;
    double ca = cos(p3d_angle), sa = sin(p3d_angle);
    for (int i = 0; i < p3d_model_n; i++) {
        M3dPart *m = &p3d_model[i];
        double wx = p3d_px + ca * m->x + sa * m->z;
        double wz = p3d_pz - sa * m->x + ca * m->z;
        double wy = p3d_py + m->y;
        double pitch = m->sway * s;
        /* bias 0.55 — детали держатся поверх плиток пола (см. render3d.inc). */
        uint32_t col = ((uint32_t)m->ca << 24) | ((uint32_t)m->cr << 16)
                     | ((uint32_t)m->cg << 8) | (uint32_t)m->cb;
        if (m->shape == 1)
            ball3d(wx, wy, wz, m->sx, (int)m->seg, p3d_angle, pitch,
                   m->pvx, m->pvy, m->pvz, 0.55, col);
        else
            rbox3d(wx, wy, wz, m->sx, m->sy, m->sz, m->r, (int)m->seg,
                   p3d_angle, pitch, m->pvx, m->pvy, m->pvz, 0.55, col);
    }
}

static void p3d_draw_hud(void) {
    /* Таймер. */
    double mm = floor(p3d_time / 60);
    double ss = floor(p3d_time - mm * 60);
    char timer[16];
    snprintf(timer, sizeof timer, "%d:%02d", (int)mm, (int)ss);
    ui_ctext_scaled(timer, 92, UI_TEXT, 0.8);
    /* Монеты. */
    circle(60, 118, 15, 0xFFFFD34D);
    char coins[24];
    snprintf(coins, sizeof coins, "%d/%d", p3d_coins, P3D_COINS);
    text_scaled(coins, 86, 106, UI_TEXT, 0.62f);
    /* Падения. */
    char falls[32];
    snprintf(falls, sizeof falls, "Падений: %d", p3d_deaths);
    text_scaled(falls, (float)(screen_w - 60 - text_ink_width(falls) * 0.55), 34,
                UI_TEXT, 0.55f);
    /* Назад. */
    ui_button((screen_w - UI_BTN_W) / 2, UI_BACK_Y, "Назад", 1.0);
    /* Джойстик: пока активен — в точке хвата, иначе подсказка внизу слева. */
    double jx = (p3d_joy_id >= 0) ? p3d_joy_ox : 130;
    double jy = (p3d_joy_id >= 0) ? p3d_joy_oy : screen_h - 150;
    double jk = (p3d_joy_id >= 0) ? 0.55 : 0.20;
    ring((float)jx, (float)jy, (float)JOY_R, 4,
         (uint32_t)(jk * 255) * 0x1000000u);
    circle((float)(jx + p3d_joy_dx * JOY_R), (float)(jy + p3d_joy_dy * JOY_R), 34,
           (uint32_t)(jk * 255) * 0x1000000u);
    /* Кнопка прыжка. */
    circle((float)p3d_atk_x, (float)p3d_atk_y, 70, 0x4FC3F7);
    ring((float)p3d_atk_x, (float)p3d_atk_y, 70, 4, 0x000000);
    text_scaled("Прыжок", (float)(p3d_atk_x - 70 + (140 - text_ink_width("Прыжок") * 0.42) / 2),
                (float)(p3d_atk_y - 70 + (140 - text_ink_height("Прыжок") * 0.42) / 2
                        - text_ink_top("Прыжок") * 0.42 + 1.5),
                0xFFFFFFFF, 0.42f);
    /* Вспышка при падении. */
    if (p3d_flash > 0) {
        rect(0, 0, (float)screen_w, (float)screen_h,
             (uint32_t)floor(p3d_flash * 110) * 0x1000000u + 0xFF0000);
    }
    /* Экран победы. */
    if (p3d_finished) {
        double a = clamp(p3d_finish_t * 2, 0, 0.72);
        rect(0, 0, (float)screen_w, (float)screen_h,
             (uint32_t)floor(a * 255) * 0x1000000u);
        ui_ctext_scaled("Уровень пройден!", screen_h * 0.3, 0xFF7ED321, 1.2);
        char s1[32], s2[32], s3[32];
        snprintf(s1, sizeof s1, "Монеты: %d/%d", p3d_coins, P3D_COINS);
        snprintf(s2, sizeof s2, "Время: %s", timer);
        snprintf(s3, sizeof s3, "Падений: %d", p3d_deaths);
        ui_ctext_scaled(s1, screen_h * 0.3 + 90, UI_TEXT, 0.7);
        ui_ctext_scaled(s2, screen_h * 0.3 + 130, UI_TEXT, 0.7);
        ui_ctext_scaled(s3, screen_h * 0.3 + 170, UI_TEXT, 0.7);
        if (p3d_finish_t > 0.9) {
            ui_ctext_scaled("Нажмите, чтобы продолжить", screen_h * 0.3 + 240,
                            UI_TEXT, 0.55);
        }
    }
}

static void p3d_draw(void) {
    p3d_draw_sky();
    p3d_draw_world();
    p3d_draw_player();
    flush3d();
    p3d_draw_hud();
}

/* ---------------- лобби ---------------- */

static void draw_lobby(void) {
    rect(0, 0, (float)screen_w, (float)screen_h, UI_BG);
    ui_ctext_scaled("Cubic Battle 4", 90, UI_TEXT, 1.0);
    ui_ctext_scaled("3D-платформер", 134, UI_DIM, 0.6);
    ui_button((screen_w - UI_BTN_W) / 2, lobby_button_y(), "3D платформер", 1.0);
}

static void lobby_touch(double x, double y, double action) {
    if (action != 0) return;
    if (ui_button_hit(x, y, (screen_w - UI_BTN_W) / 2, lobby_button_y())) {
        p3d_start();
        start_transition(ST_GAME);
    }
}

/* ---------------- ввод ---------------- */

static void p3d_touch(double x, double y, double action, double pid) {
    if (p3d_finished) {
        if (action == 0 && p3d_finish_t > 0.9) start_transition(ST_LOBBY);
        return;
    }
    if (action == 3) { /* отменённое касание */
        p3d_joy_id = -1;
        p3d_drag_id = -1;
        p3d_jump_id = -1;
        p3d_joy_dx = 0; p3d_joy_dy = 0;
        return;
    }
    if (ui_button_hit(x, y, (screen_w - UI_BTN_W) / 2, UI_BACK_Y) && action == 0) {
        start_transition(ST_LOBBY);
        return;
    }
    if (action == 0) {
        /* Кнопка прыжка: нажатие ставит буфер (срабатывает в p3d_update). */
        double ddx = x - p3d_atk_x, ddy = y - p3d_atk_y;
        if (sqrt(ddx * ddx + ddy * ddy) <= 96) {
            p3d_jump_id = (int)pid;
            p3d_buf_t = p3d_t;
            return;
        }
        /* Правая половина (вне кнопок) — поворот камеры перетаскиванием. */
        if (p3d_drag_id == -1 && x > screen_w * 0.45) {
            p3d_drag_id = (int)pid;
            p3d_drag_x = x;
            p3d_drag_y = y;
            return;
        }
        /* Левая половина — джойстик. */
        if (p3d_joy_id == -1) {
            p3d_joy_id = (int)pid;
            p3d_joy_ox = x;
            p3d_joy_oy = y;
            p3d_joy_dx = 0;
            p3d_joy_dy = 0;
            return;
        }
    }
    if (action == 2) {
        if (pid == p3d_drag_id) {
            /* Взгляд следует за пальцем: тянешь влево — смотришь влево
             * (yaw растёт), тянешь вниз — камера наклоняется вниз (pitch
             * убывает). Ограничения не дают смотреть прямо вверх/вниз. */
            p3d_cam_yaw -= (x - p3d_drag_x) * 0.008;
            p3d_cam_pitch = clamp(p3d_cam_pitch - (y - p3d_drag_y) * 0.006,
                                  P3D_PITCH_MIN, P3D_PITCH_MAX);
            p3d_drag_x = x;
            p3d_drag_y = y;
            return;
        }
        if (pid == p3d_joy_id) {
            double dx = x - p3d_joy_ox, dy = y - p3d_joy_oy;
            double len = sqrt(dx * dx + dy * dy);
            if (len > JOY_R) {
                dx *= JOY_R / len;
                dy *= JOY_R / len;
            }
            p3d_joy_dx = dx / JOY_R;
            p3d_joy_dy = dy / JOY_R;
        }
    }
    if (action == 1) {
        if (pid == p3d_jump_id) p3d_jump_id = -1;
        if (pid == p3d_drag_id) p3d_drag_id = -1;
        if (pid == p3d_joy_id) {
            p3d_joy_id = -1;
            p3d_joy_dx = 0;
            p3d_joy_dy = 0;
        }
    }
}

/* ---------------- точки входа для main.c ---------------- */

void init(AAssetManager *assets) {
    ds_set_asset_manager(assets);
    p3d_model_load();
    reset();
}

void reset(void) {
    p3d_init_world();
    game_state = ST_LOBBY;
    t_dir = 0;
    t_target = ST_LOBBY;
    t_fade = 0;
    p3d_joy_id = -1;
    p3d_drag_id = -1;
    p3d_jump_id = -1;
    p3d_joy_dx = 0; p3d_joy_dy = 0;
}

void update(void) {
    update_transition();
    if (game_state == ST_GAME) p3d_update();
}

void draw(Buffer *buffer) {
    (void)buffer;
    if (t_fade >= 1.0) {
        rect(0, 0, (float)screen_w, (float)screen_h, 0xFF000000);
        return;
    }
    if (game_state == ST_GAME) p3d_draw();
    else draw_lobby();
    if (t_fade > 0) {
        rect(0, 0, (float)screen_w, (float)screen_h,
             (1 + (uint32_t)floor(t_fade * 254)) * 0x1000000u);
    }
}

void touch(float x, float y, int action, int pointer_id) {
    if (game_state == ST_GAME) {
        p3d_touch((double)x, (double)y, (double)action, (double)pointer_id);
    } else {
        lobby_touch((double)x, (double)y, (double)action);
    }
}

int back_pressed(void) {
    if (game_state != ST_LOBBY) {
        start_transition(ST_LOBBY);
        return 1;
    }
    return 0;
}
