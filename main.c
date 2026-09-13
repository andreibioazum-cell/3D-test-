#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <android_native_app_glue.h>
#include "runtime.h"
#include "net.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/native_activity.h>
#include <errno.h>
#include <unistd.h>
static int init_done = 0;
static int script_active = 0;
static AAssetManager *script_assets = NULL;
static uint64_t restart_after_ns = 0;
static unsigned int restart_failures = 0;
static uint64_t prev_frame_ns = 0;
static struct android_app *g_app = NULL;
static uint64_t monotonic_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}
/* Экономия заряда без жёсткого глобального лимита: скрипт из настроек задаёт
 * 1) апскейл — игра рисуется в виртуальном буфере screen/scale и растянывается
 *    nearest-neighbor на окно (пиксели, но в scale^2 раз меньше пикселей
 *    рендерит софтрассер);
 * 2) лимит FPS (0 = без ограничения, по умолчанию так и остаётся). */
static volatile int render_scale_setting = 1;
static volatile int fps_cap_setting = 0;
void ds_set_render_scale(int s) {
    if (s < 1) s = 1;
    if (s > 3) s = 3;
    render_scale_setting = s;
}
void ds_set_fps_cap(int c) {
    if (c < 0) c = 0;
    fps_cap_setting = c;
}
static int current_render_scale(void) {
    int s = render_scale_setting;
    if (s < 1) s = 1;
    if (s > 3) s = 3;
    return s;
}
static int phys_w = 0, phys_h = 0;
static int active_scale = 1; /* масштаб, которым реально рисуем: тот же для тачей */
static uint32_t *virt_pixels = NULL;
static int virt_w = 0, virt_h = 0;
static void virt_ensure(int w, int h) {
    if (virt_w == w && virt_h == h) return;
    free(virt_pixels);
    virt_pixels = NULL;
    virt_w = 0;
    virt_h = 0;
    if (w < 1 || h < 1) return;
    void *p = malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!p) return; /* не хватило памяти - рисуем в полный размер без апскейла */
    memset(p, 0, (size_t)w * (size_t)h * sizeof(uint32_t));
    virt_pixels = (uint32_t *)p;
    virt_w = w;
    virt_h = h;
}
/* Виртуальный экран = окно / масштаб. screen_w/screen_h — то, что видит скрипт. */
static void apply_screen_size(void) {
    if (phys_w < 1 || phys_h < 1) return;
    int s = current_render_scale();
    int vw = phys_w / s, vh = phys_h / s;
    if (vw < 1) vw = 1;
    if (vh < 1) vh = 1;
    screen_w = vw;
    screen_h = vh;
    /* Буфер сверяем всегда, а не только при смене размера: если прошлый
     * malloc не удался, следующая попытка будет уже в этом кадре. */
    if (!virt_pixels || virt_w != vw || virt_h != vh) virt_ensure(vw, vh);
}
/* nearest-neighbor: каждый виртуальный пиксель масштабируется в s x s.
 * Координаты источника клампятся: ширина/высота окна не обязаны делиться на
 * масштаб нацело, и без клампа крайние столбцы/строки читали бы мусор за
 * концом виртуального буфера. Строки приёмника шагаем по его страйду, а не по
 * ширине: у ANativeWindow stride бывает шире видимой области, и запись по
 * ширине давала бы «полосатую кашу» со сдвигом строк. */
static void upscale_nearest(const uint32_t *src, int sw, int sh,
                            uint32_t *dst, int dw, int dh, int dst_stride, int s) {
    if (!src || !dst || sw < 1 || sh < 1 || dw < 1 || dh < 1 || s < 1) return;
    if (dst_stride < dw) dst_stride = dw;
    for (int y = 0; y < dh; y++) {
        int sy = y / s;
        if (sy >= sh) sy = sh - 1;
        const uint32_t *srow = src + (size_t)sy * (size_t)sw;
        uint32_t *drow = dst + (size_t)y * (size_t)dst_stride;
        for (int x = 0; x < dw; x++) {
            int sx = x / s;
            if (sx >= sw) sx = sw - 1;
            drow[x] = srow[sx];
        }
    }
}
/* Лимит FPS: досыпаем остаток кадра сном. 0 - без ограничения. */
static void cap_frame_sleep(uint64_t frame_start_ns) {
    int cap = fps_cap_setting;
    if (cap < 1) return;
    uint64_t interval = 1000000000ull / (uint64_t)cap;
    uint64_t deadline = frame_start_ns + interval;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return;
    uint64_t nowns = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
    if (nowns >= deadline) return;
    uint64_t rem = deadline - nowns;
    struct timespec req;
    req.tv_sec = (time_t)(rem / 1000000000ull);
    req.tv_nsec = (long)(rem % 1000000000ull);
    while (nanosleep(&req, &req) == -1 && errno == EINTR) { /* прерван - досыпаем */ }
}
static void protected_init(void *userdata) { init((AAssetManager *)userdata); }
static void protected_reset(void *userdata) { (void)userdata; reset(); }
static void protected_update(void *userdata) { (void)userdata; update(); }
static void protected_draw(void *userdata) { draw((Buffer *)userdata); }
typedef struct { float x; float y; int action; int id; } TouchCall;
static void protected_touch(void *userdata) {
    TouchCall *call = (TouchCall *)userdata;
    touch(call->x, call->y, call->action, call->id);
}
static int back_consumed = 0;
typedef struct { int handled; } BackCall;
static void protected_back(void *userdata) {
    BackCall *call = (BackCall *)userdata;
    call->handled = back_pressed();
}
static void mark_script_failed(const char *hook) {
    const char *message = ds_runtime_error_message();
    __android_log_print(ANDROID_LOG_ERROR, "DimScript","script hook '%s' stopped: %s; scheduling a restart",hook?hook:"unknown",message);
    ds_console_log(1, "script error: hook '%s' stopped: %s; restarting", hook?hook:"unknown", message);
    unsigned int shift = restart_failures < 5 ? restart_failures : 5;
    uint64_t delay = 1000000000ull << shift;
    script_active = 0;
    ds_request_script_restart();
    restart_after_ns = monotonic_ns() + delay;
    ++restart_failures;
}
static int start_script(int reset_state) {
    int ok;
    ds_clear_runtime_error(); ds_clear_script_restart(); ds_string_pool_reset();
    if (reset_state) { ok = ds_call_protected(protected_reset, NULL, "reset"); if (!ok) { mark_script_failed("reset"); return 0; } }
    ds_clear_runtime_error();
    ok = ds_call_protected(protected_init, script_assets, "init");
    if (!ok) { mark_script_failed("init"); return 0; }
    ds_clear_runtime_error(); restart_failures = 0; script_active = 1; return 1;
}
static void restart_script_if_due(void) {
    uint64_t now; if (script_active || !ds_script_restart_requested()) return;
    now = monotonic_ns(); if (now < restart_after_ns) return; (void)start_script(1);
}
static void handle_cmd(struct android_app *app, int32_t command) {
    if (!app) { ds_runtime_error("no app"); return; }
    g_app = app;
    switch (command) {
        case APP_CMD_INIT_WINDOW:
            if (!app->window) { init_done = 0; return; }
            phys_w = ANativeWindow_getWidth(app->window);
            phys_h = ANativeWindow_getHeight(app->window);
            if (phys_w <= 0 || phys_h <= 0) { init_done = 0; return; }
            apply_screen_size();
            script_assets = app->activity ? app->activity->assetManager : NULL;
            ANativeWindow_setBuffersGeometry(app->window, 0, 0, WINDOW_FORMAT_RGBA_8888);
            ds_set_activity(app->activity);
            if (!ds_graphics_init(script_assets)) { init_done = 0; return; }
            /* Звуки лежат в тех же assets (sounds/...), играют через OpenSL ES. */
            ds_sound_init(script_assets);
            ds_sound_resume();
            init_done = 1; script_active = 0; restart_failures = 0;
            ds_clear_script_restart(); (void)start_script(0); break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONTENT_RECT_CHANGED:
        case APP_CMD_CONFIG_CHANGED:
            /* adjustResize changes the game surface while the IME is open. */
            if (app->window) {
                /* Re-assert the pixel format: some devices switch the surface
                 * format after an IME-driven resize, which made ANativeWindow_lock
                 * return buffers the renderer would reject (blank/crashy frames). */
                ANativeWindow_setBuffersGeometry(app->window, 0, 0, WINDOW_FORMAT_RGBA_8888);
                int w = ANativeWindow_getWidth(app->window);
                int h = ANativeWindow_getHeight(app->window);
                if (w > 0 && h > 0) { phys_w = w; phys_h = h; apply_screen_size(); }
            }
            break;
        case APP_CMD_TERM_WINDOW:
            init_done = 0; script_active = 0; keyboard_hide(); ds_graphics_shutdown(); ds_sound_shutdown(); break;
        case APP_CMD_GAINED_FOCUS: ds_sound_resume(); break;
        case APP_CMD_LOST_FOCUS: ds_sound_pause(); break;
        default: break;
    }
}
static int32_t handle_input(struct android_app *app, AInputEvent *event) {
    (void)app;
    if (!event) return 0;
    int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        if (!script_active) return 0;
        TouchCall call; size_t count, index, i; int raw, action;
        count = AMotionEvent_getPointerCount(event); if (count == 0) return 0;
        raw = AMotionEvent_getAction(event); action = raw & AMOTION_EVENT_ACTION_MASK;
        if (action == AMOTION_EVENT_ACTION_POINTER_DOWN) action = AMOTION_EVENT_ACTION_DOWN;
        else if (action == AMOTION_EVENT_ACTION_POINTER_UP) action = AMOTION_EVENT_ACTION_UP;
        index = (size_t)((raw & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        if (index >= count) index = 0;
        i = (action == AMOTION_EVENT_ACTION_MOVE) ? 0 : index;
        count = (action == AMOTION_EVENT_ACTION_MOVE) ? count : index + 1;
        for (; i < count; i++) {
            /* Координаты окна делим на масштаб апскейла: скрипт живёт в
             * виртуальных пикселях (screen_w x screen_h), а не в физических. */
            call.x = AMotionEvent_getX(event, i) / (float)active_scale;
            call.y = AMotionEvent_getY(event, i) / (float)active_scale;
            /* Край окна при масштабе: phys/s округляется вниз, и пара крайних
             * пикселей может лечь за виртуальный экран — прижимаем к нему. */
            if (screen_w > 0) {
                if (call.x < 0) call.x = 0;
                if (call.x > (float)(screen_w - 1)) call.x = (float)(screen_w - 1);
            }
            if (screen_h > 0) {
                if (call.y < 0) call.y = 0;
                if (call.y > (float)(screen_h - 1)) call.y = (float)(screen_h - 1);
            }
            call.action = action; call.id = AMotionEvent_getPointerId(event, i);
            if (!ds_call_protected(protected_touch, &call, "touch")) { mark_script_failed("touch"); break; }
        }
        return 1;
    } else if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t action = AKeyEvent_getAction(event);
        int32_t key = AKeyEvent_getKeyCode(event);
        int32_t meta = AKeyEvent_getMetaState(event);
        if (key == AKEYCODE_BACK && action == AKEY_EVENT_ACTION_DOWN &&
            (keyboard_visible() || keyboard_uses_editor())) {
            keyboard_hide();
            return 1;
        }
        if (keyboard_visible() &&
            (action == AKEY_EVENT_ACTION_DOWN || action == AKEY_EVENT_ACTION_MULTIPLE)) {
            if (keyboard_handle_key(key, action, meta)) return 1;
        }
        if (key == AKEYCODE_BACK) {
            /* Системный «Назад» сначала отдаём скрипту: он закрывает чат или
             * возвращает на прошлый экран. Если скрипт его не взял (лобби),
             * и DOWN, и UP уходят системе - Android сворачивает игру сам. */
            if (action == AKEY_EVENT_ACTION_DOWN) {
                BackCall call = {0};
                back_consumed = 0;
                if (!script_active) return 0;
                if (!ds_call_protected(protected_back, &call, "back_pressed")) { mark_script_failed("back_pressed"); return 1; }
                back_consumed = call.handled ? 1 : 0;
                return back_consumed;
            }
            if (action == AKEY_EVENT_ACTION_UP) { int c = back_consumed; back_consumed = 0; return c; }
            return back_consumed;
        }
        /* Когда текст ведёт системный EditText, клавиши (в том числе Backspace)
         * должны дойти до него, иначе удаление применяется только к буферу игры,
         * редактор остаётся со старым текстом и дописывает его к новому вводу.
         * Проверяем именно редактор, а не флаг видимости: на ландшафте эвристика
         * «клавиатура видна» может ошибаться, и клавиши не должны теряться. */
        if (keyboard_uses_editor()) return 0;
        return 1;
    }
    return 0;
}
void android_main(struct android_app *app) {
    Buffer frame = {0}; if (!app) return;
    /* rand() в скриптах использует libc-генератор, который сам себя не
     * сидит: без srand() спавн леденцов, их направление полёта и прочие
     * «случайные» броски шли бы по одной и той же последовательности. */
    srand((unsigned)(time(NULL) * 2654435761u) ^ ((unsigned)getpid() * 0x9E3779B9u));
    app->onAppCmd = handle_cmd; app->onInputEvent = handle_input;
    net_set_java_vm(app->activity->vm);
    ds_sound_set_java_vm((void *)app->activity->vm);
    net_set_data_path(app->activity->internalDataPath);
    ds_set_activity(app->activity);
    ds_log("DimScript Android 10 arm64/arm32 only + system keyboard (JNI)");
    for (;;) {
        struct android_poll_source *source = NULL; int ident;
        while ((ident = ALooper_pollOnce(script_active ? 0 : 10, NULL, NULL, (void **)&source)) >= 0) {
            if (source && source->process) source->process(app, source);
            if (app->destroyRequested) {
                init_done = 0; script_active = 0; keyboard_hide(); ds_graphics_shutdown(); ds_sound_shutdown(); return;
            }
        }
        if (!app->window || !init_done || app->destroyRequested) continue;
        restart_script_if_due();
        uint64_t frame_start = monotonic_ns();
        /* Настройки могут сменить апскейл в любой момент - пересчитываем
         * виртуальный экран и буфер перед каждым кадром (операция дешёвая). */
        apply_screen_size();
        if (script_active) {
            uint64_t now = frame_start;
            dt = prev_frame_ns ? (double)(now - prev_frame_ns) / 1000000000.0 : 0.0;
            if (dt < 0.0) dt = 0.0; if (dt > 0.1) dt = 0.1;
            prev_frame_ns = now;
            if (!ds_call_protected(protected_update, NULL, "update")) mark_script_failed("update");
            else if (ds_script_restart_requested()) { script_active = 0; restart_after_ns = monotonic_ns(); }
        }
        ANativeWindow_Buffer native_buffer;
        if (ANativeWindow_lock(app->window, &native_buffer, NULL) == 0) {
            int frame_valid;
            /* Размеры настоящего буфера — истина: окно могли пересоздать или
             * повернуть между колбэками, а апскейл считает от phys_w/phys_h. */
            if (native_buffer.width > 0 && native_buffer.height > 0 &&
                (native_buffer.width != phys_w || native_buffer.height != phys_h)) {
                phys_w = native_buffer.width;
                phys_h = native_buffer.height;
            }
            apply_screen_size();
            int s = current_render_scale();
            if (s > 1 && (!virt_pixels || virt_w != screen_w || virt_h != screen_h)) {
                /* Виртуального буфера нет (не хватило памяти) — рисуем в
                 * полном размере и возвращаем скрипту полный экран, иначе
                 * интерфейс соберётся под маленький экран в углу большого. */
                s = 1;
                screen_w = native_buffer.width;
                screen_h = native_buffer.height;
            }
            active_scale = s;
            /* Апскейл: игра рисуется в виртуальном буфере, потом растягивается
             * nearest-neighbor на всё окно - меньше пикселей, пиксельный стиль. */
            if (s > 1) {
                frame.pixels = virt_pixels;
                frame.width = virt_w; frame.height = virt_h; frame.stride = virt_w;
            } else {
                frame.pixels = (uint32_t *)native_buffer.bits;
                frame.width = native_buffer.width; frame.height = native_buffer.height; frame.stride = native_buffer.stride;
            }
            frame_valid = frame.pixels && frame.width > 0 && frame.height > 0 && frame.stride >= frame.width
                && native_buffer.bits && native_buffer.width > 0 && native_buffer.height > 0
                && native_buffer.stride >= native_buffer.width
                && native_buffer.format == WINDOW_FORMAT_RGBA_8888;
            if (frame_valid && ds_graphics_begin_frame(&frame)) {
                int draw_failed = 0;
                if (script_active) {
                    if (!ds_call_protected(protected_draw, &frame, "draw")) { mark_script_failed("draw"); draw_failed = 1; }
                    else if (ds_script_restart_requested()) { script_active = 0; restart_after_ns = monotonic_ns(); }
                }
                if (!script_active) {
                    if (draw_failed || ds_script_has_error()) ds_graphics_error_screen(ds_runtime_error_message());
                    ds_graphics_cancel_frame();
                } else ds_graphics_end_frame();
            }
            if (s > 1 && virt_pixels) {
                upscale_nearest(virt_pixels, virt_w, virt_h,
                                (uint32_t *)native_buffer.bits,
                                native_buffer.width, native_buffer.height,
                                native_buffer.stride, s);
            }
            ANativeWindow_unlockAndPost(app->window);
        }
        /* Лимит FPS из настроек: 0 - без ограничения (по умолчанию). */
        cap_frame_sleep(frame_start);
    }
}
#include "graphics.c"
#include "net.c"
#include "sound.c"