/* Заглушка для хост-тестов. */
#ifndef HOST_STUB_ANDROID_LOOPER_H
#define HOST_STUB_ANDROID_LOOPER_H
#include <stdint.h>
struct ALooper; typedef struct ALooper ALooper;
struct android_app; struct android_poll_source;
typedef struct android_poll_source { int32_t id; struct android_poll_source *next; void (*process)(struct android_app *app, struct android_poll_source *source); } android_poll_source;
ALooper *ALooper_forThread(void);
int ALooper_pollOnce(int timeoutMillis, int *outFd, int *outEvents, void **outData);
#endif
