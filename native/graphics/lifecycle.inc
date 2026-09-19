/* Публичный API графики для скриптов и main.c: загрузка текстур и шрифта,
 * команды рисования (rect/roundrect/rect_rot/circle/ring/line/tex/tex_tint/
 * text), кадры begin/end/cancel и экран ошибки DimScript.
 *
 * Команды только накапливаются: всю отрисовку делает Vulkan-бэкенд
 * (vulkan_backend.inc) в конце кадра. Растеризация на CPU больше не
 * выполняется вовсе. */
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
    if (!amgr) { ds_log_err("texture not loaded: no asset manager for '%s'", n); return NULL; }
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
    ds_vk_pending_texture(t); /* зальётся на GPU в конце текущего кадра */
    ds_log("texture loaded: %s (%dx%d)", n, t->w, t->h);
    return t;
}
static int ensure_font(void) {
    if (font) return 1;
    if (font_tried) return 0;
    font_tried = 1;
    uint8_t *data = NULL; size_t sz = 0;
    if (!open_asset(DS_FONT_ASSET, &data, &sz)) {
        ds_log_err("font not loaded: %s not found", DS_FONT_ASSET);
        return 0;
    }
    font = ds_font_create(data, sz, DS_FONT_PIXEL_HEIGHT);
    free(data);
    if (!font) { ds_log_err("font not loaded: could not parse TrueType font"); return 0; }
    vk_pending_font = 1; /* атлас зальётся на GPU в конце текущего кадра */
    ds_log("font loaded: %s", DS_FONT_ASSET);
    return 1;
}

/* --- список команд кадра --- */

static DSCmd *push(DSCommand t) {
    if (!frame_open) return NULL;
    if (cmd_n == cmd_cap) {
        size_t cap = cmd_cap ? cmd_cap*2 : 256;
        if (cap < cmd_n || cap > SIZE_MAX/sizeof(*cmds)) { ds_runtime_error("too many renderer commands"); return NULL; }
        DSCmd *nc = (DSCmd *)realloc(cmds, cap*sizeof(*cmds));
        if (!nc) { ds_runtime_error("out of memory in command buffer"); return NULL; }
        cmds = nc; cmd_cap = cap;
    }
    DSCmd *c = &cmds[cmd_n++];
    memset(c, 0, sizeof(*c)); c->t = t; return c;
}
static void discard_commands(void) {
    for (size_t i = 0; i < cmd_n; i++) {
        if (cmds[i].t == DS_CMD_TEXT) {
            free(cmds[i].v.tt.s);
            cmds[i].v.tt.s = NULL;
        }
    }
    cmd_n = 0;
}

/* --- активы --- */

void ds_release_assets(void) {
    /* GPU-копии (если были) - под чистую: зальются заново при загрузке. */
    for (Texture *t = textures; t; t = t->next) ds_vk_texture_gpu_release(t);
    ds_vk_pending_reset();
    ds_vk_font_gpu_release();
    Texture *t = textures;
    while (t) { Texture *n = t->next; free(t->pixels); free(t->name); free(t); t = n; }
    textures = NULL;
    ds_font_destroy(font); font = NULL; font_tried = 0;
    amgr = NULL;
}
void ds_set_asset_manager(AAssetManager *a) {
    if (amgr != a) { ds_release_assets(); amgr = a; }
    if (!amgr) ds_runtime_error("Android asset manager is unavailable");
}
int png_load(const char *n) { return load_png(n) != NULL; }

/* --- команды рисования (API скриптов, не меняется) --- */

void rect(float x, float y, float w, float h, uint32_t c) {
    DSCmd *p = push(DS_CMD_RECT); if (!p) return;
    p->v.rc.x=x; p->v.rc.y=y; p->v.rc.w=w; p->v.rc.h=h; p->v.rc.c=pack_c(c);
}
void clear_screen(uint32_t c) {
    rect(0.0f, 0.0f, (float)screen_w, (float)screen_h, c);
}
void roundrect(float x, float y, float w, float h, float r, uint32_t c) {
    DSCmd *p = push(DS_CMD_ROUND); if (!p) return;
    p->v.rr.x=x; p->v.rr.y=y; p->v.rr.w=w; p->v.rr.h=h; p->v.rr.r=r; p->v.rr.c=pack_c(c);
}
/* Повёрнутый прямоугольник со смешиванием (обычный конвейер, как у rect):
 * хитбоксы рисуются им — ровная фигура с острыми углами вместо круглой
 * капсулы line() и вместо непрозрачного roundrect(). */
void rect_rot(float x, float y, float w, float h, float ang, uint32_t c) {
    DSCmd *p = push(DS_CMD_RECT_ROT); if (!p) return;
    p->v.rot.x=x; p->v.rot.y=y; p->v.rot.w=w; p->v.rot.h=h; p->v.rot.ang=ang; p->v.rot.c=pack_c(c);
}
void circle(float x, float y, float r, uint32_t c) {
    DSCmd *p = push(DS_CMD_CIRCLE); if (!p) return;
    p->v.ci.x=x; p->v.ci.y=y; p->v.ci.r=r; p->v.ci.c=pack_c(c);
}
void ring(float x, float y, float r, float t, uint32_t c) {
    DSCmd *p = push(DS_CMD_RING); if (!p) return;
    p->v.rg.x=x; p->v.rg.y=y; p->v.rg.r=r; p->v.rg.th=t; p->v.rg.c=pack_c(c);
}
void line(float x1, float y1, float x2, float y2, float thickness, uint32_t c) {
    DSCmd *p = push(DS_CMD_LINE); if (!p) return;
    p->v.ln.x1=x1; p->v.ln.y1=y1; p->v.ln.x2=x2; p->v.ln.y2=y2;
    p->v.ln.th=thickness; p->v.ln.c=pack_c(c);
}
void tex(float x, float y, const char *name, float a, float s) {
    if (!frame_open) return;
    Texture *t = load_png(name); if (!t) return;
    DSCmd *p = push(DS_CMD_TEX); if (!p) return;
    p->v.tx.x=x; p->v.tx.y=y; p->v.tx.a=a; p->v.tx.sc=s; p->v.tx.tx=t;
}
void tex_tint(float x, float y, const char *name, float a, float s, uint32_t c) {
    if (!frame_open) return;
    Texture *t = load_png(name); if (!t) return;
    DSCmd *p = push(DS_CMD_TEX_TINT); if (!p) return;
    p->v.tx2.x=x; p->v.tx2.y=y; p->v.tx2.a=a; p->v.tx2.sc=s; p->v.tx2.tx=t; p->v.tx2.c=pack_c(c);
}
/* Весь текст в игре рисуется чисто белым (255,255,255). Скрипты по-прежнему
 * передают цвет, но из него остаётся только альфа - она нужна плавным
 * затуханиям подписей (fade_color в core/ui.ds, админский баннер, экран
 * предупреждения). RGB подменяется на белый, поэтому ни золотые/зелёные
 * надписи достижений, ни красные/голубые ники админов на экране не
 * появляются: одно правило на всю игру вместо правки каждого вызова. */
static uint32_t text_force_white(uint32_t c) {
    // Preserve admin nickname colors (red for Dimasi4ek229, blue for qwetyuiopaj1234)
    if (c == 0xFFFF4444u || c == 0xFF4FC3F7u || c == 0xFFFF3333u || c == 0xFF33A8FFu)
        return c;
    /* Тёмные чернила пузыря чата (chat_bubble_ink из config.ds): белый текст
     * на белом окошке нечитаем, поэтому тёмный RGB оставляем как есть,
     * сохраняя альфу плавного затухания. Сверяем только RGB. */
    if ((c & 0x00ffffffu) == 0x00202020u)
        return c;
    return (c & 0xff000000u) | 0x00ffffffu;
}
void text_scaled(const char *s, float x, float y, uint32_t c, float sc) {
    if (!frame_open || !s || !ensure_font()) return;
    DSCmd *p = push(DS_CMD_TEXT); if (!p) return;
    /* Network and script strings can come from reused buffers; renderer commands
     * must own text until the frame is flushed or cancelled. */
    p->v.tt.s = strdup_safe(s);
    if (!p->v.tt.s) { ds_runtime_error("out of memory copying renderer text"); return; }
    p->v.tt.x=x; p->v.tt.y=y; p->v.tt.sc=sc; p->v.tt.c=pack_c(text_force_white(c));
}
void text(const char *s, float x, float y, uint32_t c) { text_scaled(s, x, y, c, 1.0f); }

/* --- метрики текста (остаются на CPU, как и были) --- */

int text_ink_width(const char *s) {
    if (!s || !ensure_font()) return 0;
    const DSFontGlyph *ref = ds_font_glyph(font, 'S');
    float pen = -(ref ? ref->bearing_x : 0);
    int first = 1; float minL = 0, maxR = 0;
    for (const char *c = s; *c;) {
        int cp = utf8_dec(&c);
        const DSFontGlyph *g = ds_font_glyph(font, (uint32_t)cp);
        if (!g) continue;
        float dl = pen + g->bearing_x, dr = dl + g->width;
        if (first || dl < minL) minL = dl;
        if (first || dr > maxR) maxR = dr;
        first = 0; pen += g->advance;
    }
    return first ? 0 : (int)(maxR - minL + 0.5f);
}
int text_ink_height(const char *s) {
    if (!s || !ensure_font()) return 0;
    const DSFontGlyph *ref = ds_font_glyph(font, 'S');
    float base = ref ? ref->bearing_top : 0;
    int first = 1; float minT = 0, maxB = 0;
    for (const char *c = s; *c;) {
        int cp = utf8_dec(&c);
        const DSFontGlyph *g = ds_font_glyph(font, (uint32_t)cp);
        if (!g) continue;
        float dt = base - g->bearing_top, db = dt + g->height;
        /* Нижние выносные элементы ('р','у','д','g','y' и т.п.) не должны
         * раздувать высоту ink-бокса: иначе строка с ними центрируется выше
         * строк без них, и текст «уезжает вверх» на часть выносного элемента.
         * Обрезаем низ по базовой линии - она ровно в dt == base. */
        if (db > base) db = base;
        if (first || dt < minT) minT = dt;
        if (first || db > maxB) maxB = db;
        first = 0;
    }
    return first ? 0 : (int)(maxB - minT + 0.5f);
}
int text_width(const char *s) { return text_ink_width(s); }
int text_height(const char *s) { return text_ink_height(s); }
int text_ink_top(const char *s) {
    if (!s || !ensure_font()) return 0;
    const DSFontGlyph *ref = ds_font_glyph(font, 'S');
    float base = ref ? ref->bearing_top : 0;
    int first = 1; float minT = 0;
    for (const char *c = s; *c;) {
        int cp = utf8_dec(&c);
        const DSFontGlyph *g = ds_font_glyph(font, (uint32_t)cp);
        if (!g) continue;
        float dt = base - g->bearing_top;
        if (first || dt < minT) minT = dt;
        first = 0;
    }
    return first ? 0 : (int)floorf(minT);
}

/* --- кадры --- */

int ds_graphics_init(AAssetManager *a, ANativeWindow *window) {
    if (amgr != a) { ds_release_assets(); amgr = a; }
    return ds_vk_init_backend(window);
}
/* Автоматическое внутреннее разрешение: ни кнопки, ни настройки, ни сохранённого
 * значения — только реакция на фактическую скорость кадров. Vsync (FIFO)
 * квантует частоту: стоит кадру стать чуть дольше 16.6 мс, и вместо 60 будет 30.
 * На слабых GPU в бою (много слоёв смешивания на весь экран) полный размер окна
 * в этот бюджет не укладывается, поэтому при накоплении промахов кадр рисуется
 * в оффскрин в scale раз меньше и растягивается на окно; как только запас
 * появляется — возвращаемся к 1:1. Картинка всегда резкая, пока устройство
 * держит 60 fps; мягче она становится только вместо провала в 30.
 *
 * Гистерезис здесь принципиально несимметричный, чтобы масштаб не «дышал»:
 * грубее уходим быстро (10 промахов подряд или 9 из 45 кадров), а пробу
 * «резче» допускаем только после 300 кадров (~5 с) вообще без промахов и не
 * чаще раза в 15 с; две провалившиеся пробы за сеанс навсегда фиксируют
 * текущий масштаб (gfx_finer_off) — лучше стабильная мягкость, чем видимая
 * пульсация резкости и пересоздание оффскрина каждые несколько секунд.
 * История промахов сбрасывается на каждом новом окне (gfx_autoscale_new_window):
 * первые кадры после возврата в приложение всегда медленные и не должны
 * сразу тянуть масштаб вниз. */
static int gfx_auto_scale = 1;
static int gfx_cooldown = 0;        /* кадров до любых решений после смены */
static int gfx_finer_cooldown = 0;  /* кадров до следующей пробы «резче» */
static int gfx_probe_finer = 0;     /* идёт проба «резче»: промах откатит */
static int gfx_probe_left = 0;      /* кадров вердикта пробы; дожил до 0 - успех */
static int gfx_probe_fails = 0;     /* сколько проб провалилось за сеанс */
static int gfx_finer_off = 0;       /* после двух провалов пробы выключены */
static int gfx_clean = 0;           /* кадров подряд без промаха */
static int gfx_miss_streak = 0, gfx_win_frames = 0, gfx_win_misses = 0;

int ds_graphics_pixel_scale(void) { return gfx_auto_scale; }

/* Старт/возврат в приложение/смена размера окна: история скорости устарела. */
static void gfx_autoscale_new_window(void) {
    gfx_cooldown = 60;
    gfx_probe_finer = 0;
    gfx_probe_left = 0;
    gfx_miss_streak = 0; gfx_win_frames = 0; gfx_win_misses = 0; gfx_clean = 0;
}

static void gfx_set_scale(int s, int cooldown) {
    if (s < 1) s = 1;
    if (s > 3) s = 3;
    if (s == gfx_auto_scale) return;
    gfx_auto_scale = s;
    gfx_cooldown = cooldown;
    gfx_miss_streak = 0; gfx_win_frames = 0; gfx_win_misses = 0;
    ds_log("graphics: internal resolution 1/%d (auto)", s);
}

/* Кадровый интервал главного цикла (секунды): вызывается после каждого кадра. */
void ds_graphics_report_frame_interval(double sec) {
    if (!(sec > 0.0) || sec > 0.25) return;   /* пауза, сворачивание, отладчик */
    if (gfx_finer_cooldown > 0) gfx_finer_cooldown--;
    /* Промах по vsync 60 Гц: кадр пришёл ко второму вертикальному интервалу
     * (33 мс) или позже. 20 мс — граница с запасом против дрожания 16.7. */
    int missed = sec > 0.020;
    gfx_clean = missed ? 0 : gfx_clean + 1;
    if (gfx_cooldown > 0) { gfx_cooldown--; return; }
    /* Вердикт пробы «резче»: окно кадров после её кулдауна. Одиночный поздний
     * промах спустя минуту не должен откатывать уже прижившийся масштаб. */
    if (gfx_probe_finer && gfx_probe_left > 0) {
        gfx_probe_left--;
        if (gfx_probe_left == 0) gfx_probe_finer = 0;  /* проба прижилась */
    }
    if (missed) {
        if (gfx_probe_finer) {   /* проба «резче» провалилась — откат и надолго */
            gfx_probe_finer = 0;
            gfx_probe_left = 0;
            gfx_probe_fails++;
            if (gfx_probe_fails >= 2) gfx_finer_off = 1;
            gfx_finer_cooldown = gfx_finer_off ? 1000000000 : 1800;
            gfx_set_scale(gfx_auto_scale + 1, 90);
            return;
        }
        gfx_miss_streak++;
    } else {
        gfx_miss_streak = 0;
    }
    gfx_win_frames++;
    if (missed) gfx_win_misses++;
    if (gfx_miss_streak >= 10 || gfx_win_frames >= 45) {
        int bad = gfx_miss_streak >= 10 || gfx_win_misses >= 9;
        if (bad) {
            if (gfx_auto_scale < 3) gfx_set_scale(gfx_auto_scale + 1, 90);
        } else if (gfx_win_misses == 0 && gfx_clean >= 300 && gfx_auto_scale > 1 &&
                   gfx_finer_cooldown == 0 && !gfx_finer_off) {
            gfx_probe_finer = 1;
            gfx_finer_cooldown = 900;
            gfx_set_scale(gfx_auto_scale - 1, 45);
            gfx_probe_left = 45 + 60;  /* кулдаун пробы плюс окно вердикта */
        }
        gfx_win_frames = 0; gfx_win_misses = 0;
    }
}

int ds_graphics_begin_frame(Buffer *b) {
    if (!b || b->width <= 0 || b->height <= 0 || b->stride < b->width) return 0;
    /* b->pixels больше не используется: рисуем через Vulkan. Оффскрин равен
     * окну, пока автомасштаб не увидел промахов по vsync. */
    uint32_t s = (uint32_t)gfx_auto_scale;
    uint32_t ow = (uint32_t)b->width / s, oh = (uint32_t)b->height / s;
    if (ow < 1) ow = 1;
    if (oh < 1) oh = 1;
    if (!ds_vk_begin_frame_backend((uint32_t)b->width, (uint32_t)b->height, ow, oh)) return 0;
    discard_commands();
    cur_buf = b; frame_open = 1;
    return 1;
}
void ds_graphics_end_frame(void) {
    if (!frame_open) return;
    ds_vk_end_frame_backend();
    cmd_n = 0; frame_open = 0; cur_buf = NULL;
}
void ds_graphics_cancel_frame(void) {
    discard_commands();
    frame_open = 0; cur_buf = NULL;
    /* Захваченный кадр Vulkan оставляет держаться: следующий begin_frame
     * переиспользует его без повторного acquire. */
}
void ds_graphics_error_screen(const char *m) {
    if (!frame_open) return;
    /* Экран ошибки рисуется теми же командами и тем же Vulkan-конвейером. */
    discard_commands();
    rect(0.0f, 0.0f, (float)(cur_buf ? cur_buf->width : screen_w),
         (float)(cur_buf ? cur_buf->height : screen_h), pack_c(0xff1c0b10));
    if (!ensure_font()) return;
    int y = 12;
    text_scaled("=== DIMSCRIPT ERROR ===", 16, (float)y, pack_c(0xffffffff), 0.7f);
    y += 30;
    if (m && *m) {
        text_scaled(m, 16, (float)y, pack_c(0xffffb0b0), 0.6f);
        y += 28;
    }
    y += 4;
    text_scaled("--- console (last lines) ---", 16, (float)y, pack_c(0xff9aa0b0), 0.55f);
    y += 24;
    int n = console_count();
    int start = n > 16 ? n - 16 : 0;
    for (int i = start; i < n; i++) {
        const char *ln = console_line(i);
        if (!ln || !*ln) { y += 19; continue; }
        uint32_t col = console_type(i) ? pack_c(0xffff8a80) : pack_c(0xffc8d0dc);
        text_scaled(ln, 16, (float)y, col, 0.55f);
        y += 19;
        if (y > (cur_buf ? cur_buf->height : screen_h) - 8) break;
    }
}
void ds_graphics_shutdown(void) {
    ds_graphics_cancel_frame();
    ds_vk_shutdown_backend(); /* GPU уходит первым; CPU-пиксели остаются */
    ds_release_assets();
    free(cmds); cmds = NULL; cmd_cap = 0; cmd_n = 0;
    free(geo_verts); geo_verts = NULL; geo_vcap = geo_vn = 0;
    free(geo_idx); geo_idx = NULL; geo_icap = geo_in = 0;
    geo_trig_free();
}
#include "ttf_font.c"
