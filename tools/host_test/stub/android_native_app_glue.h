/* Заглушка android_native_app_glue для хост-тестов (минимум для main.c). */
#ifndef HOST_STUB_NATIVE_APP_GLUE_H
#define HOST_STUB_NATIVE_APP_GLUE_H
#include <stdint.h>
#include <android/looper.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/native_activity.h>
#include <android/configuration.h>
struct android_app;
typedef void (*android_app_cmd)(struct android_app *app, int32_t cmd);
typedef int32_t (*android_app_input)(struct android_app *app, AInputEvent *event);
enum { APP_CMD_INIT_WINDOW = 5, APP_CMD_TERM_WINDOW = 6, APP_CMD_WINDOW_RESIZED = 7,
       APP_CMD_CONFIG_CHANGED = 9, APP_CMD_CONTENT_RECT_CHANGED = 10,
       APP_CMD_GAINED_FOCUS = 12, APP_CMD_LOST_FOCUS = 13 };
struct android_app {
    void *userData;
    android_app_cmd onAppCmd;
    android_app_input onInputEvent;
    ANativeActivity *activity;
    ANativeWindow *window;
    AConfiguration *config;
    int destroyRequested;
};
#endif
