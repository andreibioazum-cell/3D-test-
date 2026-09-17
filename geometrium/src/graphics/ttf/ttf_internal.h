/* graphics/ttf/ttf_internal.h — shared interface of the minimal TTF engine.
 * The outline parser lives in ttf_outline.c, atlas baking in ttf_font.c. */
#ifndef TTF_INTERNAL_H
#define TTF_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct Font Font;

typedef struct {
    uint32_t codepoint;
    float advance;
    float bearing_x;
    float bearing_top;
    int width;
    int height;
    float u0, v0, u1, v1;
} FontGlyph;

/* ttf_font.c */
Font *font_create(const uint8_t *data, size_t size, int pixel_height);
void font_destroy(Font *font);
const FontGlyph *font_glyph(const Font *font, uint32_t cp);
int font_aw(const Font *font);
int font_ah(const Font *font);
const uint8_t *font_alpha(const Font *font);
float font_lineh(const Font *font);
float font_ascent(const Font *font);

/* ── internals for ttf_outline.c / ttf_font.c ────────────────────────── */
#define FONT_ATLAS_W 1024
#define FONT_ATLAS_H 2048
#define FONT_SS 4
#define FONT_MAX 640

#define TTF_ARG_WORDS    0x0001
#define TTF_ARG_XY       0x0002
#define TTF_HAVE_SCALE   0x0008
#define TTF_MORE         0x0020
#define TTF_HAVE_XY_SCALE 0x0040
#define TTF_HAVE_2X2     0x0080

#define TTF_TAG(a,b,c,d) ((uint32_t)(a)<<24|(uint32_t)(b)<<16|(uint32_t)(c)<<8|(uint32_t)(d))

typedef struct { float x, y; int on_curve; } FontPoint;

typedef struct {
    FontPoint *points; int pc, pp;
    int *ends;     int cc, cp;
} FontOutline;

typedef struct { float x, y; } FontFPoint;
typedef struct { FontFPoint *points; int count, cap; } FontContour;

struct Font {
    uint8_t *data; size_t size;
    int upem, loc_format, ng, nhm, asc_u, desc_u, lg_u;
    uint32_t head, hhea, hmtx, maxp, loca, glyf, cmap, cmap4, cmap12;
    float scale, ascent, line_h;
    int aw, ah;
    uint8_t *alpha;
    FontGlyph *glyphs; int gcount;
};

/* binary table reads (safe, bounds-checked) */
int ttf_in_range(const Font *f, size_t off, size_t len);
uint16_t ttf_u16(const Font *f, size_t off);
int16_t ttf_s16(const Font *f, size_t off);
uint32_t ttf_u32(const Font *f, size_t off);
int ttf_table_bound(const Font *f, uint32_t tag, uint32_t *off, uint32_t *len);
int ttf_glyph_offsets(const Font *f, int glyph, uint32_t *start, uint32_t *end);
int ttf_glyph_metrics(const Font *f, int glyph, int *advance, int *lsb);
int ttf_glyph_for_cp(const Font *f, uint32_t cp);

/* outlines */
void ttf_outline_init(FontOutline *o);
void ttf_outline_free(FontOutline *o);
int ttf_outline_reserve_points(FontOutline *o, int n);
int ttf_outline_reserve_contours(FontOutline *o, int n);
int ttf_outline_add(FontOutline *o, const FontPoint *p, int n);
int ttf_read_outline(const Font *f, int glyph, int depth,
                     float a, float b, float c, float d,
                     float tx, float ty, FontOutline *dst);
int ttf_flatten(const FontPoint *p, int n, FontContour *flat);

#endif
