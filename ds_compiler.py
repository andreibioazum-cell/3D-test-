"""Компилятор DimScript v2: синтаксис Lua + строгость Java/C#.

Грамматика (всё, что не перечислено, — ошибка компиляции):

    import Dim.System.Gui            -- импорт модуля/пространства имён
    -- комментарий                   -- только двойной дефис, как в Lua

    class Player                     -- класс: поля + методы + конструктор
        private number hp = 10       -- поле с модификатором и типом
        public string nick = "bot"
        public function new()        -- конструктор (выполняется после полей)
            self.hp = 10
        end
        public function heal(number amount) -> void
            self.hp = self.hp + amount
        end
        public static function make() -> Player
            return new Player()
        end
    end

    public number score = 0          -- глобалка модуля с модификатором и типом
    private string title = "game"

    public function tick(number dt) -> void   -- свободная функция
        local number step = dt * 2            -- локальная переменная (local)
        local Player p = new Player()         -- тип можно не писать: выведется
        p.heal(step)
        Gui.text(24, $"Баланс: {score}")      -- интерполяция строк как в C#
        if p.hp > 0 and not p.dead then       -- блоки Lua: then / do / end
            score += 1
        end
        for i = 1 to 3 do
            ds_log(i)
        end
    end

    local Player g = new Player()    -- инструкции верхнего уровня (ds_main)
    g.heal(1)

Строгость: необъявленная переменная, неизвестное имя, неизвестный вызов,
несовпадение числа аргументов, несовпадение типов (number/string/класс),
чужое приватное поле и отсутствие поля класса — ошибки компиляции, а не
тихий пропуск строки, как это было в v1.
"""

import os
import re
import sys

TYPES = {
    'num': 'double',
    'number': 'double',
    'int': 'double',
    'float': 'double',
    'double': 'double',
    'str': 'const char*',
    'string': 'const char*',
    'bool': 'double',
    'col': 'uint32_t',
    'color': 'uint32_t',
    'arr': 'DSArray*',
    'array': 'DSArray*',
}

_TYPE_ALIAS = {
    'number': 'num',
    'int': 'num',
    'float': 'num',
    'double': 'num',
    'string': 'str',
    'bool': 'num',
    'color': 'col',
    'array': 'arr',
}


def canon_type(t):
    return _TYPE_ALIAS.get(t, t)


BUILTINS = frozenset({
    'rect', 'roundrect', 'rect_rot', 'circle', 'ring', 'line', 'tex', 'tex_tint', 'text',
    'text_scaled', 'text_ink_width', 'text_ink_height', 'text_ink_top',
    'png_load', 'clear_screen', 'text_width', 'text_height', 'sqrt', 'sin', 'cos',
    'atan2', 'floor', 'rand', 'snd_load', 'snd_play', 'snd_loop', 'snd_stop',
    'snd_playing', 'snd_volume', 'snd_stop_all', 'sound_play', 'net_connect',
    'net_disconnect', 'net_publish', 'net_publish_punch', 'net_publish_snow',
    'net_publish_station', 'net_publish_universe',
    'net_set_class', 'net_status', 'net_slot', 'net_player_online',
    'net_player_x', 'net_player_y', 'net_player_angle', 'net_player_hp',
    'net_player_alive', 'net_player_nick', 'net_player_punch_x',
    'net_player_punch_y', 'net_player_punch_dx', 'net_player_punch_dy',
    'net_player_punch', 'net_player_snow_x', 'net_player_snow_y',
    'net_player_snow_dx', 'net_player_snow_dy', 'net_player_snow',
    'net_player_station_x', 'net_player_station_y', 'net_player_station_hp',
    'net_player_station2_x', 'net_player_station2_y', 'net_player_station2_hp',
    'net_player_station3_x', 'net_player_station3_y', 'net_player_station3_hp',
    'net_player_station', 'net_player_universe_x', 'net_player_universe_y',
    'net_player_universe', 'net_publish_turrets', 'net_publish_dash',
    'net_player_dash', 'net_player_dash_x', 'net_player_dash_y',
    'net_player_dash_dx', 'net_player_dash_dy',
    'net_publish_thud', 'net_player_thud',
    'net_player_class', 'net_player_level', 'net_player_prime_level',
    'net_set_level', 'net_set_prime_level', 'net_set_skin', 'net_player_skin', 'net_event', 'net_event_set',
    'net_chat_send',
    'net_chat_trim', 'net_chat_count', 'net_chat_text', 'net_chat_uid',
    'net_chat_key',
    'net_is_banned', 'net_banned', 'net_ban_set', 'net_chat_is_ban',
    'net_chat_is_unban', 'net_chat_ban_target', 'net_chat_unban_target',
    'net_chat_is_text_cmd', 'net_chat_text_cmd_text', 'net_chat_text_cmd_color',
    'net_banner_send', 'net_banner_ts', 'net_banner_text', 'net_banner_color',
    'net_autologin', 'net_set_nick', 'net_login_status', 'net_login_nick',
    'net_set_firebase_key',
    'net_login_pass',
    'net_auth', 'net_logout', 'net_leaderboard_fetch', 'net_leaderboard_status',
    'net_leaderboard_count', 'net_leaderboard_nick', 'net_leaderboard_cups',
    'net_load_cups', 'net_load_candies', 'net_load_primes', 'net_load_class',
    'net_load_azum', 'net_load_santa', 'net_load_ebuc', 'net_load_level',
    'net_load_levels_unlocked', 'net_load_ordinary_level',
    'net_load_ordinary_levels_unlocked', 'net_load_azum_level',
    'net_load_azum_levels_unlocked', 'net_load_santa_level',
    'net_load_santa_levels_unlocked', 'net_load_ebuc_level',
    'net_load_ebuc_levels_unlocked', 'net_load_bp_level', 'net_load_azum_skin',
    'net_load_ordinary_prime_level',
    'net_load_azum_prime_level', 'net_load_santa_prime_level',
    'net_save_progress', 'net_save_progress_all', 'net_load_achievement_flags',
    'net_save_achievement_flags', 'net_has_achievement_flag',
    'net_mark_achievement_flag', 'net_load_azum_revives', 'net_save_azum_revives',
    'net_promo_code', 'net_promo_register', 'net_promo_used',
    'net_promo_mark_used', 'net_promo_streak', 'net_promo_bump_streak',
    'net_promo_reset_streak', 'net_promo_card_found', 'net_promo_mark_card_found',
    'net_load_playtime', 'net_save_playtime', 'net_add_playtime',
    'net_quest_now', 'net_save_quest_state', 'net_load_quest_state', 'net_quest_has_state',
    'net_load_language', 'net_load_hitboxes', 'net_save_settings',
    'net_load_music_volume', 'net_save_music_volume',
    'net_load_winter_theme', 'net_save_winter_theme',
    'net_load_fps_meter', 'net_save_fps_meter',
    'settings_mark_legal', 'settings_legal_ts',
    'keyboard_show',
    'keyboard_hide', 'keyboard_get_text', 'keyboard_get_raw', 'keyboard_clear',
    'keyboard_enter_pressed', 'keyboard_type', 'keyboard_visible', 'str_len',
    'str_eq', 'str_contains', 'str_index_of', 'str_sub', 'str_to_num', 'str_trim',
    'str_starts_with', 'str_ends_with', 'str_lower', 'str_upper',
    'ds_log', 'console_count', 'console_line', 'console_type',
    'console_clear', 'arr_new', 'arr_push', 'arr_get', 'arr_set', 'arr_len',
    'arr_clear', 'clamp', 'lerp', 'dist',
    # Математика для скриптов. В C у этих имён префикс ds_ (см. FUNCTION_MAP и
    # native/runtime/core.inc), чтобы не спорить с libc: abs(), round() и т.п.
    # уже есть в stdlib с другими сигнатурами.
    'min', 'max', 'abs', 'round', 'sign', 'mod', 'trunc',
})

# Имена скрипта -> имена в C. Пусто = имя совпадает.
FUNCTION_MAP = {
    'min': 'ds_min',
    'max': 'ds_max',
    'abs': 'ds_abs',
    'round': 'ds_round',
    'sign': 'ds_sign',
    'mod': 'ds_mod',
    'trunc': 'ds_trunc',
}

ENGINE_VARS = {
    'screen_w': 'num',
    'screen_h': 'num',
    'dt': 'num',
    'joy': 'joy',
    'mouse_clicked': 'num',
    'ds_mouse_x': 'num',
    'ds_mouse_y': 'num',
}

STR_BUILTINS = frozenset({
    'console_line',
    'keyboard_get_text',
    'keyboard_get_raw',
    'net_chat_text',
    'net_chat_uid',
    'net_chat_key',
    'net_chat_ban_target',
    'net_chat_unban_target',
    'net_chat_text_cmd_text',
    'net_chat_text_cmd_color',
    'net_banner_text',
    'net_banner_color',
    'net_promo_code',
    'net_login_nick',
    'net_login_pass',
    'net_player_nick',
    'net_leaderboard_nick',
    'str_sub',
    'str_trim',
    'str_lower',
    'str_upper',
})

# Встроенные пространства имён: import Dim.System.Graphics разрешает писать
# Graphics.rect(...), но и прежние короткие вызовы rect(...) остаются.
STD_NAMESPACES = {
    'Dim.System.Graphics': 'Graphics',
    'Dim.System.Math': 'Math',
}
_MATH_NS = {'min', 'max', 'abs', 'round', 'sign', 'mod', 'trunc', 'clamp',
            'lerp', 'dist', 'sqrt', 'sin', 'cos', 'atan2', 'floor', 'rand'}

_NAME = r'[A-Za-z_]\w*'
_NUM_RE = re.compile(r'^(?:[-+]?\d+(?:\.\d+)?|0[xX][0-9a-fA-F]+)$')
_LHS_RE = re.compile(r'^(' + _NAME + r')(?:\.(' + _NAME + r'))?$')
_FOR_RE = re.compile(
    r'^for\s+(' + _NAME + r')\s*=\s*(.+?)\s+(?:to|до)\s+(.+?)'
    r'(?:\s+(?:step|шаг)\s+(.+?))?\s+do$', re.IGNORECASE)
_COMPOUND_OPS = ('+=', '-=', '*=', '/=')
# Математика из math.h, которую подключает сгенерированный game.c.
NATIVE_MATH = frozenset({'fabs'})
_MODS = ('public', 'private')


def num_value(text):
    """Числовое значение литерала или None, если это не просто число."""
    text = text.strip()
    if not _NUM_RE.match(text):
        return None
    try:
        return float(int(text, 16)) if text[:2].lower() == '0x' else float(text)
    except ValueError:
        return None


def scan(text):
    """Глубина скобок и маска строковых литералов для каждого символа."""
    n = len(text)
    depth = [0] * n
    quoted = [False] * n
    in_str = False
    esc = False
    lvl = 0
    i = 0
    while i < n:
        c = text[i]
        quoted[i] = in_str
        if esc:
            esc = False
        elif in_str:
            if c == '\\':
                esc = True
            elif c == '"':
                in_str = False
        elif text.startswith('$"', i):
            in_str = True
            quoted[i] = False
            depth[i] = lvl
            i += 1
            quoted[i] = in_str
        elif c == '"':
            in_str = True
        elif c == '(':
            lvl += 1
        elif c == ')':
            lvl = max(0, lvl - 1)
        depth[i] = lvl
        i += 1
    return depth, quoted


def open_parens(text):
    """Сколько '(' остались незакрытыми (вне строковых литералов)."""
    _, quoted = scan(text)
    balance = 0
    for i, c in enumerate(text):
        if quoted[i]:
            continue
        if c == '(':
            balance += 1
        elif c == ')':
            balance -= 1
    return balance


def split_top(text, sep):
    depth, quoted = scan(text)
    parts = []
    start = 0
    for i, c in enumerate(text):
        if depth[i] == 0 and not quoted[i] and c == sep:
            parts.append(text[start:i].strip())
            start = i + 1
    parts.append(text[start:].strip())
    return parts


def strip_comment(line):
    """Комментарий v2 — только '--' (Lua). Внутри строк дефисы остаются."""
    out = []
    i = 0
    in_str = False
    while i < len(line):
        c = line[i]
        if in_str:
            out.append(c)
            if c == '\\' and i + 1 < len(line):
                out.append(line[i + 1])
                i += 2
                continue
            if c == '"':
                in_str = False
        elif line.startswith('$"', i):
            out.append('$"')
            in_str = True
            i += 2
            continue
        elif c == '"':
            in_str = True
            out.append(c)
        elif c == '-' and i + 1 < len(line) and line[i + 1] == '-':
            break
        else:
            out.append(c)
        i += 1
    return ''.join(out)


def find_compound(line):
    """Позиция составного оператора (+=, -=, *=, /=) вне строк и скобок."""
    depth, quoted = scan(line)
    for i, c in enumerate(line):
        if quoted[i] or depth[i] or i + 1 >= len(line):
            continue
        if line[i + 1] == '=' and c in '+-*/':
            return i
    return -1


def find_assign(line):
    depth, quoted = scan(line)
    for i, c in enumerate(line):
        if quoted[i] or depth[i]:
            continue
        if c == '=' and (i == 0 or line[i - 1] not in '<>!') and (i + 1 >= len(line) or line[i + 1] != '='):
            return i
    return -1


def used_outside_strings(text, name):
    pat = re.compile(r'\b' + re.escape(name) + r'\b')
    _, quoted = scan(text)
    return any(not quoted[m.start()] for m in pat.finditer(text))


def sub_unquoted(e, pat, repl):
    """Replace pat with repl outside string literals."""
    _, quoted = scan(e)
    if not any(quoted):
        return pat.sub(repl, e)
    out = []
    start = 0
    for m in pat.finditer(e):
        if quoted[m.start()]:
            continue
        out.append(e[start:m.start()])
        out.append(m.expand(repl))
        start = m.end()
    out.append(e[start:])
    return ''.join(out)


def interp_holes(text):
    """Разбирает $"...{expr}...": список ('lit', s) / ('hole', expr)."""
    parts = []
    i = 2
    lit = []
    while i < len(text):
        c = text[i]
        if c == '\\' and i + 1 < len(text):
            lit.append(text[i + 1])
            i += 2
            continue
        if c == '"':
            break
        if c == '{':
            depth = 1
            j = i + 1
            while j < len(text) and depth:
                if text[j] == '{':
                    depth += 1
                elif text[j] == '}':
                    depth -= 1
                j += 1
            if lit:
                parts.append(('lit', ''.join(lit)))
                lit = []
            parts.append(('hole', text[i + 1:j - 1]))
            i = j
            continue
        lit.append(c)
        i += 1
    if lit:
        parts.append(('lit', ''.join(lit)))
    return parts


class DimScriptCompiler:
    def __init__(self):
        self.objects = {}        # класс -> {поле: (видимость, тип, default)}
        self.methods = {}        # класс -> {имя: (видимость, static, params, ret, body)}
        self.vars = {}           # глобалки: имя -> (видимость, тип, default)
        self.functions = {}      # свободные функции: имя -> (видимость, params, body)
        self.func_ret = {}       # имя -> тип возврата ('num'/'str'/класс/'void')
        self.imports = {}        # пространство имён -> путь импорта
        self.top = []
        self.lines = []
        self.errors = 0
        self.warnings = 0
        self.warned = set()
        self.output = []
        self.indent = 0
        self.scope = {}
        self.blocks = []
        self.cur_class = None
        self.sources = []
        self._parsed_classes = set()

    def _error(self, msg):
        self.errors += 1
        print(f"DimScript error: {msg}", file=sys.stderr)

    def _warn(self, msg):
        if msg in self.warned:
            return
        self.warned.add(msg)
        self.warnings += 1
        print(f"DimScript warning: {msg}", file=sys.stderr)

    # ─────────────────────────── загрузка ───────────────────────────

    def _load(self, paths):
        self.sources = [os.path.abspath(p) for p in paths]
        queue = list(paths)
        seen = set()
        while queue:
            p = queue.pop(0)
            ap = os.path.abspath(p)
            if ap in seen:
                continue
            seen.add(ap)
            pending = ''
            try:
                with open(p, 'r', encoding='utf-8-sig') as f:
                    for raw in f:
                        line = strip_comment(raw).strip()
                        if not line:
                            continue
                        if pending:
                            line = pending + ' ' + line
                        if open_parens(line) > 0:
                            pending = line
                            continue
                        pending = ''
                        for q in (s.strip() for s in split_top(line, ';')):
                            if not q:
                                continue
                            imp = re.match(r'^import\s+([\w.]+)$', q)
                            if imp:
                                extra = self._resolve_import(imp.group(1), p)
                                if extra:
                                    queue.append(extra)
                                continue
                            self.lines.append(q)
            except OSError as e:
                self._error(f"cannot read '{p}': {e}")
                return False
            if pending:
                self._error(f"unfinished call in '{p}' (unclosed parenthesis): {pending}")
        return True

    def _resolve_import(self, path, from_file):
        """import Dim.System.Gui -> файл Dim/System/Gui.ds рядом с исходниками."""
        ns = path.split('.')[-1]
        if path in STD_NAMESPACES:
            self.imports[ns] = path
            return None
        roots = [os.path.dirname(os.path.abspath(from_file))]
        roots += [os.path.dirname(s) for s in self.sources]
        roots.append(os.path.dirname(os.path.abspath(__file__)))
        rel = os.path.join(*path.split('.')) + '.ds'
        for root in roots:
            cur = root
            for _ in range(4):
                for cand in (os.path.join(cur, rel), os.path.join(cur, 'std', rel)):
                    if os.path.isfile(cand):
                        self.imports[ns] = path
                        return cand
                cur = os.path.dirname(cur)
        # Разрешится позже: класс с таким именем может объявить сам проект.
        self.imports[ns] = path
        return None

    # ─────────────────────────── разбор ───────────────────────────

    def _decl_list(self, line):
        """[public|private] [local] type a=1, b=2 -> [(vis, local, type, name, val)]."""
        m = re.match(
            r'^(?:(public|private)\s+)?(local\s+)?(' + _NAME + r')\s+(.+)$', line)
        if not m:
            return None
        vis, local, t, rest = m.group(1), bool(m.group(2)), canon_type(m.group(3)), m.group(4).strip()
        if t not in TYPES and t not in self.objects and t != 'joy':
            return None
        res = []
        for part in split_top(rest, ','):
            part = part.strip()
            if not part:
                continue
            mm = re.match(r'^(' + _NAME + r')(?:\s*=\s*(.*))?$', part)
            if not mm:
                return None
            v = mm.group(2)
            res.append((vis, local, t, mm.group(1), v.strip() if v else v))
        return res or None

    def parse(self):
        for line in self.lines:
            m = re.match(r'^(?:(?:public|private)\s+)?class\s+(' + _NAME + r')\s*$', line)
            if m and m.group(1) not in self.objects:
                self.objects[m.group(1)] = {}
        i = 0
        while i < len(self.lines):
            line = self.lines[i]
            if line == 'end':
                self._error("unexpected 'end' at top level")
                i += 1
            elif line.startswith('include '):
                self._error(f"v2: 'include' удалён, используйте import: {line}")
                i += 1
            elif line.startswith('object '):
                self._error(f"v2: 'object' заменён на 'class': {line}")
                i += 1
            elif re.match(r'^(public|private)\s+(static\s+)?class\s+', line) or line.startswith('class '):
                i = self._parse_class(i)
            elif re.match(r'^(public|private)\s+static\s+function\s+', line):
                self._error(f"'static' допустим только внутри class: {line}")
                i += 1
            elif re.match(r'^(public|private)\s+function\s+', line):
                i = self._parse_function(i)
            elif line.startswith('function '):
                self._error(
                    f"v2: функции нужен модификатор доступа: 'public function ...': {line}")
                i += 1
            elif self._top_decl(line):
                self._parse_global(line)
                i += 1
            elif re.match(r'^(number|string|color|array|num|str|col|arr)\s+', line):
                self._error(
                    f"v2: объявлению нужен модификатор или local: 'public ...' / 'local ...': {line}")
                i += 1
            else:
                self.top.append(line)
                i += 1
        self._check_imports()
        return self.errors == 0

    def _top_decl(self, line):
        lst = self._decl_list(line)
        if not lst:
            return None
        if any(t not in TYPES and t not in self.objects for _v, _l, t, _n, _d in lst):
            return None
        if any(local for _v, local, _t, _n, _d in lst):
            return None  # local верхнего уровня — инструкция, не объявление
        if any(vis is None for vis, _l, _t, _n, _d in lst):
            return None
        return lst

    def _check_imports(self):
        for ns, path in self.imports.items():
            if path in STD_NAMESPACES:
                continue
            if ns not in self.objects:
                self._error(f"import {path}: класс '{ns}' не найден среди исходников")

    def _parse_class(self, i):
        m = re.match(r'^(?:(public|private)\s+)?class\s+(' + _NAME + r')\s*$', self.lines[i])
        if not m:
            self._error(f"invalid class header: {self.lines[i]}")
            return i + 1
        name = m.group(2)
        if name in self._parsed_classes:
            self._error(f"dup class '{name}'")
            return i + 1
        self._parsed_classes.add(name)
        fields = self.objects.setdefault(name, {})
        methods = {}
        j = i + 1
        while j < len(self.lines):
            line = self.lines[j]
            if line == 'end':
                self.objects[name] = fields
                self.methods[name] = methods
                return j + 1
            lst = self._decl_list(line)
            if lst and not any(local for _v, local, _t, _n, _d in lst):
                for vis, _loc, t, fn, v in lst:
                    if not vis:
                        self._error(f"class '{name}': у поля нужен модификатор "
                                    f"public/private: {line}")
                    elif t not in TYPES and t not in self.objects:
                        self._error(f"class '{name}': поле '{fn}' — ожидается тип "
                                    f"(встроенный или класс)")
                    elif fn in fields:
                        self._error(f"dup field '{name}.{fn}'")
                    else:
                        fields[fn] = (vis, t, v)
                j += 1
                continue
            fm = re.match(
                r'^(public|private)\s+(static\s+)?function\s+(' + _NAME + r')'
                r'(?:\s*(\(.*\)|.*?))?(?:\s*->\s*(' + _NAME + r'))?\s*$', line)
            if fm:
                vis, static, mname = fm.group(1), bool(fm.group(2)), fm.group(3)
                params = self._parse_params(fm.group(4) or '')
                ret = canon_type(fm.group(5)) if fm.group(5) else None
                if ret == 'void':
                    ret = 'void'
                elif ret is not None and ret not in TYPES and ret not in self.objects \
                        and ret != name:
                    self._error(f"class '{name}': неизвестный тип возврата '{ret}'")
                body, j = self._collect_block(j + 1, f"method '{name}.{mname}'")
                if mname in methods:
                    self._error(f"dup method '{name}.{mname}'")
                else:
                    methods[mname] = (vis, static, params, ret, body)
                if mname != 'new':
                    self._note_ret(name + '.' + mname, ret, body, params)
                continue
            if re.match(r'^function\s+', line):
                self._error(f"v2: методу нужен модификатор: 'public function ...': {line}")
                j += 1
                continue
            self._error(f"class '{name}': ожидалось поле или метод, получено: {line}")
            j += 1
        self._error(f"class '{name}' no end")
        return j

    def _note_ret(self, key, ret, body, params):
        """Запоминает тип возврата: явный '-> t' или вывод по return."""
        if ret and ret != 'void':
            self.func_ret[key] = ret
        elif ret == 'void':
            self.func_ret[key] = 'void'
        elif any(line.startswith('return ') for line in body):
            self.func_ret[key] = 'num'  # уточнится в _infer_returns
        else:
            self.func_ret[key] = 'void'

    def _parse_function(self, i):
        m = re.match(
            r'^(public|private)\s+function\s+(' + _NAME + r')'
            r'(?:\s*(\(.*\)|.*?))?(?:\s*->\s*(' + _NAME + r'))?\s*$', self.lines[i])
        vis, name = m.group(1), m.group(2)
        params = self._parse_params(m.group(3) or '')
        ret = canon_type(m.group(4)) if m.group(4) else None
        if ret == 'void':
            pass
        elif ret is not None and ret not in TYPES and ret not in self.objects:
            self._error(f"function '{name}': неизвестный тип возврата '{ret}'")
        body, j = self._collect_block(i + 1, f"function '{name}'")
        if name in self.functions:
            self._error(f"dup function '{name}'")
        else:
            self.functions[name] = (vis, params, body)
        self._note_ret(name, ret, body, params)
        return j

    def _parse_params(self, text):
        text = (text or '').strip()
        if text.startswith('(') and text.endswith(')'):
            text = text[1:-1].strip()
        params = []
        for part in split_top(text, ',') if text.strip() else []:
            w = part.split()
            if len(w) != 2 or (canon_type(w[0]) not in TYPES and w[0] not in self.objects):
                self._error(f"invalid param '{part}' (нужно 'type name')")
                continue
            params.append((canon_type(w[0]), w[1]))
        return params

    def _collect_block(self, i, what):
        depth = 0
        body = []
        while i < len(self.lines):
            line = self.lines[i]
            if line == 'end':
                if depth == 0:
                    return body, i + 1
                depth -= 1
            elif line.startswith(('if ', 'while ', 'for ')):
                depth += 1
            elif line == 'else' or line.startswith(('else if ', 'elif ')):
                if depth == 0:
                    self._error(f"{what}: 'else' without 'if'")
                    return body, i + 1
            elif line.startswith(('class ', 'object ')) or re.match(
                    r'^(public|private)\s+(static\s+)?function\s+', line):
                self._error(f"{what}: nested declarations not allowed")
                body.append(line)
                i += 1
                continue
            body.append(line)
            i += 1
        self._error(f"{what} no end")
        return body, i

    def _parse_global(self, line):
        for vis, _loc, t, n, v in self._top_decl(line) or []:
            if n in self.vars:
                self._error(f"dup var '{n}'")
                continue
            if t in self.objects:
                if not v or not re.match(r'^new\s+' + re.escape(t) + r'\s*\(\)\s*$', v):
                    self._error(f"'{n}': must be 'new {t}()'")
                    continue
            self.vars[n] = (vis, t, v)

    # ─────────────────────────── типы ───────────────────────────

    def c_type(self, t):
        if t in TYPES:
            return TYPES[t]
        if t in self.objects:
            return t + ' *'
        return 'double'

    def default_value(self, t):
        return 'NULL' if t == 'str' else '0'

    def static_expr(self, v):
        return bool(_NUM_RE.match(v)) or (len(v) >= 2 and v[0] == '"' and v[-1] == '"')

    def _holder_type(self, name):
        if name == 'self':
            return self.cur_class
        if name in self.scope:
            return self.scope[name]
        if name in self.vars:
            return self.vars[name][1]
        return None

    def expr_type(self, expr):
        expr = expr.strip()
        if expr.startswith('$"'):
            return 'str'
        if expr.startswith('"') and expr.endswith('"'):
            return 'str'
        if expr.startswith('new '):
            m = re.match(r'^new\s+(' + _NAME + r')\s*\(\)$', expr)
            return m.group(1) if m else 'num'
        m = re.match(r'^(' + _NAME + r')\.(' + _NAME + r')$', expr)
        if m:
            ot = self._holder_type(m.group(1))
            fields = self.objects.get(ot)
            if fields and m.group(2) in fields:
                return fields[m.group(2)][1]
            return None
        if expr == 'self':
            return self.cur_class
        if expr in self.scope:
            return self.scope[expr]
        if expr in self.vars:
            return self.vars[expr][1]
        if expr in ENGINE_VARS:
            return ENGINE_VARS[expr]
        call = re.match(r'^(' + _NAME + r')\s*\(.*\)$', expr)
        if call:
            name = call.group(1)
            if name in self.func_ret:
                r = self.func_ret[name]
                return None if r == 'void' else r
            if name in STR_BUILTINS:
                return 'str'
            if name in BUILTINS or name in NATIVE_MATH:
                return 'num'
        mcall = re.match(r'^(self|' + _NAME + r')(?:\.(' + _NAME + r'))+\s*\(.*\)$', expr)
        if mcall:
            return self._method_ret_type(expr)
        return None

    def _method_ret_type(self, expr):
        m = re.match(r'^(self|' + _NAME + r')((?:\.' + _NAME + r')+)?\s*\(', expr)
        if not m:
            return None
        pieces = [m.group(1)] + [p for p in (m.group(2) or '').split('.') if p]
        base, method = pieces[0], pieces[-1]
        mids = pieces[1:-1]
        if base == 'self':
            cls = self.cur_class
        elif base in self.imports:
            return None
        elif not mids and base in self.objects:
            cls, method = base, pieces[-1]
            mth = self.methods.get(cls, {}).get(method)
            if not mth or not mth[1]:
                return None
            r = self.func_ret.get(f'{cls}.{method}')
            return None if r == 'void' else r
        elif base in self.scope or base in self.vars:
            cls = self._holder_type(base)
        else:
            return None
        for f in mids:
            fld = self.objects.get(cls, {}).get(f)
            if not fld:
                return None
            cls = fld[1]
        mth = self.methods.get(cls, {}).get(method)
        if not mth or mth[1]:
            return None
        r = self.func_ret.get(f'{cls}.{method}')
        return None if r == 'void' else r

    # ─────────────────────────── выражения ───────────────────────────

    def expr(self, e):
        e = e.strip()
        if e.startswith('$"'):
            return self._interp(e)
        m = re.match(r'^new\s+(' + _NAME + r')\s*\(\)$', e)
        if m:
            return f'ds_new_{m.group(1)}()'
        e = self._logic(e)
        if e == 'true':
            return '1'
        if e == 'false':
            return '0'
        parts = split_top(e, '+')
        if len(parts) > 1 and all(parts) and any(self.expr_type(p) == 'str' for p in parts):
            out = self.as_str(parts[0])
            for p in parts[1:]:
                out = f'ds_concat({out}, {self.as_str(p)})'
            return out
        return self._fields(e)

    def _interp(self, e):
        """$"текст {expr} текст" -> цепочка ds_concat(...)."""
        parts = interp_holes(e)
        if not parts:
            return '""'
        chunks = []
        for kind, val in parts:
            if kind == 'lit':
                chunks.append('"' + val.replace('\\', '\\\\').replace('"', '\\"') + '"')
            else:
                chunks.append(self.as_str(val))
        out = chunks[0]
        for c in chunks[1:]:
            out = f'ds_concat({out}, {c})'
        return out

    def _logic(self, e):
        """Слова-операторы: and / or / not -> && / || / !."""
        e = e.strip()
        e = sub_unquoted(e, re.compile(r'\band\b'), '&&')
        e = sub_unquoted(e, re.compile(r'\bor\b'), '||')
        e = sub_unquoted(e, re.compile(r'\bnot\b\s*'), '!')
        return e

    def _chain_two(self, e, holder_re, holder_c, cls):
        """self.a.b / obj.a.b -> holder->a->b (два поля, без вызова на конце)."""
        def repl(m):
            a, b = m.group(1), m.group(2)
            fld = self.objects.get(cls, {}).get(a)
            if not fld or fld[1] not in self.objects:
                return m.group(0)
            if b not in self.objects[fld[1]]:
                return m.group(0)
            return f'{holder_c}->{a}->{b}'
        return re.sub(holder_re, repl, e)

    def _fields(self, e):
        """self.f и obj.f -> указательные поля C; затем вызовы."""
        if self.cur_class:
            e = self._chain_two(e, r'\bself\.(' + _NAME + r')\.(' + _NAME + r')(?![.(\w])',
                                'self', self.cur_class)
        for n, t in list(self.scope.items()) + [(n, v[1]) for n, v in self.vars.items()]:
            if t in self.objects:
                e = self._chain_two(e, r'\b' + re.escape(n) + r'\.(' + _NAME + r')\.('
                                    + _NAME + r')(?![.(\w])', n, t)
        if self.cur_class and re.search(r'\bself\.', e):
            e = sub_unquoted(e, re.compile(r'\bself\.(' + _NAME + r')(?![\w.(])'), r'self->\1')
        names = [n for n, (_v, t, _d) in self.vars.items() if t in self.objects] + [
            n for n, t in self.scope.items() if t in self.objects
        ]
        for n in sorted(names, key=len, reverse=True):
            e = sub_unquoted(e, re.compile(r'\b' + re.escape(n) + r'\.(' + _NAME + r')(?![\w.(])'), n + r'->\1')
        return self._calls(e)

    def _resolve_dotted(self, e):
        """'self.win.isOpen(a, b)' -> (base_c, 'Win.isOpen', [args]).

        Поддерживает цепочки self.a.b(...) и obj.m(...); базой может быть имя,
        self или результат вызова (тогда база компилируется рекурсией).
        """
        m = re.match(r'^(.*?)\.(' + _NAME + r')\s*\((.*)\)$', e, re.S)
        if not m:
            return None, None, None
        base_src, method, args_src = m.group(1).strip(), m.group(2), m.group(3)
        args = split_top(args_src, ',') if args_src.strip() else []
        base_src = self._logic(base_src)
        if base_src == 'self':
            cls = self.cur_class
            base_c = 'self'
        elif re.match(r'^' + _NAME + r'$', base_src):
            cls = self._holder_type(base_src)
            base_c = self._fields(base_src)
        else:
            cls = self.expr_type(base_src)
            base_c = '(' + self.expr(base_src) + ')'
        return base_c, (cls, method), args

    def _calls(self, e):
        """Вызовы: методы классов, static-методы, пространства имён, функции.

        Lookbehind вместо поглощающего префикса: иначе символ перед вызовом
        ('(' предыдущего вызова, запятая) съедался матчем, и следующий вызов
        оставался без подстановки ds_fn_.
        """
        e = sub_unquoted(e, re.compile(r'\bnew\s+(' + _NAME + r')\s*\(\)'), r'ds_new_\1()')
        _, quoted = scan(e)
        out = []
        pos = 0
        for m in re.finditer(r'(?<![.\w])((?:' + _NAME + r'\.)+' + _NAME + r')\s*\(', e):
            if quoted[m.start()]:
                continue
            start = m.start()
            chain = m.group(1)
            pieces = chain.split('.')
            base, method = pieces[0], pieces[-1]
            open_at = m.end() - 1
            close = self._match_paren(e, open_at)
            args_src = e[open_at + 1:close]
            args = split_top(args_src, ',') if args_src.strip() else []
            repl = self._compile_dotted_call(base, pieces[1:-1], method, args,
                                              e[start:close + 1])
            out.append(e[pos:start])
            out.append(repl)
            pos = close + 1
        out.append(e[pos:])
        e = ''.join(out)
        # Свободные функции и builtins.
        _, quoted = scan(e)
        out = []
        start = 0
        for m in re.finditer(r'(?<![.\w])(' + _NAME + r')\s*\(', e):
            if quoted[m.start()]:
                continue
            name = m.group(1)
            if name in self.functions:
                repl = 'ds_fn_' + name
            elif name in NATIVE_MATH:
                repl = name
            elif name in FUNCTION_MAP:
                repl = FUNCTION_MAP[name]
            elif name in BUILTINS:
                repl = name
            else:
                continue
            out.append(e[start:m.start()])
            out.append(repl)
            start = m.end(1)
        out.append(e[start:])
        return ''.join(out)

    def _match_paren(self, e, open_at):
        depth = 0
        _, quoted = scan(e)
        for i in range(open_at, len(e)):
            if quoted[i]:
                continue
            if e[i] == '(':
                depth += 1
            elif e[i] == ')':
                depth -= 1
                if depth == 0:
                    return i
        return len(e) - 1

    def _compile_dotted_call(self, base, mid, method, args, whole):
        if base == 'self':
            cls = self.cur_class
            base_c = 'self'
            if not cls:
                self._error(f"'self' вне метода класса: {whole}")
                return '0'
        elif not mid and base in self.objects:
            return self._call_static(base, method, args, whole)
        elif not mid and base in self.imports:
            path = self.imports[base]
            if path in STD_NAMESPACES:
                ns = STD_NAMESPACES[path]
                pool = BUILTINS if ns == 'Graphics' else _MATH_NS
                if method not in pool:
                    self._error(f"{base}.{method}: нет такой встроенной функции")
                    return '0'
                return (FUNCTION_MAP.get(method, method) + '('
                        + ', '.join(self.expr(a) for a in args) + ')')
            if base in self.objects:
                return self._call_static(base, method, args, whole)
            self._error(f"import {path} не даёт класс '{base}': {whole}")
            return '0'
        elif base in self.scope or base in self.vars:
            cls = self._holder_type(base)
            base_c = self._fields(base)
        else:
            self._error(f"неизвестный вызов '{whole}'")
            return '0'
        for f in mid:
            fld = self.objects.get(cls, {}).get(f)
            if not fld:
                self._error(f"у класса '{cls}' нет поля '{f}': {whole}")
                return '0'
            if fld[0] == 'private' and cls != self.cur_class:
                self._error(f"приватное поле '{cls}.{f}' прочитано извне класса")
                return '0'
            base_c = f'{base_c}->{f}'
            cls = fld[1]
        return self._call_method(cls, method, base_c, args, whole)

    def _call_method(self, cls, method, base_c, args, whole):
        m = self.methods.get(cls, {}).get(method)
        if not m:
            self._error(f"у класса '{cls}' нет метода '{method}': {whole}")
            return '0'
        vis, static, params, _ret, _body = m
        if vis == 'private' and cls != self.cur_class:
            self._error(f"приватный метод '{cls}.{method}' вызван извне класса")
            return '0'
        if static:
            self._error(f"'{cls}.{method}' — static, вызов через экземпляр: {whole}")
            return '0'
        if len(args) != len(params):
            self._error(f"метод '{cls}.{method}' ждёт {len(params)} аргумент(а), "
                        f"передано {len(args)}: {whole}")
            return '0'
        return (f'ds_mth_{cls}_{method}({base_c}' +
                ('' if not args else ', ' + ', '.join(self.expr(a) for a in args)) + ')')

    def _call_static(self, cls, method, args, whole):
        m = self.methods.get(cls, {}).get(method)
        if not m:
            self._error(f"у класса '{cls}' нет static метода '{method}': {whole}")
            return '0'
        vis, static, params, _ret, _body = m
        if not static:
            self._error(f"'{cls}.{method}' — не static, нужен экземпляр: {whole}")
            return '0'
        if vis == 'private' and cls != self.cur_class:
            self._error(f"приватный static '{cls}.{method}' вызван извне класса")
            return '0'
        if len(args) != len(params):
            self._error(f"static '{cls}.{method}' ждёт {len(params)} аргумент(а), "
                        f"передано {len(args)}: {whole}")
            return '0'
        return (f'ds_stc_{cls}_{method}(' +
                ', '.join(self.expr(a) for a in args) + ')')

    def as_str(self, e):
        if self.expr_type(e) == 'str':
            return self.expr(e)
        return f'ds_num_to_string((double)({self.expr(e)}))'

    # ─────────────────────────── строгость ───────────────────────────

    def _check_idents(self, e, where):
        """Необъявленных имён в выражении быть не должно (строгость v2)."""
        for m in re.finditer(r'\$"(?:[^"\\]|\\.)*"', e):
            for kind, val in interp_holes(m.group(0)):
                if kind == 'hole':
                    self._check_idents(val, where)
        e = re.sub(r'\$"(?:[^"\\]|\\.)*"', '""', e)
        depth, quoted = scan(e)
        for m in re.finditer(r'\b' + _NAME + r'\b', e):
            i = m.start()
            if quoted[i] or depth[i]:
                continue
            if i > 0 and e[i - 1] == '.':
                continue  # поле/метод: проверятся отдельно
            nxt = e[m.end():m.end() + 1]
            name = m.group(0)
            if nxt == '(':
                if (name in self.functions or name in BUILTINS or name == 'new'
                        or name in self.objects or name in self.imports
                        or name in NATIVE_MATH):
                    continue
                self._error(f"{where}: неизвестный вызов '{name}(...)'")
                continue
            if name in ('true', 'false', 'and', 'or', 'not', 'new', 'self',
                        'to', 'step', 'do', 'then'):
                continue
            if name in NATIVE_MATH or name in ('unsigned', 'int', 'double',
                                                'char', 'float'):
                continue  # честные C-касты и math.h внутри выражений
            if name in self.scope or name in self.vars or name in ENGINE_VARS:
                continue
            if name in self.objects or name in self.imports:
                continue  # база цепочки или имя класса
            self._error(f"{where}: неизвестное имя '{name}'")

    def _check_types(self, lhs_type, rhs, where):
        rt = self.expr_type(rhs)
        if lhs_type is None or rt is None:
            return
        if lhs_type == rt:
            return
        if lhs_type in TYPES and rt in TYPES:
            if {canon_type(lhs_type), canon_type(rt)} == {'num', 'str'}:
                self._error(f"{where}: несовместимые типы {lhs_type} и {rt}")
            return
        if lhs_type in self.objects or rt in self.objects:
            self._error(f"{where}: несовместимые типы {lhs_type} и {rt}")

    # ─────────────────────────── вывод кода ───────────────────────────

    def _out(self, s):
        self.output.append('    ' * self.indent + s)

    def _emit(self, s):
        self.output.append(s)

    def _emit_line(self, line, where):
        if line == 'end':
            if not self.blocks:
                self._error(f"unexpected 'end': {where}")
                return
            _header, footer = self.blocks.pop()
            self.indent -= 1
            if footer:
                self._out(footer)
            self._out('}')
            return
        if line.startswith('if '):
            cond = line[3:].strip()
            if not cond.endswith('then'):
                self._error(f"v2: 'if <условие> then' (без then блок не открывается): {line}")
                return
            self._open_block(f'if ({self.expr(cond[:-4].strip())})')
            return
        if line.startswith('while '):
            cond = line[6:].strip()
            if not cond.endswith('do'):
                self._error(f"v2: 'while <условие> do': {line}")
                return
            self._open_block(f'while ({self.expr(cond[:-2].strip())})')
            return
        if line.startswith('loop '):
            self._error(f"v2: 'loop' заменён на 'while ... do': {line}")
            return
        if line.startswith('for '):
            self._open_for(line)
            return
        if line in ('break', 'continue'):
            if not any(h.startswith('while (') for h, _f in self.blocks):
                self._error(f"'{line}' outside of a loop: {line}")
                return
            self._out(line + ';')
            return
        if line == 'else' or line.startswith(('else if ', 'elif ')):
            if not self.blocks:
                self._error(f"'else' without 'if': {line}")
                return
            if line == 'else':
                header = 'else'
            else:
                rest = line[8:] if line.startswith('else if ') else line[5:]
                rest = rest.strip()
                if not rest.endswith('then'):
                    self._error(f"v2: 'else if <условие> then': {line}")
                    return
                header = 'else if (' + self.expr(rest[:-4].strip()) + ')'
            self.indent -= 1
            self._out(f'}} {header} {{')
            self.indent += 1
            return
        if line == 'return':
            self._out('return;')
            return
        if line.startswith('return '):
            rhs = line[7:].strip()
            self._check_idents(rhs, where)
            self._out(f'return {self.expr(rhs)};')
            return
        lm = re.match(r'^local\s+(' + _NAME + r')\s*(?:=\s*(.+))?$', line)
        if lm and canon_type(lm.group(1)) not in TYPES and lm.group(1) not in self.objects:
            name, v = lm.group(1), (lm.group(2) or '').strip()
            if name in self.scope:
                self._error(f"dup var '{name}'")
                return
            t = self.expr_type(v) if v else None
            t = t if t in TYPES or t in self.objects else 'num'
            self._check_idents(v, where) if v else None
            self.scope[name] = t
            init = f'= {self.expr(v)}' if v else f'= {self.default_value(t)}'
            self._out(f'{self.c_type(t)} {name} {init};')
            return
        lst = self._decl_list(line)
        if lst and lst[0][1]:  # local ...
            for vis, _loc, t, n, v in lst:
                if vis:
                    self._error(f"v2: у local не бывает модификатора: {line}")
                    continue
                if n in self.scope:
                    self._error(f"dup var '{n}'")
                    continue
                if v:
                    self._check_idents(v, where)
                    self._check_types(t, v, f"local '{n}'")
                    init = f'= {self.expr(v)}'
                else:
                    init = f'= {self.default_value(t)}'
                self.scope[n] = t
                self._out(f'{self.c_type(t)} {n} {init};')
            return
        if lst and not lst[0][1] and not lst[0][0]:
            self._error(f"v2: внутри функции локальные переменные объявляются через local: {line}")
            return
        if lst:
            self._error(f"v2: модификатор public/private внутри функции не нужен, используйте local: {line}")
            return
        self._emit_statement(line, where)

    def _open_block(self, header, footer=None):
        self.blocks.append((header, footer))
        self._out(header + ' {')
        self.indent += 1

    def _open_for(self, line):
        m = _FOR_RE.match(line)
        if not m:
            self._error(
                "v2: цикл пишется 'for <var> = <от> to <до> [step <n>] do': " + line)
            return
        var, lo, hi, step = m.group(1), m.group(2), m.group(3), (m.group(4) or '').strip()
        nums = [num_value(lo), num_value(hi)]
        down = all(v is not None for v in nums) and nums[0] > nums[1]
        op = '>=' if down else '<='
        step_expr = self.expr(step) if step else ('-1' if down else '1')
        if var in self.scope or var in self.vars:
            self._out(f'{self._fields(var)} = {self.expr(lo)};')
        else:
            self.scope[var] = 'num'
            self._out(f'{self.c_type("num")} {var} = {self.expr(lo)};')
        hi_expr = self.expr(hi)
        self._open_block(
            f'while ({var} {op} {hi_expr})',
            footer=f'{self._fields(var)} += {step_expr};')

    def _split_call(self, line):
        m = re.match(r'^(' + _NAME + r')\s*\((.*)\)\s*$', line, re.S)
        if m:
            return m.group(1), (m.group(2) or '').strip()
        return None, None

    def _emit_statement(self, line, where):
        ci = find_compound(line)
        if ci >= 0:
            self._emit_compound(line[:ci].strip(), line[ci], line[ci + 2:].strip(), where)
            return
        i = find_assign(line)
        if i >= 0:
            self._emit_assign(line[:i].strip(), line[i + 1:].strip(), where)
            return
        # Инструкция-вызов: скобки обязательны (v2).
        if re.match(r'^' + _NAME + r'\s+[^=]+$', line) and not line.startswith(('local ', 'return ')):
            self._error(f"v2: вызов пишется со скобками: '{line.split()[0]}(...)': {line}")
            return
        name, rest = self._split_call(line)
        if not name:
            # Метод/self/цепочка как инструкция: self.m(x), obj.m(x), Ns.f(x).
            if re.match(r'^(self|' + _NAME + r')\.', line):
                self._check_idents(line, where)
                self._out(self.expr(line) + ';')
                return
            self._error(f"v2: нераспознанная инструкция: {line}")
            return
        args = split_top(rest, ',') if rest else []
        for a in args:
            self._check_idents(a, where)
        if name in self.functions:
            vis, params, _body = self.functions[name]
            if len(args) != len(params):
                self._error(f"call '{name}' expects {len(params)} arg(s), got {len(args)}: {line}")
                return
            fn = f'ds_fn_{name}'
        elif name in BUILTINS:
            fn = FUNCTION_MAP.get(name, name)
        else:
            self._error(f"неизвестный вызов '{name}' (v2: объявите функцию или "
                        f"добавьте нативную в BUILTINS): {line}")
            return
        self._out(f'{fn}({", ".join(self.expr(a) for a in args)});')

    def _emit_compound(self, lhs, op, rhs, where):
        m = re.match(r'^(self\.)?(' + _NAME + r')(?:\.(' + _NAME + r'))?$', lhs)
        if not m:
            self._error(f"cannot assign to '{lhs}': {lhs} {op}= {rhs}")
            return
        self_ref, name, field = m.group(1), m.group(2), m.group(3)
        if self_ref:
            if field:
                self._error(f"{where}: присваивание цепочке self.{name}.{field} не поддерживается")
                return
            if not self._check_field_write('self', name, where):
                return
            target = f'self->{name}'
        else:
            if name not in self.scope and name not in self.vars and name not in ENGINE_VARS:
                self._error(f"необъявленная переменная в '{lhs} {op}= {rhs}'")
                return
            target = self._fields(lhs)
        self._check_idents(rhs, where)
        self._out(f'{target} {op}= {self.expr(rhs)};')

    def _check_field_write(self, name, field, where):
        cls = self.cur_class
        if name != 'self' or not cls:
            self._error(f"{where}: присваивание полю без self.: {name}.{field}")
            return False
        f = self.objects.get(cls, {}).get(field)
        if not f:
            self._error(f"{where}: у класса '{cls}' нет поля '{field}'")
            return False
        return True

    def _emit_assign(self, lhs, rhs, where):
        m = re.match(r'^(self\.)?(' + _NAME + r')(?:\.(' + _NAME + r'))?$', lhs)
        if not m:
            self._error(f"v2: нераспознанное присваивание: {lhs} = {rhs}")
            return
        self_ref, name, field = m.group(1), m.group(2), m.group(3)
        self._check_idents(rhs, where)
        if self_ref:
            if field:
                self._error(f"{where}: присваивание цепочке self.{name}.{field} не поддерживается")
                return
            if not self._check_field_write('self', name, where):
                return
            f = self.objects[self.cur_class][name]
            self._check_types(f[1], rhs, f"поле '{self.cur_class}.{name}'")
            self._out(f'self->{name} = {self.expr(rhs)};')
            return
        if rhs.startswith('new '):
            mm = re.match(r'^new\s+(' + _NAME + r')\s*\(\)$', rhs)
            if name in self.scope and mm and self.scope[name] in self.objects:
                self._out(f'{name} = ds_new_{mm.group(1)}();')
                return
            if name in self.vars and mm and self.vars[name][1] in self.objects:
                self._out(f'{name} = ds_new_{mm.group(1)}();')
                return
            self._error(f"v2: 'new' допустим только в объявлении: {lhs} = {rhs}")
            return
        if name not in self.scope and name not in self.vars and name not in ENGINE_VARS:
            self._error(f"присваивание необъявленной переменной '{name}': {lhs} = {rhs}")
            return
        holder_type = self._holder_type(name)
        if field:
            if holder_type not in self.objects:
                if name in ENGINE_VARS:
                    self._out(f'{lhs} = {self.expr(rhs)};')
                    return
                self._error(f"'{name}' не объект класса: {lhs} = {rhs}")
                return
            f = self.objects[holder_type].get(field)
            if not f:
                self._error(f"у класса '{holder_type}' нет поля '{field}'")
                return
            if f[0] == 'private' and holder_type != self.cur_class:
                self._error(f"приватное поле '{holder_type}.{field}' изменено извне класса")
                return
            self._check_types(f[1], rhs, f"поле '{holder_type}.{field}'")
            self._out(f'{self._fields(lhs)} = {self.expr(rhs)};')
            return
        self._check_types(holder_type, rhs, f"переменная '{name}'")
        self._out(f'{name} = {self.expr(rhs)};')

    # ─────────────────────────── генерация ───────────────────────────

    def _infer_returns(self):
        keys = [n for n in self.functions]
        keys += [f'{c}.{m}' for c, ms in self.methods.items() for m in ms]
        for _ in range(4):
            changed = False
            for key in keys:
                if key in self.functions:
                    _vis, params, body = self.functions[key]
                else:
                    c, m = key.split('.')
                    _v, _s, params, _r, body = self.methods[c][m]
                if self.func_ret.get(key) not in (None, 'num'):
                    continue
                saved_scope, saved_class = self.scope, self.cur_class
                self.scope = {pn: pt for pt, pn in params}
                if '.' in key:
                    self.cur_class = key.split('.')[0]
                    self.scope['self'] = self.cur_class
                kind = 'num'
                for line in body:
                    lst = self._decl_list(line)
                    if lst and lst[0][1]:
                        for _v, _l, t, n, v in lst:
                            self.scope[n] = t
                        continue
                    lm = re.match(r'^local\s+(' + _NAME + r')\s*=\s*(.+)$', line)
                    if lm:
                        t = self.expr_type(lm.group(2))
                        self.scope[lm.group(1)] = t if t else 'num'
                        continue
                    if line.startswith('return '):
                        if self.expr_type(line[7:].strip()) == 'str':
                            kind = 'str'
                            break
                self.scope, self.cur_class = saved_scope, saved_class
                if self.func_ret.get(key) != kind:
                    self.func_ret[key] = kind
                    changed = True
            if not changed:
                break

    def _ret_c(self, key):
        r = self.func_ret.get(key)
        if not r or r == 'void':
            return 'void'
        return self.c_type(r)

    def _params_c(self, params):
        if not params:
            return 'void'
        return ', '.join(f'{self.c_type(t)} {n}' for t, n in params)

    def generate(self):
        self._infer_returns()
        self.output = []
        self.indent = 0
        self._emit('#include "runtime.h"')
        self._emit('#include "net.h"')
        self._emit('#include <math.h>')
        self._emit('')
        for name in self.objects:
            self._emit(f'typedef struct {name} {name};')
        self._emit('')
        for name, fields in self.objects.items():
            self._emit(f'struct {name} {{')
            for f, (_vis, t, _v) in fields.items():
                self._emit(f'    {self.c_type(t)} {f};')
            self._emit('};')
        self._emit('')
        for name in self.objects:
            self._emit(f'static {name} *ds_new_{name}(void);')
            self._emit(f'static void ds_free_{name}({name} *self);')
        for cls, ms in self.methods.items():
            for mname, (vis, static, params, _ret, _body) in ms.items():
                if mname == 'new':
                    args, pre = f'{cls} *self', 'ds_mth_'
                elif static:
                    args, pre = self._params_c(params), 'ds_stc_'
                else:
                    args = f'{cls} *self' + (', ' + self._params_c(params) if params else '')
                    pre = 'ds_mth_'
                self._emit(f'static {self._ret_c(cls + "." + mname)} {pre}{cls}_{mname}({args});')
        self._emit('')
        init_lines = []
        for n, (_vis, t, v) in self.vars.items():
            if t in self.objects:
                self._emit(f'{t} *{n} = NULL;')
            elif v and self.static_expr(v):
                self._emit(f'{self.c_type(t)} {n} = {self.expr(v)};')
            else:
                self._emit(f'{self.c_type(t)} {n} = {self.default_value(t)};')
            if v:
                init_lines.append(n)
        if self.vars:
            self._emit('')
        for n, (_vis, _params, _b) in self.functions.items():
            self._emit(f'static {self._ret_c(n)} ds_fn_{n}({self._params_c(_params)});')
        if self.functions:
            self._emit('')
        for name, fields in self.objects.items():
            self._emit(f'static {name} *ds_new_{name}(void) {{')
            self._emit(f'    {name} *self = ({name} *)calloc(1, sizeof(*self));')
            self._emit(f'    if (!self) {{ ds_runtime_error("out of memory: {name}"); return NULL; }}')
            for f, (_vis, _t, v) in fields.items():
                if v:
                    self._emit(f'    self->{f} = {self.expr(v)};')
            ctor = self.methods.get(name, {}).get('new')
            if ctor:
                self._emit(f'    ds_mth_{name}_new(self);')
            self._emit('    return self;')
            self._emit('}')
            self._emit(f'static void ds_free_{name}({name} *self) {{ free(self); }}')
            self._emit('')
        for cls, ms in self.methods.items():
            for mname, (vis, static, params, _ret, body) in ms.items():
                if mname == 'new':
                    cname = f'ds_mth_{cls}_new'
                    args = f'{cls} *self'
                elif static:
                    cname = f'ds_stc_{cls}_{mname}'
                    args = self._params_c(params)
                else:
                    cname = f'ds_mth_{cls}_{mname}'
                    args = f'{cls} *self' + (', ' + self._params_c(params) if params else '')
                self._emit(f'static {self._ret_c(cls + "." + mname)} {cname}({args}) {{')
                self.indent = 1
                self.scope = {pn: pt for pt, pn in params}
                if not static:
                    self.scope['self'] = cls
                self.cur_class = cls
                self.blocks = []
                body_text = '\n'.join(body)
                for _pt, pn in params:
                    if not used_outside_strings(body_text, pn):
                        self._out(f'(void){pn};')
                if mname == 'new' and not used_outside_strings(body_text, 'self'):
                    self._out('(void)self;')
                for line in body:
                    self._emit_line(line, f'{cls}.{mname}')
                self._emit('}')
                self._emit('')
        for n, (_vis, params, body) in self.functions.items():
            self._emit(f'static {self._ret_c(n)} ds_fn_{n}({self._params_c(params)}) {{')
            self.indent = 1
            self.scope = {pn: pt for pt, pn in params}
            self.cur_class = None
            self.blocks = []
            body_text = '\n'.join(body)
            for _pt, pn in params:
                if not used_outside_strings(body_text, pn):
                    self._out(f'(void){pn};')
            for line in body:
                self._emit_line(line, n)
            self._emit('}')
            self._emit('')
        self._emit('static int ds_main(void) {')
        self.indent = 1
        self.scope = {}
        self.cur_class = None
        self.blocks = []
        for n in init_lines:
            t = self.vars[n][1]
            if t in self.objects:
                self._out(f'{n} = ds_new_{t}();')
            elif self.vars[n][2]:
                try:
                    self._out(f'{n} = {self.expr(self.vars[n][2])};')
                except Exception:
                    pass
        for line in self.top:
            self._emit_line(line, 'top level')
        self._emit('    return 0;')
        self._emit('}')
        self._emit('')
        self._emit('void reset(void) {')
        self.indent = 1
        for n, (_vis, t, v) in self.vars.items():
            if t in self.objects:
                self._out(f'if ({n}) ds_free_{t}({n});')
                self._out(f'{n} = NULL;')
            elif t == 'arr':
                self._out(f'if ({n}) arr_free({n});')
                self._out(f'{n} = arr_new();')
            elif v and self.static_expr(v):
                self._out(f'{n} = {self.expr(v)};')
            else:
                self._out(f'{n} = {self.default_value(t)};')
        self._emit('}')
        self._emit('')
        self._emit('void init(AAssetManager *assets) {')
        self.indent = 1
        self._out('ds_set_asset_manager(assets);')
        self._out('ds_main();')
        if 'init' in self.functions:
            self._out('ds_fn_init();')
        self._emit('}')
        self._emit('')
        self._emit('void update(void) {')
        self.indent = 1
        if 'update' in self.functions:
            self._out('ds_fn_update();')
        self._emit('}')
        self._emit('')
        self._emit('void draw(Buffer *buffer) {')
        self.indent = 1
        self._out('(void)buffer;')
        if 'draw' in self.functions:
            self._out('ds_fn_draw();')
        self._emit('}')
        self._emit('')
        self._emit('int back_pressed(void) {')
        self.indent = 1
        if 'back_pressed' in self.functions and self.func_ret.get('back_pressed') == 'num':
            self._out('return ds_fn_back_pressed() != 0;')
        else:
            self._out('return 0;')
        self._emit('}')
        self._emit('')
        self._emit('void touch(float x, float y, int action, int pointer_id) {')
        self.indent = 1
        self._out('mouse_clicked = (action == 0) ? 1 : 0;')
        self._out('if (action == 0) { ds_mouse_x = x; ds_mouse_y = y; }')
        if 'touch' in self.functions:
            args = []
            for i, (pt, _pn) in enumerate(self.functions['touch'][1]):
                if i >= 4 or pt == 'str':
                    break
                args.append(f'({self.c_type(pt)}){("x", "y", "action", "pointer_id")[i]}')
            self._out(f'ds_fn_touch({", ".join(args)});')
        else:
            self._out('(void)x; (void)y; (void)action; (void)pointer_id;')
        self._emit('}')

    def compile(self, sources, output):
        if not self._load(sources):
            return False
        self.parse()
        try:
            self.generate()
        except Exception as exc:  # noqa: BLE001
            self._error(f"internal {exc}")
            return False
        with open(output, 'w', encoding='utf-8') as f:
            f.write('\n'.join(self.output) + '\n')
        return self.errors == 0


def main():
    output = 'game/game.c'
    sources = []
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] in ('-o', '--output') and i + 1 < len(args):
            output = args[i + 1]
            i += 2
        else:
            sources.append(args[i])
            i += 1
    if not sources:
        print("Usage: python ds_compiler.py file.ds [-o output.c]", file=sys.stderr)
        sys.exit(2)
    ok = DimScriptCompiler().compile(sources, output)
    print(f"{output}: {'OK' if ok else 'FAILED'}")
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
