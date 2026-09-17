/* Заглушка для хост-тестов (tools/host_test): на устройстве это NDK-заголовок. */
#ifndef HOST_STUB_ANDROID_LOG_H
#define HOST_STUB_ANDROID_LOG_H
enum { ANDROID_LOG_INFO = 4, ANDROID_LOG_WARN = 5, ANDROID_LOG_ERROR = 6 };
int __android_log_print(int prio, const char *tag, const char *fmt, ...);
#endif
