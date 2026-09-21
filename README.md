# MyCodeApp

![status](https://img.shields.io/badge/status-%D0%B2%20%D1%80%D0%B0%D0%B7%D1%80%D0%B0%D0%B1%D0%BE%D1%82%D0%BA%D0%B5-orange)
![Qt](https://img.shields.io/badge/Qt-6.11-green)
![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![build](https://img.shields.io/badge/CMake-3.20%2B-informational)

**MyCodeApp** — редактор кода (VS Code-lite) на C++20 и Qt 6 / QML.
Целевые ориентиры: большие файлы до 1 ГБ (piece table + mmap), multi-cursor,
LSP (clangd / pyright / rust-analyzer), интеграция Git (libgit2), плагины и темы.
GUI полностью на QML; рендер редактора — на QSG-нодах (пул QSGTextNode
и GPU-атлас глифов).

> Проект в начале пути: сейчас реализован рендер-прототип редактора,
> MVP (M1) в работе. Скриншот появится после появления первой рабочей сборки.

<!-- ![Скриншот](docs/screenshot.png) -->

---

## Содержание

- [Статус](#статус-status)
- [Стек](#стек-tech-stack)
- [Ключевые решения (этап 0)](#ключевые-решения-этап-0)
- [Сборка](#сборка-build)
- [План работ / Roadmap](#план-работ--roadmap)
- [Лицензия](#лицензия-license)

---

## Статус (Status)

**В разработке.** Этап 0 (архитектура и спайки рендера) завершён 2026-09-21,
работа идёт над M1 (MVP).

## Стек (Tech Stack)

| Компонент | Технология |
|---|---|
| Язык | C++20 |
| GUI | Qt 6.11 (QML / Qt Quick) |
| Сборка | CMake 3.20+ / Ninja |
| Рендер редактора | QSG-ноды (QSGTextNode, GPU-атлас глифов) |
| Планируется | tree-sitter (подсветка), libgit2 (Git), LSP-клиент (JSON-RPC) |

## Ключевые решения (этап 0)

Архитектурные развилки закрыты двумя замерами на файле 131 МБ / 1,16 млн строк:

| Спайк | Подход | Результат | Вердикт |
|---|---|---|---|
| №1 | `QQuickPaintedItem` (CPU-растеризация) | 13–23 FPS | Отклонён |
| №2 | QSG-ноды (пул `QSGTextNode` + GPU-атлас) | **60–61 FPS** | Принят |

Итог: GUI — полностью QML; глифы кэшируются в GPU-атласе Qt, при скролле
ноды не перерисовываются — двигается только матрица transform-ноды.
Наивный загрузчик уже открывает 131 МБ / 1,16 млн строк за ~380 мс.

## Сборка (Build)

### Windows

Требования: Qt 6.11.2 (MinGW 64-bit), CMake 3.20+, Ninja.

```bash
# конфигурация (путь к Qt подставить свой)
cmake -G Ninja -B build -S . ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64

# сборка
cmake --build build

# запуск (Qt DLL и плагины должны быть в PATH или рядом с exe)
build/appMyCodeApp.exe
```

Через Qt Creator проект открывается как обычный CMake-проект
(проверено: комплект Qt 6.11.2 MinGW 64-bit).

## План работ / Roadmap

Один этап = один майлстоун. Критерий завершения этапа: чистая сборка,
тесты ядра зелёные, коммит с тегом (`m1`, `m2`, ...).

- [x] **Этап 0 — архитектура и спайки рендера** (завершён 2026-09-21)
      GUI: полностью QML; рендер редактора: QSG-ноды — 60–61 FPS на 131 МБ.
      Текст-движок: собственный piece table.
- [ ] **M1 — MVP** (в работе)
      TextBuffer, кодировки (UTF-8/16, cp1251, Latin-1), line endings,
      EditorView (окно, скролл, ввод, один курсор, выделение), табы,
      подсветка по JSON-правилам (C++/Python/JSON/MD), поиск в файле,
      инфраструктура (spdlog, каталоги, каркас Qt Test, .clang-format).
- [ ] **M2 — ядро редактора**
      Piece table, mmap read-only, undo/redo без ограничения глубины,
      multi-cursor, auto-indent / auto-close скобок, folding, миникарта.
- [ ] **M3 — проект**
      Дерево файлов, workspace и сессия, file watcher,
      асинхронный поиск по проекту (.gitignore-aware), Ctrl+P fuzzy-поиск.
- [ ] **M4 — LSP**
      JSON-RPC клиент поверх stdio, автодополнение, goto definition /
      find references / hover, диагностика (панель «Проблемы»),
      конфигурация clangd / pyright / rust-analyzer.
- [ ] **M5 — Git**
      libgit2: изменённые строки в gutter, diff-view,
      панель Git (stage/unstage/commit, ветки, checkout), blame.
- [ ] **M6 — расширяемость**
      Plugin API (C++, QLibrary, JSON-манифест), сниппеты,
      темы (JSON + QSS, hot-reload), настройка горячих клавиш.
- [ ] **M7 — полировка**
      Покрытие ядра тестами > 70%, CI (GitHub Actions, clang-tidy,
      санитайзеры), локализация RU/EN, портативный режим, релиз 1.0.

## Лицензия (License)

Не определена. Пока все права сохранены за автором.