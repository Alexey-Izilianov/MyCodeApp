#pragma once

#include <QString>
#include <QVector>
#include <functional>
#include <optional>

namespace core::snippet {

// Синтаксис сниппетов VS Code/LSP: $1, ${1}, ${1:заглушка}, ${1|a,b,c|}, $0 —
// финальная позиция, $ИМЯ и ${ИМЯ:запасной текст} — переменные; \$ \} \\ — экранирование.
// Повтор того же номера — зеркало: тот же текст, правится одновременно.

struct Range {
    int start = 0; // смещение в развёрнутом тексте, QChar
    int length = 0;
};

struct TabStop {
    int number = 0;
    QVector<Range> ranges; // по порядку в тексте
};

struct Expansion {
    QString text;
    QVector<TabStop> stops; // порядок обхода: 1, 2, ..., затем 0 (всегда есть)
};

// Значение переменной; пустой optional — неизвестная, подставляется запасной текст
using Variables = std::function<std::optional<QString>(const QString &name)>;

Expansion expand(const QString &body, const Variables &variables = {});

} // namespace core::snippet
