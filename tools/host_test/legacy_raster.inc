#include "runtime.h"
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"
#ifdef STB_IMAGE_STATIC
#undef STB_IMAGE_STATIC
#endif
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Texture Texture;
typedef struct DSFont DSFont;
typedef struct {
    uint32_t codepoint;
    float advance;
    float bearing_x;
    float bearing_top;
    int width;
    int height;
    float u0, v0, u1, v1;
} DSFontGlyph;
struct Texture {
    Texture *next;
    char *name;
    int w, h, opaque;
    uint32_t *pixels;
};
typedef enum {
    DS_CMD_RECT, DS_CMD_ROUND, DS_CMD_CIRCLE, DS_CMD_RING, DS_CMD_LINE, DS_CMD_TEX, DS_CMD_TEXT, DS_CMD_TEX_TINT
} DSCommand;
typedef struct {
    DSCommand t;
    union {
        struct { float x, y, w, h; uint32_t c; } rc;
        struct { float x, y, w, h, r; uint32_t c; } rr;
        struct { float x, y, r; uint32_t c; } ci;
        struct { float x, y, r, th; uint32_t c; } rg;
        struct { float x1, y1, x2, y2, th; uint32_t c; } ln;
        struct { float x, y, a, sc; Texture *tx; } tx;
        struct { char *s; float x, y, sc; uint32_t c; } tt;
        struct { float x, y, a, sc; Texture *tx; uint32_t c; } tx2;
    } v;
} DSCmd;
static uint32_t pack_c(uint32_t c) {
    uint32_t a = (c >> 24) & 0xff, r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    if (!a) a = 255;
    return r | (g << 8) | (b << 16) | (a << 24);
}
static uint32_t blend(uint32_t d, uint32_t s) {
    uint32_t a = (s >> 24) & 0xff;
    if (!a) return d;
    if (a == 255) return s;
    uint32_t inv = 255 - a;
    uint32_t r = (((s & 0xff)*a + (d & 0xff)*inv + 127) / 255);
    uint32_t g = ((((s >> 8) & 0xff)*a + ((d >> 8) & 0xff)*inv + 127) / 255);
    uint32_t b = ((((s >> 16) & 0xff)*a + ((d >> 16) & 0xff)*inv + 127) / 255);
    uint32_t oa = a + ((((d >> 24) & 0xff)*inv + 127) / 255);
    return r | (g << 8) | (b << 16) | (oa << 24);
}
static int cl_floor(float v, int lim) {
    if (v <= 0) return 0;
    if (v >= lim) return lim;
    int r = (int)floorf(v);
    return r < 0 ? 0 : r > lim ? lim : r;
}
static int cl_ceil(float v, int lim) {
    if (v <= 0) return 0;
    if (v >= lim) return lim;
    int r = (int)ceilf(v);
    return r < 0 ? 0 : r > lim ? lim : r;
}
static char *strdup_safe(const char *s) {
    if (!s) { char *o = (char *)malloc(1); if (o) o[0] = '\0'; return o; }
    size_t n = strlen(s) + 1;
    char *o = (char *)malloc(n);
    if (o) memcpy(o, s, n);
    return o;
}
static void fill_span(uint32_t *d, int n, uint32_t c) {
    while (n >= 8) { d[0]=c; d[1]=c; d[2]=c; d[3]=c; d[4]=c; d[5]=c; d[6]=c; d[7]=c; d+=8; n-=8; }
    while (n-- > 0) *d++ = c;
}
static void paint_span(uint32_t *d, int n, uint32_t c) {
    if ((c >> 24) >= 255) { fill_span(d, n, c); return; }
    while (n-- > 0) { *d = blend(*d, c); d++; }
}
static void clear_buf(Buffer *b, uint32_t c) {
    if (!b || !b->pixels || b->width <= 0 || b->height <= 0 || b->stride < b->width) return;
    for (int y = 0; y < b->height; y++) fill_span(b->pixels + y*b->stride, b->width, c);
}
static void render_rect(Buffer *b, float x, float y, float w, float h, uint32_t c) {
    if (!b || !isfinite(x+y+w+h) || w <= 0 || h <= 0) return;
    if (x >= b->width || y >= b->height || x+w <= 0 || y+h <= 0) return;
    int l = cl_floor(floorf(x), b->width), t = cl_floor(floorf(y), b->height);
    int r = cl_ceil(ceilf(x+w), b->width), bo = cl_ceil(ceilf(y+h), b->height);
    if ((c >> 24) >= 255) {
        for (int row = t; row < bo; row++) fill_span(b->pixels + row*b->stride + l, r-l, c);
    } else {
        for (int row = t; row < bo; row++) {
            uint32_t *d = b->pixels + row*b->stride + l;
            for (int col = 0; col < r-l; col++) d[col] = blend(d[col], c);
        }
    }
}
static void render_roundrect(Buffer *b, float x, float y, float w, float h, float rad, uint32_t c) {
    if (!b || !isfinite(x+y+w+h+rad) || w <= 0 || h <= 0) return;
    if (x >= b->width || y >= b->height || x+w <= 0 || y+h <= 0) return;
    if (rad < 0) rad = 0;
    if (rad > w*0.5f) rad = w*0.5f;
    if (rad > h*0.5f) rad = h*0.5f;
    int t0 = cl_floor(floorf(y), b->height), t1 = cl_ceil(ceilf(y+h), b->height);
    for (int row = t0; row < t1; row++) {
        float yi = (float)row + 0.5f - y, ins = 0;
        if (rad > 0) {
            if (yi < rad) { float d = rad-yi; if (d > rad) d = rad; ins = rad - sqrtf(rad*rad - d*d); }
            else if (yi > h - rad) { float d = yi-(h-rad); if (d > rad) d = rad; ins = rad - sqrtf(rad*rad - d*d); }
        }
        int s = (int)ceilf(x+ins); if (s < 0) s = 0;
        int e = (int)ceilf(x+w-ins); if (e > b->width) e = b->width;
        if (e > s) fill_span(b->pixels + row*b->stride + s, e-s, c);
    }
}
static void render_circle(Buffer *b, float x, float y, float rad, uint32_t c) {
    if (!b || !isfinite(x+y+rad) || rad <= 0) return;
    int r = (int)ceilf(rad); if (r <= 0) return;
    int cx = (int)floorf(x+0.5f), cy = (int)floorf(y+0.5f);
    long long r2 = (long long)r*r;
    for (int dy = -r; dy <= r; dy++) {
        int sy = cy+dy; if (sy < 0 || sy >= b->height) continue;
        int hw = (int)sqrt((double)(r2 - (long long)dy*dy));
        int l = cx-hw, rr = cx+hw+1;
        if (l < 0) l = 0; if (rr > b->width) rr = b->width;
        if (l < rr) paint_span(b->pixels + sy*b->stride + l, rr-l, c);
    }
}
static void render_ring(Buffer *b, float x, float y, float rad, float th, uint32_t c) {
    if (!b || !isfinite(x+y+rad+th) || rad <= 0 || th <= 0) return;
    int out = (int)ceilf(rad), in = (int)floorf(rad - th);
    if (in <= 0) { render_circle(b, x, y, rad, c); return; }
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
            if (l < rr) paint_span(b->pixels + sy*b->stride + l, rr-l, c);
        } else {
            int il = cx-ih, ir = cx+ih+1;
            int lr = il < rr ? il : rr, rl = ir > l ? ir : l;
            if (l < lr) paint_span(b->pixels + sy*b->stride + l, lr-l, c);
            if (rl < rr) paint_span(b->pixels + sy*b->stride + rl, rr-rl, c);
        }
    }
}
static void render_line(Buffer *b, float x1, float y1, float x2, float y2, float th, uint32_t c) {
    if (!b || !isfinite(x1+y1+x2+y2+th) || th <= 0) return;
    float dx = x2-x1, dy = y2-y1, len2 = dx*dx + dy*dy;
    float rad = th * 0.5f, rad2 = rad * rad;
    if (len2 <= 0.0001f) { render_circle(b, x1, y1, rad, c); return; }
    float minx = (x1 < x2 ? x1 : x2) - rad;
    float maxx = (x1 > x2 ? x1 : x2) + rad;
    float miny = (y1 < y2 ? y1 : y2) - rad;
    float maxy = (y1 > y2 ? y1 : y2) + rad;
    int left = cl_floor(floorf(minx), b->width);
    int right = cl_ceil(ceilf(maxx), b->width);
    int top = cl_floor(floorf(miny), b->height);
    int bottom = cl_ceil(ceilf(maxy), b->height);
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
                *pixel = blend(*pixel, c);
            }
        }
    }
}
static Buffer *cur_buf;
static DSCmd *cmds;
static size_t cmd_n, cmd_cap;
static int frame_open;
static AAssetManager *amgr;
static Texture *textures;
static DSFont *font;
static int font_tried;
DSFont *ds_font_create(const uint8_t *data, size_t size, int ph);
void ds_font_destroy(DSFont *font);
const DSFontGlyph *ds_font_glyph(const DSFont *font, uint32_t cp);
int ds_font_aw(const DSFont *font);
int ds_font_ah(const DSFont *font);
const uint8_t *ds_font_alpha(const DSFont *font);
float ds_font_lineh(const DSFont *font);
float ds_font_ascent(const DSFont *font);
static Texture *find_tx(const char *n) {
    for (Texture *t = textures; t; t = t->next) if (strcmp(t->name, n) == 0) return t;
    return NULL;
}
static const char *norm_name(const char *n) {
    if (!n) return NULL;
    while (strncmp(n, "./", 2) == 0) n += 2;
    if (strncmp(n, "game/assets/", 12) == 0) n += 12;
    else if (strncmp(n, "assets/", 7) == 0) n += 7;
    if (!*n || *n == '/' || strchr(n, '\\')) return NULL;
    for (const char *c = n; *c;) {
        const char *s = c; while (*c && *c != '/') c++;
        if (c-s == 2 && s[0]=='.' && s[1]=='.') return NULL;
        if (*c == '/') c++;
    }
    return n;
}
static int open_asset(const char *n, uint8_t **out, size_t *sz) {
    if (!n || !out || !sz || !amgr) return 0;
    AAsset *a = AAssetManager_open(amgr, n, AASSET_MODE_BUFFER);
    if (!a) return 0;
    off_t len = AAsset_getLength(a);
    if (len <= 0 || (uint64_t)len > SIZE_MAX) { AAsset_close(a); return 0; }
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf) { AAsset_close(a); return 0; }
    size_t off = 0;
    while (off < (size_t)len) {
        int nr = AAsset_read(a, buf+off, (size_t)len-off);
        if (nr <= 0) break; off += nr;
    }
    AAsset_close(a);
    if (off != (size_t)len) { free(buf); return 0; }
    *out = buf; *sz = (size_t)len; return 1;
}
static Texture *load_png(const char *req) {
    const char *n = norm_name(req);
    if (!n) { ds_log_err("texture not loaded: invalid PNG asset path '%s'", req ? req : "(null)"); return NULL; }
    Texture *t = find_tx(n);
    if (t) return t->pixels ? t : NULL;
#ifdef __ANDROID__
    if (!amgr) { ds_log_err("texture not loaded: no asset manager for '%s'", n); return NULL; }
#endif
    t = (Texture *)calloc(1, sizeof(*t));
    if (!t) { ds_log_err("texture not loaded: out of memory caching '%s'", n); return NULL; }
    t->name = strdup_safe(n);
    if (!t->name) { free(t); ds_log_err("texture not loaded: out of memory caching name '%s'", n); return NULL; }
    t->next = textures; textures = t;
    uint8_t *enc = NULL; size_t enc_sz = 0;
    if (!open_asset(n, &enc, &enc_sz) || enc_sz > (size_t)INT_MAX) {
        free(enc); ds_log_err("texture not loaded: asset not found or unreadable: '%s'", n); return NULL;
    }
    int ch = 0;
    stbi_uc *dec = stbi_load_from_memory(enc, (int)enc_sz, &t->w, &t->h, &ch, STBI_rgb_alpha);
    free(enc);
    if (!dec || t->w <= 0 || t->h <= 0) {
        ds_log_err("texture not loaded: could not decode PNG '%s': %s", n, stbi_failure_reason() ? stbi_failure_reason() : "unknown");
        stbi_image_free(dec); return NULL;
    }
    size_t pc = (size_t)t->w * t->h;
    t->pixels = (uint32_t *)malloc(pc * sizeof(*t->pixels));
    if (!t->pixels) { stbi_image_free(dec); ds_log_err("texture not loaded: out of memory uploading PNG '%s'", n); return NULL; }
    t->opaque = 1;
    for (size_t i = 0; i < pc; i++) {
        uint8_t a = dec[i*4+3];
        t->pixels[i] = dec[i*4] | ((uint32_t)dec[i*4+1] << 8) | ((uint32_t)dec[i*4+2] << 16) | ((uint32_t)a << 24);
        if (a != 255) t->opaque = 0;
    }
    stbi_image_free(dec);
    ds_log("texture loaded: %s (%dx%d)", n, t->w, t->h);
    return t;
}
static int ensure_font(void) {
    if (font) return 1;
    if (font_tried) return 0;
    font_tried = 1;
    uint8_t *data = NULL; size_t sz = 0;
    if (!open_asset("fonts/Inter_24pt-Medium.ttf", &data, &sz)) {
        ds_log_err("font not loaded: fonts/Inter_24pt-Medium.ttf not found");
        return 0;
    }
    font = ds_font_create(data, sz, 32);
    free(data);
    if (!font) { ds_log_err("font not loaded: could not parse TrueType font"); return 0; }
    ds_log("font loaded: fonts/Inter_24pt-Medium.ttf");
    return 1;
}
static int utf8_dec(const char **c) {
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
static void render_text_now(Buffer *b, const char *s, float x, float y, uint32_t c, float sc) {
    if (!b || !font || !s || !isfinite(x+y+sc) || sc <= 0) return;
    int aw = ds_font_aw(font), ah = ds_font_ah(font);
    const uint8_t *al = ds_font_alpha(font);
    float asc = ds_font_ascent(font), lb = 0;
    const DSFontGlyph *ref = ds_font_glyph(font, 'S');
    if (ref) { asc = ref->bearing_top; lb = ref->bearing_x; }
    float pen = x - lb*sc, base = y + asc*sc;
    for (const char *cur = s; *cur;) {
        int cp = utf8_dec(&cur);
        if (cp == '\n') { pen = x - lb*sc; base += ds_font_lineh(font)*sc; continue; }
        const DSFontGlyph *g = ds_font_glyph(font, (uint32_t)cp);
        if (!g) continue;
        int sx = (int)floorf(g->u0*aw+0.5f), sy = (int)floorf(g->v0*ah+0.5f);
        int dw = (int)ceilf(g->width*sc), dh = (int)ceilf(g->height*sc);
        int dx = (int)floorf(pen + g->bearing_x*sc), dy = (int)floorf(base - g->bearing_top*sc);
        for (int yy = 0; yy < dh; yy++) {
            int scr_y = dy + yy; if (scr_y < 0 || scr_y >= b->height) continue;
            int srow = sy + (int)(yy / sc);
            if (srow < 0 || srow >= ah) continue;
            for (int xx = 0; xx < dw; xx++) {
                int scr_x = dx + xx; if (scr_x < 0 || scr_x >= b->width) continue;
                int scol = sx + (int)(xx / sc);
                if (scol < 0 || scol >= aw) continue;
                uint8_t cov = al[srow*aw + scol];
                if (cov) {
                    uint32_t ca = (c & 0xffffff) | (((uint32_t)cov * (c>>24) / 255) << 24);
                    b->pixels[scr_y*b->stride + scr_x] = blend(b->pixels[scr_y*b->stride + scr_x], ca);
                }
            }
        }
        pen += g->advance * sc;
    }
}
static void draw_tx_unrot(Buffer *b, const Texture *t, float x, float y, float sc) {
    if (!b || !t || !t->pixels || !isfinite(x+y+sc) || sc <= 0) return;
    float w = t->w*sc, h = t->h*sc;
    if (x >= b->width || y >= b->height || x+w <= 0 || y+h <= 0) return;
    int l = cl_floor(floorf(x), b->width), t0 = cl_floor(floorf(y), b->height);
    int r = cl_ceil(ceilf(x+w), b->width),  bo = cl_ceil(ceilf(y+h), b->height);
    if (sc == 1.0f && x == floorf(x) && y == floorf(y) && t->opaque && l == (int)x && t0 == (int)y) {
        int sl = l - (int)x, st = t0 - (int)y;
        for (int sy = t0; sy < bo; sy++) {
            int src_y = st + sy - t0;
            int cw = r - l;
            if (src_y < 0 || src_y >= t->h) continue;
            if (sl < 0) { cw += sl; sl = 0; }
            if (sl + cw > t->w) cw = t->w - sl;
            if (cw > 0) memcpy(b->pixels + sy*b->stride + l, t->pixels + src_y*t->w + sl, (size_t)cw*4);
        }
        return;
    }
    for (int sy = t0; sy < bo; sy++) {
        int src_y = (int)(((float)sy + 0.5f - y) / sc);
        if (src_y < 0) src_y = 0; if (src_y >= t->h) src_y = t->h - 1;
        for (int sx = l; sx < r; sx++) {
            int src_x = (int)(((float)sx + 0.5f - x) / sc);
            if (src_x < 0) src_x = 0; if (src_x >= t->w) src_x = t->w - 1;
            b->pixels[sy*b->stride + sx] = blend(b->pixels[sy*b->stride + sx], t->pixels[src_y*t->w + src_x]);
        }
    }
}
static void draw_tx_rot(Buffer *b, const Texture *t, float x, float y, float ang, float sc) {
    float hw = t->w*0.5f*sc, hh = t->h*0.5f*sc;
    float cx = x+hw, cy = y+hh, ca = cosf(ang), sa = sinf(ang);
    float dx = fabsf(hw*ca) + fabsf(hh*sa), dy = fabsf(hw*sa) + fabsf(hh*ca);
    int l = cl_floor(floorf(cx-dx), b->width), t0 = cl_floor(floorf(cy-dy), b->height);
    int r = cl_ceil(ceilf(cx+dx), b->width),   bo = cl_ceil(ceilf(cy+dy), b->height);
    for (int sy = t0; sy < bo; sy++) {
        float py = (float)sy + 0.5f - cy;
        for (int sx = l; sx < r; sx++) {
            float px = (float)sx + 0.5f - cx;
            int tx = (int)floorf((px*ca + py*sa)/sc + t->w*0.5f);
            int ty = (int)floorf((-px*sa + py*ca)/sc + t->h*0.5f);
            if (tx < 0 || tx >= t->w || ty < 0 || ty >= t->h) continue;
            b->pixels[sy*b->stride + sx] = blend(b->pixels[sy*b->stride + sx], t->pixels[ty*t->w + tx]);
        }
    }
}
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static void draw_tx(Buffer *b, const Texture *t, float x, float y, float a, float sc) {
    if (!b || !t || !t->pixels || !isfinite(x+y+a+sc) || sc <= 0) return;
    if (fabsf(a) < 0.0005f) { draw_tx_unrot(b, t, x, y, sc); return; }
    draw_tx_rot(b, t, x, y, a, sc);
}
/* Тонированная текстура: рисуется силуэт по альфа-каналу текстуры, залитый
 * цветом tint (0xAARRGGBB). Нужно для теней - текстура становится полностью
 * чёрной, сохраняя форму персонажа. */
static void draw_tx_unrot_tint(Buffer *b, const Texture *t, float x, float y, float sc, uint32_t tint) {
    if (!b || !t || !t->pixels || !isfinite(x+y+sc) || sc <= 0) return;
    float w = t->w*sc, h = t->h*sc;
    if (x >= b->width || y >= b->height || x+w <= 0 || y+h <= 0) return;
    int l = cl_floor(floorf(x), b->width), t0 = cl_floor(floorf(y), b->height);
    int r = cl_ceil(ceilf(x+w), b->width),  bo = cl_ceil(ceilf(y+h), b->height);
    uint32_t tr = tint & 0xff, tg = (tint >> 8) & 0xff, tb = (tint >> 16) & 0xff, ta = (tint >> 24) & 0xff;
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
            b->pixels[sy*b->stride + sx] = blend(b->pixels[sy*b->stride + sx], s);
        }
    }
}
static void draw_tx_rot_tint(Buffer *b, const Texture *t, float x, float y, float ang, float sc, uint32_t tint) {
    float hw = t->w*0.5f*sc, hh = t->h*0.5f*sc;
    float cx = x+hw, cy = y+hh, ca = cosf(ang), sa = sinf(ang);
    float dx = fabsf(hw*ca) + fabsf(hh*sa), dy = fabsf(hw*sa) + fabsf(hh*ca);
    int l = cl_floor(floorf(cx-dx), b->width), t0 = cl_floor(floorf(cy-dy), b->height);
    int r = cl_ceil(ceilf(cx+dx), b->width),   bo = cl_ceil(ceilf(cy+dy), b->height);
    uint32_t tr = tint & 0xff, tg = (tint >> 8) & 0xff, tb = (tint >> 16) & 0xff, ta = (tint >> 24) & 0xff;
    for (int sy = t0; sy < bo; sy++) {
        float py = (float)sy + 0.5f - cy;
        for (int sx = l; sx < r; sx++) {
            float px = (float)sx + 0.5f - cx;
            int tx = (int)floorf((px*ca + py*sa)/sc + t->w*0.5f);
            int ty = (int)floorf((-px*sa + py*ca)/sc + t->h*0.5f);
            if (tx < 0 || tx >= t->w || ty < 0 || ty >= t->h) continue;
            uint32_t p = t->pixels[ty*t->w + tx];
            uint32_t sa = (p >> 24) & 0xff;
            if (!sa) continue;
            uint32_t a = (sa * ta) / 255;
            if (!a) continue;
            uint32_t s = tr | (tg << 8) | (tb << 16) | (a << 24);
            b->pixels[sy*b->stride + sx] = blend(b->pixels[sy*b->stride + sx], s);
        }
    }
}
static void draw_tx_tint(Buffer *b, const Texture *t, float x, float y, float a, float sc, uint32_t tint) {
    if (!b || !t || !t->pixels || !isfinite(x+y+a+sc) || sc <= 0) return;
    if (fabsf(a) < 0.0005f) { draw_tx_unrot_tint(b, t, x, y, sc, tint); return; }
    draw_tx_rot_tint(b, t, x, y, a, sc, tint);
}
