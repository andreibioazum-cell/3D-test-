/* Заглушка для хост-тестов. */
#ifndef HOST_STUB_ANDROID_WINDOW_H
#define HOST_STUB_ANDROID_WINDOW_H
#include <stdint.h>
typedef struct ANativeWindow ANativeWindow;
enum { WINDOW_FORMAT_RGBA_8888 = 1 };
typedef struct { void *bits; int width, height, stride; int format; } ANativeWindow_Buffer;
int32_t ANativeWindow_getWidth(ANativeWindow *window);
int32_t ANativeWindow_getHeight(ANativeWindow *window);
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *w, int32_t width, int32_t height, int32_t format);
int ANativeWindow_lock(ANativeWindow *window, ANativeWindow_Buffer *outBuffer, void *inOutResizeBuffer);
int ANativeWindow_unlockAndPost(ANativeWindow *window);
ANativeWindow *ANativeWindow_acquire(ANativeWindow *window);
void ANativeWindow_release(ANativeWindow *window);
#endif
