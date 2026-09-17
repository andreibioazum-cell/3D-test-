/* Logging to logcat (Android) or stderr (preview). */
#include "engine.h"
#include <stdarg.h>
void app_log(const char *format, ...) {
    va_list args; va_start(args,format);
    __android_log_vprint(ANDROID_LOG_INFO,"Enjoer",format,args);
    va_end(args);
}
void app_log_error(const char *format, ...) {
    va_list args; va_start(args,format);
    __android_log_vprint(ANDROID_LOG_ERROR,"Enjoer",format,args);
    va_end(args);
}
