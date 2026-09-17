#!/usr/bin/env python3
"""Проверка синтаксиса DimScript v2 на примере из спецификации языка.

Пример ClickerGame (классы, private-поля, конструктор, self, static-вызовы,
local с типом, new, интерполяция $"...", import Dim.System.Gui, top-level
инструкции) компилируется дословно:

  * против настоящего стандартного модуля std/Dim/System/Gui.ds — проверка,
    что стандартная библиотека и пример сходятся без правок;
  * против тестового мока Gui — программа реально выполняется на хосте:
    игровой цикл крутится, клики и апгрейды считаются, а интерполированные
    строки $"Баланс: {self.score}" приходят в ds_log ровно теми, которые
    даёт независимая симуляция на Python.

Запуск: python3 tools/test_dimscript_v2.py
"""

import os
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from ds_compiler import DimScriptCompiler  # noqa: E402

CLICKER_EXAMPLE = '''import Dim.System.Gui

class ClickerGame
    -- Приватные поля с типами данных (как в Java/C#)
    private int score = 0
    private int clickPower = 1
    private int upgradeCost = 10
    private Window win

    -- Конструктор класса
    public function new()
        -- Инициализируем окно через встроенный GUI движок
        self.win = Gui.init("DimScript Lua-C# Hybrid", 400, 500)
    end

    -- Главный игровой цикл
    public function start() -> void
        while self.win.isOpen() do
            self.win.beginFrame()

            -- Вывод текста. Строки из C# ($), блоки из Lua (end)
            Gui.text(24, $"Баланс: {self.score}")
            Gui.text(18, $"Сила клика: +{self.clickPower}")
            Gui.space(20)

            -- Настройка кнопки в объектном стиле Java/C#
            local ButtonStyle clickBtnStyle = new ButtonStyle()
            clickBtnStyle.bgColor = Color.fromRGB(41, 128, 185)
            clickBtnStyle.textColor = Color.fromRGB(255, 255, 255)

            if Gui.button("КЛИКНУТЬ!", 200, 50, clickBtnStyle) then
                self.score = self.score + self.clickPower
            end

            Gui.space(15)

            -- Кнопка апгрейда с динамической сменой цвета
            local ButtonStyle upgradeStyle = new ButtonStyle()
            upgradeStyle.textColor = Color.fromRGB(255, 255, 255)

            if self.score >= self.upgradeCost then
                upgradeStyle.bgColor = Color.fromRGB(39, 174, 96)
            else
                upgradeStyle.bgColor = Color.fromRGB(149, 165, 166)
            end

            if Gui.button($"Улучшение ({self.upgradeCost})", 200, 50, upgradeStyle) then
                if self.score >= self.upgradeCost then
                    self.score = self.score - self.upgradeCost
                    self.clickPower = self.clickPower + 1
                    self.upgradeCost = (self.upgradeCost * 15) / 10
                end
            end

            self.win.endFrame()
        end

        self.win.shutdown()
    end
end

-- Точка входа (top-level выполнение как в Lua/Python)
local ClickerGame game = new ClickerGame()
game.start()
'''

MOCK_GUI = '''-- Тестовый мок Dim.System.Gui: кадров ограничен, кнопки всегда «нажаты»,
-- каждый Gui.text уходит в ds_log, чтобы Python мог сверить строки.
private number mock_frames = 0

class ButtonStyle
    public color bgColor = 0
    public color textColor = 0
end

class Color
    public static function fromRGB(number r, number g, number b) -> color
        return 0xFF000000 + r * 65536 + g * 256 + b
    end
end

class Window
    private number open = 1
    public string title = ""
    public function isOpen() -> bool
        return self.open
    end
    public function beginFrame() -> void
        mock_frames += 1
        if mock_frames >= 40 then
            self.open = 0
        end
    end
    public function endFrame() -> void
    end
    public function shutdown() -> void
        self.open = 0
    end
end

class Gui
    public static function init(string title, number w, number h) -> Window
        local Window win = new Window()
        win.title = title
        return win
    end
    public static function text(number y, string s) -> void
        ds_log(s)
    end
    public static function space(number h) -> void
    end
    public static function button(string label, number w, number h, ButtonStyle style) -> bool
        return 1
    end
end
'''

HARNESS = '''
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "net.h"

double ds_mouse_x = 0, ds_mouse_y = 0;
int mouse_clicked = 0;

static char pool[8][512];
static int pool_i;
char *ds_concat(const char *a, const char *b) {
    pool_i = (pool_i + 1) & 7;
    snprintf(pool[pool_i], sizeof pool[pool_i], "%s%s", a ? a : "", b ? b : "");
    return pool[pool_i];
}
char *ds_num_to_string(double v) {
    pool_i = (pool_i + 1) & 7;
    snprintf(pool[pool_i], sizeof pool[pool_i], "%g", v);
    return pool[pool_i];
}
void ds_log(const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    vprintf(f, ap);
    va_end(ap);
    putchar(10);
}
void ds_runtime_error(const char *f, ...) {
    fputs(f, stderr);
    abort();
}
void ds_set_asset_manager(AAssetManager *assets) {
    (void)assets;
}
int main(void) {
    init(NULL);
    return 0;
}
'''

FRAMES = 40


def simulate():
    """Независимая симуляция примера: те же строки, что печатает программа."""
    score, power, cost = 0.0, 1.0, 10.0
    lines = []
    for _ in range(FRAMES):
        lines.append(f"Баланс: {'%g' % score}")
        lines.append(f"Сила клика: +{'%g' % power}")
        score += power                      # кнопка «КЛИКНУТЬ!» всегда нажата
        if score >= cost:                   # кнопка «Улучшение» всегда нажата
            score -= cost
            power += 1
            cost = (cost * 15) / 10
    return lines


def check_compiles_with_real_std():
    c = DimScriptCompiler()
    with tempfile.TemporaryDirectory(prefix="dim-v2-std-") as directory:
        temp = Path(directory)
        (temp / "clicker.ds").write_text(CLICKER_EXAMPLE, encoding="utf-8")
        assert c.compile([str(temp / "clicker.ds")], str(temp / "game.c"))
        assert not c.errors and not c.warnings, (c.errors, c.warnings)
        text = (temp / "game.c").read_text(encoding="utf-8")
    assert 'ds_stc_Gui_init(' in text and 'ds_mth_Window_isOpen(' in text
    assert 'ds_new_ButtonStyle()' in text and 'ds_stc_Color_fromRGB(' in text
    print("example v2: компилируется дословно против std/Dim/System/Gui.ds")


def check_runs_with_mock():
    with tempfile.TemporaryDirectory(prefix="dim-v2-run-") as directory:
        temp = Path(directory)
        (temp / "clicker.ds").write_text(CLICKER_EXAMPLE, encoding="utf-8")
        mock_dir = temp / "Dim" / "System"
        mock_dir.mkdir(parents=True)
        (mock_dir / "Gui.ds").write_text(MOCK_GUI, encoding="utf-8")
        android = temp / "android"
        android.mkdir()
        (android / "asset_manager.h").write_text(
            "typedef struct AAssetManager AAssetManager;\n")
        (android / "log.h").touch()
        (android / "native_window.h").write_text(
            "typedef struct ANativeWindow ANativeWindow;\n")
        (temp / "test.c").write_text(HARNESS)
        c = DimScriptCompiler()
        assert c.compile([str(temp / "clicker.ds")], str(temp / "game.c"))
        assert not c.errors and not c.warnings
        subprocess.run([
            *shlex.split(os.environ.get("CC", "cc")), "-std=gnu99", "-O0",
            "-Werror=implicit-function-declaration",
            "-Werror=incompatible-pointer-types",
            "-I", str(temp), "-I", str(ROOT),
            str(temp / "test.c"), str(temp / "game.c"),
            "-lm", "-o", str(temp / "clicker"),
        ], check=True)
        proc = subprocess.run([str(temp / "clicker")], check=True,
                              capture_output=True, text=True)
    got = proc.stdout.splitlines()
    want = simulate()
    assert got == want, f"first diff: {next((i, a, b) for i, (a, b) in enumerate(zip(got, want)) if a != b) if len(got) == len(want) else (len(got), len(want))}"
    print(f"example v2: выполнен на хосте, {len(got)} строк вывода совпали "
          f"с симуляцией (интерполяция $\"...\" верна)")


def main():
    check_compiles_with_real_std()
    check_runs_with_mock()
    print("dimscript v2 example: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
