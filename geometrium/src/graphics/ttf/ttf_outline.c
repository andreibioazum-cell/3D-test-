/* graphics/ttf/ttf_outline.c — reading the binary TTF tables and glyph
 * outlines (glyf: simple and composite), expanding curves into polygons.
 * Atlas baking lives in ttf_font.c. */
#include "../gfx_internal.h"
#include <stdlib.h>
#include <string.h>

int ttf_in_range(const Font *f, size_t o, size_t l) {
    return f && o <= f->size && l <= f->size - o;
}

uint16_t ttf_u16(const Font *f, size_t o) {
    if (!ttf_in_range(f, o, 2)) return 0;
    return (uint16_t)((f->data[o]<<8)|f->data[o+1]);
}

int16_t ttf_s16(const Font *f, size_t o) { return (int16_t)ttf_u16(f, o); }

uint32_t ttf_u32(const Font *f, size_t o) {
    if (!ttf_in_range(f, o, 4)) return 0;
    return (uint32_t)f->data[o]<<24|(uint32_t)f->data[o+1]<<16|
           (uint32_t)f->data[o+2]<<8|(uint32_t)f->data[o+3];
}

int ttf_table_bound(const Font *f, uint32_t tag, uint32_t *off, uint32_t *len) {
    if (!f || !ttf_in_range(f, 0, 12)) return 0;
    uint16_t n = ttf_u16(f, 4);
    for (size_t i = 0; i < n; i++) {
        size_t r = 12 + i*16;
        if (!ttf_in_range(f, r, 16)) return 0;
        if (ttf_u32(f, r) == tag) {
            uint32_t a = ttf_u32(f, r+8), l = ttf_u32(f, r+12);
            if (!ttf_in_range(f, a, l)) return 0;
            if (off) *off = a;
            if (len) *len = l;
            return 1;
        }
    }
    return 0;
}

void ttf_outline_init(FontOutline *o) { memset(o, 0, sizeof(*o)); }

void ttf_outline_free(FontOutline *o) {
    if (!o) return;
    free(o->points); free(o->ends); memset(o, 0, sizeof(*o));
}

int ttf_outline_reserve_points(FontOutline *o, int n) {
    int need = o->pc + n, cap = o->pp ? o->pp : 32;
    while (cap < need) { if (cap > 1e6) return 0; cap *= 2; }
    FontPoint *p = (FontPoint *)realloc(o->points, (size_t)cap*sizeof(*p));
    if (!p) return 0;
    o->points = p; o->pp = cap;
    return 1;
}

int ttf_outline_reserve_contours(FontOutline *o, int n) {
    int need = o->cc + n, cap = o->cp ? o->cp : 8;
    while (cap < need) cap *= 2;
    int *e = (int *)realloc(o->ends, (size_t)cap*sizeof(*e));
    if (!e) return 0;
    o->ends = e; o->cp = cap;
    return 1;
}

int ttf_outline_add(FontOutline *o, const FontPoint *p, int n) {
    if (n <= 0 || !ttf_outline_reserve_points(o, n) || !ttf_outline_reserve_contours(o, 1)) return 0;
    memcpy(o->points + o->pc, p, (size_t)n*sizeof(*p));
    o->pc += n;
    o->ends[o->cc++] = o->pc - 1;
    return 1;
}

int ttf_glyph_offsets(const Font *f, int g, uint32_t *s, uint32_t *e) {
    if (!f || g < 0 || g >= f->ng) return 0;
    uint32_t a, b;
    if (f->loc_format == 0) {
        a = (uint32_t)ttf_u16(f, f->loca + (size_t)g*2) * 2;
        b = (uint32_t)ttf_u16(f, f->loca + (size_t)(g+1)*2) * 2;
    } else {
        a = ttf_u32(f, f->loca + (size_t)g*4);
        b = ttf_u32(f, f->loca + (size_t)(g+1)*4);
    }
    if (a > b || !ttf_in_range(f, (size_t)f->glyf + a, b - a)) return 0;
    if (s) *s = a;
    if (e) *e = b;
    return 1;
}

static int read_simple(const Font *f, uint32_t off, int nc, float a, float b, float c, float d,
                       float tx, float ty, FontOutline *dst) {
    if (nc <= 0 || nc > 4096) return 1;
    int *ends = (int *)malloc((size_t)nc*sizeof(*ends));
    if (!ends) return 0;
    for (int i = 0; i < nc; i++) ends[i] = (int)ttf_u16(f, (size_t)f->glyf + off + 10 + i*2);
    int pc = ends[nc-1] + 1;
    if (pc <= 0 || pc > 200000) { free(ends); return 0; }
    size_t cur = (size_t)f->glyf + off + 10 + (size_t)nc*2;
    int ilen = ttf_u16(f, cur); cur += 2 + ilen;
    if (!ttf_in_range(f, cur, 1)) { free(ends); return 0; }
    int *flags = (int *)malloc((size_t)pc*sizeof(*flags));
    FontPoint *pts = (FontPoint *)calloc((size_t)pc, sizeof(*pts));
    if (!flags || !pts) { free(ends); free(flags); free(pts); return 0; }
    int p = 0;
    while (p < pc) {
        if (!ttf_in_range(f, cur, 1)) goto fail;
        uint8_t fl = f->data[cur++];
        flags[p++] = fl;
        int rep = (fl & 8) ? (int)f->data[cur++] : 0;
        while (rep-- > 0 && p < pc) flags[p++] = fl;
    }
    int x = 0;
    for (p = 0; p < pc; p++) {
        int fl = flags[p], dlt = 0;
        if (fl & 2) { if (!ttf_in_range(f, cur, 1)) goto fail; dlt = f->data[cur++];
        if (!(fl & 16)) dlt = -dlt; }
        else if (!(fl & 16)) { if (!ttf_in_range(f, cur, 2)) goto fail; dlt = ttf_s16(f, cur); cur += 2; }
        x += dlt; pts[p].x = (float)x; pts[p].on_curve = (fl & 1) != 0;
    }
    int y = 0;
    for (p = 0; p < pc; p++) {
        int fl = flags[p], dlt = 0;
        if (fl & 4) { if (!ttf_in_range(f, cur, 1)) goto fail; dlt = f->data[cur++];
        if (!(fl & 32)) dlt = -dlt; }
        else if (!(fl & 32)) { if (!ttf_in_range(f, cur, 2)) goto fail; dlt = ttf_s16(f, cur); cur += 2; }
        y += dlt; pts[p].y = (float)y;
    }
    int st = 0;
    for (int cn = 0; cn < nc; cn++) {
        int en = ends[cn], cnt = en - st + 1;
        FontPoint *tr = (FontPoint *)malloc((size_t)cnt*sizeof(*tr));
        if (!tr) goto fail;
        for (int i = 0; i < cnt; i++) {
            FontPoint in = pts[st + i];
            tr[i].x = a*in.x + c*in.y + tx;
            tr[i].y = b*in.x + d*in.y + ty;
            tr[i].on_curve = in.on_curve;
        }
        if (!ttf_outline_add(dst, tr, cnt)) { free(tr); goto fail; }
        free(tr); st = en + 1;
    }
    free(ends); free(flags); free(pts);
    return 1;
fail:
    free(ends); free(flags); free(pts); return 0;
}

static int read_composite(const Font *f, uint32_t off, int depth, float a, float b, float c, float d,
                          float tx, float ty, FontOutline *dst) {
    size_t cur = (size_t)f->glyf + off + 10;
    int flags = TTF_MORE;
    while (flags & TTF_MORE) {
        if (!ttf_in_range(f, cur, 4)) return 0;
        flags = ttf_u16(f, cur);
        int comp = ttf_u16(f, cur + 2);
        cur += 4;
        int a1, a2;
        if (flags & TTF_ARG_WORDS) { a1 = ttf_s16(f, cur); a2 = ttf_s16(f, cur+2); cur += 4; }
        else { a1 = (int8_t)f->data[cur]; a2 = (int8_t)f->data[cur+1]; cur += 2; }
        float ca=1, cb=0, cc=0, cd=1, dx=0, dy=0;
        if (flags & TTF_ARG_XY) { dx = (float)a1; dy = (float)a2; }
        if (flags & TTF_HAVE_SCALE) {
            int16_t s = ttf_s16(f, cur); ca = cd = (float)s/16384.0f; cur += 2;
        } else if (flags & TTF_HAVE_XY_SCALE) {
            ca = (float)ttf_s16(f, cur)/16384.0f; cd = (float)ttf_s16(f, cur+2)/16384.0f; cur += 4;
        } else if (flags & TTF_HAVE_2X2) {
            ca = (float)ttf_s16(f, cur)/16384.0f;   cb = (float)ttf_s16(f, cur+2)/16384.0f;
            cc = (float)ttf_s16(f, cur+4)/16384.0f; cd = (float)ttf_s16(f, cur+6)/16384.0f; cur += 8;
        }
        float na = a*ca + c*cb, nb = b*ca + d*cb, nc = a*cc + c*cd, nd = b*cc + d*cd;
        float ntx = a*dx + c*dy + tx, nty = b*dx + d*dy + ty;
        if (!ttf_read_outline(f, comp, depth+1, na, nb, nc, nd, ntx, nty, dst)) return 0;
    }
    return 1;
}

int ttf_read_outline(const Font *f, int g, int depth, float a, float b, float c, float d,
                     float tx, float ty, FontOutline *dst) {
    uint32_t s, e;
    if (depth > 16 || !ttf_glyph_offsets(f, g, &s, &e)) return 0;
    if (s == e) return 1;
    if (!ttf_in_range(f, (size_t)f->glyf + s, 10)) return 0;
    int16_t cc = ttf_s16(f, (size_t)f->glyf + s);
    if (cc >= 0) return read_simple(f, s, cc, a, b, c, d, tx, ty, dst);
    return read_composite(f, s, depth, a, b, c, d, tx, ty, dst);
}

static int flat_rsrv(FontContour *f, int n) {
    int need = f->count + n, cap = f->cap ? f->cap : 32;
    while (cap < need) cap *= 2;
    FontFPoint *p = (FontFPoint *)realloc(f->points, (size_t)cap*sizeof(*p));
    if (!p) return 0;
    f->points = p; f->cap = cap;
    return 1;
}

static int flat_push(FontContour *f, float x, float y) {
    if (!flat_rsrv(f, 1)) return 0;
    f->points[f->count].x = x; f->points[f->count].y = y; f->count++;
    return 1;
}

static int flat_q(FontContour *f, FontFPoint from, FontPoint c, FontFPoint to) {
    for (int s = 1; s <= 8; s++) {
        float t = (float)s/8.0f, u = 1-t;
        if (!flat_push(f,
            u*u*from.x + 2*u*t*c.x + t*t*to.x,
            u*u*from.y + 2*u*t*c.y + t*t*to.y)) return 0;
    }
    return 1;
}

int ttf_flatten(const FontPoint *p, int n, FontContour *flat) {
    if (n <= 0) return 1;
    int first_on = p[0].on_curve, idx, proc;
    FontFPoint start, cur;
    if (first_on) { start.x=p[0].x; start.y=p[0].y; idx=1; proc=1; }
    else if (p[n-1].on_curve) { start.x=p[n-1].x; start.y=p[n-1].y; idx=0; proc=0; }
    else { start.x=(p[n-1].x+p[0].x)*0.5f; start.y=(p[n-1].y+p[0].y)*0.5f; idx=0; proc=0; }
    cur = start;
    if (!flat_push(flat, start.x, start.y)) return 0;
    while (proc < n) {
        const FontPoint *one = &p[idx % n];
        if (one->on_curve) {
            cur.x=one->x; cur.y=one->y;
            if (!flat_push(flat, cur.x, cur.y)) return 0;
            idx++; proc++;
        } else {
            const FontPoint *two = &p[(idx+1) % n];
            FontFPoint end;
            if (two->on_curve) { end.x=two->x; end.y=two->y; idx+=2; proc+=2; }
            else { end.x=(one->x+two->x)*0.5f; end.y=(one->y+two->y)*0.5f; idx++; proc++; }
            if (!flat_q(flat, cur, *one, end)) return 0;
            cur = end;
        }
    }
    if (fabsf(cur.x-start.x) > 0.001f || fabsf(cur.y-start.y) > 0.001f) {
        if (!flat_push(flat, start.x, start.y)) return 0;
    }
    return 1;
}
