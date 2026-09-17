/* Host replacement for <android/log.h> in the preview build. */
#ifndef HOST_COMPAT_LOG_H
#define HOST_COMPAT_LOG_H
#include <stdarg.h>

enum {
    ANDROID_LOG_UNKNOWN = 0,
    ANDROID_LOG_DEFAULT = 1,
    ANDROID_LOG_VERBOSE = 2,
    ANDROID_LOG_DEBUG = 3,
    ANDROID_LOG_INFO = 4,
    ANDROID_LOG_WARN = 5,
    ANDROID_LOG_ERROR = 6,
    ANDROID_LOG_FATAL = 7,
};

int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap);
int __android_log_print(int prio, const char *tag, const char *fmt, ...);

#endif
