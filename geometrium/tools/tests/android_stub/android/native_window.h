#ifndef STUB_NATIVE_WINDOW_H
#define STUB_NATIVE_WINDOW_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ANativeWindow ANativeWindow;
enum { WINDOW_FORMAT_RGBA_8888 = 1, WINDOW_FORMAT_RGBX_8888 = 2,
       WINDOW_FORMAT_RGB_565 = 4 };
typedef struct ANativeWindow_Buffer {
  int32_t width; int32_t height; int32_t stride; int32_t format;
  void *bits; uint32_t reserved[1];
} ANativeWindow_Buffer;
int32_t ANativeWindow_getWidth(const ANativeWindow *);
int32_t ANativeWindow_getHeight(const ANativeWindow *);
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *, int32_t, int32_t, int32_t);
int32_t ANativeWindow_lock(ANativeWindow *, ANativeWindow_Buffer *, void *);
int32_t ANativeWindow_unlockAndPost(ANativeWindow *);
#ifdef __cplusplus
}
#endif
#endif
