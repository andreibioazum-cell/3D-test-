#!/usr/bin/env python3
"""Native progress checks: a bought class must survive a relogin.

The bug this guards: after a rules update the buk (and only it) vanished after
relogging even though the other classes stayed. Two causes in native/net:

  * the very first net_save_progress_all() of a launch ran before progress.dat
    had ever been read into `pg`, so the "sticky ownership" merge saw nothing
    and the file was overwritten with whatever the script had in memory;
  * a cloud PATCH that failed (no network, 5xx, app killed mid-flight) was
    simply logged; the next login then took the stale cloud record and the
    purchase was gone. Now a failed/unfinished PATCH leaves progress.dirty on
    the device and the next login pushes the local progress back.

Compiles the REAL net.c (with temporary Android/JNI header stubs) and drives
it against a real temporary data directory:

  first-save  — save before any read: previously bought classes are kept;
  offline-buy — buy offline (no session), then log in against a cloud record
                without the class: class, currencies and selection come from
                the device and the merge asks to push (returns 1);
  clean       — no dirty mark and a fresher cloud record: the cloud wins.

Requires a host C compiler (CC).
"""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from test_settings_storage import ANDROID_LOG_H, JNI_H  # noqa: E402

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

/* Облако «забыло» бука и помнит старые леденцы. */
static const char *CLOUD_NO_BUK =
    "{\"nick\":\"tester\",\"cups\":100,\"candies\":200,\"cls\":0,\"azum\":1,\"santa\":0,"
    "\"level\":0,\"levels\":0}";
/* Облако свежее устройства: на другом телефоне выбрали Азума и заработали кубки. */
static const char *CLOUD_FRESH =
    "{\"nick\":\"tester\",\"cups\":120,\"candies\":50,\"cls\":1,\"azum\":1,\"santa\":0,"
    "\"ebuc\":1,\"level\":0,\"levels\":0}";

/* Первый запуск: бук уже куплен ранее (файл есть), скрипт сохраняет ДО того,
 * как кто-либо вызвал net_load_*. Владение не должно потеряться. */
static int run_first_save(const char *dir) {
    net_set_data_path(dir);
    /* Скрипт с «пустой» памятью сохраняет только кубки. */
    net_save_progress_all(7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    assert(net_load_ebuc() == 1);
    assert(net_load_azum() == 1);
    assert(net_load_cups() == 7);
    puts("first save: classes bought earlier survive a save issued before any read");
    return 0;
}

/* Покупка без сессии, затем вход. */
static int run_offline_buy(const char *dir) {
    net_set_data_path(dir);
    assert(net_load_ebuc() == 0);
    net_save_progress_all(100, 50, 3, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    assert(cloud_dirty_get() == 1);
    int push = apply_user_json_keep_local(CLOUD_NO_BUK);
    assert(push == 1);
    assert(net_load_ebuc() == 1);
    assert(net_load_class() == 3);
    assert(net_load_candies() == 50);   /* леденцы не «вернулись» из облака */
    assert(net_load_cups() == 100);
    puts("offline buy: the buk, its selection and the spent candies come back from the device");
    return 0;
}

/* Чистое состояние: облако свежее, метки нет - облако побеждает. */
static int run_clean(const char *dir) {
    net_set_data_path(dir);
    net_save_progress_all(100, 50, 3, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    cloud_dirty_set(0);
    int push = apply_user_json_keep_local(CLOUD_FRESH);
    assert(push == 0);
    assert(net_load_class() == 1);
    assert(net_load_cups() == 120);
    assert(net_load_ebuc() == 1);
    puts("clean: with no dirty mark a fresher cloud record wins, ownership intact");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: test <mode> <dir>\n"); return 2; }
    if (strcmp(argv[1], "first-save") == 0) return run_first_save(argv[2]);
    if (strcmp(argv[1], "offline-buy") == 0) return run_offline_buy(argv[2]);
    if (strcmp(argv[1], "clean") == 0) return run_clean(argv[2]);
    fprintf(stderr, "unknown mode '%s'\n", argv[1]);
    return 2;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="cubic-cloud-") as directory:
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
        run = [str(temp / "test")]

        # progress.dat: cups cls azum santa candies level levels 8×levels ebuc bp skin
        first = temp / "first"
        first.mkdir()
        (first / "progress.dat").write_text(
            "10 3 1 0 5 0 0 0 0 0 0 0 0 0 0 1 0 0\n", encoding="utf-8")
        subprocess.run([*run, "first-save", str(first)], check=True)
        saved = (first / "progress.dat").read_text(encoding="utf-8").split()
        assert saved[2] == "1" and saved[15] == "1", saved

        offline = temp / "offline"
        offline.mkdir()
        subprocess.run([*run, "offline-buy", str(offline)], check=True)
        assert (offline / "progress.dirty").exists()

        clean = temp / "clean"
        clean.mkdir()
        subprocess.run([*run, "clean", str(clean)], check=True)
        assert not (clean / "progress.dirty").exists()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
