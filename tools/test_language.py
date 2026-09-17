#!/usr/bin/env python3
"""DimScript language checks: the readable syntax must compile, not just look nice.

What is guarded here:
  1. Each new construct (`#` comments, `and`/`or`/`not`, `elif`, `while`,
     `+= -= *= /=`, `break`, `continue`, `for ... to ... step`) produces the
     expected C in the generated file — a construct that parses but emits
     nothing used to vanish from game.c silently.
  2. The new math commands (`min`, `max`, `abs`, `round`, `sign`, `mod`,
     `trunc`) are compiled AND executed against the real implementations in
     native/runtime/core.inc, so a wrong ds_round() fails here.
  3. The whole real game script still compiles with zero errors and zero
     warnings, i.e. the new syntax did not break anything that already worked.

Needs a host C compiler (CC) for part 2. No Android, no Firebase.
"""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from ds_compiler import DimScriptCompiler, num_value  # noqa: E402
from gen import find_ds_files  # noqa: E402

ANDROID_LOG_H = """
#ifndef STUB_ANDROID_LOG_H
#define STUB_ANDROID_LOG_H
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_ERROR 6
static inline int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    (void)prio; (void)tag; (void)fmt; return 0;
}
static inline int __android_log_write(int prio, const char *tag, const char *text) {
    (void)prio; (void)tag; (void)text; return 0;
}
static inline int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap) {
    (void)prio; (void)tag; (void)fmt; (void)ap; return 0;
}
#endif
"""

# A tiny program that calls the real native math helpers.
MATH_HARNESS = r'''
#include <stdarg.h>
#include <stdio.h>
#include "runtime.h"
#include "native/runtime/core.inc"

static int failures = 0;

static void expect(const char *what, double got, double want) {
    if (got != want) {
        printf("FAIL %s: got %g, want %g\n", what, got, want);
        failures++;
    }
}

int main(void) {
    expect("min(3,7)", ds_min(3, 7), 3);
    expect("max(3,7)", ds_max(3, 7), 7);
    expect("min(-2,-9)", ds_min(-2, -9), -9);
    expect("abs(-4.5)", ds_abs(-4.5), 4.5);
    expect("abs(0)", ds_abs(0), 0);
    expect("round(2.4)", ds_round(2.4), 2);
    expect("round(2.5)", ds_round(2.5), 3);
    expect("round(-2.5)", ds_round(-2.5), -3);
    expect("sign(-8)", ds_sign(-8), -1);
    expect("sign(0)", ds_sign(0), 0);
    expect("sign(8)", ds_sign(8), 1);
    expect("mod(7,3)", ds_mod(7, 3), 1);
    expect("mod(-7,3)", ds_mod(-7, 3), -1);
    expect("mod(7,0)", ds_mod(7, 0), 0);
    expect("trunc(2.9)", ds_trunc(2.9), 2);
    expect("trunc(-2.9)", ds_trunc(-2.9), -2);
    if (failures) {
        printf("%d math helper(s) wrong\n", failures);
        return 1;
    }
    printf("native math helpers: min/max/abs/round/sign/mod/trunc behave\n");
    return 0;
}
'''


def compile_snippet(source, tmp=None):
    """Compile a piece of DimScript and return the generated C text.

    The snippet is wrapped in a function because blocks (`if`/`loop`/`for`) live
    inside functions — a top-level `end` is a compile error in real scripts too.
    """
    source = 'function lang_probe()\n' + source + 'end\n'
    c = DimScriptCompiler()
    with tempfile.TemporaryDirectory(prefix="cubic-lang-snippet-") as directory:
        temp = Path(directory)
        ds = temp / "snippet.ds"
        ds.write_text(source, encoding="utf-8")
        out_c = temp / "snippet.c"
        # compile() — тот же вход, которым пользуется gen.py: загрузка, разбор,
        # генерация и запись файла.
        if not c.compile([str(ds)], str(out_c)):
            raise AssertionError(f"snippet did not compile: {source}")
        out = out_c.read_text(encoding="utf-8")
    if c.errors or c.warnings:
        raise AssertionError(
            f"snippet produced {c.errors} error(s), {c.warnings} warning(s): {source}")
    return out


def check_syntax():
    cases = [
        # (source, fragment that must appear in the generated C)
        ('num a = 1\n# это комментарий, а не код\n', None),
        ('num alive = 1\nif alive and not alive\nds_log 1\nend\n', 'if (alive && !alive) {'),
        ('num x = 0\nif x or x == 1\nds_log 2\nend\n', 'if (x || x == 1) {'),
        ('num x = 0\nif x\nds_log 1\nelif x == 2\nds_log 2\nelse\nds_log 3\nend\n',
         '} else if (x == 2) {'),
        ('num x = 0\nif x\nds_log 1\nelse if x == 2\nds_log 2\nend\n',
         '} else if (x == 2) {'),
        ('num i = 0\nwhile i < 3\ni += 1\nend\n', 'while (i < 3) {'),
        ('num hp = 10\nhp -= 4\nhp *= 2\nhp /= 3\n', 'hp -= 4;'),
        ('num i = 0\nloop i < 5\nif i == 2\ncontinue\nend\ni += 1\nend\n', 'continue;'),
        ('num i = 0\nloop i < 5\nif i == 2\nbreak\nend\ni += 1\nend\n', 'break;'),
        ('for i = 1 to 3\nds_log i\nend\n', 'double i = 1;'),
        ('for i = 1 to 3\nds_log i\nend\n', 'while (i <= 3) {'),
        ('for i = 1 to 3\nds_log i\nend\n', 'i += 1;'),
        ('for i = 10 to 1\nds_log i\nend\n', 'while (i >= 1) {'),
        ('for i = 10 to 1\nds_log i\nend\n', 'i += -1;'),
        ('for i = 0 to 8 step 2\nds_log i\nend\n', 'i += 2;'),
        ('num r = min(3, 4) + max(1, 9) + abs(-2) + round(1.5) + sign(-3) + mod(7, 4)'
         ' + trunc(2.7)\n',
         'ds_min(3, 4) + ds_max(1, 9) + ds_abs(-2) + ds_round(1.5) + ds_sign(-3) + '
         'ds_mod(7, 4) + ds_trunc(2.7);'),
        # '#' и слова-операторы не должны трогать содержимое строк.
        ('string s = "and or # not"\nds_log s\n', '"and or # not"'),
    ]
    with tempfile.TemporaryDirectory(prefix="cubic-lang-") as directory:
        for source, fragment in cases:
            out = compile_snippet(source, tmp=directory)
            if fragment and fragment not in out:
                raise AssertionError(f"missing {fragment!r} in output for:\n{source}\n{out}")
    print("syntax: comments, and/or/not, elif, while, += -= *= /=, break, continue, for")


def check_num_value():
    assert num_value('0xFF00FF') == 16711935.0, num_value('0xFF00FF')
    assert num_value('-3') == -3.0
    assert num_value('hp + 1') is None
    print("syntax: hex/decimal bounds are compared correctly")


def check_math_helpers():
    with tempfile.TemporaryDirectory(prefix="cubic-lang-c-") as directory:
        temp = Path(directory)
        android = temp / "android"
        android.mkdir()
        (android / "log.h").write_text(ANDROID_LOG_H)
        (android / "asset_manager.h").write_text("typedef struct AAssetManager AAssetManager;\n")
        (android / "native_window.h").write_text("typedef struct ANativeWindow ANativeWindow;\n")
        (temp / "test.c").write_text(MATH_HARNESS)
        subprocess.run([
            *shlex.split(os.environ.get("CC", "cc")), "-std=gnu99", "-O0",
            "-D_POSIX_C_SOURCE=200809L",
            "-Werror=implicit-function-declaration",
            "-I", str(temp), "-I", str(ROOT),
            str(temp / "test.c"), "-lm", "-lpthread", "-o", str(temp / "test"),
        ], check=True)
        subprocess.run([str(temp / "test")], check=True)


def check_whole_game():
    """The real 37-file script must still compile clean with the new parser."""
    c = DimScriptCompiler()
    files = find_ds_files(str(ROOT / "game/scripts"))
    assert len(files) >= 30, f"expected the full script set, got {len(files)} files"
    with tempfile.TemporaryDirectory(prefix="cubic-lang-game-") as directory:
        out_c = Path(directory) / "game.c"
        if not c.compile(files, str(out_c)):
            raise AssertionError("game script did not compile")
        assert out_c.stat().st_size > 100_000, out_c.stat().st_size
        text = out_c.read_text(encoding="utf-8")
        # 'else if' в скриптах 47 штук; однажды условие уезжало в C как
        # '} else if (if i == 1) {' и game.c не собирался.
        assert '} else if (if ' not in text, "broken 'else if' in generated C"
        assert '} elif ' not in text and 'else if (if' not in text
    if c.errors or c.warnings:
        raise AssertionError(f"game script: {c.errors} error(s), {c.warnings} warning(s)")
    print(f"game script: {len(files)} files compile with 0 errors and 0 warnings")


def main():
    check_syntax()
    check_num_value()
    check_math_helpers()
    check_whole_game()
    print("language checks: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
