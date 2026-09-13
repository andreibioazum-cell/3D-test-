"""Generate C code for a DimScript game project."""
import os
import sys
from ds_compiler import DimScriptCompiler


def find_ds_files(directory):
    """Рекурсивно собирает все .ds файлы и сортирует их по модулям.

    Порядок важен для глобальных объявлений (объекты и их экземпляры должны
    идти до первого использования), поэтому файлы внутри game/scripts лежат по
    модулям, а порядок модулей зафиксирован здесь: конфигурация и состояние —
    раньше, бой и эффекты — позже, движок (главный цикл) замыкает список.
    """
    order = [
        "core/config.ds",          # константы: экраны, тема, сетка, баланс
        "ui/locale_core.ds",       # основные переводы RU/EN
        "ui/locale_progress.ds",   # переводы прогресса, наград и магазина
        "ui/locale_extra.ds",      # квесты, топ
        "core/entities.ds",        # объекты и состояние боя
        "core/ui.ds",              # UI-кит: кнопки, карточки, хит-тесты, текст
        "ui/progress_classes.ds",  # классы, уровни и скины
        "ui/progress_rewards.ds",  # награды, сохранение и синхронизация
        "ui/promo.ds",             # карточки в соло и экран промокодов
        "ui/layout.ds",            # геометрия экранов (draw_* и touch_*)
        "ui/chat.ds",              # онлайн-чат
        "ui/menu_screens.ds",      # отрисовка экранов меню
        "ui/quests.ds",            # задания
        "ui/menu_input.ds",        # переходы и обработка нажатий меню
        "combat/battle_rules.ds",  # данные классов: текстуры, HP, урон, яд
        "combat/battle_turrets.ds", # турели-деспенсеры бука
        "combat/battle_hitscan.ds", # геометрия попаданий удара и рывка
        "combat/battle_damage.ds", # урон, лечение, оглушение, возрождение
        "combat/battle_setup.ds",  # запуск боя, спавн и движение игрока
        "combat/battle_movement.ds", # прицеливание и движение бота
        "combat/battle_ai.ds",     # решения и атаки бота
        "combat/battle_status.ds", # заморозка, оглушение и яд
        "combat/battle_enemy_class.ds", # случайный класс врага и его ульты
        "combat/battle_enemy_dash.ds", # рывок врага и рывок игрока в соло
        "combat/battle_enemy_turrets.ds", # турели-щиты врага-буКа
        "combat/battle_shield.ds", # щит турелей бука и пронзание снежинок
        "combat/battle_abilities.ds", # способности и основной update боя
        "combat/battle_online.ds", # сетевые снимки и завершение матча
        "combat/battle_actions_fx.ds", # запуск атак и способности
        "combat/battle_super_render.ds", # рисунок турелей, луча и вселенной
        "combat/battle_hitboxes.ds", # хитбоксы способностей с уроном
        "combat/battle_hitbox_fades.ds", # плавные альфы этих хитбоксов
        "combat/battle_render.ds", # поле, бойцы и интерфейс боя
        "combat/battle_event_plates.ds", # ивент «плиты и Дед Мороз»
        "combat/battle_events_input.ds", # события, баннеры и ввод
        "fx/weather.ds",           # фон арены и общий эффект снега
        "fx/dust.ds",              # след пыли
        "core/engine.ds",          # главный цикл
    ]
    files = []
    for root, _dirs, names in os.walk(directory):
        for n in names:
            if n.endswith('.ds'):
                files.append(os.path.join(root, n))

    def key(p):
        rel = os.path.relpath(p, directory).replace(os.sep, '/')
        try:
            return (0, order.index(rel))
        except ValueError:
            # Незнакомые файлы (например, экспериментальные) идут после всех
            # модулей, по алфавиту.
            return (1, rel)

    return sorted(files, key=key)


def run_lint(scripts_dir):
    """Прогоняет tools/ds_lint.py по только что скомпилированным скриптам.

    Компилятор DimScript молча выбрасывает присваивания необъявленным
    именам, поэтому без линта опечатка, скажем, в menu_input.ds исчезала бы из
    game.c без следа и проявлялась уже в собранной игре. По умолчанию линт
    только печатает найденное; строгим его делает DS_LINT_STRICT=1 — тогда
    gen.py завершается ошибкой (удобно включить в CI).
    """
    if not scripts_dir:
        return []
    try:
        from tools import ds_lint
    except Exception as e:  # линт не должен ломать генерацию
        print(f"ds_lint skipped: {e}")
        return []
    errors = ds_lint.lint_dir(scripts_dir)
    if errors:
        print(f"ds_lint: {len(errors)} problem(s)")
        for e in errors:
            print(" -", e)
    else:
        print("ds_lint: ok")
    return errors


def usage(stream=sys.stdout):
    print(
        "Usage: python gen.py [--dump] "
        "[game-directory [output.c]] | [source.ds output.c]",
        file=stream,
    )


def main():
    game_dir = 'game'
    args = sys.argv[1:]
    dump_c = os.environ.get('DIMSCRIPT_DUMP_C', '').lower() in ('1', 'true', 'yes')
    if '--help' in args or '-h' in args:
        usage()
        return 0
    if '--dump' in args:
        dump_c = True
        args = [arg for arg in args if arg != '--dump']
    if len(args) == 0:
        input_path = game_dir
        output_path = os.path.join(game_dir, 'game.c')
    elif len(args) == 1:
        input_path = args[0]
        if os.path.isdir(input_path):
            output_path = os.path.join(input_path, 'game.c')
        else:
            output_path = os.path.splitext(input_path)[0] + '.c'
    elif len(args) == 2:
        input_path, output_path = args
    else:
        usage(sys.stderr)
        return 2

    if os.path.isdir(input_path):
        src_dir = input_path
        # В проекте скрипты лежат в <game_dir>/scripts, а список модулей в
        # find_ds_files отсчитывается именно от этой папки. Если передан корень
        # проекта (например, game), ищем скрипты в его подкаталоге scripts,
        # иначе порядок модулей не совпадёт с order в find_ds_files.
        scripts = os.path.join(input_path, 'scripts')
        if os.path.isdir(scripts):
            src_dir = scripts
        sources = find_ds_files(src_dir)
        if not sources:
            print(f"Error: no .ds files found in {input_path}", file=sys.stderr)
            return 1
    else:
        if not os.path.isfile(input_path):
            print(f"Error: file not found: {input_path}", file=sys.stderr)
            return 1
        sources = [input_path]

    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)
    compiler = DimScriptCompiler()
    if not compiler.compile(sources, output_path):
        print("Compilation failed", file=sys.stderr)
        return 1

    note = ""
    if hasattr(compiler, 'warnings') and compiler.warnings:
        note = f" with {compiler.warnings} warning(s)"
    if compiler.errors:
        note += f" (errors above are non-fatal: game still builds)"
    print(f"{output_path} generated from {len(compiler.lines)} line(s){note}")

    lint_errors = run_lint(src_dir if os.path.isdir(input_path) else None)
    if lint_errors and os.environ.get('DS_LINT_STRICT', '').lower() in ('1', 'true', 'yes'):
        return 1

    if dump_c:
        print("\n" + "=" * 60)
        print("GENERATED C CODE:")
        print("=" * 60)
        with open(output_path, 'r', encoding='utf-8') as generated:
            print(generated.read())
        print("=" * 60)
        print("END OF GENERATED C CODE")
        print("=" * 60 + "\n")
    return 0


if __name__ == '__main__':
    sys.exit(main())
