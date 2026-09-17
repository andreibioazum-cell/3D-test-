#!/usr/bin/env python3
"""Сборка GLSL-шейдеров Cubic Battle в SPIR-V и запекание их в C-заголовок
native/graphics/shaders_spirv.inc (массивы uint32_t, которые Vulkan-бэкенд
кормит vkCreateShaderModule напрямую).

Нужен glslangValidator (переменная окружения GLSLANG_VALIDATOR или в PATH).
Сгенерированный заголовок коммитится в репозиторий, поэтому CI и обычная
сборка НДК не требуют glslang: перегенерация нужна только при правке
tools/shaders/*.vert|frag.

Запуск:  python3 tools/shaders/build_shaders.py
"""

import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "native", "graphics", "shaders_spirv.inc")

SHADERS = [
    ("sprite_vert", "sprite.vert"),
    ("solid_frag", "solid.frag"),
    ("image_frag", "image.frag"),
    ("tint_frag", "tint.frag"),
]


def find_glslang() -> str:
    env = os.environ.get("GLSLANG_VALIDATOR")
    if env and os.path.isfile(env):
        return env
    found = shutil.which("glslangValidator")
    if found:
        return found
    sys.exit("glslangValidator не найден: задайте GLSLANG_VALIDATOR=путь/до/binary")


def compile_shader(glslang: str, name: str, fname: str) -> bytes:
    stage = "vert" if fname.endswith(".vert") else "frag"
    src = os.path.join(HERE, fname)
    spv = os.path.join(HERE, fname + ".spv")
    cmd = [glslang, "-V", "--target-env", "vulkan1.0", "-S", stage, "-o", spv, src]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.exit(f"Ошибка компиляции {fname}:\n{res.stdout}\n{res.stderr}")
    if res.stderr.strip():
        print(f"  предупреждение {fname}: {res.stderr.strip()}")
    with open(spv, "rb") as f:
        data = f.read()
    os.remove(spv)
    if len(data) % 4 != 0 or len(data) < 20:
        sys.exit(f"{name}: SPIR-V повреждён ({len(data)} байт)")
    if data[:4] != b"\x03\x02\x23\x07":
        sys.exit(f"{name}: нет магии SPIR-V")
    return data


def main() -> None:
    glslang = find_glslang()
    print(f"glslangValidator: {glslang}")
    blobs = []
    for name, fname in SHADERS:
        data = compile_shader(glslang, name, fname)
        words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
        blobs.append((name, words))
        print(f"  {fname}: {len(words)} слов SPIR-V")
    lines = [
        "/* Автогенератор: tools/shaders/build_shaders.py (glslangValidator,",
        " * SPIR-V 1.0 для Vulkan 1.0). Не правьте руками: правьте GLSL в",
        " * tools/shaders и запустите генератор заново. */",
        "#ifndef DS_SHADERS_SPIRV_INC",
        "#define DS_SHADERS_SPIRV_INC",
        "",
    ]
    for name, words in blobs:
        lines.append(f"static const uint32_t ds_spirv_{name}[] = {{")
        for i in range(0, len(words), 6):
            row = ", ".join(f"0x{w:08x}u" for w in words[i:i + 6])
            lines.append(f"    {row},")
        lines.append("};")
        lines.append("")
    lines.append("#endif /* DS_SHADERS_SPIRV_INC */")
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"OK -> {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
