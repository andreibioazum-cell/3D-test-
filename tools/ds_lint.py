#!/usr/bin/env python3
"""Статическая проверка скриптов DimScript v2 (game/scripts/*.ds).

Компилятор v2 строгий: необъявленная переменная, неизвестное имя, неизвестный
вызов, несовпадение арности и типов, чужое приватное поле — ошибки компиляции.
Линтер дублирует часть проверок именами (без генерации кода), чтобы падать на
опечатках до сборки C, и дополнительно проверяет то, что компилятор терпит:
чтение неизвестного идентификатора в выражении, `name.field` несуществующего
поля, вызов функции скрипта с другим числом аргументов.

Запуск: python3 tools/ds_lint.py [каталог-со-скриптами]
"""

from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ds_compiler import (  # noqa: E402
    BUILTINS, ENGINE_VARS, NATIVE_MATH, STR_BUILTINS, interp_holes, open_parens,
    split_top, strip_comment,
)
from gen import find_ds_files  # noqa: E402

_NAME = r'[A-Za-z_]\w*'
_DECL_RE = re.compile(
    r'^(?:(public|private)\s+)?(local\s+)?(' + _NAME + r')\s+(.+)$')
_TYPES = {'number', 'string', 'color', 'array', 'num', 'str', 'col', 'arr',
          'int', 'float', 'double', 'bool'}
# В бою есть несколько мест, где в выражение вставлен честный C-каст:
# такие слова не имена скрипта, а часть нативного выражения.
_RAW_C_WORDS = {'unsigned', 'int', 'double', 'char', 'float'}
_NUM_RE = re.compile(r'\b0[xX][0-9a-fA-F]+\b|\b\d+(?:\.\d+)?\b')
_BLOCK_OPEN = ('if ', 'while ', 'for ')


def iter_blocks(lines):
    """Разбивает модуль на ('function'|'method', class, name, params, body)."""
    i = 0
    cur_class = None
    while i < len(lines):
        line = lines[i]
        m = re.match(r'^class\s+(' + _NAME + r')$', line)
        if m:
            cur_class = m.group(1)
            i += 1
            continue
        m = re.match(r'^(?:public|private)\s+(?:static\s+)?function\s+(' + _NAME + r')\s*(.*)$', line)
        if m:
            body, i = collect_block(lines, i + 1)
            yield 'method' if cur_class else 'function', cur_class, m.group(1), \
                parse_params(m.group(2)), body
            continue
        if line == 'end':
            cur_class = None
        i += 1


def collect_block(lines, i):
    depth = 0
    body = []
    while i < len(lines):
        line = lines[i]
        if line == 'end':
            if depth == 0:
                return body, i + 1
            depth -= 1
        elif line.startswith(_BLOCK_OPEN):
            depth += 1
        body.append(line)
        i += 1
    return body, i


def parse_params(text):
    text = (text or '').strip()
    if text.startswith('(') and text.endswith(')'):
        text = text[1:-1].strip()
    if text.endswith(')') is False and '(' in text:
        text = text[text.index('(') + 1:text.rindex(')')] if ')' in text else ''
    params = []
    for part in split_top(text, ',') if text else []:
        words = part.split()
        if len(words) == 2:
            params.append(words[1])
    return params


def identifiers(text):
    """Идентификаторы выражения вне строк, чисел и точек (поля/методы)."""
    depth, quoted = _scan(text)
    text = _NUM_RE.sub(lambda m: ' ' * len(m.group(0)), text)
    out = []
    for m in re.finditer(_NAME, text):
        if quoted[m.start()] or depth[m.start()]:
            continue
        if m.start() > 0 and text[m.start() - 1] == '.':
            continue  # поле/метод: проверяются отдельно
        out.append((m.group(0), m.start()))
    return out


def _scan(text):
    n = len(text)
    depth = [0] * n
    quoted = [False] * n
    lvl, in_str, esc = 0, False, False
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
            i += 1
            quoted[i] = True
        elif c == '"':
            in_str = True
        elif c == '(':
            lvl += 1
        elif c == ')':
            lvl = max(0, lvl - 1)
        depth[i] = lvl
        i += 1
    return depth, quoted


class Lint:
    def __init__(self):
        self.globals = {}          # имя -> тип ('Class' для классовых)
        self.objects = {}          # имя класса -> set(поля)
        self.functions = {}        # имя -> [параметры]
        self.errors = []

    def error(self, where, msg):
        self.errors.append(f'{where}: {msg}')

    def scan_classes(self, lines):
        i = 0
        while i < len(lines):
            m = re.match(r'^class\s+(' + _NAME + r')$', lines[i])
            if not m:
                i += 1
                continue
            name = m.group(1)
            fields = set()
            i += 1
            while i < len(lines) and lines[i] != 'end':
                fm = _DECL_RE.match(lines[i])
                if fm and fm.group(3) in _TYPES and not fm.group(2):
                    for part in split_top(fm.group(4), ','):
                        pm = re.match(r'^(' + _NAME + r')', part.strip())
                        if pm:
                            fields.add(pm.group(1))
                i += 1
            self.objects[name] = fields
            i += 1

    def scan_declarations(self, lines, where):
        for line in lines:
            m = _DECL_RE.match(line)
            if not m or m.group(2) or not m.group(1):
                continue  # local и объявления без модификатора — не глобалки
            if m.group(3) not in _TYPES and m.group(3) not in self.objects:
                continue
            for part in split_top(m.group(4), ','):
                mm = re.match(r'^(' + _NAME + r')\s*(?:=.*)?$', part.strip())
                if mm:
                    self.globals[mm.group(1)] = m.group(3)

    def known(self, name):
        return (name in self.globals or name in self.functions
                or name in BUILTINS or name in ENGINE_VARS
                or name in STR_BUILTINS or name in _RAW_C_WORDS
                or name in NATIVE_MATH or name in self.objects
                or name in ('self', 'true', 'false'))

    def check_function(self, where, name, params, body, class_fields):
        scope = set(params)
        if class_fields is not None:
            scope.add('self')
        for line in body:
            m = _DECL_RE.match(line)
            if m and m.group(2) and (m.group(3) in _TYPES or m.group(3) in self.objects):
                for part in split_top(m.group(4), ','):
                    pm = re.match(r'^(' + _NAME + r')\s*(?:=(.*))?$', part.strip())
                    if pm:
                        scope.add(pm.group(1))
                        if pm.group(2):
                            self.check_expression(where, name, pm.group(2), scope,
                                                  class_fields)
                continue
            if line.startswith('else if ') or line.startswith('elif '):
                cond = line[8:] if line.startswith('else if ') else line[5:]
                self.check_expression(where, name, _no_then(cond), scope, class_fields)
                continue
            if line.startswith(('if ', 'while ')):
                self.check_expression(where, name, _no_then(line.split(' ', 1)[1]),
                                      scope, class_fields)
                continue
            if line.startswith('for '):
                self.check_expression(where, name, line[4:], scope, class_fields)
                continue
            if line.startswith('return '):
                self.check_expression(where, name, line[7:], scope, class_fields)
                continue
            if line in ('return', 'end', 'else', 'break', 'continue'):
                continue
            i = find_assign(line)
            if i >= 0:
                lhs, rhs = line[:i].strip(), line[i + 1:].strip()
                self.check_lhs(where, name, lhs, scope, class_fields)
                self.check_expression(where, name, rhs, scope, class_fields)
                continue
            cm = re.match(r'^(' + _NAME + r')\s*(?:\((.*)\))?$', line)
            if cm:
                self.check_call(where, name, cm.group(1), cm.group(2) or '', scope,
                                class_fields)
                continue
            if re.match(r'^(self|' + _NAME + r')\.', line):
                # методов/цепочек компилятор проверяет сам; имена аргументов — наши
                for arg in _dotted_args(line):
                    self.check_expression(where, name, arg, scope, class_fields)

    def check_lhs(self, where, fn, lhs, scope, class_fields):
        m = re.match(r'^(self\.)?(' + _NAME + r')(?:\.(' + _NAME + r'))?$', lhs)
        if not m:
            return
        self_ref, name, field = m.group(1), m.group(2), m.group(3)
        if self_ref:
            if class_fields is not None and name not in class_fields:
                self.error(where, f"функция '{fn}': у класса нет поля '{name}'")
            return
        if name not in scope and name not in self.globals and name not in ENGINE_VARS:
            self.error(where, f"функция '{fn}': присваивание необъявленной "
                              f"переменной '{name}'")
            return
        if field:
            holder = self.globals.get(name) or self._scope_type(scope, name)
            fields = self.objects.get(holder)
            if fields is not None and field not in fields:
                self.error(where, f"функция '{fn}': у объекта '{holder}' нет поля '{field}'")

    def _scope_type(self, scope, name):
        return None  # типы локальных линтер не выводит: это работа компилятора

    def check_expression(self, where, fn, expr, scope, class_fields):
        for m in re.finditer(r'\$"(?:[^"\\]|\\.)*"', expr):
            for kind, val in interp_holes(m.group(0)):
                if kind == 'hole':
                    self.check_expression(where, fn, val, scope, class_fields)
        masked = re.sub(r'\$"(?:[^"\\]|\\.)*"', '""', expr)
        for ident, _pos in identifiers(masked):
            if ident in scope or self.known(ident):
                continue
            self.error(where, f"функция '{fn}': неизвестное имя '{ident}' в выражении")
        depth, quoted = _scan(masked)
        for m in re.finditer(r'(?<![.\w])(' + _NAME + r')\s*\(', masked):
            if quoted[m.start()]:
                continue
            self.check_call(where, fn, m.group(1), args_of(masked, m.end()), scope,
                            class_fields)
        for m in re.finditer(r'\b(' + _NAME + r')\.(' + _NAME + r')\b', masked):
            if quoted[m.start()]:
                continue
            holder = m.group(1)
            if holder == 'self':
                if class_fields is not None and m.group(2) not in class_fields:
                    self.error(where, f"функция '{fn}': у класса нет поля '{m.group(2)}'")
                continue
            if holder in scope or holder in self.globals:
                fields = self.objects.get(self.globals.get(holder) or '', set())
                if fields and m.group(2) not in fields:
                    self.error(where, f"функция '{fn}': у объекта '{holder}' нет поля '{m.group(2)}'")

    def check_call(self, where, fn, name, args_text, scope, class_fields=True):
        if name in self.functions:
            args = split_top(args_text, ',') if args_text.strip() else []
            want = len(self.functions[name])
            if len(args) != want:
                self.error(where, f"функция '{fn}': вызов '{name}' ждёт {want} "
                                  f"аргумент(а), передано {len(args)}")
        elif name not in BUILTINS and name not in _RAW_C_WORDS and name not in NATIVE_MATH:
            self.error(where, f"функция '{fn}': неизвестный вызов '{name}'")
        if args_text.strip():
            for arg in split_top(args_text, ','):
                self.check_expression(where, fn, arg, scope,
                                      class_fields if isinstance(class_fields, set) else None)


def _no_then(cond):
    cond = cond.strip()
    for suf in (' then', ' do'):
        if cond.endswith(suf):
            return cond[:-len(suf)].strip()
    return cond


def _dotted_args(line):
    open_at = line.find('(')
    if open_at < 0:
        return []
    return split_top(args_of(line, open_at + 1), ',')


def find_assign(line):
    depth, quoted = _scan(line)
    for i, c in enumerate(line):
        if quoted[i] or depth[i]:
            continue
        if c == '=' and (i == 0 or line[i - 1] not in '<>!') and line[i + 1:i + 2] != '=':
            return i
    return -1


def args_of(expr, open_paren):
    depth = 0
    for i in range(open_paren - 1, len(expr)):
        if expr[i] == '(':
            depth += 1
        elif expr[i] == ')':
            depth -= 1
            if depth == 0:
                return expr[open_paren:i]
    return expr[open_paren:]


def lint_dir(scripts):
    """Проверяет каталог со скриптами, возвращает список строк-ошибок."""
    sources = find_ds_files(scripts)
    modules = {}
    unfinished = []
    for path in sources:
        lines = []
        pending = ''
        with open(path, encoding='utf-8-sig') as fh:
            for raw in fh:
                line = strip_comment(raw).strip()
                if not line:
                    continue
                if pending:
                    line = pending + ' ' + line
                if open_parens(line) > 0:
                    pending = line
                    continue
                pending = ''
                lines.extend(q for q in (p.strip() for p in split_top(line, ';')) if q)
        if pending:
            unfinished.append(f"{path}: незакрытая скобка: {pending}")
        modules[path] = lines

    lint = Lint()
    for lines in modules.values():
        lint.scan_classes(lines)
    for lines in modules.values():
        lint.scan_declarations(lines, '')
    for path, lines in modules.items():
        for kind, cls, name, params, body in iter_blocks(lines):
            if name in lint.functions:
                lint.error(path, f"повторное объявление функции '{name}'")
            lint.functions[name] = params
    for path, lines in modules.items():
        for kind, cls, name, params, body in iter_blocks(lines):
            fields = lint.objects.get(cls) if cls else None
            lint.check_function(path, name, params, body, fields)

    return unfinished + lint.errors


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'game'
    scripts = os.path.join(root, 'scripts')
    if not os.path.isdir(scripts):
        scripts = root
    errors = lint_dir(scripts)
    if errors:
        print(f'ds_lint: {len(errors)} problem(s)')
        for e in errors:
            print(' -', e)
        return 1
    print('ds_lint: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
