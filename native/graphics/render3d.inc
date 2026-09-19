/* 3D-слой DimScript: перспективная камера и кубы поверх 2D-рендера.
 *
 * Как это устроено. Отдельного 3D-конвейера Vulkan нет: каждый вызов
 * cube3d/line3d проецирует свои точки на CPU (перспективная камера cam3d),
 * готовые 2D-многоугольники складываются в очередь граней, а при первой
 * 2D-команде после 3D-вызовов (или в конце кадра) очередь сортируется
 * от дальних граней к ближним (алгоритм художника) и выдаётся в командный
 * список как треугольники DS_CMD_TRI / линии DS_CMD_LINE. Дальше работает
 * обычный геометрический конвейер: тот же сплошной шейдер с альфа-смешиванием,
 * что у rect. Такой слой без depth-буфера честно рисует платформер и любые
 * сцены из непересекающихся выпуклых тел; грань, пересекающая ближнюю
 * плоскость, обрезается (Sutherland–Hodgman), целиком за глазом — пропускается.
 *
 * Мировые оси: Y — вверх, X и Z — плоскость земли. Камера задаётся на кадр:
 *   cam3d(глаз_x, глаз_y, глаз_z, цель_x, цель_y, цель_z, fov_в_градусах)
 * Куб задаётся центром и полным размером:
 *   cube3d(x, y, z, sx, sy, sz, цвет)                    — оси мира
 *   cube3d_yaw(x, y, z, sx, sy, sz, поворот, цвет)       — поворот вокруг Y
 * Линия в мире (толщина в экранных пикселях):
 *   line3d(x1, y1, z1, x2, y2, z2, толщина, цвет)
 * Принудительно нарисовать очередь 3D (не дожидаясь 2D-команды):
 *   flush3d()
 *
 * Грани куба освещаются фиксированным направленным светом: верх ярче, стенки
 * и дно темнее — по скалярному произведению нормали на направление света.
 * Задние грани (отвернувшиеся от камеры) не строятся вовсе. */
#include <math.h>

#ifndef DS3D_NEAR
#define DS3D_NEAR 0.08f
#endif
/* Жёсткий потолок очереди граней: переполнение не ломает кадр, лишние грани
 * просто не рисуются (сцена платформера использует десятки, не тысячи). */
#define DS3D_FACE_MAX 20000
/* Точек в грани после обрезки по ближней плоскости: четырёхугольник даёт
 * максимум 5, запас — на будущие примитивы. */
#define DS3D_FACE_PTS 9

typedef struct {
    float ex, ey, ez;        /* положение глаза */
    float rx, ry, rz;        /* базис: вправо, вверх, вперёд */
    float ux, uy, uz;
    float fx, fy, fz;
    float fl;                /* фокусное расстояние в пикселях */
    int ready;               /* базис посчитан */
} DS3DCam;

static DS3DCam ds3d_cam;
static int ds3d_cam_dirty = 1;

typedef struct {
    float x[DS3D_FACE_PTS], y[DS3D_FACE_PTS];
    int n;                   /* число точек (3..DS3D_FACE_PTS) */
    float depth;             /* средняя глубина вдоль взгляда: больше — дальше */
    float thick;             /* >0 — это 2D-линия толщиной thick пикселей */
    uint32_t c;
} DS3DFace;

static DS3DFace *ds3d_faces;
static int ds3d_fn, ds3d_fcap;
static int ds3d_flushing;    /* защита от рекурсии: flush сам зовёт push() */
static int ds3d_overflow_warned;

/* Соседние модули (lifecycle.inc) сбрасывают/освобождают очередь 3D. */
static void ds3d_flush_pending(void);
static void ds3d_discard(void);
static void ds3d_release(void);

static float ds3d_dot3(float ax, float ay, float az,
                       float bx, float by, float bz) {
    return ax * bx + ay * by + az * bz;
}

static void ds3d_cross3(float ax, float ay, float az,
                        float bx, float by, float bz,
                        float *x, float *y, float *z) {
    *x = ay * bz - az * by;
    *y = az * bx - ax * bz;
    *z = ax * by - ay * bx;
}

/* Пересчёт базиса камеры. Двойная точность на входе (из скриптов), float
 * внутри — точности хватает с запасом для экранных координат. */
static void ds3d_update_cam(double ex, double ey, double ez,
                            double tx, double ty, double tz, double fov_deg) {
    double fx = tx - ex, fy = ty - ey, fz = tz - ez;
    double flen = sqrt(fx * fx + fy * fy + fz * fz);
    if (!(flen > 1e-9)) { fx = 0; fy = 0; fz = 1; flen = 1; }
    fx /= flen; fy /= flen; fz /= flen;
    /* Вертикаль: (0,1,0), но взгляд строго вверх/вниз вырождает базис —
     * тогда вертикалью становится (0,0,1), камера «катится» на бок, как в
     * Aviation-съёмке, и сцена остаётся видимой. */
    double wx = 0, wy = 1, wz = 0;
    if (fabs(fy) > 0.999) { wx = 0; wy = 0; wz = 1; }
    double rxx = fy * wz - fz * wy, ryy = fz * wx - fx * wz, rzz = fx * wy - fy * wx;
    double rlen = sqrt(rxx * rxx + ryy * ryy + rzz * rzz);
    if (!(rlen > 1e-9)) { rxx = 1; ryy = 0; rzz = 0; rlen = 1; }
    rxx /= rlen; ryy /= rlen; rzz /= rlen;
    /* up = right × forward — ортонормированный базис без накопления ошибки. */
    double ux = ryy * fz - rzz * fy, uy = rzz * fx - rxx * fz, uz = rxx * fy - ryy * fx;
    ds3d_cam.ex = (float)ex; ds3d_cam.ey = (float)ey; ds3d_cam.ez = (float)ez;
    ds3d_cam.fx = (float)fx; ds3d_cam.fy = (float)fy; ds3d_cam.fz = (float)fz;
    ds3d_cam.rx = (float)rxx; ds3d_cam.ry = (float)ryy; ds3d_cam.rz = (float)rzz;
    ds3d_cam.ux = (float)ux; ds3d_cam.uy = (float)uy; ds3d_cam.uz = (float)uz;
    double fov = fov_deg;
    if (!(fov >= 10.0) ) fov = 10.0;
    if (fov > 150.0) fov = 150.0;
    double sh = (double)screen_h;
    if (sh < 1.0) sh = 720.0;
    ds3d_cam.fl = (float)(0.5 * sh / tan(fov * (M_PI / 180.0) * 0.5));
    ds3d_cam.ready = 1;
    ds3d_cam_dirty = 0;
}

static void ds3d_ensure_cam(void) {
    if (!ds3d_cam_dirty && ds3d_cam.ready) return;
    /* Камера ещё не задана скриптом в этом кадре: мягкий дефолт — глаз чуть
     * выше и перед центром мира, смотрит в начало координат. */
    ds3d_update_cam(0, 2.4, -9.0, 0, 0, 0, 60.0);
}

/* Мир -> экран. Возвращает глубину (вдоль взгляда) или -1, если точка за
 * ближней плоскостью (для точек это брак, для полигонов — сигнал резать). */
static float ds3d_depth(double wx, double wy, double wz) {
    float dx = (float)wx - ds3d_cam.ex;
    float dy = (float)wy - ds3d_cam.ey;
    float dz = (float)wz - ds3d_cam.ez;
    return ds3d_dot3(dx, dy, dz, ds3d_cam.fx, ds3d_cam.fy, ds3d_cam.fz);
}

static void ds3d_view(double wx, double wy, double wz, float *vx, float *vy, float *vz) {
    float dx = (float)wx - ds3d_cam.ex;
    float dy = (float)wy - ds3d_cam.ey;
    float dz = (float)wz - ds3d_cam.ez;
    *vx = ds3d_dot3(dx, dy, dz, ds3d_cam.rx, ds3d_cam.ry, ds3d_cam.rz);
    *vy = ds3d_dot3(dx, dy, dz, ds3d_cam.ux, ds3d_cam.uy, ds3d_cam.uz);
    *vz = ds3d_dot3(dx, dy, dz, ds3d_cam.fx, ds3d_cam.fy, ds3d_cam.fz);
}

static void ds3d_screen_xy(float vx, float vy, float vz, float *sx, float *sy) {
    float cx = (float)screen_w * 0.5f;
    float cy = (float)screen_h * 0.5f;
    if (cx < 1.0f) cx = 640.0f;
    if (cy < 1.0f) cy = 360.0f;
    float k = ds3d_cam.fl / vz;
    *sx = cx + vx * k;
    *sy = cy - vy * k;   /* экранная ось Y смотрит вниз */
}

/* Очередь граней. */
static int ds3d_push_face(const float *sx, const float *sy, int n,
                          float depth, uint32_t c, float thick) {
    if (n < 2 || !isfinite(depth)) return 1;   /* n==2 — это линия */
    if (ds3d_fn >= DS3D_FACE_MAX) {
        if (!ds3d_overflow_warned) {
            ds3d_overflow_warned = 1;
            ds_log_err("3d: face queue overflow, extra faces dropped");
        }
        return 1;
    }
    if (ds3d_fn == ds3d_fcap) {
        int cap = ds3d_fcap ? ds3d_fcap * 2 : 1024;
        if (cap > DS3D_FACE_MAX) cap = DS3D_FACE_MAX;
        DS3DFace *nf = (DS3DFace *)realloc(ds3d_faces, (size_t)cap * sizeof(*nf));
        if (!nf) { ds_log_err("3d: out of memory for face queue"); return 0; }
        ds3d_faces = nf; ds3d_fcap = cap;
    }
    DS3DFace *f = &ds3d_faces[ds3d_fn++];
    for (int i = 0; i < n; i++) { f->x[i] = sx[i]; f->y[i] = sy[i]; }
    f->n = n; f->depth = depth; f->thick = thick; f->c = c;
    return 1;
}

/* Обрезка полигона в пространстве взгляда по плоскости z >= NEAR
 * (Sutherland–Hodgman). in/out — массивы длиной DS3D_FACE_PTS. */
static int ds3d_clip_near(const float *vx, const float *vy, const float *vz, int n,
                          float *ox, float *oy, float *oz) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float ax = vx[i], ay = vy[i], az = vz[i];
        float bx = vx[j], by = vy[j], bz = vz[j];
        float adiff = az - DS3D_NEAR, bdiff = bz - DS3D_NEAR;
        if (adiff >= 0) {
            if (m < DS3D_FACE_PTS) { ox[m] = ax; oy[m] = ay; oz[m] = az; m++; }
        }
        if ((adiff >= 0) != (bdiff >= 0)) {
            float t = adiff / (adiff - bdiff);
            if (m < DS3D_FACE_PTS) {
                ox[m] = ax + (bx - ax) * t;
                oy[m] = ay + (by - ay) * t;
                oz[m] = az + (bz - az) * t;
                m++;
            }
        }
    }
    return m;
}

static uint32_t ds3d_shade(uint32_t c, float k) {
    uint32_t a = (c >> 24) & 0xff, r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    r = (uint32_t)((float)r * k); if (r > 255) r = 255;
    g = (uint32_t)((float)g * k); if (g > 255) g = 255;
    b = (uint32_t)((float)b * k); if (b > 255) b = 255;
    if (!a) a = 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Проекция готового многоугольника мира на экран и постановка в очередь.
 * nx,ny,nz — внешняя нормаль (для отбора задних граней и света). */
static void ds3d_emit_poly(const double *wx, const double *wy, const double *wz, int n,
                           float nx, float ny, float nz, uint32_t color, float thick) {
    /* Задняя грань: смотрит от камеры — не строится. Мягкий допуск, чтобы
     * рёбра куба не мигали на грани «ребро к глазу». */
    float cx = 0, cy = 0, cz = 0;
    for (int i = 0; i < n; i++) { cx += (float)wx[i]; cy += (float)wy[i]; cz += (float)wz[i]; }
    cx /= (float)n; cy /= (float)n; cz /= (float)n;
    float dx = cx - ds3d_cam.ex, dy = cy - ds3d_cam.ey, dz = cz - ds3d_cam.ez;
    if (ds3d_dot3(nx, ny, nz, dx, dy, dz) > -1e-4f) return;

    float vx[DS3D_FACE_PTS], vy[DS3D_FACE_PTS], vz[DS3D_FACE_PTS];
    for (int i = 0; i < n; i++) ds3d_view(wx[i], wy[i], wz[i], &vx[i], &vy[i], &vz[i]);
    float ox[DS3D_FACE_PTS], oy[DS3D_FACE_PTS], oz[DS3D_FACE_PTS];
    int m = ds3d_clip_near(vx, vy, vz, n, ox, oy, oz);
    if (m < 3) return;
    float sx[DS3D_FACE_PTS], sy[DS3D_FACE_PTS], depth = 0;
    for (int i = 0; i < m; i++) {
        ds3d_screen_xy(ox[i], oy[i], oz[i], &sx[i], &sy[i]);
        depth += oz[i];
    }
    depth /= (float)m;
    /* Направленный свет: чем ближе нормаль к свету, тем ярче; тень не гасит
     * цвет в ноль — полдень без чёрных стенок. Затемнение — ещё в формате
     * скриптов 0xRRGGBBAA, pack_c делает уже байтовый порядок кадра. */
    float k = 0.52f + 0.48f * ds3d_dot3(nx, ny, nz, -0.45f, 0.85f, 0.30f);
    if (k < 0) k = 0;
    ds3d_push_face(sx, sy, m, depth, pack_c(ds3d_shade(color, k)), thick);
}

/* Проекция отрезка: обе точки режутся по ближней плоскости параметрически. */
static void ds3d_emit_line(double x1, double y1, double z1,
                           double x2, double y2, double z2,
                           double thick, uint32_t color) {
    float v1x, v1y, v1z, v2x, v2y, v2z;
    ds3d_view(x1, y1, z1, &v1x, &v1y, &v1z);
    ds3d_view(x2, y2, z2, &v2x, &v2y, &v2z);
    float dz = v2z - v1z;
    float t0 = 0, t1 = 1;
    if (v1z < DS3D_NEAR && v2z < DS3D_NEAR) return;      /* целиком за глазом */
    if ((v1z < DS3D_NEAR) != (v2z < DS3D_NEAR)) {
        float t = (DS3D_NEAR - v1z) / dz;
        if (v1z < DS3D_NEAR) t0 = t; else t1 = t;
    }
    if (t0 >= t1) return;
    float kz1 = v1z + dz * t0, kz2 = v1z + dz * t1;
    float sx1, sy1, sx2, sy2;
    ds3d_screen_xy(v1x + (v2x - v1x) * t0, v1y + (v2y - v1y) * t0, kz1, &sx1, &sy1);
    ds3d_screen_xy(v1x + (v2x - v1x) * t1, v1y + (v2y - v1y) * t1, kz2, &sx2, &sy2);
    float sx[2] = { sx1, sx2 }, sy[2] = { sy1, sy2 };
    float depth = (kz1 + kz2) * 0.5f;
    float th = (float)thick;
    if (!(th > 0)) th = 1.0f;
    ds3d_push_face(sx, sy, 2, depth, pack_c(color), th);
}

void cam3d(double ex, double ey, double ez, double tx, double ty, double tz, double fov) {
    if (!isfinite(ex + ey + ez + tx + ty + tz + fov)) {
        ds_log_err("cam3d: non-finite camera arguments ignored");
        return;
    }
    ds3d_update_cam(ex, ey, ez, tx, ty, tz, fov);
}

/* Куб: (x,y,z) — центр, (sx,sy,sz) — полные размеры, yaw — поворот вокруг
 * вертикальной оси в радианах. Восемь вершин считаются явно, шесть граней
 * перечислены индексами — так не перепутать обход и нормали. */
void cube3d_yaw(double x, double y, double z,
                double sx, double sy, double sz, double yaw, uint32_t color) {
    if (!frame_open) return;
    if (!isfinite(x + y + z + sx + sy + sz + yaw)) return;
    if (sx <= 0 || sy <= 0 || sz <= 0) return;
    ds3d_ensure_cam();
    float hx = (float)(sx * 0.5), hy = (float)(sy * 0.5), hz = (float)(sz * 0.5);
    float ca = (float)cos(yaw), sa = (float)sin(yaw);
    double cx[8], cy[8], cz[8];
    static const float lx[8] = { -1, 1, -1, 1, -1, 1, -1, 1 };
    static const float ly[8] = { -1, -1, 1, 1, -1, -1, 1, 1 };
    static const float lz[8] = { -1, -1, -1, -1, 1, 1, 1, 1 };
    for (int i = 0; i < 8; i++) {
        float ax = lx[i] * hx, ay = ly[i] * hy, az = lz[i] * hz;
        cx[i] = x + (double)(ca * ax + sa * az);
        cy[i] = y + (double)ay;
        cz[i] = z + (double)(-sa * ax + ca * az);
    }
    /* Вершина = (X?1:0)*2 + (Y?1:0)*4 + (Z?1:0) по знаку локальной координаты:
     * локальные (-1,-1,-1)=0, (1,-1,-1)=1, (-1,1,-1)=2, (1,1,-1)=3,
     * (-1,-1,1)=4, (1,-1,1)=5, (-1,1,1)=6, (1,1,1)=7. */
    static const int top[4] = { 2, 3, 7, 6 }, bot[4] = { 0, 4, 5, 1 };
    static const int px_[4] = { 1, 5, 7, 3 }, nx_[4] = { 0, 2, 6, 4 };
    static const int pz_[4] = { 4, 6, 7, 5 }, nz_[4] = { 0, 1, 3, 2 };
    const int *faces[6] = { top, bot, px_, nx_, pz_, nz_ };
    static const float normals[6][3] = {
        { 0, 1, 0 }, { 0, -1, 0 },
        { 1, 0, 0 }, { -1, 0, 0 },
        { 0, 0, 1 }, { 0, 0, -1 },
    };
    for (int f = 0; f < 6; f++) {
        const int *idx = faces[f];
        double wx[4], wy[4], wz[4];
        for (int i = 0; i < 4; i++) {
            wx[i] = cx[idx[i]]; wy[i] = cy[idx[i]]; wz[i] = cz[idx[i]];
        }
        /* Нормаль поворачивается yaw'ом вместе с кубом. */
        float nxl = normals[f][0], nyl = normals[f][1], nzl = normals[f][2];
        float fnx = ca * nxl + sa * nzl;
        float fnz = -sa * nxl + ca * nzl;
        ds3d_emit_poly(wx, wy, wz, 4, fnx, nyl, fnz, color, 0);
    }
}

void cube3d(double x, double y, double z, double sx, double sy, double sz, uint32_t color) {
    cube3d_yaw(x, y, z, sx, sy, sz, 0.0, color);
}

void line3d(double x1, double y1, double z1, double x2, double y2, double z2,
            double thickness, uint32_t color) {
    if (!frame_open) return;
    if (!isfinite(x1 + y1 + z1 + x2 + y2 + z2 + thickness)) return;
    ds3d_ensure_cam();
    ds3d_emit_line(x1, y1, z1, x2, y2, z2, thickness, color);
}

void flush3d(void) {
    if (!frame_open) return;
    ds3d_flush_pending();
}

static int ds3d_face_cmp(const void *pa, const void *pb) {
    const DS3DFace *a = (const DS3DFace *)pa, *b = (const DS3DFace *)pb;
    /* Дальние раньше ближних (алгоритм художника). */
    if (a->depth < b->depth) return 1;
    if (a->depth > b->depth) return -1;
    return 0;
}

static void ds3d_flush_pending(void) {
    if (!frame_open || ds3d_flushing || ds3d_fn == 0) return;
    ds3d_flushing = 1;
    qsort(ds3d_faces, (size_t)ds3d_fn, sizeof(DS3DFace), ds3d_face_cmp);
    for (int i = 0; i < ds3d_fn; i++) {
        DS3DFace *f = &ds3d_faces[i];
        if (f->n == 2) {
            /* Линия: рисуется своей 2D-командой, толщина в пикселях экрана. */
            DSCmd *p = push(DS_CMD_LINE);
            if (p) {
                p->v.ln.x1 = f->x[0]; p->v.ln.y1 = f->y[0];
                p->v.ln.x2 = f->x[1]; p->v.ln.y2 = f->y[1];
                p->v.ln.th = f->thick; p->v.ln.c = f->c;
            }
            continue;
        }
        /* Триангуляция веером: общие вершины соседних треугольников бит в бит
         * совпадают, поэтому швы между ними GPU не закрашивает дважды. */
        for (int k = 1; k + 1 < f->n; k++) {
            DSCmd *p = push(DS_CMD_TRI);
            if (!p) break;
            p->v.tri.x0 = f->x[0];      p->v.tri.y0 = f->y[0];
            p->v.tri.x1 = f->x[k];      p->v.tri.y1 = f->y[k];
            p->v.tri.x2 = f->x[k + 1];  p->v.tri.y2 = f->y[k + 1];
            p->v.tri.c = f->c;
        }
    }
    ds3d_fn = 0;
    ds3d_flushing = 0;
}

static void ds3d_discard(void) { ds3d_fn = 0; }

static void ds3d_release(void) {
    free(ds3d_faces); ds3d_faces = NULL; ds3d_fcap = ds3d_fn = 0;
    ds3d_cam_dirty = 1; ds3d_cam.ready = 0; ds3d_overflow_warned = 0;
}
