#ifndef STUB_JNI_H
#define STUB_JNI_H
#include <stdint.h>
#include <stdarg.h>
typedef int8_t jboolean; typedef int8_t jbyte; typedef uint16_t jchar;
typedef int16_t jshort; typedef int32_t jint; typedef int64_t jlong;
typedef float jfloat; typedef double jdouble; typedef jint jsize;
typedef void *jobject; typedef jobject jclass; typedef jobject jstring;
typedef jobject jarray; typedef jarray jshortArray; typedef jarray jintArray;
typedef jobject jthrowable;
typedef const struct JNINativeInterface_ *JNIEnv;
typedef const struct JNIInvokeInterface_ *JavaVM;
typedef struct _jmethodID *jmethodID; typedef struct _jfieldID *jfieldID;
#define JNI_VERSION_1_6 0x00010006
#define JNI_OK 0
#define JNI_FALSE 0
#define JNI_TRUE 1
#define JNI_ERR (-1)
struct JNINativeInterface_ {
  jint (*PushLocalFrame)(JNIEnv *, jint);
  jobject (*PopLocalFrame)(JNIEnv *, jobject);
  jclass (*FindClass)(JNIEnv *, const char *);
  jboolean (*ExceptionCheck)(JNIEnv *);
  void (*ExceptionClear)(JNIEnv *);
  jmethodID (*GetMethodID)(JNIEnv *, jclass, const char *, const char *);
  jmethodID (*GetStaticMethodID)(JNIEnv *, jclass, const char *, const char *);
  jobject (*NewObject)(JNIEnv *, jclass, jmethodID, ...);
  jint (*CallStaticIntMethod)(JNIEnv *, jclass, jmethodID, ...);
  jint (*CallIntMethod)(JNIEnv *, jobject, jmethodID, ...);
  void (*CallVoidMethod)(JNIEnv *, jobject, jmethodID, ...);
  jobject (*CallObjectMethod)(JNIEnv *, jobject, jmethodID, ...);
  jclass (*GetObjectClass)(JNIEnv *, jobject);
  jobject (*NewGlobalRef)(JNIEnv *, jobject);
  void (*DeleteGlobalRef)(JNIEnv *, jobject);
  void (*DeleteLocalRef)(JNIEnv *, jobject);
  jshortArray (*NewShortArray)(JNIEnv *, jsize);
  void (*SetShortArrayRegion)(JNIEnv *, jshortArray, jsize, jsize, const jshort *);
};
struct JNIInvokeInterface_ {
  jint (*AttachCurrentThread)(JavaVM *, JNIEnv **, void *);
  jint (*DetachCurrentThread)(JavaVM *);
  jint (*GetEnv)(JavaVM *, void **, jint);
};
#endif
