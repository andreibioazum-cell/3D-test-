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

/* --- индекс глифов: codepoint -> номер в f->glyphs --- */
#define DS_FONT_HCAP 512
static unsigned ds_font_hash(uint32_t cp) {
    cp ^= cp >> 16; cp *= 0x7feb352du;
    cp ^= cp >> 15; cp *= 0x846ca68bu;
    cp ^= cp >> 16;
    return cp;
}
static int ds_font_lookup(const DSFont *f, uint32_t cp) {
    if (!f->hmap) return -1;
    unsigned mask = (unsigned)f->hcap - 1;
    unsigned i = ds_font_hash(cp) & mask;
    for (unsigned probe = 0; probe < (unsigned)f->hcap; probe++) {
        int v = f->hmap[i];
        if (v == 0) return -1;
        if (f->glyphs[v - 1].codepoint == cp) return v - 1;
        i = (i + 1) & mask;
    }
    return -1;
}
static int ds_font_index_build(DSFont *f) {
    free(f->hmap);
    f->hmap = (int *)calloc(DS_FONT_HCAP, sizeof(int));
    if (!f->hmap) { f->hcap = 0; return 0; }
    f->hcap = DS_FONT_HCAP;
    f->qmark = -1;
    unsigned mask = DS_FONT_HCAP - 1;
    for (int g = 0; g < f->gcount; g++) {
        uint32_t cp = f->glyphs[g].codepoint;
        if (cp == '?') f->qmark = g;
        unsigned i = ds_font_hash(cp) & mask;
        while (f->hmap[i] != 0) i = (i + 1) & mask;
        f->hmap[i] = g + 1;
    }
    return 1;
}

DSFont *ds_font_create(const uint8_t *data, size_t size, int ph) {
    if (!data || size < 12 || ph <= 0 || ph > 256) return NULL;
    DSFont *f = (DSFont *)calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->data = (uint8_t *)malloc(size);
    f->glyphs = (DSFontGlyph *)calloc(DS_FONT_MAX, sizeof(*f->glyphs));
    f->alpha = (uint8_t *)calloc((size_t)DS_FONT_ATLAS_W * DS_FONT_ATLAS_H, 1);
    if (!f->data || !f->glyphs || !f->alpha) { ds_font_destroy(f); return NULL; }
    memcpy(f->data, data, size);
    f->size = size;
    f->aw = DS_FONT_ATLAS_W; f->ah = DS_FONT_ATLAS_H;
    if (!init_tables(f)) { ds_font_destroy(f); return NULL; }
    f->scale = (float)ph / f->upem;
    f->ascent = f->asc_u * f->scale;
    f->line_h = (f->asc_u - f->desc_u + f->lg_u) * f->scale;
    if (f->line_h < ph) f->line_h = (float)ph;
    for (int cp = 32; cp <= 126; cp++) add_cp(f, (uint32_t)cp);
    for (int cp = 0x0410; cp <= 0x044F; cp++) add_cp(f, (uint32_t)cp);
    add_cp(f, 0x0401); add_cp(f, 0x0451);
    /* Символы интерфейса, которые игра выводит на экран (все они есть в этом
     * шрифте): средняя точка, кавычки-«ёлочки», длинное и короткое тире, знак
     * градуса, плюс-минус, маркер списка и галочка статуса. Японский язык
     * из игры убран, поэтому хирагану, катакану и кандзи больше не запекаем -
     * это и уменьшает атлас, и ускоряет запуск. Любой отсутствующий в наборе
     * символ по-прежнему заменяется вопросительным знаком ниже. */
    if (!add_utf8(f, "·«»--°±•✓")) {
        ds_font_destroy(f); return NULL;
    }
    add_cp(f, '?');
    int ax = 1, ay = 1, rh = 0;
    for (int i = 0; i < f->gcount; i++) {
        DSFontGlyph *gl = &f->glyphs[i];
        int gw = 0;
        int gi = g_for(f, gl->codepoint);
        DSOutline ol; ol_init(&ol);
        int st = 0, mnx = 0, mxx = 0, has = 0;
        if (read_outline(f, gi, 0, 1, 0, 0, 1, 0, 0, &ol)) {
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
        ol_free(&ol);
        if (gw < 1) gw = 1;
        if (ax + gw + 1 >= f->aw) { ax = 1; ay += rh + 1; rh = 0; }
        if (ay + ph + 2 >= f->ah) { ds_font_destroy(f); return NULL; }
        int nh = bake_glyph(f, gl, ax, ay, rh);
        ax += gl->width + 1;
        if (nh > rh) rh = nh;
    }
    /* Индекс глифов строится последним: набор codepoint'ов к этому моменту
     * окончательный. Без индекса поиск остаётся линейным (работает, но медленнее). */
    ds_font_index_build(f);
    return f;
}
void ds_font_destroy(DSFont *f) {
    if (!f) return;
    free(f->data); free(f->alpha); free(f->glyphs); free(f->hmap); free(f);
}
const DSFontGlyph *ds_font_glyph(const DSFont *f, uint32_t cp) {
    if (!f) return NULL;
    int idx = ds_font_lookup(f, cp);
    if (idx >= 0) return &f->glyphs[idx];
    /* Отсутствующий в наборе символ по-прежнему заменяется вопросительным. */
    if (f->qmark >= 0) return &f->glyphs[f->qmark];
    return NULL;
}
int ds_font_aw(const DSFont *f) { return f ? f->aw : 0; }
int ds_font_ah(const DSFont *f) { return f ? f->ah : 0; }
const uint8_t *ds_font_alpha(const DSFont *f) { return f ? f->alpha : NULL; }
float ds_font_lineh(const DSFont *f) { return f ? f->line_h : 0; }
float ds_font_ascent(const DSFont *f) { return f ? f->ascent : 0; }