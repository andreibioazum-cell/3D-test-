/* Разбиение примитивов DrawScript на треугольники для GPU.
 *
 * Эквивалентно софтрендеру, который здесь был раньше: прямоугольник —
 * выровненный quad, круг — веер с адаптивным числом сегментов, кольцо —
 * лента между двумя окружностями, линия — quad с круглыми заглушками на
 * концах (как и раньше: покрытие = «расстояние до отрезка <= толщина/2»),
 * скруглённый прямоугольник — центр + боковые полосы + четверти-веера,
 * повёрнутый прямоугольник (rect_rot) — quad вокруг центра фигуры, как у
 * текстуры, но без UV: им рисуются хитбоксы, которым нужны острые углы и
 * настоящее альфа-смешивание,
 * текстура — повёрнутый quad с UV (тот же поворот, что был в попиксельном
 * цикле), текст — по кваду на глиф из шрифтового атласа.
 *
 * Тут нет ничего вулкановского: модуль проверяется юнит-тестом на хосте
 * (tools/host_test), сравнивающим растр треугольников со старым
 * программным растеризатором. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { float x, y, u, v; uint32_t c; } GeoVert;

static GeoVert *geo_verts;
static size_t geo_vn, geo_vcap;
static uint16_t *geo_idx;
static size_t geo_in, geo_icap;

static int geo_push_vert(float x, float y, float u, float v, uint32_t c) {
    /* Индексы uint16 адресуют максимум 65536 вершин: переполнение давало бы
     * чтение мусорных вершин на GPU (вплоть до device lost) - лишнее
     * молча не добавляем. */
    if (geo_vn >= 65535) return 0;
    if (geo_vn == geo_vcap) {
        size_t cap = geo_vcap ? geo_vcap * 2 : 1024;
        if (cap > SIZE_MAX / sizeof(*geo_verts)) return 0;
        GeoVert *nv = (GeoVert *)realloc(geo_verts, cap * sizeof(*nv));
        if (!nv) return 0;
        geo_verts = nv; geo_vcap = cap;
    }
    GeoVert *g = &geo_verts[geo_vn++];
    g->x = x; g->y = y; g->u = u; g->v = v; g->c = c;
    return 1;
}
static int geo_push_index(uint16_t i) {
    if (geo_in == geo_icap) {
        size_t cap = geo_icap ? geo_icap * 2 : 2048;
        if (cap > SIZE_MAX / sizeof(*geo_idx)) return 0;
        uint16_t *ni = (uint16_t *)realloc(geo_idx, cap * sizeof(*ni));
        if (!ni) return 0;
        geo_idx = ni; geo_icap = cap;
    }
    geo_idx[geo_in++] = i;
    return 1;
}
/* Готовый quad из четырёх вершин в порядке (0,1,2,3). */
static int geo_quad(const GeoVert q[4]) {
    size_t base = geo_vn;
    for (int i = 0; i < 4; i++)
        if (!geo_push_vert(q[i].x, q[i].y, q[i].u, q[i].v, q[i].c)) return 0;
    static const uint16_t order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; i++)
        if (!geo_push_index((uint16_t)(base + order[i]))) return 0;
    return 1;
}
static void geo_reset(void) { geo_vn = 0; geo_in = 0; }

/* --- кэш тригонометрии для вееров ---
 *
 * Круги и кольца встречаются в кадре сотнями (пыль, кнопки управления,
 * хитбоксы, частицы вселенной и взрывы), и почти все они тесселируются с одним
 * и тем же числом сегментов. cosf/sinf — самая дорогая часть построения
 * геометрии, поэтому значения считаются один раз на пару (число сегментов,
 * формула) и переиспользуются. Формулы углов ровно те же, что были при прямом
 * вычислении, поэтому вершины получаются идентичными прежним. */
#define GEO_TRIG_SLOTS 12
typedef struct { int n, mode; float *cosv, *sinv; } GeoTrig;
static GeoTrig geo_trig[GEO_TRIG_SLOTS];
static int geo_trig_next;

/* mode 0 — веер круга: a = i * 2pi / n (float-арифметика, как и раньше);
 * mode 1 — лента кольца: a = (i / n) * 2pi (double-арифметика, как и раньше). */
static const GeoTrig *geo_trig_table(int n, int mode) {
    if (n < 3 || n > 4096) return NULL;
    for (int i = 0; i < GEO_TRIG_SLOTS; i++) {
        GeoTrig *t = &geo_trig[i];
        if (t->cosv && t->n == n && t->mode == mode) return t;
    }
    GeoTrig *t = &geo_trig[geo_trig_next];
    geo_trig_next = (geo_trig_next + 1) % GEO_TRIG_SLOTS;
    free(t->cosv); free(t->sinv);
    t->cosv = NULL; t->sinv = NULL; t->n = 0;
    float *c = (float *)malloc((size_t)(n + 1) * sizeof(float));
    float *v = (float *)malloc((size_t)(n + 1) * sizeof(float));
    if (!c || !v) { free(c); free(v); return NULL; }
    for (int i = 0; i <= n; i++) {
        float a = mode == 0 ? (float)i * (float)(2.0 * M_PI) / (float)n
                            : (float)((double)i / n * 2.0 * M_PI);
        c[i] = cosf(a);
        v[i] = sinf(a);
    }
    t->cosv = c; t->sinv = v; t->n = n; t->mode = mode;
    return t;
}
static void geo_trig_free(void) {
    for (int i = 0; i < GEO_TRIG_SLOTS; i++) {
        free(geo_trig[i].cosv); free(geo_trig[i].sinv);
        geo_trig[i].cosv = geo_trig[i].sinv = NULL;
        geo_trig[i].n = geo_trig[i].mode = 0;
    }
}

/* --- примитивы --- */

static int geo_rect(float x, float y, float w, float h, uint32_t c) {
    if (!isfinite(x + y + w + h) || w <= 0 || h <= 0) return 1;
    GeoVert q[4] = {
        { x,     y,     0, 0, c }, { x + w, y,     1, 0, c },
        { x + w, y + h, 1, 1, c }, { x,     y + h, 0, 1, c },
    };
    return geo_quad(q);
}

/* Адаптивное число сегментов окружности: ошибка формы меньше половины
 * пикселя при любом радиусе, но веер не раздувается для мелких кругов. */
static int geo_circle_segs(float r) {
    if (r < 0) r = -r;
    int n = (int)(r * 1.5f);
    if (n < 16) n = 16;
    if (n > 128) n = 128;
    return n;
}

static int geo_circle(float x, float y, float r, uint32_t c) {
    if (!isfinite(x + y + r) || r <= 0) return 1;
    int n = geo_circle_segs(r);
    size_t base = geo_vn;
    if (!geo_push_vert(x, y, 0.5f, 0.5f, c)) return 0;
    const GeoTrig *tt = geo_trig_table(n, 0);
    for (int i = 0; i < n; i++) {
        float ca, sa;
        if (tt) { ca = tt->cosv[i]; sa = tt->sinv[i]; }
        else { float a = (float)i * (float)(2.0 * M_PI) / (float)n; ca = cosf(a); sa = sinf(a); }
        if (!geo_push_vert(x + ca * r, y + sa * r, 0, 0, c)) return 0;
    }
    for (int i = 1; i <= n; i++)
        if (!geo_push_index((uint16_t)base) ||
            !geo_push_index((uint16_t)(base + i)) ||
            !geo_push_index((uint16_t)(base + (i % n) + 1))) return 0;
    return 1;
}

static int geo_ring(float x, float y, float r, float th, uint32_t c) {
    if (!isfinite(x + y + r + th) || r <= 0 || th <= 0) return 1;
    if (th >= r) return geo_circle(x, y, r, c);
    int n = geo_circle_segs(r);
    size_t base = geo_vn;
    const GeoTrig *tt = geo_trig_table(n, 1);
    for (int i = 0; i <= n; i++) {
        float ca, sa;
        if (tt) { ca = tt->cosv[i]; sa = tt->sinv[i]; }
        else { float a = (float)((double)i / n * 2.0 * M_PI); ca = cosf(a); sa = sinf(a); }
        if (!geo_push_vert(x + ca * (r - th), y + sa * (r - th), 0, 0, c)) return 0;
        if (!geo_push_vert(x + ca * r, y + sa * r, 0, 0, c)) return 0;
    }
    for (int i = 0; i < n; i++) {
        size_t b = base + (size_t)i * 2;
        if (!geo_push_index((uint16_t)b) || !geo_push_index((uint16_t)(b + 1)) ||
            !geo_push_index((uint16_t)(b + 2))) return 0;
        if (!geo_push_index((uint16_t)(b + 1)) || !geo_push_index((uint16_t)(b + 3)) ||
            !geo_push_index((uint16_t)(b + 2))) return 0;
    }
    return 1;
}

/* Линия = прямоугольник вдоль отрезка + две полукруглые заглушки (у
 * попиксельного варианта покрытие было дисковым радиусом толщины/2). */
static int geo_line(float x1, float y1, float x2, float y2, float th, uint32_t c) {
    if (!isfinite(x1 + y1 + x2 + y2 + th) || th <= 0) return 1;
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    float rad = th * 0.5f;
    if (len <= 0.0001f) return geo_circle(x1, y1, rad, c);
    float nx = -dy / len * rad, ny = dx / len * rad;
    GeoVert q[4] = {
        { x1 + nx, y1 + ny, 0, 0, c }, { x2 + nx, y2 + ny, 1, 0, c },
        { x2 - nx, y2 - ny, 1, 1, c }, { x1 - nx, y1 - ny, 0, 1, c },
    };
    if (!geo_quad(q)) return 0;
    int n = geo_circle_segs(rad) / 2;
    if (n < 6) n = 6;
    for (int cap = 0; cap < 2; cap++) {
        float cx = cap ? x2 : x1, cy = cap ? y2 : y1;
        float dir = cap ? -1.0f : 1.0f; /* заглушка наружу от отрезка */
        float base_ang = atan2f(dy, dx) + (float)M_PI * 0.5f;
        size_t base = geo_vn;
        if (!geo_push_vert(cx, cy, 0, 0, c)) return 0;
        for (int i = 0; i <= n; i++) {
            float a = base_ang + dir * (float)((double)i / n * M_PI);
            if (!geo_push_vert(cx + cosf(a) * rad, cy + sinf(a) * rad, 0, 0, c)) return 0;
        }
        for (int i = 1; i <= n; i++)
            if (!geo_push_index((uint16_t)base) ||
                !geo_push_index((uint16_t)(base + i)) ||
                !geo_push_index((uint16_t)(base + i + 1))) return 0;
    }
    return 1;
}

static int geo_roundrect(float x, float y, float w, float h, float rad, uint32_t c) {
    if (!isfinite(x + y + w + h + rad) || w <= 0 || h <= 0) return 1;
    if (rad < 0) rad = 0;
    if (rad > w * 0.5f) rad = w * 0.5f;
    if (rad > h * 0.5f) rad = h * 0.5f;
    if (rad < 0.5f) return geo_rect(x, y, w, h, c);
    if (!geo_rect(x + rad, y, w - rad * 2, h, c)) return 0;               /* центр */
    if (!geo_rect(x, y + rad, rad, h - rad * 2, c)) return 0;             /* слева */
    if (!geo_rect(x + w - rad, y + rad, rad, h - rad * 2, c)) return 0;   /* справа */
    /* Сегментов на четверть - из расчета ошибки формы заметно меньше
     * пикселя при любом радиусе (r*(1-cos(pi/(2n)))): 12 сегментов дают
     * ~0.9% радиуса, дальше точность растет с ростом n. */
    int n = (int)ceilf(rad * 1.2f);
    if (n < 12) n = 12;
    if (n > 64) n = 64;
    static const float cxs[4] = { 1, 0, 0, 1 }, cys[4] = { 1, 1, 0, 0 };
    static const float a0s[4] = { 0, (float)M_PI * 0.5f, (float)M_PI, (float)M_PI * 1.5f };
    for (int k = 0; k < 4; k++) {
        float cx = x + cxs[k] * (w - rad) + (cxs[k] ? 0 : rad);
        float cy = y + cys[k] * (h - rad) + (cys[k] ? 0 : rad);
        size_t base = geo_vn;
        if (!geo_push_vert(cx, cy, 0, 0, c)) return 0;
        for (int i = 0; i <= n; i++) {
            float a = a0s[k] + (float)((double)i / n * M_PI * 0.5);
            if (!geo_push_vert(cx + cosf(a) * rad, cy + sinf(a) * rad, 0, 0, c)) return 0;
        }
        for (int i = 1; i <= n; i++)
            if (!geo_push_index((uint16_t)base) ||
                !geo_push_index((uint16_t)(base + i)) ||
                !geo_push_index((uint16_t)(base + i + 1))) return 0;
    }
    return 1;
}

/* Повёрнутый прямоугольник: тот же поворот, что у текстуры (углы вокруг центра
 * фигуры), но без UV и без выборки — сплошная заливка цветом вершин. Нужен
 * хитбоксам: зона удара и клетки рывка рисуются ровными прямоугольниками с
 * острыми углами (у line концы круглые, а roundrect идёт конвейером без
 * смешивания и потому непрозрачный). */
static int geo_rect_rot(float x, float y, float w, float h, float ang, uint32_t c) {
    if (!isfinite(x + y + w + h + ang) || w <= 0 || h <= 0) return 1;
    /* Без поворота результат обязан совпадать с geo_rect бит в бит. */
    if (ang == 0.0f) return geo_rect(x, y, w, h, c);
    float hw = w * 0.5f, hh = h * 0.5f;
    float ca = cosf(ang), sa = sinf(ang);
    float cx = x + hw, cy = y + hh;
    static const float sx[4] = { -1, 1, 1, -1 }, sy[4] = { -1, -1, 1, 1 };
    GeoVert q[4];
    for (int i = 0; i < 4; i++) {
        float lx = sx[i] * hw, ly = sy[i] * hh;
        q[i].x = cx + ca * lx - sa * ly;
        q[i].y = cy + sa * lx + ca * ly;
        q[i].u = 0; q[i].v = 0; q[i].c = c;
    }
    return geo_quad(q);
}

/* Текстура: тот же поворот, что в старом попиксельном цикле (обратное
 * отображение p -> R(a)*p + центр, поэтому вершины — R(-a)*углы). x,y — левый
 * верхний угол невыровненной текстуры, sc — масштаб, a — угол. */
static int geo_tex(float x, float y, float ang, float sc, float tw, float th_, uint32_t c) {
    if (!isfinite(x + y + ang + sc) || sc <= 0 || tw <= 0 || th_ <= 0) return 1;
    float hw = tw * 0.5f * sc, hh = th_ * 0.5f * sc;
    /* Плитка фона арены — это десятки текстур без поворота за кадр: для нуля
     * косинус и синус известны наперёд, лишний вызов тригонометрии не нужен. */
    float ca = ang == 0.0f ? 1.0f : cosf(ang), sa = ang == 0.0f ? 0.0f : sinf(ang);
    float cx = x + hw, cy = y + hh;
    static const float sx[4] = { -1, 1, 1, -1 }, sy[4] = { -1, -1, 1, 1 };
    static const float uu[4] = { 0, 1, 1, 0 },  vv[4] = { 0, 0, 1, 1 };
    GeoVert q[4];
    for (int i = 0; i < 4; i++) {
        float lx = sx[i] * hw, ly = sy[i] * hh;
        q[i].x = cx + ca * lx - sa * ly;
        q[i].y = cy + sa * lx + ca * ly;
        q[i].u = uu[i]; q[i].v = vv[i]; q[i].c = c;
    }
    return geo_quad(q);
}

/* Текст: квад на глиф, UV — окно глифа в шрифтовом атласе. Метрики и
 * раскладка пера — один в один с прежним render_text_now. */
static int geo_text(const char *s, float x, float y, uint32_t c, float sc, const DSFont *fnt) {
    if (!s || !fnt || !isfinite(x + y + sc) || sc <= 0) return 1;
    float asc = ds_font_ascent(fnt), lb = 0;
    const DSFontGlyph *ref = ds_font_glyph(fnt, 'S');
    if (ref) { asc = ref->bearing_top; lb = ref->bearing_x; }
    float pen = x - lb * sc, base = y + asc * sc;
    for (const char *cur = s; *cur;) {
        int cp = utf8_dec(&cur);
        if (cp == '\n') { pen = x - lb * sc; base += ds_font_lineh(fnt) * sc; continue; }
        const DSFontGlyph *g = ds_font_glyph(fnt, (uint32_t)cp);
        if (!g) continue;
        if (g->width > 0 && g->height > 0) {
            GeoVert q[4] = {
                { pen + g->bearing_x * sc,                base - g->bearing_top * sc,                 g->u0, g->v0, c },
                { pen + (g->bearing_x + g->width) * sc,   base - g->bearing_top * sc,                 g->u1, g->v0, c },
                { pen + (g->bearing_x + g->width) * sc,   base - (g->bearing_top - g->height) * sc,   g->u1, g->v1, c },
                { pen + g->bearing_x * sc,                base - (g->bearing_top - g->height) * sc,   g->u0, g->v1, c },
            };
            if (!geo_quad(q)) return 0;
        }
        pen += g->advance * sc;
    }
    return 1;
}
