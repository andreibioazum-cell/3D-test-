#!/usr/bin/env python3
"""Юридический минимум: гейт согласия об эпилепсии и экран приватности.

Проверяет связность механизма, описанного в README («Приватность,
предупреждения и дисклеймеры»), чисто по исходникам, без компиляции:

  gate    — предупреждение об эпилепсии не скрывается само: update_warning
            больше не закрывает warn_open, вход только через legal_accept,
            а системная «Назад» на этом экране отдаёт управление Android
            (выход из игры), т.е. войти без согласия нельзя;
  stamp   — нажатие согласия пишется в settings.dat нативно
            (settings_mark_legal / строка "legal %d"), метка локальная и в
            облачный PATCH не попадает;
  privacy — экран ST_PRIVACY нарисован, достижим из настроек (8-я строка) и
            возвращается в настройки кнопкой «Назад»; текст есть на обоих
            языках;
  perms   — в манифесте ровно два разрешения: INTERNET и
            ACCESS_NETWORK_STATE (нет ничего про геолокацию, контакты и т.п.).
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def read(*parts):
    return (ROOT.joinpath(*parts)).read_text(encoding="utf-8")


def function_body(source, name):
    """Тело public function name ... end (DimScript)."""
    match = re.search(
        r"public function %s\(.*?\nend\n" % re.escape(name), source, re.S)
    assert match, "функция %s не найдена" % name
    return match.group(0)


def main():
    engine = read("game", "scripts", "core", "engine.ds")
    menu_input = read("game", "scripts", "ui", "menu_input.ds")
    menu_screens = read("game", "scripts", "ui", "menu_screens.ds")
    layout = read("game", "scripts", "ui", "layout.ds")
    locale = read("game", "scripts", "ui", "locale_core.ds")
    config = read("game", "scripts", "core", "config.ds")
    storage = read("native", "net", "settings_storage.inc")
    manifest = read("game", "AndroidManifest.xml")

    # --- гейт согласия: без авто-скрытия, вход только кнопкой -------------
    warn_update = function_body(engine, "update_warning")
    assert "warn_open = 0" not in warn_update, \
        "предупреждение снова скрывается само: %s" % warn_update
    assert "warn_hold" not in warn_update and "warn_fade" not in warn_update, \
        "остался таймер скрытия предупреждения"
    accept = function_body(menu_input, "legal_accept")
    assert "warn_open = 0" in accept and "settings_mark_legal()" in accept, \
        "согласие не закрывает гейт или не пишет метку: %s" % accept
    assert "if warn_ready < 1 then" in accept
    assert "warn_t = warn_t + dt" in warn_update and \
        "warn_ready = clamp(warn_t / warn_wait, 0, 1)" in warn_update, \
        "отсчёт секунд должен вестись по warn_t"
    assert "warn_t=0, warn_wait=3" in config
    touch_warn = function_body(menu_input, "touch_warn")
    assert "hit_warn_btn" in touch_warn and "legal_accept()" in touch_warn
    touch = function_body(engine, "touch")
    assert "touch_warn(x, y, action)" in touch, \
        "тапы на экране предупреждения не обрабатываются"
    assert not re.search(r"if warn_open == 1 \|\| studio_open == 1 then\s*\n\s*return\s*\n",
                         touch), "тапы на предупреждении всё ещё глотаются"
    back = function_body(engine, "back_pressed")
    assert re.search(r"if warn_open == 1 then\s*\n(?:\s*--[^\n]*\n)*\s*return 0", back), \
        "системная «Назад» на предупреждении не выходит из игры"
    draw_warn = function_body(menu_input, "draw_warning")
    assert "tr_legal_accept()" in draw_warn and "warn_btn_y()" in draw_warn, \
        "на экране предупреждения нет кнопки согласия"
    # Кнопка не «разгорается»: яркость постоянная, как в конце отсчёта.
    assert "brightness" not in draw_warn, \
        "кнопка согласия снова меняет яркость со временем"
    # Пока отсчёт идёт, в подписи держатся секунды «(N)», потом пропадают.
    assert "warn_wait - floor(warn_t)" in draw_warn and "warn_ready < 1" in draw_warn, \
        "на кнопке нет отсчёта секунд до разблокировки"
    assert "warn_btn_w" in layout and "hit_warn_btn" in layout

    # --- метка согласия: локальная, переживает перезапуск ----------------
    assert 'sscanf(line, "legal %d", &value)' in storage
    assert '"legal %d\\n"' in storage or "legal %d\\n" in storage
    assert "void settings_mark_legal(void)" in storage
    assert "double settings_legal_ts(void)" in storage
    push = re.search(r"static void settings_push_to_cloud.*?\n}\n", storage, re.S)
    assert push and "legal" not in push.group(0).split("snprintf(body")[1], \
        "метка согласия не должна уходить в облако"
    net_h = read("net.h")
    assert "void settings_mark_legal(void);" in net_h
    assert "double settings_legal_ts(void);" in net_h
    compiler = read("ds_compiler.py")
    assert "'settings_mark_legal'" in compiler, \
        "нативная функция не зарегистрирована в BUILTINS компилятора"

    # --- экран приватности: рисунок, вход из настроек, выход назад -------
    assert "ST_PRIVACY=16" in config
    assert "SETTINGS_ROWS=8" in layout
    draw_settings = function_body(menu_screens, "draw_settings")
    assert "settings_row_y(7)" in draw_settings and "tr_privacy()" in draw_settings
    draw_privacy = function_body(menu_screens, "draw_privacy")
    for i in range(1, 11):
        assert "tr_pv%d()" % i in draw_privacy, "строка tr_pv%d не рисуется" % i
    touch_settings = function_body(menu_input, "touch_settings")
    assert re.search(r"hit_settings_row\(x, y, 7\) == 1 then\s*\n\s*start_transition\(ST_PRIVACY\)",
                     touch_settings), "строка приватности не открывает экран"
    touch_menu = function_body(menu_input, "touch_menu")
    assert "ST_PRIVACY" in touch_menu and "back_hit" in touch_menu
    assert re.search(r"if game_state == ST_PRIVACY then\s*\n\s*start_transition\(ST_SETTINGS\)",
                     back), "«Назад» с экрана приватности не ведёт в настройки"
    draw_screen = function_body(engine, "draw_screen")
    assert "draw_privacy()" in draw_screen

    # --- тексты на обоих языках ------------------------------------------
    for name in ["tr_legal_accept", "tr_privacy", "tr_privacy_title"] + \
                ["tr_pv%d" % i for i in range(1, 11)]:
        body = function_body(locale, name)
        assert "language == 1" in body, "%s: нет русской ветки" % name
        returns = re.findall(r'return "([^"]+)"', body)
        assert len(returns) == 2 and returns[0] and returns[1], \
            "%s: нужны RU и EN строки" % name
    accept_ru = function_body(locale, "tr_legal_accept")
    assert "риск" in accept_ru and "risk" in accept_ru

    # --- разрешения манифеста: только сеть -------------------------------
    perms = re.findall(r'uses-permission android:name="android\.permission\.([A-Z_]+)"', manifest)
    assert perms == ["INTERNET", "ACCESS_NETWORK_STATE"], perms

    print("ИТОГ: гейт согласия об эпилепсии, метка в settings.dat, экран "
          "приватности и состав разрешений на месте")
    return 0


if __name__ == "__main__":
    sys.exit(main())
