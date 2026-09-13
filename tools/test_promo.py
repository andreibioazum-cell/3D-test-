#!/usr/bin/env python3
"""Native promo checks: personal code from nick, streak and promo.dat file.

Compiles the REAL net.c (the same translation unit the Android build uses,
with temporary Android/JNI header stubs — this is not a PC build) and runs it
against a real temporary data directory:

  write  — the code is deterministic per nick (case-insensitive), unique per
           nick, in the CB4-XXXX-XXXX-XXXX format; streak/used live in
           promo.dat;
  read   — a fresh process ("restart") restores used/streak from promo.dat;
  cloud  — on a clean device the cloud profile wins: promo_used=1 is adopted,
           a missing promo is registered (the PATCH itself is a no-op without
           the JVM, exactly as in other storage tests).

Requires a host C compiler (CC).
"""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

ANDROID_LOG_H = """
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_ERROR 6
int __android_log_print(int prio, const char *tag, const char *fmt, ...);
"""

JNI_H = """
#ifndef STUB_JNI_H
#define STUB_JNI_H
typedef int jint; typedef unsigned char jbyte; typedef int jsize; typedef int jboolean;
#define JNI_OK 0
#define JNI_TRUE 1
#define JNI_FALSE 0
#define JNI_VERSION_1_6 0x00010006
typedef void *jobject; typedef void *jclass; typedef void *jstring;
typedef void *jbyteArray; typedef void *jthrowable; typedef void *jmethodID;
struct JNINativeInterface {
    void *reserved0;
    jclass (*FindClass)(void *env, const char *name);
    jobject (*NewObject)(void *env, jclass cls, jmethodID id, ...);
    jclass (*GetObjectClass)(void *env, jobject obj);
    jmethodID (*GetMethodID)(void *env, jclass cls, const char *name, const char *sig);
    jobject (*CallObjectMethod)(void *env, jobject obj, jmethodID id, ...);
    jint (*CallIntMethod)(void *env, jobject obj, jmethodID id, ...);
    void (*CallVoidMethod)(void *env, jobject obj, jmethodID id, ...);
    jstring (*NewStringUTF)(void *env, const char *bytes);
    const char *(*GetStringUTFChars)(void *env, jstring s, jboolean *is_copy);
    void (*ReleaseStringUTFChars)(void *env, jstring s, const char *bytes);
    jbyteArray (*NewByteArray)(void *env, jsize len);
    void (*SetByteArrayRegion)(void *env, jbyteArray a, jsize start, jsize len, const jbyte *buf);
    void (*GetByteArrayRegion)(void *env, jbyteArray a, jsize start, jsize len, jbyte *buf);
    void (*ExceptionClear)(void *env);
    jboolean (*ExceptionCheck)(void *env);
    jthrowable (*ExceptionOccurred)(void *env);
    int (*PushLocalFrame)(void *env, jint capacity);
    jobject (*PopLocalFrame)(void *env, jobject result);
};
struct JNIInvokeInterface {
    void *reserved0;
    int (*AttachCurrentThread)(void *vm, void *env, void *args);
    int (*DetachCurrentThread)(void *vm);
    int (*GetEnv)(void *vm, void *env, int version);
};
typedef const struct JNINativeInterface *JNIEnv;
typedef const struct JNIInvokeInterface *JavaVM;
#endif
"""

# Тест включает настоящий net.c, поэтому видит и статические функции модуля
# (promo_code_for_nick, promo_sync_with_cloud, сессию lg).
HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "net.c"

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    (void)prio; (void)tag; (void)fmt;
    return 0;
}
void ds_console_log(int is_error, const char *format, ...) { (void)is_error; (void)format; }

static int run_write(const char *dir) {
    char code1[24], code2[24], again[24];
    net_set_data_path(dir);
    promo_code_for_nick("Andrei", code1, sizeof(code1));
    promo_code_for_nick("andrei", again, sizeof(again));
    promo_code_for_nick("Boris", code2, sizeof(code2));
    /* Формат 3 буквы + 1 цифра: 4 символа, например ABC1. */
    assert(strlen(code1) == 4);
    assert(strlen(code2) == 4);
    int letters1=0, digits1=0;
    for(int i=0;i<4;i++){ if(code1[i]>='A'&&code1[i]<='Z') letters1++; else if(code1[i]>='0'&&code1[i]<='9') digits1++; }
    assert(letters1==3 && digits1==1);
    /* Тот же ник (без учёта регистра) - тот же код, другой ник - другой. */
    assert(strcmp(code1, again) == 0);
    assert(strcmp(code1, code2) != 0);
    assert(strcmp(net_promo_code(), "") == 0);  /* без сессии кода нет */
    /* Стрик и флаг живут в promo.dat. */
    assert(net_promo_used() == 0);
    assert(net_promo_streak() == 0);
    net_promo_bump_streak();
    net_promo_bump_streak();
    assert(net_promo_streak() == 2);
    net_promo_mark_used();
    assert(net_promo_used() == 1);
    net_promo_reset_streak();
    assert(net_promo_streak() == 0);
    printf("PROMO_CODE=%s\n", code1);
    puts("promo write: personal code from nick, streak and used flag in promo.dat");
    return 0;
}

static int run_read(const char *dir) {
    net_set_data_path(dir);
    assert(net_promo_used() == 1);
    assert(net_promo_streak() == 0);
    puts("promo read: a fresh process restores used/streak from promo.dat");
    return 0;
}

/* Чистое устройство: облако решает — promo_used=1 переносится локально,
 * отсутствующий promo дописывается (PATCH без JVM - no-op, как и везде). */
static int run_cloud(const char *dir) {
    net_set_data_path(dir);
    lg_lock();
    lg.status = NET_LOGIN_OK;
    snprintf(lg.session_nick, sizeof(lg.session_nick), "tester");
    lg_unlock();
    assert(promo_sync_with_cloud("{\"nick\":\"tester\",\"promo_used\":1}") == 1);
    assert(net_promo_used() == 1);
    assert(strlen(net_promo_code()) == 4); /* код не пустой, формат 3L+1D */
    {
        const char *c = net_promo_code();
        int l=0,d=0;
        for(int i=0;i<4;i++){ if(c[i]>='A'&&c[i]<='Z') l++; else if(c[i]>='0'&&c[i]<='9') d++; }
        assert(l==3 && d==1);
    }
    puts("promo cloud: cloud promo_used adopted, code derived from the session nick");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: test <write|read|cloud> <dir>\n"); return 2; }
    if (strcmp(argv[1], "write") == 0) return run_write(argv[2]);
    if (strcmp(argv[1], "read") == 0) return run_read(argv[2]);
    if (strcmp(argv[1], "cloud") == 0) return run_cloud(argv[2]);
    fprintf(stderr, "unknown mode '%s'\n", argv[1]);
    return 2;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="cubic-promo-") as directory:
        temp = Path(directory)
        android = temp / "android"
        android.mkdir()
        (android / "log.h").write_text(ANDROID_LOG_H)
        (android / "asset_manager.h").write_text(
            "typedef struct AAssetManager AAssetManager;\n")
        (android / "native_window.h").touch()
        (temp / "jni.h").write_text(JNI_H)
        (temp / "test.c").write_text(HARNESS)
        subprocess.run([
            *shlex.split(os.environ.get("CC", "cc")), "-std=gnu99", "-O0",
            "-D_POSIX_C_SOURCE=200809L", "-D__ANDROID__",
            "-Werror=implicit-function-declaration",
            "-I", str(temp), "-I", str(ROOT),
            str(temp / "test.c"), "-lm", "-lpthread", "-o", str(temp / "test"),
        ], check=True)

        data = temp / "data"
        data.mkdir()
        run = [str(temp / "test")]
        written = subprocess.run(
            [*run, "write", str(data)], check=True,
            capture_output=True, text=True).stdout
        assert "PROMO_CODE=" in written, written
        # validate 3L+1D format
        line = [l for l in written.splitlines() if "PROMO_CODE=" in l][0]
        code = line.split("=")[1].strip()
        assert len(code) == 4, code
        l_cnt = sum(1 for ch in code if 'A' <= ch <= 'Z')
        d_cnt = sum(1 for ch in code if '0' <= ch <= '9')
        assert l_cnt == 3 and d_cnt == 1, code
        saved = (data / "promo.dat").read_text(encoding="utf-8")
        assert "used 1" in saved and "streak 0" in saved, saved
        subprocess.run([*run, "read", str(data)], check=True)

        fresh = temp / "fresh-device"
        fresh.mkdir()
        subprocess.run([*run, "cloud", str(fresh)], check=True)
        adopted = (fresh / "promo.dat").read_text(encoding="utf-8")
        assert "used 1" in adopted, adopted
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
