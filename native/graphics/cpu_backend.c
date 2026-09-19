/* CPU-резервный рендер: полный запасной путь без Vulkan.
 *
 * Зачем: на части устройств (например, Mali-G57 в TECNO KL4) Vulkan-инициализация
 * или создание конвейеров падает внутри драйвера - процесс умирает мгновенно, без
 * экрана и лога (у игрока нет adb). Прошлая версия игры работала на этом же
 * телефоне именно программным растеризатором. Этот файл - тот же попиксельный
 * рендер, приведённый к текущим структурам (types.inc), плюс разбор команд
 * скрипта в пиксели. Используется двумя путями:
 *  - предыдущий запуск упал (см. native/crash_report.inc) - стартуем сразу в CPU;
 *  - Vulkan-инициализация вернула неудачу - переключаемся на лету.
 *
 * Всё в одном транляционном юните: типы берутся из types.inc (точная раскладка
 * Texture/DSCmd/DSFontGlyph), шрифт рисуется через глобальные аксессоры
 * ttf_font (ds_font_glyph/alpha/aw/ah/ascent/lineh), ассетов этот юнит не читает -
 * текстуры уже загружены lifecycle-ем, в командах лежат указатели на них.
 */
#include "runtime.h"
#include "native/graphics/types.inc"
#include <math.h>
#include <string.h>

/* --- пиксельная математика (как в прежнем попиксельном рендере) --- */

static uint32_t cpu_pack(uint32_t c) {
    uint32_t a = (c >> 24) & 0xff, r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    if (!a) a = 255;
    return r | (g << 8) | (b << 16) | (a << 24);
}
static uint32_t cpu_blend(uint32_t d, uint32_t s) {
    uint32_t a = (s >> 24) & 0xff;
    if (!a) return d;
    if (a == 255) return s;
    uint32_t inv = 255 - a;
    uint32_t dr = d & 0xff, dg = (d >> 8) & 0xff, db = (d >> 16) & 0xff;
    uint32_t sr = s & 0xff, sg = (s >> 8) & 0xff, sb = (s >> 16) & 0xff;
    uint32_t r = (sr * a + dr * inv) / 255;
    uint32_t g = (sg * a + dg * inv) / 255;
    uint32_t b2 = (sb * a + db * inv) / 255;
    return r | (g << 8) | (b2 << 16) | 0xff000000u;
}
static int cpu_floor(float v, int lim) {
    if (v <= 0) return 0;
    if (v >= lim) return lim;
    int r = (int)floorf(v);
    return r < 0 ? 0 : r > lim ? lim : r;
}
static int cpu_ceil(float v, int lim) {
    if (v <= 0) return 0;
    if (v >= lim) return lim;
    int r = (int)ceilf(v);
    return r < 0 ? 0 : r > lim ? lim : r;
}
static void cpu_fill_span(uint32_t *d, int n, uint32_t c) {
    while (n >= 8) { d[0]=c; d[1]=c; d[2]=c; d[3]=c; d[4]=c; d[5]=c; d[6]=c; d[7]=c; d+=8; n-=8; }
    while (n-- > 0) *d++ = c;
}
static void cpu_paint_span(uint32_t *d, int n, uint32_t c) {
    if ((c >> 24) >= 255) { cpu_fill_span(d, n, c); return; }
    while (n-- > 0) { *d = cpu_blend(*d, c); d++; }
}

/* --- примитивы (тела перенесены из прежнего raster) --- */

static void cpu_rect(Buffer *b, float x, float y, float w, float h, uint32_t c) {
    if (!b || !isfinite(x+y+w+h) || w <= 0 || h <= 0) return;
    if (x >= b->width || y >= b->height || x+w <= 0 || y+h <= 0) return;
    int l = cpu_floor(floorf(x), b->width), t = cpu_floor(floorf(y), b->height);
    int r = cpu_ceil(ceilf(x+w), b->width), bo = cpu_ceil(ceilf(y+h), b->height);
    if ((c >> 24) >= 255) {
        for (int row = t; row < bo; row++) cpu_fill_span(b->pixels + row*b->stride + l, r-l, c);
    } else {
        for (int row = t; row < bo; row++) {
            uint32_t *d = b->pixels + row*b->stride + l;
            for (int col = 0; col < r-l; col++) d[col] = cpu_blend(d[col], c);
        }
    }
}
static void cpu_roundrect(Buffer *b, float x, float y, float w, float h, float rad, uint32_t c) {
    if (!b || !isfinite(x+y+w+h+rad) || w <= 0 || h <= 0) return;
    if (x >= b->width || y >= b->height || x+w <= 0 || y+h <= 0) return;
    if (rad < 0) rad = 0;
    if (rad > w*0.5f) rad = w*0.5f;
    if (rad > h*0.5f) rad = h*0.5f;
    int t0 = cpu_floor(floorf(y), b->height), t1 = cpu_ceil(ceilf(y+h), b->height);
    for (int row = t0; row < t1; row++) {
        float yi = (float)row + 0.5f - y, ins = 0;
        if (rad > 0) {
            if (yi < rad) { float d = rad-yi; if (d > rad) d = rad; ins = rad - sqrtf(rad*rad - d*d); }
            else if (yi > h - rad) { float d = yi-(h-rad); if (d > rad) d = rad; ins = rad - sqrtf(rad*rad - d*d); }
        }
        int s = (int)ceilf(x+ins); if (s < 0) s = 0;
        int e = (int)ceilf(x+w-ins); if (e > b->width) e = b->width;
        if (e > s) cpu_fill_span(b->pixels + row*b->stride + s, e-s, c);
    }
}
static void cpu_circle(Buffer *b, float x, float y, float rad, uint32_t c) {
    if (!b || !isfinite(x+y+rad) || rad <= 0) return;
    int r = (int)ceilf(rad); if (r <= 0) return;
    int cx = (int)floorf(x+0.5f), cy = (int)floorf(y+0.5f);
    long long r2 = (long long)r*r;
    for (int dy = -r; dy <= r; dy++) {
        int sy = cy+dy; if (sy < 0 || sy >= b->height) continue;
        int hw = (int)sqrt((double)(r2 - (long long)dy*dy));
        int l = cx-hw, rr = cx+hw+1;
        if (l < 0) l = 0; if (rr > b->width) rr = b->width;
        if (l < rr) cpu_paint_span(b->pixels + sy*b->stride + l, rr-l, c);
    }
}
static void cpu_ring(Buffer *b, float x, float y, float rad, float th, uint32_t c) {
    if (!b || !isfinite(x+y+rad+th) || rad <= 0 || th <= 0) return;
    int out = (int)ceilf(rad), in = (int)floorf(rad - th);
    if (in <= 0) { cpu_circle(b, x, y, rad, c); return; }
    int cx = (int)floorf(x+0.5f), cy = (int)floorf(y+0.5f);
    long long o2 = (long long)out*out, i2 = (long long)in*in;
    for (int dy = -out; dy <= out; dy++) {
        int sy = cy+dy; if (sy < 0 || sy >= b->height) continue;
        int oh = (int)sqrt((double)(o2 - (long long)dy*dy));
        int ih = -1;
        if (abs(dy) <= in) ih = (int)sqrt((double)(i2 - (long long)dy*dy));
        int l = cx-oh, rr = cx+oh+1;
        if (l < 0) l = 0; if (rr > b->width) rr = b->width;
        if (ih < 0) {
            if (l < rr) cpu_paint_span(b->pixels + sy*b->stride + l, rr-l, c);
        } else {
            int il = cx-ih, ir = cx+ih+1;
            int lr = il < rr ? il : rr, rl = ir > l ? ir : l;
            if (l < lr) cpu_paint_span(b->pixels + sy*b->stride + l, lr-l, c);
            if (rl < rr) cpu_paint_span(b->pixels + sy*b->stride + rl, rr-rl, c);
        }
    }
}
static void cpu_line(Buffer *b, float x1, float y1, float x2, float y2, float th, uint32_t c) {
    if (!b || !isfinite(x1+y1+x2+y2+th) || th <= 0) return;
    float dx = x2-x1, dy = y2-y1, len2 = dx*dx + dy*dy;
    float rad = th * 0.5f, rad2 = rad * rad;
    if (len2 <= 0.0001f) { cpu_circle(b, x1, y1, rad, c); return; }
    float minx = (x1 < x2 ? x1 : x2) - rad;
    float maxx = (x1 > x2 ? x1 : x2) + rad;
    float miny = (y1 < y2 ? y1 : y2) - rad;
    float maxy = (y1 > y2 ? y1 : y2) + rad;
    int left = cpu_floor(floorf(minx), b->width);
    int right = cpu_ceil(ceilf(maxx), b->width);
    int top = cpu_floor(floorf(miny), b->height);
    int bottom = cpu_ceil(ceilf(maxy), b->height);
    for (int py = top; py < bottom; py++) {
        float fy = (float)py + 0.5f;
        for (int px = left; px < right; px++) {
            float fx = (float)px + 0.5f;
            float u = ((fx-x1)*dx + (fy-y1)*dy) / len2;
            if (u < 0 || u > 1) continue;
            float ox = x1 + u*dx - fx;
            float oy = y1 + u*dy - fy;
            if (ox*ox + oy*oy <= rad2) {
                uint32_t *pixel = &b->pixels[py*b->stride + px];
                *pixel = cpu_blend(*pixel, c);
            }
        }
    }
}
/* Повёрнутый прямоугольник: перебор bbox с обратным отображением точки внутрь
 * прямоугольника (те же вершины, что строит geo_rect_rot для GPU). */
static void cpu_rect_rot(Buffer *b, float x, float y, float w, float h, float ang, uint32_t c) {
    if (!b || !isfinite(x+y+w+h+ang) || w <= 0 || h <= 0) return;
    if (ang == 0.0f) { cpu_rect(b, x, y, w, h, c); return; }
    float hw = w * 0.5f, hh = h * 0.5f;
    float cx = x + hw, cy = y + hh;
    float ca = cosf(ang), sa = sinf(ang);
    float px[4], py[4];
    const float sx[4] = { -1, 1, 1, -1 }, sy[4] = { -1, -1, 1, 1 };
    for (int i = 0; i < 4; i++) {
        float lx = sx[i] * hw, ly = sy[i] * hh;
        px[i] = cx + ca * lx - sa * ly;
        py[i] = cy + sa * lx + ca * ly;
    }
    float minx = px[0], maxx = px[0], miny = py[0], maxy = py[0];
    for (int i = 1; i < 4; i++) {
        if (px[i] < minx) minx = px[i];
        if (px[i] > maxx) maxx = px[i];
        if (py[i] < miny) miny = py[i];
        if (py[i] > maxy) maxy = py[i];
    }
    int l = cpu_floor(floorf(minx), b->width), t = cpu_floor(floorf(miny), b->height);
    int r = cpu_ceil(ceilf(maxx), b->width), bo = cpu_ceil(ceilf(maxy), b->height);
    for (int yy = t; yy < bo; yy++) {
        float fy = (float)yy + 0.5f;
        for (int xx = l; xx < r; xx++) {
            float fx = (float)xx + 0.5f;
            int inside = 1;
            for (int i = 0; i < 4 && inside; i++) {
                int j = (i + 1) % 4;
                float cross = (px[j] - px[i]) * (fy - py[i]) - (py[j] - py[i]) * (fx - px[i]);
                if (cross < 0.0f) inside = 0;   /* вершины идут по часовой (y вниз) */
            }
            if (inside) {
                uint32_t *pixel = &b->pixels[yy*b->stride + xx];
                *pixel = cpu_blend(*pixel, c);
            }
        }
    }
}

/* --- шрифт --- */

static int cpu_utf8(const char **c) {
    const uint8_t *p = (const uint8_t *)*c;
    int r;
    if (!p || !*p) return -1;
    if (*p < 0x80) r = *p++;
    else if ((*p&0xe0)==0xc0 && (p[1]&0xc0)==0x80) { r = ((*p&0x1f)<<6)|(p[1]&0x3f); p+=2; }
    else if ((*p&0xf0)==0xe0 && (p[1]&0xc0)==0x80 && (p[2]&0xc0)==0x80) { r=((*p&0x0f)<<12)|((p[1]&0x3f)<<6)|(p[2]&0x3f); p+=3; }
    else if ((*p&0xf8)==0xf0 && (p[1]&0xc0)==0x80 && (p[2]&0xc0)==0x80 && (p[3]&0xc0)==0x80) { r=((*p&7)<<18)|((p[1]&0x3f)<<12)|((p[2]&0x3f)<<6)|(p[3]&0x3f); p+=4; }
    else r = *p++;
    *c = (const char *)p; return r;
}
static void cpu_text(Buffer *b, const char *s, float x, float y, uint32_t c, float sc, const DSFont *fnt) {
    if (!b || !fnt || !s || !isfinite(x+y+sc) || sc <= 0) return;
    int aw = ds_font_aw(fnt), ah = ds_font_ah(fnt);
    const uint8_t *al = ds_font_alpha(fnt);
    if (!al || aw <= 0 || ah <= 0) return;
    float asc = ds_font_ascent(fnt), lb = 0;
    const DSFontGlyph *ref = ds_font_glyph(fnt, 'S');
    if (ref) { asc = ref->bearing_top; lb = ref->bearing_x; }
    float pen = x - lb*sc, base = y + asc*sc;
    for (const char *cur = s; *cur;) {
        int cp = cpu_utf8(&cur);
        if (cp == '\n') { pen = x - lb*sc; base += ds_font_lineh(fnt)*sc; continue; }
        if (cp < 0) break;
        const DSFontGlyph *g = ds_font_glyph(fnt, (uint32_t)cp);
        if (!g) continue;
        int gx = (int)floorf(g->u0*aw+0.5f), gy = (int)floorf(g->v0*ah+0.5f);
        int dw = (int)ceilf(g->width*sc), dh = (int)ceilf(g->height*sc);
        int dx = (int)floorf(pen + g->bearing_x*sc), dy = (int)floorf(base - g->bearing_top*sc);
        for (int yy = 0; yy < dh; yy++) {
            int scr_y = dy + yy; if (scr_y < 0 || scr_y >= b->height) continue;
            int srow = gy + (int)(yy / sc);
            if (srow < 0 || srow >= ah) continue;
            for (int xx = 0; xx < dw; xx++) {
                int scr_x = dx + xx; if (scr_x < 0 || scr_x >= b->width) continue;
                int scol = gx + (int)(xx / sc);
                if (scol < 0 || scol >= aw) continue;
                uint8_t cov = al[srow*aw + scol];
                if (cov) {
                    uint32_t ca = (c & 0xffffff) | (((uint32_t)cov * (c>>24) / 255) << 24);
                    b->pixels[scr_y*b->stride + scr_x] = cpu_blend(b->pixels[scr_y*b->stride + scr_x], ca);
                }
            }
        }
        pen += g->advance * sc;
    }
}

/* --- текстуры (пиксели уже в памяти - их читаем напрямую) --- */

static void cpu_tex(Buffer *b, const Texture *t, float x, float y, float ang, float sc) {
    if (!b || !t || !t->pixels || !isfinite(x+y+ang+sc) || sc <= 0) return;
    if (fabsf(ang) < 0.0005f) {
        float w = t->w*sc, h = t->h*sc;
        if (x >= b->width || y >= b->height || x+w <= 0 || y+h <= 0) return;
        int l = cpu_floor(floorf(x), b->width), t0 = cpu_floor(floorf(y), b->height);
        int r = cpu_ceil(ceilf(x+w), b->width), bo = cpu_ceil(ceilf(y+h), b->height);
        for (int sy = t0; sy < bo; sy++) {
            int src_y = (int)(((float)sy + 0.5f - y) / sc);
            if (src_y < 0) src_y = 0; if (src_y >= t->h) src_y = t->h - 1;
            for (int sx = l; sx < r; sx++) {
                int src_x = (int)(((float)sx + 0.5f - x) / sc);
                if (src_x < 0) src_x = 0; if (src_x >= t->w) src_x = t->w - 1;
                b->pixels[sy*b->stride + sx] = cpu_blend(b->pixels[sy*b->stride + sx], t->pixels[src_y*t->w + src_x]);
            }
        }
        return;
    }
    float hw = t->w*0.5f*sc, hh = t->h*0.5f*sc;
    float cx = x+hw, cy = y+hh, ca = cosf(ang), sa = sinf(ang);
    float dx = fabsf(hw*ca) + fabsf(hh*sa), dy = fabsf(hw*sa) + fabsf(hh*ca);
    int l = cpu_floor(floorf(cx-dx), b->width), t0 = cpu_floor(floorf(cy-dy), b->height);
    int r = cpu_ceil(ceilf(cx+dx), b->width), bo = cpu_ceil(ceilf(cy+dy), b->height);
    for (int sy = t0; sy < bo; sy++) {
        float py = (float)sy + 0.5f - cy;
        for (int sx = l; sx < r; sx++) {
            float px = (float)sx + 0.5f - cx;
            int tx = (int)floorf((px*ca + py*sa)/sc + t->w*0.5f);
            int ty = (int)floorf((-px*sa + py*ca)/sc + t->h*0.5f);
            if (tx < 0 || tx >= t->w || ty < 0 || ty >= t->h) continue;
            b->pixels[sy*b->stride + sx] = cpu_blend(b->pixels[sy*b->stride + sx], t->pixels[ty*t->w + tx]);
        }
    }
}
static void cpu_tex_tint(Buffer *b, const Texture *t, float x, float y, float ang, float sc, uint32_t tint) {
    if (!b || !t || !t->pixels || !isfinite(x+y+ang+sc) || sc <= 0) return;
    uint32_t tr = tint & 0xff, tg = (tint >> 8) & 0xff, tb = (tint >> 16) & 0xff, ta = (tint >> 24) & 0xff;
    if (fabsf(ang) < 0.0005f) {
        float w = t->w*sc, h = t->h*sc;
        if (x >= b->width || y >= b->height || x+w <= 0 || y+h <= 0) return;
        int l = cpu_floor(floorf(x), b->width), t0 = cpu_floor(floorf(y), b->height);
        int r = cpu_ceil(ceilf(x+w), b->width), bo = cpu_ceil(ceilf(y+h), b->height);
        for (int sy = t0; sy < bo; sy++) {
            int src_y = (int)(((float)sy + 0.5f - y) / sc);
            if (src_y < 0) src_y = 0; if (src_y >= t->h) src_y = t->h - 1;
            for (int sx = l; sx < r; sx++) {
                int src_x = (int)(((float)sx + 0.5f - x) / sc);
                if (src_x < 0) src_x = 0; if (src_x >= t->w) src_x = t->w - 1;
                uint32_t p = t->pixels[src_y*t->w + src_x];
                uint32_t sa = (p >> 24) & 0xff;
                if (!sa) continue;
                uint32_t a = (sa * ta) / 255;
                if (!a) continue;
                uint32_t s = tr | (tg << 8) | (tb << 16) | (a << 24);
                b->pixels[sy*b->stride + sx] = cpu_blend(b->pixels[sy*b->stride + sx], s);
            }
        }
        return;
    }
    float hw = t->w*0.5f*sc, hh = t->h*0.5f*sc;
    float cx = x+hw, cy = y+hh, ca = cosf(ang), sa = sinf(ang);
    float dx = fabsf(hw*ca) + fabsf(hh*sa), dy = fabsf(hw*sa) + fabsf(hh*ca);
    int l = cpu_floor(floorf(cx-dx), b->width), t0 = cpu_floor(floorf(cy-dy), b->height);
    int r = cpu_ceil(ceilf(cx+dx), b->width), bo = cpu_ceil(ceilf(cy+dy), b->height);
    for (int sy = t0; sy < bo; sy++) {
        float py = (float)sy + 0.5f - cy;
        for (int sx = l; sx < r; sx++) {
            float px = (float)sx + 0.5f - cx;
            int tx = (int)floorf((px*ca + py*sa)/sc + t->w*0.5f);
            int ty = (int)floorf((-px*sa + py*ca)/sc + t->h*0.5f);
            if (tx < 0 || tx >= t->w || ty < 0 || ty >= t->h) continue;
            uint32_t p = t->pixels[ty*t->w + tx];
            uint32_t spa = (p >> 24) & 0xff;
            if (!spa) continue;
            uint32_t a = (spa * ta) / 255;
            if (!a) continue;
            uint32_t s = tr | (tg << 8) | (tb << 16) | (a << 24);
            b->pixels[sy*b->stride + sx] = cpu_blend(b->pixels[sy*b->stride + sx], s);
        }
    }
}

/* --- вход: команды скрипта -> пиксели окна --- */

void ds_cpu_render_commands(const DSCmd *cs, size_t n, Buffer *b, const DSFont *fnt) {
    if (!b || !b->pixels || !cs) return;
    for (size_t i = 0; i < n; i++) {
        const DSCmd *c = &cs[i];
        switch (c->t) {
            case DS_CMD_RECT:     cpu_rect(b, c->v.rc.x, c->v.rc.y, c->v.rc.w, c->v.rc.h, cpu_pack(c->v.rc.c)); break;
            case DS_CMD_ROUND:    cpu_roundrect(b, c->v.rr.x, c->v.rr.y, c->v.rr.w, c->v.rr.h, c->v.rr.r, cpu_pack(c->v.rr.c)); break;
            case DS_CMD_RECT_ROT: cpu_rect_rot(b, c->v.rot.x, c->v.rot.y, c->v.rot.w, c->v.rot.h, c->v.rot.ang, cpu_pack(c->v.rot.c)); break;
            case DS_CMD_CIRCLE:   cpu_circle(b, c->v.ci.x, c->v.ci.y, c->v.ci.r, cpu_pack(c->v.ci.c)); break;
            case DS_CMD_RING:     cpu_ring(b, c->v.rg.x, c->v.rg.y, c->v.rg.r, c->v.rg.th, cpu_pack(c->v.rg.c)); break;
            case DS_CMD_LINE:     cpu_line(b, c->v.ln.x1, c->v.ln.y1, c->v.ln.x2, c->v.ln.y2, c->v.ln.th, cpu_pack(c->v.ln.c)); break;
            case DS_CMD_TEX:
                if (c->v.tx.tx && c->v.tx.tx->pixels)
                    cpu_tex(b, c->v.tx.tx, c->v.tx.x, c->v.tx.y, c->v.tx.a, c->v.tx.sc);
                break;
            case DS_CMD_TEX_TINT:
                if (c->v.tx2.tx && c->v.tx2.tx->pixels)
                    cpu_tex_tint(b, c->v.tx2.tx, c->v.tx2.x, c->v.tx2.y, c->v.tx2.a, c->v.tx2.sc, cpu_pack(c->v.tx2.c));
                break;
            case DS_CMD_TEXT:
                cpu_text(b, c->v.tt.s, c->v.tt.x, c->v.tt.y, cpu_pack(c->v.tt.c), c->v.tt.sc, fnt);
                break;
            default: break;
        }
    }
}
