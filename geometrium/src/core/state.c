/* Platform state and recoverable C-engine failures. No interpreter. */
#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#ifdef __ANDROID__
#include <jni.h>
#include <unistd.h>
static void *app_activity;
static JavaVM *app_java_vm;
void app_set_activity(void *activity) { app_activity = activity; }
void app_set_java_vm(void *vm) { app_java_vm = (JavaVM *)vm; }
#else
#include <unistd.h>
void app_set_activity(void *activity) { (void)activity; }
void app_set_java_vm(void *vm) { (void)vm; }
#endif

/* The Quit button ends the process: Activity.finish() on Android,
 * _exit() on the host preview. */
void app_quit(void) {
#ifdef __ANDROID__
    if (app_java_vm && app_activity) {
        JNIEnv *env = NULL;
        int attached = 0;
        if ((*app_java_vm)->GetEnv(app_java_vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK ||
            ((*app_java_vm)->AttachCurrentThread(app_java_vm, (void **)&env, NULL) == JNI_OK)) {
            jclass cls = (*env)->GetObjectClass(env, (jobject)app_activity);
            jmethodID finish = cls ? (*env)->GetMethodID(env, cls, "finish", "()V") : NULL;
            if (finish) {
                (*env)->CallVoidMethod(env, (jobject)app_activity, finish);
                if (attached) (*app_java_vm)->DetachCurrentThread(app_java_vm);
                return;
            }
        }
    }
#endif
    _exit(0);
}

int screen_w, screen_h;
double dt;
static jmp_buf error_jump;
static int handler_active, failed;
static char last_error[768], storage[512];

void app_fail(const char *format, ...) {
    va_list args;
    va_start(args,format);
    vsnprintf(last_error,sizeof(last_error),format,args);
    va_end(args);
    app_log_error("%s",last_error);
    failed=1;
    if (handler_active) longjmp(error_jump,1);
}
int app_call(AppCallback callback, void *arg, const char *label) {
    if (!callback) { app_fail("Missing engine callback: %s",label ? label : "unknown"); return 0; }
    if (handler_active) { callback(arg); return !failed; }
    handler_active=1;
    if (setjmp(error_jump)==0) callback(arg);
    handler_active=0;
    return !failed;
}
const char *app_error(void) { return last_error[0] ? last_error : "Unknown engine error"; }
int app_failed(void) { return failed; }
void app_clear_error(void) { failed=0; last_error[0]=0; }
double app_now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)!=0) return 0;
    return t.tv_sec+t.tv_nsec*1e-9;
}
void app_set_storage(const char *directory) {
    snprintf(storage,sizeof(storage),"%s",directory ? directory : "");
}
int app_save_path(char *out,size_t size,const char *name) {
    if (!storage[0] || !out || !size || !name || strchr(name,'/') || strchr(name,'\\')) return 0;
    int n=snprintf(out,size,"%s/%s",storage,name);
    return n>0 && (size_t)n<size;
}
