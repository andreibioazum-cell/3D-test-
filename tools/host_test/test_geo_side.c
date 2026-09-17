/* Обёртка над новым тесселятором (native/graphics/geometry.inc) для
 * хост-теста эквивалентности со старым программным растеризатором.
 * Компилируется отдельным трансляционным юнитом: у legacy-растеризатора и
 * новой геометрии общие имена статических функций, поэтому они не могут
 * жить в одном .c. */
#include "native/graphics/types.inc"
#include "native/graphics/geometry.inc"

/* Тестовый шрифт: один глиф с фиксированными метриками - достаточно, чтобы
 * проверить раскладку пера (позиции квадов) по сравнению с формулой
 * прежнего render_text_now. Структура завершается здесь собственной
 * раскладкой - в этом TU аксессоры шрифта тестовые. */
struct DSFont {
    int aw, ah;
    float ascent, line_h;
    DSFontGlyph *glyphs;
    int gcount;
};
static DSFontGlyph test_glyphs[4];
static DSFont test_font_storage;
static int test_font_ready(void) {
    static int done = 0;
    if (done) return 1;
    done = 1;
    test_font_storage.aw = 64; test_font_storage.ah = 64;
    test_font_storage.ascent = 30.0f; test_font_storage.line_h = 40.0f;
    /* 'S' - опорный глиф: bearing_top=30, bearing_x=2 */
    test_glyphs[0] = (DSFontGlyph){ 'S', 20.0f, 2.0f, 30.0f, 14, 30, 0.0f, 0.0f, 14.0f/64.0f, 30.0f/64.0f };
    test_glyphs[1] = (DSFontGlyph){ 'A', 18.0f, 1.0f, 28.0f, 16, 28, 16.0f/64.0f, 0.0f, 32.0f/64.0f, 28.0f/64.0f };
    test_glyphs[2] = (DSFontGlyph){ '?', 16.0f, 1.0f, 28.0f, 14, 28, 32.0f/64.0f, 0.0f, 46.0f/64.0f, 28.0f/64.0f };
    test_font_storage.glyphs = test_glyphs;
    test_font_storage.gcount = 3;
    return 1;
}
const DSFontGlyph *ds_font_glyph(const DSFont *f, uint32_t cp) {
    if (f != &test_font_storage) return NULL;
    for (int i = 0; i < 3; i++) if (test_glyphs[i].codepoint == cp) return &test_glyphs[i];
    return &test_glyphs[2]; /* '?' - замена отсутствующих */
}
int ds_font_aw(const DSFont *f) { return f ? f->aw : 0; }
int ds_font_ah(const DSFont *f) { return f ? f->ah : 0; }
const uint8_t *ds_font_alpha(const DSFont *f) { (void)f; return NULL; }
float ds_font_lineh(const DSFont *f) { return f ? f->line_h : 0; }
float ds_font_ascent(const DSFont *f) { return f ? f->ascent : 0; }

/* --- обёртки для теста --- */

void geo_side_reset(void) { geo_reset(); }
size_t geo_side_vert_count(void) { return geo_vn; }
size_t geo_side_index_count(void) { return geo_in; }
const GeoVert *geo_side_verts(void) { return geo_verts; }
const uint16_t *geo_side_indices(void) { return geo_idx; }

int geo_side_rect(float x, float y, float w, float h, uint32_t c) { return geo_rect(x, y, w, h, c); }
int geo_side_circle(float x, float y, float r, uint32_t c) { return geo_circle(x, y, r, c); }
int geo_side_ring(float x, float y, float r, float th, uint32_t c) { return geo_ring(x, y, r, th, c); }
int geo_side_line(float x1, float y1, float x2, float y2, float th, uint32_t c) { return geo_line(x1, y1, x2, y2, th, c); }
int geo_side_roundrect(float x, float y, float w, float h, float r, uint32_t c) { return geo_roundrect(x, y, w, h, r, c); }
int geo_side_tex(float x, float y, float a, float sc, float w, float h, uint32_t c) { return geo_tex(x, y, a, sc, w, h, c); }
const DSFont *geo_side_font(void) { test_font_ready(); return &test_font_storage; }
int geo_side_text(const char *s, float x, float y, uint32_t c, float sc) {
    test_font_ready();
    return geo_text(s, x, y, c, sc, &test_font_storage);
}
