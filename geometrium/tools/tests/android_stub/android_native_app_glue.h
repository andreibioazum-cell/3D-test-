#ifndef STUB_NATIVE_APP_GLUE_H
#define STUB_NATIVE_APP_GLUE_H
#include <stdint.h>
#include <android/input.h>
#include <android/native_activity.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {
  APP_CMD_INPUT_CHANGED, APP_CMD_INIT_WINDOW, APP_CMD_TERM_WINDOW,
  APP_CMD_WINDOW_RESIZED, APP_CMD_WINDOW_REDRAW_NEEDED,
  APP_CMD_CONTENT_RECT_CHANGED, APP_CMD_GAINED_FOCUS, APP_CMD_LOST_FOCUS,
  APP_CMD_CONFIG_CHANGED, APP_CMD_LOW_MEMORY, APP_CMD_START, APP_CMD_RESUME,
  APP_CMD_SAVE_STATE, APP_CMD_PAUSE, APP_CMD_STOP, APP_CMD_DESTROY
};
struct android_app;
struct android_poll_source {
  int32_t id; int32_t ident;
  void (*process)(struct android_app *, struct android_poll_source *);
};
struct android_app {
  void *userData;
  void (*onAppCmd)(struct android_app *, int32_t);
  int32_t (*onInputEvent)(struct android_app *, AInputEvent *);
  ANativeWindow *window;
  ANativeActivity *activity;
  int destroyRequested;
};
int ALooper_pollOnce(int timeout, int *outFd, int *outEvents, void **outData);
#ifdef __cplusplus
}
#endif
#endif
