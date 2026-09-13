import re
import sys

TYPES = {
    'num': 'double',
    'number': 'double',
    'str': 'const char*',
    'string': 'const char*',
    'col': 'uint32_t',
    'color': 'uint32_t',
    'arr': 'DSArray*',
    'array': 'DSArray*',
}

_TYPE_ALIAS = {
    'number': 'num',
    'string': 'str',
    'color': 'col',
    'array': 'arr',
}


def canon_type(t):
    return _TYPE_ALIAS.get(t, t)


BUILTINS = frozenset({
    'rect', 'roundrect', 'circle', 'ring', 'line', 'tex', 'tex_tint', 'text',
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
    'net_load_language', 'net_load_hitboxes', 'net_save_settings',
    'net_load_music_volume', 'net_save_music_volume',
    'net_load_fps_cap', 'net_save_fps_cap',
    'net_load_render_scale', 'net_save_render_scale',
    'ds_set_fps_cap', 'ds_set_render_scale',
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

_NAME = r'[A-Za-z_]\w*'
_FUNC_RE = re.compile(r'^function\s+(' + _NAME + r')(?:\s*(.*))?$')
_NUM_RE = re.compile(r'^(?:[-+]?\d+(?:\.\d+)?|0[xX][0-9a-fA-F]+)$')
_CALL_RE = re.compile(r'^(' + _NAME + r')(?:\s+(.*))?$')
_LHS_RE = re.compile(r'^(' + _NAME + r')(?:\.(' + _NAME + r'))?$')
# for <переменная> = <от> to <до> [step <шаг>] — русские слова-синонимы тоже
# принимаются, чтобы цикл можно было писать и по-русски.
_FOR_RE = re.compile(
    r'^for\s+(' + _NAME + r')\s*=\s*(.+?)\s+(?:to|до)\s+(.+?)'
    r'(?:\s+(?:step|шаг)\s+(.+))?$', re.IGNORECASE)
_COMPOUND_OPS = ('+=', '-=', '*=', '/=')


def num_value(text):
    """Числовое значение литерала или None, если это не просто число.

    Принимает и десятичные, и шестнадцатеричные записи (0xFF00FF), потому что
    цвета в скриптах пишутся именно так.
    """
    text = text.strip()
    if not _NUM_RE.match(text):
        return None
    try:
        # У float() нет системы счисления, поэтому 0x... разбираем через int().
        return float(int(text, 16)) if text[:2].lower() == '0x' else float(text)
    except ValueError:
        return None


def find_compound(line):
    """Позиция составного оператора (+=, -=, *=, /=) вне строк и скобок.

    Ищется раньше обычного '=': иначе 'hp += 5' разрезалось бы по '=' и левая
    часть 'hp +' тихо пропадала.
    """
    depth, quoted = scan(line)
    for i, c in enumerate(line):
        if quoted[i] or depth[i] or i + 1 >= len(line):
            continue
        if line[i + 1] == '=' and c in '+-*/':
            return i
    return -1


def strip_comment(line):
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
        elif c == '"':
            in_str = True
            out.append(c)
        elif c == '/' and i + 1 < len(line) and line[i + 1] == '/':
            break
        elif c == '#':
            # Альтернативный маркер комментария: '#' привычнее, чем '//'
            # (вне строковых литералов — внутри строк '#' остаётся символом).
            break
        else:
            out.append(c)
        i += 1
    return ''.join(out)


def scan(text):
    n = len(text)
    depth = [0] * n
    quoted = [False] * n
    in_str = False
    esc = False
    lvl = 0
    for i, c in enumerate(text):
        quoted[i] = in_str
        if esc:
            esc = False
            continue
        if in_str:
            if c == '\\':
                esc = True
            elif c == '"':
                in_str = False
            continue
        if c == '"':
            in_str = True
        elif c == '(':
            lvl += 1
        elif c == ')':
            lvl = max(0, lvl - 1)
        depth[i] = lvl
    return depth, quoted


def open_parens(text):
    """Сколько '(' остались незакрытыми (вне строковых литералов).

    Нужно для склейки переносов: вызов, разбитый на несколько строк, компилятор
    раньше принимал по частям и МОЛЧА терял — так из game.c пропало
    сохранение прогресса (net_save_progress_all на восемь строк).
    """
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


def strip_kw(cond, kw):
    """Drop an optional trailing 'then'/'do' and/or ':' from a block header."""
    for suf in (' ' + kw, ' ' + kw + ':'):
        if cond.endswith(suf):
            cond = cond[:-len(suf)].strip()
            break
    return cond[:-1].strip() if cond.endswith(':') else cond


class DimScriptCompiler:
    def __init__(self):
        self.objects = {}
        self.vars = {}
        self.functions = {}
        self.func_ret = {}
        self.top = []
        self.lines = []
        self.errors = 0
        self.warnings = 0
        self.warned = set()
        self.output = []
        self.indent = 0
        self.scope = {}
        self.blocks = []

    def _error(self, msg):
        self.errors += 1
        print(f"DimScript error: {msg}", file=sys.stderr)

    def _warn(self, msg):
        """Сообщить о подозрительной строке, не роняя сборку.

        Без этого вызов несуществующей нативной функции молча исчезал из
        game.c, и в собранной игре команда просто ничего не делала.
        """
        if msg in self.warned:
            return
        self.warned.add(msg)
        self.warnings += 1
        print(f"DimScript warning: {msg}", file=sys.stderr)

    def _load(self, paths):
        for p in paths:
            pending = ''
            try:
                with open(p, 'r', encoding='utf-8-sig') as f:
                    for raw in f:
                        line = strip_comment(raw).strip()
                        if not line:
                            continue
                        # Перенос вызова: пока скобки не закрыты, строка ещё не
                        # закончилась — следующая физическая строка продолжает её.
                        if pending:
                            line = pending + ' ' + line
                        if open_parens(line) > 0:
                            pending = line
                            continue
                        pending = ''
                        self.lines.extend(q for q in map(str.strip, split_top(line, ';')) if q)
            except OSError as e:
                self._error(f"cannot read '{p}': {e}")
                return False
            if pending:
                self._error(f"unfinished call in '{p}' (unclosed parenthesis): {pending}")
        return True

    def _decl_list(self, line):
        m = re.match(r'^(' + _NAME + r')\s+(.+)$', line)
        if not m:
            return None
        t, rest = canon_type(m.group(1)), m.group(2).strip()
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
            res.append((t, mm.group(1), v.strip() if v else v))
        return res or None

    def parse(self):
        i = 0
        while i < len(self.lines):
            line = self.lines[i]
            if line == 'end':
                self._error("unexpected 'end' at top level")
                i += 1
            elif line.startswith('include '):
                i += 1
            elif line.startswith('object '):
                i = self._parse_object(i)
            elif line.startswith('function '):
                i = self._parse_function(i)
            elif self._decl_all(line):
                self._parse_global(line)
                i += 1
            else:
                self.top.append(line)
                i += 1
        return self.errors == 0

    def _decl_all(self, line):
        lst = self._decl_list(line)
        if not lst:
            return None
        if any(t not in TYPES and t not in self.objects for t, n, v in lst):
            return None
        return lst

    def _obj_fields(self, name, fields, lst):
        for t, n, v in lst:
            if t not in TYPES:
                self._error(f"object '{name}': expected type")
            elif n in fields:
                self._error(f"dup field '{name}.{n}'")
            else:
                fields[n] = (t, v)

    def _parse_object(self, i):
        m = re.match(r'^object\s+(' + _NAME + r')(?:\s+(.+))?$', self.lines[i])
        if not m:
            self._error(f"invalid object: {self.lines[i]}")
            return i + 1
        name, rest = m.group(1), (m.group(2) or '').strip()
        if name in self.objects:
            self._error(f"dup object '{name}'")
            return i + 1
        fields = {}
        if rest and rest != 'end':
            lst = self._decl_list(rest)
            if lst:
                self._obj_fields(name, fields, lst)
        j = i + 1
        while j < len(self.lines):
            line = self.lines[j]
            if line == 'end':
                self.objects[name] = fields
                return j + 1
            lst = self._decl_all(line)
            if not lst:
                self._error(f"object '{name}': expected 'type name = value', got {line}")
            else:
                self._obj_fields(name, fields, lst)
            j += 1
        self._error(f"object '{name}' no end")
        return j

    def _parse_function(self, i):
        m = _FUNC_RE.match(self.lines[i])
        name = m.group(1)
        params = self._parse_params(m.group(2) or '')
        body, j = self._collect_block(i + 1, f"function '{name}'")
        if name in self.functions:
            self._error(f"dup function '{name}'")
        else:
            self.functions[name] = (params, body)
        if any(line.startswith('return ') for line in body):
            self.func_ret[name] = 'num'
        return j

    def _infer_returns(self):
        for _ in range(4):
            ch = False
            for name in list(self.func_ret.keys()):
                params, body = self.functions[name]
                saved = self.scope
                self.scope = {pn: pt for pt, pn in params}
                kind = 'num'
                for line in body:
                    lst = self._decl_all(line)
                    if lst:
                        for t, n, v in lst:
                            if t in TYPES:
                                self.scope[n] = t
                        continue
                    if line.startswith('return '):
                        if self.expr_type(line[7:].strip()) == 'str':
                            kind = 'str'
                            break
                self.scope = saved
                if self.func_ret[name] != kind:
                    self.func_ret[name] = kind
                    ch = True
            if not ch:
                break

    def _parse_params(self, text):
        text = (text or '').strip()
        if text.startswith('(') and text.endswith(')'):
            text = text[1:-1].strip()
        params = []
        for part in split_top(text, ',') if text.strip() else []:
            w = part.split()
            if len(w) != 2 or (w[0] not in TYPES and w[0] not in self.objects):
                self._error(f"invalid param '{part}'")
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
            elif line.startswith(('if ', 'loop ', 'while ', 'for ')):
                depth += 1
            elif line == 'else' or line.startswith(('else if ', 'elif ')):
                if depth == 0:
                    self._error(f"{what}: 'else' without 'if'")
                    return body, i + 1
            elif line.startswith('object ') or line.startswith('function '):
                self._error(f"{what}: nested not allowed")
                body.append(line)
                i += 1
                continue
            body.append(line)
            i += 1
        self._error(f"{what} no end")
        return body, i

    def _parse_global(self, line):
        for t, n, v in self._decl_all(line) or []:
            if n in self.vars:
                self._error(f"dup var '{n}'")
                continue
            if t in self.objects:
                if not v or not re.match(r'^new\s+' + re.escape(t) + r'\s*\(\)?\s*$', v):
                    self._error(f"'{n}': must be 'new {t}()'")
                    continue
            self.vars[n] = (t, v)

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

    def expr_type(self, expr):
        expr = expr.strip()
        if expr.startswith('"') and expr.endswith('"'):
            return 'str'
        m = re.match(r'^(' + _NAME + r')\.(' + _NAME + r')$', expr)
        if m:
            holder = m.group(1)
            ot = self.scope.get(holder) or (self.vars[holder][0] if holder in self.vars else None)
            fields = self.objects.get(ot)
            if fields and m.group(2) in fields:
                return fields[m.group(2)][0]
        if expr in self.scope:
            return self.scope[expr]
        if expr in self.vars:
            return self.vars[expr][0]
        call = re.match(r'^(' + _NAME + r')\s*\(.*\)$', expr)
        if call:
            if call.group(1) in self.func_ret:
                return self.func_ret[call.group(1)]
            if call.group(1) in STR_BUILTINS:
                return 'str'
        return ENGINE_VARS.get(expr, 'num')

    def expr(self, e):
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

    def _logic(self, e):
        """Слова-операторы: and / or / not -> && / || / !.

        Так условие читается как обычный текст: 'if alive and hp <= 0'. Старый
        синтаксис с '&&' продолжает работать — замена применяется только вне
        строковых литералов, поэтому строки вида "and" не затрагиваются.
        """
        e = e.strip()
        e = sub_unquoted(e, re.compile(r'\band\b'), '&&')
        e = sub_unquoted(e, re.compile(r'\bor\b'), '||')
        e = sub_unquoted(e, re.compile(r'\bnot\b\s*'), '!')
        return e

    def _fields(self, e):
        names = [n for n in self.vars if self.vars[n][0] in self.objects] + [
            n for n, t in self.scope.items() if t in self.objects
        ]
        for n in sorted(names, key=len, reverse=True):
            e = sub_unquoted(e, re.compile(r'\b' + re.escape(n) + r'\.(' + _NAME + r')'), n + r'->\1')
        return self._calls(e)

    def _calls(self, e):
        if not self.functions and not FUNCTION_MAP:
            return e
        _, quoted = scan(e)
        out = []
        start = 0
        for m in re.finditer(r'\b(' + _NAME + r')\s*\(', e):
            if quoted[m.start()]:
                continue
            name = m.group(1)
            if name in self.functions:
                out.append(e[start:m.start()])
                out.append('ds_fn_' + name + '(')
            elif name in FUNCTION_MAP:
                # min/max/abs/... в C называются ds_min/ds_max/ds_abs/...
                out.append(e[start:m.start()])
                out.append(FUNCTION_MAP[name] + '(')
            else:
                continue
            start = m.end()
        out.append(e[start:])
        return ''.join(out)

    def as_str(self, e):
        if self.expr_type(e) == 'str':
            return self.expr(e)
        return f'ds_num_to_string((double)({self.expr(e)}))'

    def _out(self, s):
        self.output.append('    ' * self.indent + s)

    def _emit(self, s):
        self.output.append(s)

    def _emit_line(self, line):
        if line == 'end':
            if not self.blocks:
                self._error("unexpected 'end'")
                return
            _header, footer = self.blocks.pop()
            self.indent -= 1
            if footer:
                self._out(footer)
            self._out('}')
            return
        if line.startswith('if '):
            self._open_block(f'if ({self.expr(strip_kw(line[3:].strip(), "then"))})')
            return
        if line.startswith('loop ') or line.startswith('while '):
            cond = line.split(' ', 1)[1].strip()
            self._open_block(f'while ({self.expr(strip_kw(cond, "do"))})')
            return
        if line.startswith('for '):
            self._open_for(line)
            return
        if line in ('break', 'continue'):
            # Выйти из цикла / перейти к следующему шагу. Имеют смысл только
            # внутри loop/while/for — снаружи это ошибка, а не тихий пропуск.
            if not any(h.startswith('while (') for h, _f in self.blocks):
                self._error(f"'{line}' outside of a loop: {line}")
                return
            self._out(line + ';')
            return
        if line == 'else' or line.startswith(('else if ', 'elif ')):
            if not self.blocks:
                self._error("'else' without 'if'")
                return
            if line == 'else':
                header = 'else'
            else:
                # 'else if <условие>' -> условие начинается с 8-го символа,
                # 'elif <условие>' -> с 5-го. Одинаковое split(' ', 1) здесь
                # ломало обычное 'else if': остаток 'if i == 1' уезжал внутрь
                # скобок и game.c не собирался.
                rest = line[8:] if line.startswith('else if ') else line[5:]
                header = 'else if (' + self.expr(strip_kw(rest.strip(), 'then')) + ')'
            self.indent -= 1
            self._out(f'}} {header} {{')
            self.indent += 1
            return
        if line == 'return':
            self._out('return;')
            return
        if line.startswith('return '):
            self._out(f'return {self.expr(line[7:])};')
            return
        lst = self._decl_all(line)
        if lst and lst[0][0] in TYPES:
            for t, n, v in lst:
                if n in self.scope:
                    self._error(f"dup var '{n}'")
                    continue
                self.scope[n] = t
                init = f'= {self.expr(v)}' if v else f'= {self.default_value(t)}'
                self._out(f'{self.c_type(t)} {n} {init};')
            return
        self._emit_statement(line)

    def _open_block(self, header, footer=None):
        """Начать блок. footer — строка, которая дописывается перед '}'.

        Footer нужен циклу for: приращение счётчика ставится в самый конец тела,
        чтобы 'continue' не пропускал его (иначе цикл не двигался бы вперёд).
        """
        self.blocks.append((header, footer))
        self._out(header + ' {')
        self.indent += 1

    def _open_for(self, line):
        """for i = 1 to 10 [step 2] -> объявление + while + приращение в конце.

        Если обе границы — числа и первая больше второй, цикл идёт вниз:
        сравнение становится >=, а шаг по умолчанию -1.
        """
        m = _FOR_RE.match(line)
        if not m:
            self._error(
                "invalid for-loop, expected 'for <var> = <from> to <to> [step <n>]': " + line)
            return
        var, lo, hi, step = m.group(1), m.group(2), m.group(3), (m.group(4) or '').strip()
        nums = [num_value(lo), num_value(hi)]
        down = all(v is not None for v in nums) and nums[0] > nums[1]
        op = '>=' if down else '<='
        step_expr = self.expr(step) if step else ('-1' if down else '1')
        if var in self.scope or var in self.vars:
            self._out(f'{self._fields(var)} = {self.expr(lo)};')
            ctype = None
        else:
            self.scope[var] = 'num'
            ctype = self.c_type('num')
            self._out(f'{ctype} {var} = {self.expr(lo)};')
        hi_expr = self.expr(hi)
        self._open_block(
            f'while ({var} {op} {hi_expr})',
            footer=f'{self._fields(var)} += {step_expr};')

    def _split_call(self, line):
        m = re.match(r'^(' + _NAME + r')\s*\((.*)\)\s*$', line, re.S)
        if m:
            return m.group(1), (m.group(2) or '').strip()
        m = _CALL_RE.match(line)
        if m:
            return m.group(1), (m.group(2) or '').strip()
        return None, None

    def _emit_statement(self, line):
        ci = find_compound(line)
        if ci >= 0:
            self._emit_compound(line[:ci].strip(), line[ci], line[ci + 2:].strip())
            return
        i = find_assign(line)
        if i >= 0:
            self._emit_assign(line[:i].strip(), line[i + 1:].strip())
            return
        name, rest = self._split_call(line)
        if not name:
            # Раньше такая строка просто исчезала из game.c без единого слова:
            # так пропало сохранение прогресса. Теперь это предупреждение, а
            # тесты компиляции требуют нуля предупреждений.
            self._warn(f"unrecognized statement, dropped: {line}")
            return
        args = split_top(rest, ',') if rest else []
        if name in self.functions:
            if len(args) != len(self.functions[name][0]):
                self._warn(
                    f"call '{name}' expects {len(self.functions[name][0])} "
                    f"arg(s), got {len(args)} — statement dropped: {line}"
                )
                return
            fn = f'ds_fn_{name}'
        elif name in BUILTINS:
            fn = FUNCTION_MAP.get(name, name)
        else:
            # Ни функция скрипта, ни нативная функция: в C такой вызов
            # перенести некуда. Раньше строка просто исчезала из game.c.
            self._warn(
                f"unknown call '{name}', statement dropped "
                f"(add it to BUILTINS in ds_compiler.py if it is native): {line}"
            )
            return
        self._out(f'{fn}({", ".join(self.expr(a) for a in args)});')

    def _emit_compound(self, lhs, op, rhs):
        """hp += 5  ->  hp = hp + 5;  (в C остаётся тот же '+=')."""
        m = _LHS_RE.match(lhs)
        if not m:
            self._warn(f"cannot assign to '{lhs}': {lhs} {op}= {rhs}")
            return
        name = m.group(1)
        if name not in self.scope and name not in self.vars and name not in ENGINE_VARS:
            self._warn(f"unknown variable in '{lhs} {op}= {rhs}'")
            return
        target = self._fields(lhs)
        self._out(f'{target} {op}= {self.expr(rhs)};')

    def _emit_assign(self, lhs, rhs):
        m = _LHS_RE.match(lhs)
        if not m:
            return
        name, field = m.group(1), m.group(2)
        if rhs.startswith('new '):
            return
        if name not in self.scope and name not in self.vars and name not in ENGINE_VARS:
            return
        holder_type = self.scope.get(name) or self.vars.get(name, ('', None))[0]
        if field and holder_type in self.objects:
            lhs = self._fields(lhs)
        self._out(f'{lhs} = {self.expr(rhs)};')

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
            for f, (t, _v) in fields.items():
                self._emit(f'    {self.c_type(t)} {f};')
            self._emit('};')
        self._emit('')
        for name in self.objects:
            self._emit(f'static {name} *ds_new_{name}(void);')
            self._emit(f'static void ds_free_{name}({name} *self);')
        self._emit('')
        init_lines = []
        for n, (t, v) in self.vars.items():
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
        for n, (params, _b) in self.functions.items():
            self._emit(f'static {self._ret_c(n)} ds_fn_{n}({self._params_c(params)});')
        if self.functions:
            self._emit('')
        for name, fields in self.objects.items():
            self._emit(f'static {name} *ds_new_{name}(void) {{')
            self._emit(f'    {name} *self = ({name} *)calloc(1, sizeof(*self));')
            self._emit(f'    if (!self) {{ ds_runtime_error("out of memory: {name}"); return NULL; }}')
            for f, (t, v) in fields.items():
                if v:
                    self._emit(f'    self->{f} = {self.expr(v)};')
            self._emit('    return self;')
            self._emit('}')
            self._emit(f'static void ds_free_{name}({name} *self) {{ free(self); }}')
            self._emit('')
        for n, (params, body) in self.functions.items():
            self._emit(f'static {self._ret_c(n)} ds_fn_{n}({self._params_c(params)}) {{')
            self.indent = 1
            self.scope = {pn: pt for pt, pn in params}
            self.blocks = []
            body_text = '\n'.join(body)
            for _pt, pn in params:
                if not used_outside_strings(body_text, pn):
                    self._out(f'(void){pn};')
            for line in body:
                self._emit_line(line)
            self._emit('}')
            self._emit('')
        self._emit('static int ds_main(void) {')
        self.indent = 1
        self.scope = {}
        self.blocks = []
        for n in init_lines:
            t = self.vars[n][0]
            if t in self.objects:
                self._out(f'{n} = ds_new_{t}();')
            elif self.vars[n][1]:
                try:
                    self._out(f'{n} = {self.expr(self.vars[n][1])};')
                except Exception:
                    pass
        for line in self.top:
            self._emit_line(line)
        self._emit('    return 0;')
        self._emit('}')
        self._emit('')
        self._emit('void reset(void) {')
        self.indent = 1
        for n, (t, v) in self.vars.items():
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
            for i, (pt, _pn) in enumerate(self.functions['touch'][0]):
                if i >= 4 or pt == 'str':
                    break
                args.append(f'({self.c_type(pt)}){("x", "y", "action", "pointer_id")[i]}')
            self._out(f'ds_fn_touch({", ".join(args)});')
        else:
            self._out('(void)x; (void)y; (void)action; (void)pointer_id;')
        self._emit('}')

    def _ret_c(self, name):
        return self.c_type(self.func_ret[name]) if name in self.func_ret else 'void'

    def _params_c(self, params):
        if not params:
            return 'void'
        return ', '.join(f'{self.c_type(t)} {n}' for t, n in params)

    def compile(self, sources, output):
        if not self._load(sources):
            return False
        self.parse()
        try:
            self.generate()
        except Exception as exc:
            self._error(f"internal {exc}")
            return False
        with open(output, 'w', encoding='utf-8') as f:
            f.write('\n'.join(self.output) + '\n')
        return True


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
