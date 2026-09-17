#ifndef STUB_NATIVE_ACTIVITY_H
#define STUB_NATIVE_ACTIVITY_H
#include <jni.h>
#include <android/asset_manager.h>
#include <android/native_window.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ANativeActivity {
  void *clazz;
  JavaVM *vm;
  JNIEnv *env;
  jobject activityObj;
  const char *internalDataPath;
  const char *externalDataPath;
  AAssetManager *assetManager;
} ANativeActivity;
#ifdef __cplusplus
}
#endif
#endif
