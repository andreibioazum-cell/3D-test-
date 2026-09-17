#!/usr/bin/env python3
"""Native storage checks: settings.dat on the device and the cloud profile JSON.

Compiles the REAL net.c (the same translation unit the Android build uses, with
temporary Android/JNI header stubs — this is not a PC build) and runs it three
times against a real temporary data directory:

  write  — net_save_settings must create settings.dat;
  read   — a fresh process must read the very same file back (a "restart");
  cloud  — on a device without settings.dat the profile JSON is adopted, and on
           a device that has one the local file wins (returns 1, keeps its data);
  legacy — an old settings.dat with a mod list is read without errors and the
           next save rewrites it without mods (the mod screen is gone).

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
    /* В настоящем jni.h эти параметры — void*: так один заголовок работает и в C, и в C++. */
    int (*AttachCurrentThread)(void *vm, void *env, void *args);
    int (*DetachCurrentThread)(void *vm);
    int (*GetEnv)(void *vm, void *env, int version);
};
typedef const struct JNINativeInterface *JNIEnv;
typedef const struct JNIInvokeInterface *JavaVM;
#endif
"""

# Тест включает настоящий net.c, поэтому видит и статические функции модуля
# (settings_read/settings_write/settings_sync_with_cloud).
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

/* Профиль облака со старыми полями модов (mods_n/mods): клиент их больше не
 * читает и не пишет, но чужой старый профиль не должен ничего ломать. */
static const char *CLOUD_PROFILE =
    "{\"nick\":\"tester\",\"cups\":10,\"candies\":5,\"cls\":3,\"azum\":1,\"santa\":0,"
    "\"ebuc\":1,\"level\":1,\"levels\":1,\"lang\":1,\"hitboxes\":0,\"musicvol\":40,\"mods_n\":2,"
    "\"mods\":\"winter.zip|my mod.zip\"}";

static int run_write(const char *dir) {
    net_set_data_path(dir);
    net_save_settings(1, 0);
    assert(net_load_language() == 1);
    assert(net_load_hitboxes() == 0);
    /* Громкость музыки: значение вне диапазона приводится к 0..100. */
    net_save_music_volume(35);
    assert(net_load_music_volume() == 35);
    net_save_music_volume(150);
    assert(net_load_music_volume() == 100);
    net_save_music_volume(35);
    assert(net_load_music_volume() == 35);
    /* Устройство уже сохранено — облако ничего не отнимает. */
    assert(settings_sync_with_cloud(CLOUD_PROFILE) == 1);
    assert(net_load_language() == 1);
    assert(net_load_hitboxes() == 0);
    assert(net_load_music_volume() == 35);
    puts("native write: settings.dat created, local wins over cloud");
    return 0;
}

static int run_read(const char *dir) {
    net_set_data_path(dir);
    assert(net_load_language() == 1);
    assert(net_load_hitboxes() == 0);
    assert(net_load_music_volume() == 35);
    puts("native read: a fresh process restores language, hitboxes and music volume");
    return 0;
}

static int run_cloud(const char *dir) {
    net_set_data_path(dir);
    /* Чистое устройство: файла нет, поэтому настройки берутся из профиля.
     * Старые поля модов в профиле просто игнорируются. */
    assert(settings_sync_with_cloud(CLOUD_PROFILE) == 0);
    assert(net_load_language() == 1);
    assert(net_load_hitboxes() == 0);
    assert(net_load_music_volume() == 40);
    /* Файл на устройстве проверяет python-обвязка теста. */
    puts("native cloud: profile settings adopted on a device without settings.dat");
    return 0;
}

/* Старый settings.dat со списком модов: читается без ошибок, а следующая
 * запись оставляет в файле только язык и хитбоксы. */
static int run_legacy(const char *dir) {
    net_set_data_path(dir);
    assert(net_load_language() == 1);
    assert(net_load_hitboxes() == 0);
    /* В старом файле musicvol нет — подставляется значение по умолчанию. */
    assert(net_load_music_volume() == 70);
    net_save_settings(0, 1);
    assert(net_load_language() == 0);
    assert(net_load_hitboxes() == 1);
    puts("native legacy: an old mod list is ignored and dropped on the next save");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: test <write|read|cloud> <dir>\n"); return 2; }
    if (strcmp(argv[1], "write") == 0) return run_write(argv[2]);
    if (strcmp(argv[1], "read") == 0) return run_read(argv[2]);
    if (strcmp(argv[1], "cloud") == 0) return run_cloud(argv[2]);
    if (strcmp(argv[1], "legacy") == 0) return run_legacy(argv[2]);
    fprintf(stderr, "unknown mode '%s'\n", argv[1]);
    return 2;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="cubic-settings-") as directory:
        temp = Path(directory)
        android = temp / "android"
        android.mkdir()
        (android / "log.h").write_text(ANDROID_LOG_H)
        (android / "asset_manager.h").write_text("typedef struct AAssetManager AAssetManager;\n")
        (android / "native_window.h").write_text("typedef struct ANativeWindow ANativeWindow;\n")
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
        subprocess.run([*run, "write", str(data)], check=True)
        saved = (data / "settings.dat").read_text(encoding="utf-8")
        assert "lang 1" in saved and "hitboxes 0" in saved, saved
        assert "musicvol 35" in saved, saved
        assert "mod" not in saved, saved
        subprocess.run([*run, "read", str(data)], check=True)

        fresh = temp / "fresh-device"
        fresh.mkdir()
        subprocess.run([*run, "cloud", str(fresh)], check=True)
        assert (fresh / "settings.dat").exists()
        adopted = (fresh / "settings.dat").read_text(encoding="utf-8")
        assert "lang 1" in adopted and "hitboxes 0" in adopted, adopted
        assert "musicvol 40" in adopted, adopted

        legacy = temp / "legacy-device"
        legacy.mkdir()
        (legacy / "settings.dat").write_text(
            "lang 1\nhitboxes 0\nmods 2\nmod winter.zip\nmod my mod.zip\n",
            encoding="utf-8")
        subprocess.run([*run, "legacy", str(legacy)], check=True)
        rewritten = (legacy / "settings.dat").read_text(encoding="utf-8")
        assert "lang 0" in rewritten and "hitboxes 1" in rewritten, rewritten
        assert "musicvol 70" in rewritten, rewritten
        assert "mod" not in rewritten, rewritten
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
