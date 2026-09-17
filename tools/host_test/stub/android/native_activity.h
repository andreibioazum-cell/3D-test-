/* Заглушка для хост-тестов (совместима по использованию с настоящей). */
#ifndef HOST_STUB_ANDROID_ACTIVITY_H
#define HOST_STUB_ANDROID_ACTIVITY_H
#include <jni.h>
#include <android/asset_manager.h>
#include <android/native_window.h>
#include <android/configuration.h>
struct ANativeActivity;
typedef struct ANativeActivityCallbacks ANativeActivityCallbacks;
typedef struct ANativeActivity {
    struct ANativeActivityCallbacks *callbacks;
    JavaVM *vm;
    JNIEnv *env;
    const char *internalDataPath;
    const char *obbPath;
    AAssetManager *assetManager;
    const char *dataPath;
    void *instance;
} ANativeActivity;
#endif
