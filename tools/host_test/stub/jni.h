/* Заглушка для хост-проверок компиляции (tools/host_test): на устройстве это
 * настоящий NDK-заголовок. Как и в реальном jni.h в режиме C, JNIEnv и
 * JavaVM - указатели на таблицы функций; набор методов покрывает то, что
 * игра реально вызывает (native/net, native/sound, native/runtime). */
#ifndef HOST_STUB_JNI_H
#define HOST_STUB_JNI_H
#include <stdint.h>
#include <stddef.h>
typedef int32_t jint;
typedef int32_t jsize;
typedef int8_t jbyte;
typedef uint16_t jchar;
typedef void *jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef jobject jarray;
typedef jarray jbyteArray;
typedef jarray jshortArray;
typedef jobject jthrowable;
typedef jobject jmethodID;
#define JNI_OK 0
#define JNI_ERR (-1)
#define JNI_FALSE 0
#define JNI_TRUE 1
#define JNI_VERSION_1_6 0x00010006
#define JNI_CHECK(...) 0
struct JNINativeInterface;
struct JNIInvokeInterface;
typedef const struct JNINativeInterface *JNIEnv;
typedef const struct JNIInvokeInterface *JavaVM;
typedef const struct JNINativeInterface *JNIEnv;
typedef const struct JNIInvokeInterface *JavaVM;
struct JNINativeInterface {
    void *reserved0;
    void *reserved1;
    void *reserved2;
    jint (*GetVersion)(JNIEnv *);
    jint (*PushLocalFrame)(JNIEnv *, jint);
    jobject (*PopLocalFrame)(JNIEnv *, jobject);
    jobject (*NewGlobalRef)(JNIEnv *, jobject);
    void (*DeleteGlobalRef)(JNIEnv *, jobject);
    void (*DeleteLocalRef)(JNIEnv *, jobject);
    jclass (*FindClass)(JNIEnv *, const char *);
    jobject (*NewObject)(JNIEnv *, jclass, jmethodID, ...);
    jclass (*GetObjectClass)(JNIEnv *, jobject);
    jmethodID (*GetMethodID)(JNIEnv *, jclass, const char *, const char *);
    jmethodID (*GetStaticMethodID)(JNIEnv *, jclass, const char *, const char *);
    jobject (*CallObjectMethod)(JNIEnv *, jobject, jmethodID, ...);
    jint (*CallIntMethod)(JNIEnv *, jobject, jmethodID, ...);
    void (*CallVoidMethod)(JNIEnv *, jobject, jmethodID, ...);
    jint (*CallStaticIntMethod)(JNIEnv *, jclass, jmethodID, ...);
    jstring (*NewStringUTF)(JNIEnv *, const char *);
    const char *(*GetStringUTFChars)(JNIEnv *, jstring, unsigned char *);
    void (*ReleaseStringUTFChars)(JNIEnv *, jstring, const char *);
    jsize (*GetArrayLength)(JNIEnv *, jarray);
    jbyteArray (*NewByteArray)(JNIEnv *, jsize);
    void (*GetByteArrayRegion)(JNIEnv *, jbyteArray, jsize, jsize, jbyte *);
    void (*SetByteArrayRegion)(JNIEnv *, jbyteArray, jsize, jsize, const jbyte *);
    jshortArray (*NewShortArray)(JNIEnv *, jsize);
    void (*SetShortArrayRegion)(JNIEnv *, jshortArray, jsize, jsize, const short *);
    jint (*RegisterNatives)(JNIEnv *, jclass, const void *, jint);
    jint (*Throw)(JNIEnv *, jthrowable);
    jthrowable (*ExceptionOccurred)(JNIEnv *);
    unsigned char (*ExceptionCheck)(JNIEnv *);
    void (*ExceptionDescribe)(JNIEnv *);
    void (*ExceptionClear)(JNIEnv *);
    void (*FatalError)(JNIEnv *, const char *);
};
struct JNIInvokeInterface {
    void *reserved0;
    void *reserved1;
    void *reserved2;
    jint (*DestroyJavaVM)(JavaVM *);
    jint (*AttachCurrentThread)(JavaVM *, JNIEnv **, void *);
    jint (*DetachCurrentThread)(JavaVM *);
    jint (*GetEnv)(JavaVM *, void **, jint);
    jint (*AttachCurrentThreadAsDaemon)(JavaVM *, JNIEnv **, void *);
};
typedef const struct JNINativeInterface *JNIEnv;
typedef const struct JNIInvokeInterface *JavaVM;
#endif
