/* graphics/gfx_text.c — text: font loading, metrics, glyph rasterization
 * and the error screen.
 *
 * Anti-aliasing: glyphs are baked into the atlas larger (48px instead of 32)
 * and sampled bilinearly when drawn, not nearest-pixel — text stays smooth
 * at any scale (see atlas_sample). */
#include "gfx_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static Font *font;
static int font_tried;

/* The pixel height the font atlas is baked at. It used to be 32 — at text
 * scales 0.4...0.6 (typical UI) that left 13-19 pixels and the glyphs fell
 * apart into pixel staircases. 48 keeps detail even for large labels. */
#define GFX_FONT_PIXEL_HEIGHT 48

int gfx_font_ensure(void) {
    if (font) return 1;
    if (font_tried) return 0;
    font_tried = 1;
    uint8_t *data = NULL; size_t sz = 0;
    if (!asset_read(gfx_assets(), "fonts/ChillRoundGothic_Heavy.ttf", &data, &sz)) {
        app_log_error("font not loaded: fonts/ChillRoundGothic_Heavy.ttf not found");
        return 0;
    }
    font = font_create(data, sz, GFX_FONT_PIXEL_HEIGHT);
    free(data);
    if (!font) { app_log_error("font not loaded: could not parse TrueType font"); return 0; }
    app_log("font loaded: fonts/ChillRoundGothic_Heavy.ttf");
    return 1;
}

void gfx_font_release(void) {
    font_destroy(font); font = NULL; font_tried = 0;
}

int gfx_utf8_decode(const char **c) {
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

/* Bilinear sampling of the alpha atlas, clamped to the glyph box so
 * neighbouring glyphs cannot bleed into the edges. */
static uint8_t atlas_sample(const uint8_t *al, int aw, int ah, float x, float y,
                            float minx, float miny, float maxx, float maxy) {
    if (x < minx) x = minx;
    if (y < miny) y = miny;
    if (x > maxx) x = maxx;
    if (y > maxy) y = maxy;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    int x1 = x0 + 1, y1 = y0 + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= aw) x1 = aw - 1;
    if (y1 >= ah) y1 = ah - 1;
    float fx = x - (float)x0, fy = y - (float)y0;
    float a00 = (float)al[y0*aw + x0], a10 = (float)al[y0*aw + x1];
    float a01 = (float)al[y1*aw + x0], a11 = (float)al[y1*aw + x1];
    float top = a00 + (a10 - a00) * fx;
    float bot = a01 + (a11 - a01) * fx;
    float v = top + (bot - top) * fy;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)(v + 0.5f);
}

void gfx_render_text(Buffer *b, const char *s, float x, float y, uint32_t c, float sc) {
    if (!b || !font || !s || !isfinite(x+y+sc) || sc <= 0) return;
    int aw = font_aw(font), ah = font_ah(font);
    const uint8_t *al = font_alpha(font);
    float asc = font_ascent(font), lb = 0;
    const FontGlyph *ref = font_glyph(font, 'S');
    if (ref) { asc = ref->bearing_top; lb = ref->bearing_x; }
    float pen = x - lb*sc, base = y + asc*sc;
    float inv = 1.0f / sc;
    for (const char *cur = s; *cur;) {
        int cp = gfx_utf8_decode(&cur);
        if (cp == '\n') { pen = x - lb*sc; base += font_lineh(font)*sc; continue; }
        const FontGlyph *g = font_glyph(font, (uint32_t)cp);
        if (!g || g->width <= 0 || g->height <= 0) { if (g) pen += g->advance * sc; continue; }
        float sx0 = g->u0*aw, sy0 = g->v0*ah;
        float sx1 = g->u1*aw, sy1 = g->v1*ah;
        int dw = (int)ceilf(g->width*sc), dh = (int)ceilf(g->height*sc);
        int dx = (int)floorf(pen + g->bearing_x*sc), dy = (int)floorf(base - g->bearing_top*sc);
        for (int yy = 0; yy < dh; yy++) {
            int scr_y = dy + yy;
            if (scr_y < 0 || scr_y >= b->height) continue;
            /* screen-pixel center in atlas coordinates */
            float fy = sy0 + ((float)yy + 0.5f) * inv - 0.5f;
            for (int xx = 0; xx < dw; xx++) {
                int scr_x = dx + xx;
                if (scr_x < 0 || scr_x >= b->width) continue;
                float fx = sx0 + ((float)xx + 0.5f) * inv - 0.5f;
                uint8_t cov = atlas_sample(al, aw, ah, fx, fy, sx0, sy0, sx1 - 1.0f, sy1 - 1.0f);
                if (cov) {
                    uint32_t ca = (c & 0xffffff) | (((uint32_t)cov * (c>>24) / 255) << 24);
                    b->pixels[scr_y*b->stride + scr_x] = gfx_blend(b->pixels[scr_y*b->stride + scr_x], ca);
                }
            }
        }
        pen += g->advance * sc;
    }
}

/* HUD strings are consumed immediately; no per-frame copies or queue. */
void text_scaled(const char *s,float x,float y,uint32_t color,float scale) {
    if (!s || !gfx_current_buffer() || !gfx_font_ensure()) return;
    gfx_render_text(gfx_current_buffer(),s,x,y,gfx_pack(color),scale);
}

int text_width(const char *s) {
    if (!s || !gfx_font_ensure()) return 0;
    const FontGlyph *ref = font_glyph(font, 'S');
    float pen = -(ref ? ref->bearing_x : 0);
    int first = 1; float minL = 0, maxR = 0;
    for (const char *c = s; *c;) {
        int cp = gfx_utf8_decode(&c);
        const FontGlyph *g = font_glyph(font, (uint32_t)cp);
        if (!g) continue;
        float dl = pen + g->bearing_x, dr = dl + g->width;
        if (first || dl < minL) minL = dl;
        if (first || dr > maxR) maxR = dr;
        first = 0; pen += g->advance;
    }
    return first ? 0 : (int)(maxR - minL + 0.5f);
}

/* Recoverable engine error, not an interpreter console. */
void gfx_error_screen(const char *message) {
    Buffer *b=gfx_current_buffer();
    if (!b) return;
    gfx_clear(b,gfx_pack(0xFF201A1Au));
    if (!font) return;
    gfx_render_text(b,"Enjoer — error",16,16,gfx_pack(0xFFFFFFFFu),.7f);
    if (message) gfx_render_text(b,message,16,56,gfx_pack(0xFFFFB0B0u),.45f);
}
