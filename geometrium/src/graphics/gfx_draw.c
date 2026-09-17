/* graphics/gfx_draw.c — 2D primitives: rectangles, circles, rings,
 * lines, triangles. Colors arrive already packed (see gfx_pack). */
#include "gfx_internal.h"
#include <math.h>

uint32_t gfx_pack(uint32_t c) {
    uint32_t a = (c >> 24) & 0xff, r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    if (!a) a = 255;
    return r | (g << 8) | (b << 16) | (a << 24);
}

uint32_t gfx_blend(uint32_t d, uint32_t s) {
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

int gfx_cl_floor(float v, int lim) {
    if (v <= 0) return 0;
    if (v >= lim) return lim;
    int r = (int)floorf(v);
    return r < 0 ? 0 : r > lim ? lim : r;
}

int gfx_cl_ceil(float v, int lim) {
    if (v <= 0) return 0;
    if (v >= lim) return lim;
    int r = (int)ceilf(v);
    return r < 0 ? 0 : r > lim ? lim : r;
}

void gfx_fill_span(uint32_t *d, int n, uint32_t c) {
    while (n >= 8) { d[0]=c; d[1]=c; d[2]=c; d[3]=c; d[4]=c; d[5]=c; d[6]=c; d[7]=c; d+=8; n-=8; }
    while (n-- > 0) *d++ = c;
}

void gfx_paint_span(uint32_t *d, int n, uint32_t c) {
    if ((c >> 24) >= 255) { gfx_fill_span(d, n, c); return; }
    while (n-- > 0) { *d = gfx_blend(*d, c); d++; }
}

void gfx_clear(Buffer *b, uint32_t c) {
    if (!b || !b->pixels || b->width <= 0 || b->height <= 0 || b->stride < b->width) return;
    for (int y = 0; y < b->height; y++) gfx_fill_span(b->pixels + y*b->stride, b->width, c);
}

void gfx_render_rect(Buffer *b, float x, float y, float w, float h, uint32_t c) {
    if (!b || !isfinite(x+y+w+h) || w <= 0 || h <= 0) return;
    if (x >= b->width || y >= b->height || x+w <= 0 || y+h <= 0) return;
    int l = gfx_cl_floor(floorf(x), b->width), t = gfx_cl_floor(floorf(y), b->height);
    int r = gfx_cl_ceil(ceilf(x+w), b->width), bo = gfx_cl_ceil(ceilf(y+h), b->height);
    if ((c >> 24) >= 255) {
        for (int row = t; row < bo; row++) gfx_fill_span(b->pixels + row*b->stride + l, r-l, c);
    } else {
        for (int row = t; row < bo; row++) {
            uint32_t *d = b->pixels + row*b->stride + l;
            for (int col = 0; col < r-l; col++) d[col] = gfx_blend(d[col], c);
        }
    }
}

void gfx_render_roundrect(Buffer *b, float x, float y, float w, float h, float rad, uint32_t c) {
    if (!b || !isfinite(x+y+w+h+rad) || w <= 0 || h <= 0) return;
    if (x >= b->width || y >= b->height || x+w <= 0 || y+h <= 0) return;
    if (rad < 0) rad = 0;
    if (rad > w*0.5f) rad = w*0.5f;
    if (rad > h*0.5f) rad = h*0.5f;
    int t0 = gfx_cl_floor(floorf(y), b->height), t1 = gfx_cl_ceil(ceilf(y+h), b->height);
    for (int row = t0; row < t1; row++) {
        float yi = (float)row + 0.5f - y, ins = 0;
        if (rad > 0) {
            if (yi < rad) {
                float d = fminf(rad,rad-yi);
                ins = rad - sqrtf(rad*rad - d*d);
            } else if (yi > h-rad) {
                float d = fminf(rad,yi-(h-rad));
                ins = rad - sqrtf(rad*rad - d*d);
            }
        }
        int s = (int)ceilf(x+ins);
        if (s < 0) s = 0;
        int e = (int)ceilf(x+w-ins);
        if (e > b->width) e = b->width;
        if (e > s) gfx_paint_span(b->pixels + row*b->stride + s, e-s, c);
    }
}

void gfx_render_circle(Buffer *b, float x, float y, float rad, uint32_t c) {
    if (!b || !isfinite(x+y+rad) || rad <= 0) return;
    int r = (int)ceilf(rad);
    if (r <= 0) return;
    int cx = (int)floorf(x+0.5f), cy = (int)floorf(y+0.5f);
    long long r2 = (long long)r*r;
    for (int dy = -r; dy <= r; dy++) {
        int sy = cy+dy;
        if (sy < 0 || sy >= b->height) continue;
        int hw = (int)sqrt((double)(r2 - (long long)dy*dy));
        int l = cx-hw, rr = cx+hw+1;
        if (l < 0) l = 0;
        if (rr > b->width) rr = b->width;
        if (l < rr) gfx_paint_span(b->pixels + sy*b->stride + l, rr-l, c);
    }
}

void gfx_render_ring(Buffer *b, float x, float y, float rad, float th, uint32_t c) {
    if (!b || !isfinite(x+y+rad+th) || rad <= 0 || th <= 0) return;
    int out = (int)ceilf(rad), in = (int)floorf(rad - th);
    if (in <= 0) { gfx_render_circle(b, x, y, rad, c); return; }
    int cx = (int)floorf(x+0.5f), cy = (int)floorf(y+0.5f);
    long long o2 = (long long)out*out, i2 = (long long)in*in;
    for (int dy = -out; dy <= out; dy++) {
        int sy = cy+dy;
        if (sy < 0 || sy >= b->height) continue;
        int oh = (int)sqrt((double)(o2 - (long long)dy*dy));
        int ih = -1;
        if (abs(dy) <= in) ih = (int)sqrt((double)(i2 - (long long)dy*dy));
        int l = cx-oh, rr = cx+oh+1;
        if (l < 0) l = 0;
        if (rr > b->width) rr = b->width;
        if (ih < 0) {
            if (l < rr) gfx_paint_span(b->pixels + sy*b->stride + l, rr-l, c);
        } else {
            int il = cx-ih, ir = cx+ih+1;
            int lr = il < rr ? il : rr, rl = ir > l ? ir : l;
            if (l < lr) gfx_paint_span(b->pixels + sy*b->stride + l, lr-l, c);
            if (rl < rr) gfx_paint_span(b->pixels + sy*b->stride + rl, rr-rl, c);
        }
    }
}

void gfx_render_line(Buffer *b, float x1, float y1, float x2, float y2, float th, uint32_t c) {
    if (!b || !isfinite(x1+y1+x2+y2+th) || th <= 0) return;
    float dx = x2-x1, dy = y2-y1, len2 = dx*dx + dy*dy;
    float rad = th * 0.5f, rad2 = rad * rad;
    if (len2 <= 0.0001f) { gfx_render_circle(b, x1, y1, rad, c); return; }
    float minx = (x1 < x2 ? x1 : x2) - rad;
    float maxx = (x1 > x2 ? x1 : x2) + rad;
    float miny = (y1 < y2 ? y1 : y2) - rad;
    float maxy = (y1 > y2 ? y1 : y2) + rad;
    int left = gfx_cl_floor(floorf(minx), b->width);
    int right = gfx_cl_ceil(ceilf(maxx), b->width);
    int top = gfx_cl_floor(floorf(miny), b->height);
    int bottom = gfx_cl_ceil(ceilf(maxy), b->height);
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
                *pixel = gfx_blend(*pixel, c);
            }
        }
    }
}
