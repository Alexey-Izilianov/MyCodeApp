"""Генератор тестовых файлов MyCodeApp.

    python samples/generate.py           # small (особые кодировки) + medium + large
    python samples/generate.py --huge    # плюс large/huge.log на 1 ГБ (цель ТЗ)

Содержимое детерминировано (фиксированный seed): повторный запуск даёт те же
файлы. medium/ и large/ в git не хранятся — только этот скрипт.
"""
import argparse
import json
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MB = 1024 * 1024

WORDS = ("alpha beta gamma delta value index count buffer cursor render layout "
         "widget stream parser token scope frame vector matrix signal handler").split()


def write_until(path: Path, size: int, make_chunk, encoding="utf-8", newline="\n"):
    """Дописывает куски make_chunk(i) в файл, пока он не дорастёт до size байт."""
    path.parent.mkdir(parents=True, exist_ok=True)
    written = 0
    with path.open("w", encoding=encoding, newline=newline) as f:
        i = 0
        while written < size:
            chunk = make_chunk(i)
            f.write(chunk)
            written += len(chunk.encode(encoding))
            i += 1
    print(f"{path.relative_to(ROOT)}: {path.stat().st_size / MB:.1f} МБ")


def name(rng: random.Random) -> str:
    return rng.choice(WORDS) + rng.choice(WORDS).capitalize()


# ── small: файлы, которые нельзя положить в git «как текст» ──────────────

def small_special():
    small = ROOT / "small"
    (small / "tabs.c").write_text(
        "/* Отступы табуляцией: колонки, клики и стрелки — по табстопам */\n"
        "#include <stdio.h>\n\n"
        "int main(void)\n{\n"
        "\tint values[3] = {1, 2, 3};\t/* комментарий после таба */\n"
        "\tfor (int i = 0; i < 3; ++i) {\n"
        "\t\tif (values[i] % 2)\n"
        "\t\t\tprintf(\"%d\\n\", values[i]);\n"
        "\t}\n"
        "    return 0; /* эта строка — пробелами, должна стоять ровно под табом */\n"
        "}\n", encoding="utf-8", newline="\n")
    (small / "crlf_windows.txt").write_text(
        "Файл с переводами строк CRLF (Windows).\n"
        "В статус-баре должно быть CRLF, после сохранения — тоже CRLF.\n"
        "Третья строка.\n", encoding="utf-8", newline="\r\n")
    (small / "cp1251.txt").write_text(
        "Файл в кодировке Windows-1251.\n"
        "В статус-баре — Windows-1251, русский текст читается без «кракозябр».\n"
        "Съешь же ещё этих мягких французских булок, да выпей чаю.\n", encoding="cp1251", newline="\n")
    (small / "utf16le.txt").write_bytes(
        b"\xff\xfe" + "Файл UTF-16 LE с BOM.\nВ статус-баре — UTF-16 LE.\n".encode("utf-16-le"))
    print("small: tabs.c, crlf_windows.txt, cp1251.txt, utf16le.txt")


# ── генераторы кода ──────────────────────────────────────────────────────

def cpp_chunk(rng: random.Random, i: int) -> str:
    cls, fn, field = f"{name(rng)}{i}", name(rng), rng.choice(WORDS)
    return f"""
/* {cls}: блок #{i}, сгенерирован для проверки подсветки и прокрутки */
#define {cls.upper()}_LIMIT {rng.randint(1, 9999)}

template <typename T>
class {cls} {{
public:
    explicit {cls}(T init) : m_{field}(init) {{}}

    T {fn}(const std::vector<T> &items) const
    {{
        T total{{}};
        for (std::size_t i = 0; i < items.size(); ++i) {{
            if (items[i] > m_{field} && i % {rng.randint(2, 7)} == 0)
                total += items[i] * {rng.random():.4f};
            else
                total -= static_cast<T>({rng.randint(0, 255):#x});
        }}
        return total; // {rng.choice(WORDS)} {rng.choice(WORDS)}
    }}

private:
    T m_{field};
    const char *m_label = "{cls} \\"{rng.choice(WORDS)}\\"\\n";
}};
"""


def py_chunk(rng: random.Random, i: int) -> str:
    cls, fn = f"{name(rng).capitalize()}{i}", f"{rng.choice(WORDS)}_{i}"
    return f'''

class {cls}:
    """Блок #{i}: {rng.choice(WORDS)} {rng.choice(WORDS)}."""

    def __init__(self, {rng.choice(WORDS)}=None):
        self.items = [x ** 2 for x in range({rng.randint(3, 50)}) if x % 3]
        self.label = f"{cls}-{{len(self.items)}}"  # комментарий

    @property
    def {fn}(self) -> float:
        total = 0.0
        for index, item in enumerate(self.items):
            if item > {rng.randint(1, 100)} and index % 2:
                total += item * {rng.random():.3f}
            elif item == 0:
                continue
        return total
'''


def qml_chunk(rng: random.Random, i: int) -> str:
    return f"""
        Rectangle {{
            id: item{i}
            property int {rng.choice(WORDS)}Count: {rng.randint(0, 500)}
            width: parent.width / {rng.randint(2, 8)}
            height: {rng.randint(20, 120)}
            radius: {rng.randint(0, 12)}
            color: index % 2 ? "#{rng.randint(0, 0xFFFFFF):06x}" : "transparent"
            Text {{
                anchors.centerIn: parent
                text: qsTr("{rng.choice(WORDS)} %1").arg(item{i}.height)
            }}
            function {rng.choice(WORDS)}{i}(step) {{ return height * step + {rng.randint(1, 9)} }}
        }}
"""


def md_chunk(rng: random.Random, i: int) -> str:
    return (f"\n## Раздел {i}: {rng.choice(WORDS)} {rng.choice(WORDS)}\n\n"
            f"Текст про **{rng.choice(WORDS)}** и `{name(rng)}()`: "
            + " ".join(rng.choice(WORDS) for _ in range(rng.randint(20, 60)))
            + f"\n\n- пункт __{rng.choice(WORDS)}__\n- пункт `{rng.choice(WORDS)}`\n")


def log_chunk(rng: random.Random, i: int) -> str:
    level = rng.choice(("INFO", "INFO", "INFO", "DEBUG", "WARN", "ERROR"))
    return (f"2026-09-29 {i // 3600 % 24:02d}:{i // 60 % 60:02d}:{i % 60:02d}.{i % 1000:03d} "
            f"[{level:5}] {rng.choice(WORDS)}.{rng.choice(WORDS)}: {name(rng)} "
            f"id={rng.randint(1, 10**9)} took={rng.random() * 500:.2f}ms\n")


def json_records(rng: random.Random, size: int):
    """Массив объектов нужного размера, одной строкой на запись."""
    lines, total, i = [], 0, 0
    while total < size:
        rec = json.dumps({"id": i, "name": name(rng), "active": rng.random() > 0.5,
                          "score": round(rng.random() * 100, 3),
                          "tags": [rng.choice(WORDS) for _ in range(3)], "parent": None},
                         ensure_ascii=False)
        lines.append("    " + rec)
        total += len(rec) + 6
        i += 1
    return "[\n" + ",\n".join(lines) + "\n]\n"


def generate(folder: str, sizes: dict, seed: int):
    out = ROOT / folder
    rng = random.Random(seed)
    header = {
        "cpp": "#include <cstddef>\n#include <vector>\n\nnamespace samples {\n",
        "py": '"""Сгенерированный Python-файл."""\nimport math\n',
        "qml": "import QtQuick\n\nItem {\n    id: root\n    Column {\n",
        "md": f"# Сгенерированный Markdown ({folder})\n",
    }
    footer = {"cpp": "\n} // namespace samples\n", "qml": "    }\n}\n"}
    makers = {"cpp": cpp_chunk, "py": py_chunk, "qml": qml_chunk, "md": md_chunk}

    for filename, size in sizes.items():
        ext = filename.rsplit(".", 1)[1]
        path = out / filename
        if filename.startswith("long_line"):
            # Одна строка на много мегабайт: горизонтальная прокрутка, раскладка окна колонок
            write_until(path, size, lambda i: f'{{"k{i}": "{rng.choice(WORDS)}", "n": {i}}}, ')
        elif ext == "json":
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json_records(rng, size), encoding="utf-8", newline="\n")
            print(f"{path.relative_to(ROOT)}: {path.stat().st_size / MB:.1f} МБ")
        elif ext == "log":
            write_until(path, size, lambda i: log_chunk(rng, i))
        else:
            maker = makers[ext]
            write_until(path, size - len(footer.get(ext, "")),
                        lambda i: header.get(ext, "") if i == 0 else maker(rng, i))
            with path.open("a", encoding="utf-8", newline="\n") as f:
                f.write(footer.get(ext, ""))


def main():
    sys.stdout.reconfigure(encoding="utf-8")  # консоль Windows по умолчанию не UTF-8
    parser = argparse.ArgumentParser()
    parser.add_argument("--huge", action="store_true", help="large/huge.log на 1 ГБ")
    args = parser.parse_args()

    small_special()
    generate("medium", {
        "engine.cpp": 1 * MB, "analysis.py": 1 * MB, "Dashboard.qml": 512 * 1024,
        "dataset.json": 1 * MB, "manual.md": 512 * 1024, "server.log": 5 * MB,
    }, seed=1)
    generate("large", {
        "near_limit.cpp": int(7.5 * MB),  # чуть меньше предела tree-sitter (8 млн символов)
        "over_limit.cpp": 20 * MB,        # больше предела — подсветка регэкспами
        "big.py": 20 * MB,
        "data.json": 50 * MB,
        "app.log": 150 * MB,              # фоновая загрузка с прогрессом (> 20 МБ)
        "long_line.json": 10 * MB,        # одна строка
    }, seed=2)
    if args.huge:
        generate("large", {"huge.log": 1024 * MB}, seed=3)


if __name__ == "__main__":
    main()
