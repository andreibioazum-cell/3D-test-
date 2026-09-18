/* Резервный CPU-вывод кадра: блокировка окна и вызов cpu_backend.c.
 * Включается из graphics.c ПОСЛЕ lifecycle.inc: пользуется его статикой
 * (amgr, cmds, cmd_n, cur_buf, frame_open, font, discard_commands).
 *
 * Когда Vulkan недоступен (инициализация упала или предыдущий запуск уронил
 * драйвер), кадр рисуется так: begin блокирует окно (ANativeWindow_lock),
 * скрипт складывает команды, end разбирает их попиксельно в буфер окна и
 * показывает его (unlockAndPost). Экран ошибки работает без изменений - те же
 * команды рисует тот же путь. */

static ANativeWindow *ds_cpu_win = NULL;
static int ds_cpu_locked = 0;

void ds_cpu_render_commands(const DSCmd *cs, size_t n, Buffer *b, const DSFont *fnt);

int ds_graphics_init_cpu(AAssetManager *a, ANativeWindow *w) {
    if (amgr != a) { ds_release_assets(); amgr = a; }
    if (!amgr || !w) return 0;
    if (ds_cpu_win != w) {
        if (ds_cpu_win) ANativeWindow_release(ds_cpu_win);
        ds_cpu_win = ANativeWindow_acquire(w);
        if (!ds_cpu_win) return 0;
        /* RGBA_8888: uint32-пиксели формата AARRGGBB (pack_c) ложатся в буфер
         * окна один к одному - так же выводил прежний попиксельный рендер. */
        ANativeWindow_setBuffersGeometry(ds_cpu_win, 0, 0, WINDOW_FORMAT_RGBA_8888);
    }
    return 1;
}

int ds_graphics_begin_frame_cpu(Buffer *b) {
    if (!ds_cpu_win || !b) return 0;
    if (frame_open) return 0; /* прошлый кадр не закрыт: не блокируем окно дважды */
    ANativeWindow_Buffer nb;
    if (ANativeWindow_lock(ds_cpu_win, &nb, NULL) != 0) return 0;
    if (!nb.bits || nb.width <= 0 || nb.height <= 0 || nb.stride < nb.width) {
        ANativeWindow_unlockAndPost(ds_cpu_win);
        return 0;
    }
    b->pixels = (uint32_t *)nb.bits;
    b->width = nb.width; b->height = nb.height; b->stride = nb.stride;
    discard_commands();
    cur_buf = b; frame_open = 1; ds_cpu_locked = 1;
    return 1;
}

void ds_graphics_end_frame_cpu(Buffer *b) {
    if (!frame_open || !ds_cpu_locked) return;
    Buffer *target = b ? b : cur_buf;
    if (target && target->pixels)
        ds_cpu_render_commands(cmds, cmd_n, target, font);
    ANativeWindow_unlockAndPost(ds_cpu_win);
    ds_cpu_locked = 0;
    cmd_n = 0; frame_open = 0; cur_buf = NULL;
}

void ds_graphics_cancel_frame_cpu(void) {
    if (!frame_open) return;
    discard_commands();
    if (ds_cpu_locked && ds_cpu_win) {
        ANativeWindow_unlockAndPost(ds_cpu_win);
        ds_cpu_locked = 0;
    }
    frame_open = 0; cur_buf = NULL;
}

void ds_graphics_shutdown_cpu(void) {
    /* Окно могло остаться заблокированным при TERM посреди кадра. */
    if (frame_open) discard_commands();
    if (ds_cpu_locked && ds_cpu_win) {
        ANativeWindow_unlockAndPost(ds_cpu_win);
        ds_cpu_locked = 0;
    }
    frame_open = 0; cur_buf = NULL;
    if (ds_cpu_win) { ANativeWindow_release(ds_cpu_win); ds_cpu_win = NULL; }
}
