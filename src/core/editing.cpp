#include "editing.h"

#include <QHash>

namespace core::editing {

int visualColumn(const QString &line, int column)
{
    column = qMin(column, int(line.size()));
    if (!line.left(column).contains(u'\t'))
        return column;
    int visual = 0;
    for (int i = 0; i < column; ++i)
        visual = line.at(i) == u'\t' ? (visual / kTabWidth + 1) * kTabWidth : visual + 1;
    return visual;
}

int columnAtVisual(const QString &line, int visual)
{
    int x = 0;
    for (int i = 0; i < line.size(); ++i) {
        const int next = line.at(i) == u'\t' ? (x / kTabWidth + 1) * kTabWidth : x + 1;
        if (visual < next)
            return visual - x <= next - visual ? i : i + 1; // ближе к левому или правому краю символа
        x = next;
    }
    return int(line.size()) + qMax(0, visual - x);
}

QString expandTabs(const QString &line, QVector<int> *map)
{
    if (map)
        map->resize(line.size() + 1);
    if (!line.contains(u'\t')) {
        if (map)
            for (int i = 0; i <= line.size(); ++i)
                (*map)[i] = i;
        return line;
    }
    QString out;
    out.reserve(line.size() + 16);
    for (int i = 0; i < line.size(); ++i) {
        if (map)
            (*map)[i] = int(out.size());
        if (line.at(i) == u'\t')
            out.append(QString(kTabWidth - out.size() % kTabWidth, u' '));
        else
            out.append(line.at(i));
    }
    if (map)
        (*map)[line.size()] = int(out.size());
    return out;
}

IndentStyle detectIndent(const TextBuffer &buffer, int maxLines)
{
    int tabLines = 0, spaceLines = 0;
    QHash<int, int> widths; // шаг между соседними отступами -> сколько раз
    int previous = 0;
    for (int i = 0; i < qMin(maxLines, buffer.lineCount()); ++i) {
        const QString line = buffer.lineAt(i);
        if (line.trimmed().isEmpty())
            continue;
        if (line.startsWith(u'\t')) {
            ++tabLines;
            continue;
        }
        int spaces = 0;
        while (spaces < line.size() && line.at(spaces) == u' ')
            ++spaces;
        if (spaces > 0)
            ++spaceLines;
        const int step = qAbs(spaces - previous);
        if (step >= 2 && step <= 8)
            ++widths[step];
        previous = spaces;
    }
    IndentStyle style;
    style.tabs = tabLines > spaceLines;
    int best = 0;
    for (auto it = widths.cbegin(); it != widths.cend(); ++it)
        if (it.value() > best) {
            best = it.value();
            style.width = it.key();
        }
    return style;
}

QChar closingFor(QChar open)
{
    switch (open.unicode()) {
    case u'(': return u')';
    case u'[': return u']';
    case u'{': return u'}';
    case u'"': return u'"';
    case u'\'': return u'\'';
    default: return {};
    }
}

bool isOpeningBracket(QChar c)
{
    return c == u'(' || c == u'[' || c == u'{';
}

bool isClosingBracket(QChar c)
{
    return c == u')' || c == u']' || c == u'}';
}

bool isQuote(QChar c)
{
    return c == u'"' || c == u'\'';
}

std::optional<TextBuffer::Position> matchingBracket(const TextBuffer &buffer,
                                                    TextBuffer::Position at, int maxLines)
{
    if (at.line < 0 || at.line >= buffer.lineCount())
        return std::nullopt;
    QString line = buffer.lineAt(at.line);
    if (at.column < 0 || at.column >= line.size())
        return std::nullopt;
    const QChar self = line.at(at.column);
    const bool forward = isOpeningBracket(self);
    if (!forward && !isClosingBracket(self))
        return std::nullopt;
    const QChar other = forward ? closingFor(self)
                        : self == u')' ? u'(' : self == u']' ? u'[' : u'{';

    int depth = 0;
    int column = at.column;
    for (int l = at.line; l >= 0 && l < buffer.lineCount() && qAbs(l - at.line) <= maxLines;
         l += forward ? 1 : -1) {
        if (l != at.line) {
            line = buffer.lineAt(l);
            column = forward ? 0 : int(line.size()) - 1;
        }
        for (; column >= 0 && column < line.size(); column += forward ? 1 : -1) {
            const QChar c = line.at(column);
            if (c == self)
                ++depth;
            else if (c == other && --depth == 0)
                return TextBuffer::Position{l, column};
        }
    }
    return std::nullopt;
}

} // namespace core::editing
