/* graphics/ttf/ttf_font.c — baking the glyph atlas from a TTF.
 * Outline parsing lives in ttf_outline.c; rasterization, cmap and metrics here. */
#include "../gfx_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Fast scanline rasterization (even-odd): for each sample row collect the
 * edge intersections, sort them and fill the spans. This replaces the old
 * 16 point-in-polygon tests per pixel, which made atlas baking take seconds
 * and left a black screen hanging at startup. */
static void raster_glyph(Font *f, const FontContour *flat, int fc,
                         int mnx, int mxy, int ax, int ay, int w, int h) {
    int *accum = (int *)calloc((size_t)w * h, sizeof(int));
    float xs[128];
    if (!accum) return;
    float sxs = f->scale * FONT_SS;
    int rows = h * FONT_SS, max_i = w * FONT_SS - 1;
    for (int row = 0; row < rows; row++) {
        float fy = (float)mxy - ((float)row + 0.5f) / sxs;
        int nx = 0;
        for (int cn = 0; cn < fc; cn++) {
            const FontContour *poly = &flat[cn];
            for (int k = 0, j = poly->count-1; k < poly->count; j = k++) {
                float yi = poly->points[k].y, yj = poly->points[j].y;
                if ((yi > fy) == (yj > fy)) continue;
                float xh = poly->points[j].x +
                    (poly->points[k].x - poly->points[j].x) * (fy - yj) / (yi - yj);
                if (nx < (int)(sizeof(xs)/sizeof(xs[0])))
                    xs[nx++] = (xh - (float)mnx) * sxs;
            }
        }
        for (int a = 1; a < nx; a++) {
            float v = xs[a]; int b = a;
            while (b > 0 && xs[b-1] > v) { xs[b] = xs[b-1]; b--; }
            xs[b] = v;
        }
        int py = row / FONT_SS;
        for (int k = 0; k + 1 < nx; k += 2) {
            int i0 = (int)ceilf(xs[k] - 0.5f), i1 = (int)floorf(xs[k+1] - 0.5f);
            if (i0 < 0) i0 = 0;
            if (i1 > max_i) i1 = max_i;
            for (int i = i0; i <= i1; i++) accum[py*w + i/FONT_SS]++;
        }
    }
    for (int py = 0; py < h; py++)
        for (int px = 0; px < w; px++)
            f->alpha[(size_t)(ay+py) * f->aw + (ax+px)] =
                (uint8_t)((accum[py*w+px]*255)/(FONT_SS*FONT_SS));
    free(accum);
}

int ttf_glyph_metrics(const Font *f, int g, int *adv, int *lsb) {
    if (!f || g < 0 || g >= f->ng || !f->hmtx) return 0;
    int m = g < f->nhm ? g : f->nhm - 1;
    size_t o = (size_t)f->hmtx + (size_t)m*4;
    if (adv) *adv = ttf_u16(f, o);
    if (lsb) {
        size_t lo = g < f->nhm ? o+2 : (size_t)f->hmtx + (size_t)f->nhm*4 + (size_t)(g-f->nhm)*2;
        *lsb = ttf_s16(f, lo);
    }
    return 1;
}

static int cmap4_lk(const Font *f, uint32_t cp) {
    if (!f->cmap4 || cp > 0xFFFF) return 0;
    size_t b = f->cmap4;
    uint16_t sc = ttf_u16(f, b+6)/2;
    for (uint16_t i = 0; i < sc; i++) {
        uint16_t en = ttf_u16(f, b+14+i*2);
        uint16_t st = ttf_u16(f, b+16+sc*2+i*2);
        if (cp < st || cp > en) continue;
        int16_t dlt = ttf_s16(f, b+16+sc*4+i*2);
        uint16_t rng = ttf_u16(f, b+16+sc*6+i*2);
        if (rng == 0) return ((int)cp + dlt) & 0xFFFF;
        size_t ga = b+16+sc*6+i*2 + rng + (cp-st)*2;
        uint16_t g = ttf_u16(f, ga);
        return g ? ((int)g + dlt) & 0xFFFF : 0;
    }
    return 0;
}

static int cmap12_lk(const Font *f, uint32_t cp) {
    if (!f->cmap12) return 0;
    size_t b = f->cmap12;
    uint32_t n = ttf_u32(f, b+12);
    for (uint32_t i = 0; i < n; i++) {
        size_t at = b+16+i*12;
        uint32_t f1 = ttf_u32(f, at), l1 = ttf_u32(f, at+4);
        if (cp >= f1 && cp <= l1) return (int)(ttf_u32(f, at+8) + cp - f1);
    }
    return 0;
}

int ttf_glyph_for_cp(const Font *f, uint32_t cp) {
    int g = cmap12_lk(f, cp);
    if (!g) g = cmap4_lk(f, cp);
    if (g < 0 || g >= f->ng) g = 0;
    return g;
}

static int bake_glyph(Font *f, FontGlyph *g, int ax, int ay, int rh) {
    int gi = ttf_glyph_for_cp(f, g->codepoint);
    int adv_u = 0;
    FontOutline ol;
    int mnx=0, mxx=0, mny=0, mxy=0, has=0;
    int st = 0;
    int w, h;
    FontContour *flat = NULL;
    int fc = 0;
    float s = f->scale;
    ttf_glyph_metrics(f, gi, &adv_u, NULL);
    g->advance = adv_u * s;
    g->bearing_x = 0; g->bearing_top = 0;
    g->width = 0; g->height = 0;
    g->u0 = g->u1 = (float)ax / f->aw;
    g->v0 = g->v1 = (float)ay / f->ah;
    ttf_outline_init(&ol);
    if (!ttf_read_outline(f, gi, 0, 1, 0, 0, 1, 0, 0, &ol)) { ttf_outline_free(&ol); return rh; }
    for (int cn = 0; cn < ol.cc; cn++) {
        int en = ol.ends[cn];
        for (int p = st; p <= en; p++) {
            int x = (int)lrintf(ol.points[p].x);
            int y = (int)lrintf(ol.points[p].y);
            if (!has || x < mnx) mnx = x;
            if (!has || x > mxx) mxx = x;
            if (!has || y < mny) mny = y;
            if (!has || y > mxy) mxy = y;
            has = 1;
        }
        st = en + 1;
    }
    if (!has) { ttf_outline_free(&ol); return rh; }
    w = (int)ceilf((mxx-mnx)*s) + 2; h = (int)ceilf((mxy-mny)*s) + 2;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    g->bearing_x = mnx * s; g->bearing_top = mxy * s;
    g->width = w; g->height = h;
    flat = (FontContour *)calloc((size_t)ol.cc, sizeof(*flat));
    if (!flat) { ttf_outline_free(&ol); return rh; }
    st = 0;
    for (int cn = 0; cn < ol.cc; cn++) {
        int en = ol.ends[cn];
        if (!ttf_flatten(ol.points + st, en - st + 1, &flat[fc])) {
            for (int i = 0; i <= fc; i++) free(flat[i].points);
            free(flat); ttf_outline_free(&ol); return rh;
        }
        fc++; st = en + 1;
    }
    raster_glyph(f, flat, fc, mnx, mxy, ax, ay, w, h);
    g->u0 = (float)ax / f->aw;       g->v0 = (float)ay / f->ah;
    g->u1 = (float)(ax+w) / f->aw;   g->v1 = (float)(ay+h) / f->ah;
    for (int cn = 0; cn < fc; cn++) free(flat[cn].points);
    free(flat); ttf_outline_free(&ol);
    return h > rh ? h : rh;
}

static int init_tables(Font *f) {
    uint32_t l;
    if (!ttf_table_bound(f, TTF_TAG('h','e','a','d'), &f->head, &l) || l < 54 ||
        !ttf_table_bound(f, TTF_TAG('h','h','e','a'), &f->hhea, &l) || l < 36 ||
        !ttf_table_bound(f, TTF_TAG('h','m','t','x'), &f->hmtx, &l) ||
        !ttf_table_bound(f, TTF_TAG('m','a','x','p'), &f->maxp, &l) || l < 6 ||
        !ttf_table_bound(f, TTF_TAG('l','o','c','a'), &f->loca, &l) ||
        !ttf_table_bound(f, TTF_TAG('g','l','y','f'), &f->glyf, &l) ||
        !ttf_table_bound(f, TTF_TAG('c','m','a','p'), &f->cmap, &l) || l < 4) return 0;
    f->upem = ttf_u16(f, f->head+18);
    f->loc_format = ttf_s16(f, f->head+50);
    f->ng = ttf_u16(f, f->maxp+4);
    f->nhm = ttf_u16(f, f->hhea+34);
    f->asc_u = ttf_s16(f, f->hhea+4);
    f->desc_u = ttf_s16(f, f->hhea+6);
    f->lg_u = ttf_s16(f, f->hhea+8);
    if (f->upem <= 0 || f->ng <= 0 || f->nhm <= 0) return 0;
    uint16_t n = ttf_u16(f, f->cmap+2);
    for (uint16_t i = 0; i < n; i++) {
        size_t r = f->cmap + 4 + (size_t)i*8;
        uint16_t p = ttf_u16(f, r), e = ttf_u16(f, r+2);
        uint32_t sub = f->cmap + ttf_u32(f, r+4);
        uint16_t fmt = ttf_u16(f, sub);
        if (fmt == 12 && (p == 3 || p == 0)) {
            if (!f->cmap12 || (p == 3 && e == 10)) f->cmap12 = sub;
        } else if (fmt == 4 && (p == 3 || p == 0)) {
            if (!f->cmap4 || (p == 3 && e == 1)) f->cmap4 = sub;
        }
    }
    return f->cmap4 || f->cmap12;
}

static int add_cp(Font *f, uint32_t cp) {
    for (int i = 0; i < f->gcount; i++) if (f->glyphs[i].codepoint == cp) return 1;
    if (f->gcount >= FONT_MAX) return 0;
    f->glyphs[f->gcount++].codepoint = cp;
    return 1;
}

static int add_utf8(Font *f, const char *text) {
    const uint8_t *p = (const uint8_t *)text;
    while (p && *p) {
        uint32_t cp;
        if (*p < 0x80) { cp = *p++; }
        else if ((*p & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
            cp = ((uint32_t)(p[0] & 0x1f) << 6) | (uint32_t)(p[1] & 0x3f); p += 2;
        } else if ((*p & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
            cp = ((uint32_t)(p[0] & 0x0f) << 12) | ((uint32_t)(p[1] & 0x3f) << 6) |
                 (uint32_t)(p[2] & 0x3f); p += 3;
        } else if ((*p & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 &&
                   (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) {
            cp = ((uint32_t)(p[0] & 7) << 18) | ((uint32_t)(p[1] & 0x3f) << 12) |
                 ((uint32_t)(p[2] & 0x3f) << 6) | (uint32_t)(p[3] & 0x3f); p += 4;
        } else { p++; continue; }
        if (!add_cp(f, cp)) return 0;
    }
    return 1;
}

Font *font_create(const uint8_t *data, size_t size, int ph) {
    if (!data || size < 12 || ph <= 0 || ph > 256) return NULL;
    Font *f = (Font *)calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->data = (uint8_t *)malloc(size);
    f->glyphs = (FontGlyph *)calloc(FONT_MAX, sizeof(*f->glyphs));
    f->alpha = (uint8_t *)calloc((size_t)FONT_ATLAS_W * FONT_ATLAS_H, 1);
    if (!f->data || !f->glyphs || !f->alpha) { font_destroy(f); return NULL; }
    memcpy(f->data, data, size);
    f->size = size;
    f->aw = FONT_ATLAS_W; f->ah = FONT_ATLAS_H;
    if (!init_tables(f)) { font_destroy(f); return NULL; }
    f->scale = (float)ph / f->upem;
    f->ascent = f->asc_u * f->scale;
    f->line_h = (f->asc_u - f->desc_u + f->lg_u) * f->scale;
    if (f->line_h < ph) f->line_h = (float)ph;
    for (int cp = 32; cp <= 126; cp++) add_cp(f, (uint32_t)cp);
    for (int cp = 0x0410; cp <= 0x044F; cp++) add_cp(f, (uint32_t)cp);
    add_cp(f, 0x0401); add_cp(f, 0x0451);
    /* The interface symbols the game can print (all present in this font):
     * middle dot, dashes, degree sign, plus-minus, list marker, status check
     * and typographic quotes. CJK was removed from the game long ago, so we
     * no longer bake hiragana/katakana/kanji — a smaller atlas and a faster
     * start. Any character missing from the set still becomes a question
     * mark below. */
    if (!add_utf8(f, "·—–°±•✓…→„“”‘’")) {
        font_destroy(f); return NULL;
    }
    add_cp(f, '?');
    int ax = 1, ay = 1, rh = 0;
    for (int i = 0; i < f->gcount; i++) {
        FontGlyph *gl = &f->glyphs[i];
        int gw = 0;
        int gi = ttf_glyph_for_cp(f, gl->codepoint);
        FontOutline ol; ttf_outline_init(&ol);
        int st = 0, mnx = 0, mxx = 0, has = 0;
        if (ttf_read_outline(f, gi, 0, 1, 0, 0, 1, 0, 0, &ol)) {
            for (int cn = 0; cn < ol.cc; cn++) {
                int en = ol.ends[cn];
                for (int p = st; p <= en; p++) {
                    int x = (int)lrintf(ol.points[p].x);
                    if (!has || x < mnx) mnx = x;
                    if (!has || x > mxx) mxx = x;
                    has = 1;
                }
                st = en + 1;
            }
            if (has) gw = (int)ceilf((mxx - mnx) * f->scale) + 2;
        }
        ttf_outline_free(&ol);
        if (gw < 1) gw = 1;
        if (ax + gw + 1 >= f->aw) { ax = 1; ay += rh + 1; rh = 0; }
        if (ay + ph + 2 >= f->ah) { font_destroy(f); return NULL; }
        int nh = bake_glyph(f, gl, ax, ay, rh);
        ax += gl->width + 1;
        if (nh > rh) rh = nh;
    }
    return f;
}

void font_destroy(Font *f) {
    if (!f) return;
    free(f->data); free(f->alpha); free(f->glyphs); free(f);
}

const FontGlyph *font_glyph(const Font *f, uint32_t cp) {
    if (!f) return NULL;
    const FontGlyph *fb = NULL;
    for (int i = 0; i < f->gcount; i++) {
        if (f->glyphs[i].codepoint == cp) return &f->glyphs[i];
        if (f->glyphs[i].codepoint == '?') fb = &f->glyphs[i];
    }
    return fb;
}

int font_aw(const Font *f) { return f ? f->aw : 0; }
int font_ah(const Font *f) { return f ? f->ah : 0; }
const uint8_t *font_alpha(const Font *f) { return f ? f->alpha : NULL; }
float font_lineh(const Font *f) { return f ? f->line_h : 0; }
float font_ascent(const Font *f) { return f ? f->ascent : 0; }
