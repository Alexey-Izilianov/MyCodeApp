#pragma once

#include <QString>
#include <QVector>
#include <optional>

#include "textbuffer.h"

// Правила редактирования без состояния: визуальные колонки с учётом '\t',
// стиль отступов, парные скобки.
namespace core::editing {

constexpr int kTabWidth = 4;

// Колонка на экране (табуляция — до следующей кратной kTabWidth позиции)
int visualColumn(const QString &line, int column);
// Обратное: ближайшая колонка символа; правее конца строки — продолжает
// счёт за концом (нужно выделению столбцом)
int columnAtVisual(const QString &line, int visual);
// Строка с табуляциями, развёрнутыми в пробелы; map[i] — визуальная колонка символа i
QString expandTabs(const QString &line, QVector<int> *map = nullptr);

struct IndentStyle {
    bool tabs = false;
    int width = 4;
    QString unit() const { return tabs ? QStringLiteral("\t") : QString(width, u' '); }
};
IndentStyle detectIndent(const TextBuffer &buffer, int maxLines = 2000);

QChar closingFor(QChar open); // ( [ { " ' → ) ] } " ' ; иначе QChar()
bool isOpeningBracket(QChar c);
bool isClosingBracket(QChar c);
bool isQuote(QChar c);

// Парная скобка к той, что стоит в позиции at (наивный подсчёт глубины)
std::optional<TextBuffer::Position> matchingBracket(const TextBuffer &buffer,
                                                    TextBuffer::Position at,
                                                    int maxLines = 5000);

} // namespace core::editing
