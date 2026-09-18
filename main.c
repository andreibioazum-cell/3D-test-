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
#include "native/crash_report.inc"
static int init_done = 0;
static int script_active = 0;
static AAssetManager *script_assets = NULL;
static uint64_t restart_after_ns = 0;
static unsigned int restart_failures = 0;
static uint64_t prev_frame_ns = 0;
static struct android_app *g_app = NULL;
/* Безопасный режим: 1 - кадры рисует CPU-бэкенд без Vulkan. Включается, когда
 * прошлый запуск упал (отчёт в crash_report.txt) или Vulkan не инициализировался. */
static int g_cpu_render = 0;
/* Экран с отчётом о прошлом падении: висит до первого касания. */
static int g_show_report = 0;
static char g_report[DS_CRASH_REPORT_MAX];
static unsigned long g_frame_count = 0;
static uint64_t monotonic_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}
/* Кадр всегда рисуется в полном размере окна: ни апскейла, ни лимита FPS в
 * настройках больше нет (по просьбе игрока) - оба параметра только портили
 * картинку и заставляли ждать кадр впустую. */
static int phys_w = 0, phys_h = 0;
/* screen_w/screen_h — то, что видит скрипт: всегда полный размер окна. */
static void apply_screen_size(void) {
    if (phys_w < 1 || phys_h < 1) return;
    screen_w = phys_w;
    screen_h = phys_h;
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
    int gfx_ok;
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
            ds_set_activity(app->activity);
            ds_crumb("win");
            /* Рендер: Vulkan, а при отказе или после прошлого падения - CPU.
             * Vulkan-инициализация на части драйверов падает внутри самого
             * драйвера; CPU-путь гарантированно работает (так рендерила вся
             * прошлая версия игры). */
            if (g_cpu_render) {
                ds_crumb("gfx-cpu-init");
                gfx_ok = ds_graphics_init_cpu(script_assets, app->window);
            } else {
                ds_crumb("gfx-vk-init");
                gfx_ok = ds_graphics_init(script_assets, app->window);
                if (!gfx_ok) {
                    ds_log_err("vulkan init failed - switching to CPU renderer");
                    ds_crumb("gfx-cpu-fallback");
                    g_cpu_render = 1;
                    gfx_ok = ds_graphics_init_cpu(script_assets, app->window);
                }
            }
            if (!gfx_ok) { init_done = 0; return; }
            ds_crumb(g_cpu_render ? "gfx-cpu-ok" : "gfx-vk-ok");
            /* Звуки лежат в тех же assets (sounds/...), играют через AudioTrack. */
            ds_sound_init(script_assets);
            ds_crumb("snd");
            ds_sound_resume();
            init_done = 1; script_active = 0; restart_failures = 0;
            ds_clear_script_restart(); (void)start_script(0);
            ds_crumb("script");
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONTENT_RECT_CHANGED:
        case APP_CMD_CONFIG_CHANGED:
            /* adjustResize меняет поверхность при открытой клавиатуре; размер
             * swapchain/оффскрина Vulkan подстроит в начале следующего кадра. */
            if (app->window) {
                int w = ANativeWindow_getWidth(app->window);
                int h = ANativeWindow_getHeight(app->window);
                if (w > 0 && h > 0) { phys_w = w; phys_h = h; apply_screen_size(); }
            }
            break;
        case APP_CMD_TERM_WINDOW:
            init_done = 0; script_active = 0; keyboard_hide();
            ds_graphics_shutdown_cpu(); /* безопасно при любом рендере */
            ds_graphics_shutdown(); ds_sound_shutdown(); break;
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
        /* Экран отчёта о падении: любое касание закрывает его и снимает игру
         * с паузы (сами касания в скрипт на этом экране не передаются). */
        if (g_show_report) {
            if ((AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_DOWN) {
                g_show_report = 0;
                ds_crash_report_clear();
            }
            return 1;
        }
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
            /* Координаты окна — те же пиксели, в которых живёт скрипт. */
            call.x = AMotionEvent_getX(event, i);
            call.y = AMotionEvent_getY(event, i);
            /* Край окна: прижимаем к виртуальному экрану, чтобы касание у самой
             * кромки не уходило за его пределы. */
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
    /* Отчёт о падении ставим раньше всего: даже падение в самой инициализации
     * должно оставить след для следующего запуска. */
    ds_crash_report_init(app->activity ? app->activity->internalDataPath : NULL);
    ds_crumb("boot");
    if (ds_crash_report_load(g_report, sizeof g_report)) {
        /* Прошлый запуск упал: этот стартуем без Vulkan и показываем отчёт.
         * Файл стирается, когда игрок закрывает отчёт касанием, - тогда
         * следующий запуск снова пробует Vulkan (и при новом падении снова
         * пишет отчёт и включает CPU-режим). */
        g_cpu_render = 1;
        g_show_report = 1;
        ds_log_err("предыдущий запуск упал - включён безопасный CPU-режим");
        const char *rp = g_report;
        while (*rp) {
            char line[128];
            size_t li = 0;
            while (*rp && *rp != '\n' && li + 1 < sizeof(line)) line[li++] = *rp++;
            if (*rp == '\n') rp++;
            line[li] = '\0';
            if (li) ds_console_log(1, "%s", line);
        }
    }
    /* rand() в скриптах использует libc-генератор, который сам себя не
     * сидит: без srand() спавн леденцов, их направление полёта и прочие
     * «случайные» броски шли бы по одной и той же последовательности. */
    srand((unsigned)(time(NULL) * 2654435761u) ^ ((unsigned)getpid() * 0x9E3779B9u));
    app->onAppCmd = handle_cmd; app->onInputEvent = handle_input;
    net_set_java_vm(app->activity->vm);
    ds_sound_set_java_vm((void *)app->activity->vm);
    net_set_data_path(app->activity->internalDataPath);
    ds_set_activity(app->activity);
    ds_crumb("jni");
    ds_log("DimScript Android + renderer + system keyboard (JNI)");
    for (;;) {
        struct android_poll_source *source = NULL; int ident;
        while ((ident = ALooper_pollOnce(script_active ? 0 : 10, NULL, NULL, (void **)&source)) >= 0) {
            if (source && source->process) source->process(app, source);
            if (app->destroyRequested) {
                init_done = 0; script_active = 0; keyboard_hide();
                ds_graphics_shutdown_cpu(); ds_graphics_shutdown(); ds_sound_shutdown();
                return;
            }
        }
        if (!app->window || !init_done || app->destroyRequested) continue;
        restart_script_if_due();
        uint64_t frame_start = monotonic_ns();
        apply_screen_size();
        g_frame_count++;
        if ((g_frame_count % 120) == 1) {
            char mark[32];
            snprintf(mark, sizeof(mark), "f%lu", g_frame_count);
            ds_crumb(mark);
        }
        /* Экран отчёта: игра на паузе, кадр рисует только ошибку. */
        if (script_active && !g_show_report) {
            uint64_t now = frame_start;
            dt = prev_frame_ns ? (double)(now - prev_frame_ns) / 1000000000.0 : 0.0;
            if (dt < 0.0) dt = 0.0; if (dt > 0.1) dt = 0.1;
            prev_frame_ns = now;
            if (!ds_call_protected(protected_update, NULL, "update")) mark_script_failed("update");
            else if (ds_script_restart_requested()) { script_active = 0; restart_after_ns = monotonic_ns(); }
        } else if (g_show_report) {
            prev_frame_ns = frame_start;
        }
        /* Кадр: Vulkan (оффскрин + present) или CPU (lock + попиксельно + post). */
        frame.pixels = NULL;
        frame.width = screen_w;
        frame.height = screen_h;
        frame.stride = screen_w;
        if (frame.width > 0 && frame.height > 0) {
            int opened = g_cpu_render ? ds_graphics_begin_frame_cpu(&frame)
                                      : ds_graphics_begin_frame(&frame);
            if (!opened) continue;
            int draw_failed = 0;
            if (g_show_report) {
                ds_graphics_error_screen(g_report);
            } else if (script_active) {
                if (!ds_call_protected(protected_draw, &frame, "draw")) { mark_script_failed("draw"); draw_failed = 1; }
                else if (ds_script_restart_requested()) { script_active = 0; restart_after_ns = monotonic_ns(); }
            }
            if (!script_active && !g_show_report) {
                if (draw_failed || ds_script_has_error()) {
                    /* Экран ошибки идёт теми же командами через тот же
                     * конвейер, поэтому кадр именно завершаем, а не отменяем. */
                    ds_graphics_error_screen(ds_runtime_error_message());
                } else if (g_cpu_render) ds_graphics_cancel_frame_cpu();
                else ds_graphics_cancel_frame();
            }
            if (g_cpu_render) ds_graphics_end_frame_cpu(&frame);
            else ds_graphics_end_frame();
        }
    }
}
#include "graphics.c"
#include "net.c"
#include "sound.c"