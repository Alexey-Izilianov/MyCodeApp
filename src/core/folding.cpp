#include "folding.h"

#include "editing.h"

namespace core::folding {

namespace {

constexpr int kIndentLookahead = 100; // сколько пустых строк пропускать в canFold

// Последняя незакрытая в строке открывающая скобка; строки в кавычках и
// комментарий `//` пропускаются
int openerColumn(const QString &line)
{
    QVector<int> stack;
    QChar quote;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (!quote.isNull()) {
            if (c == u'\\')
                ++i;
            else if (c == quote)
                quote = QChar();
        } else if (editing::isQuote(c)) {
            quote = c;
        } else if (c == u'/' && i + 1 < line.size() && line.at(i + 1) == u'/') {
            break;
        } else if (editing::isOpeningBracket(c)) {
            stack.append(i);
        } else if (editing::isClosingBracket(c) && !stack.isEmpty()) {
            stack.removeLast();
        }
    }
    return stack.isEmpty() ? -1 : stack.last();
}

int indentOf(const QString &line) // -1 — пустая строка
{
    int n = 0;
    while (n < line.size() && (line.at(n) == u' ' || line.at(n) == u'\t'))
        ++n;
    return n == line.size() ? -1 : editing::visualColumn(line, n);
}

} // namespace

bool canFold(const TextBuffer &buffer, int line)
{
    if (line < 0 || line + 1 >= buffer.lineCount())
        return false;
    const QString text = buffer.lineAt(line);
    const int opener = openerColumn(text);
    if (opener >= 0) {
        // Пара на следующей строке — сворачивать нечего
        const auto match = editing::matchingBracket(buffer, {line, opener}, 1);
        return !match || match->line > line + 1;
    }
    const int base = indentOf(text);
    if (base < 0)
        return false;
    for (int l = line + 1; l < qMin(buffer.lineCount(), line + kIndentLookahead); ++l) {
        const int indent = indentOf(buffer.lineAt(l));
        if (indent >= 0)
            return indent > base;
    }
    return false;
}

std::optional<int> foldEnd(const TextBuffer &buffer, int line)
{
    if (line < 0 || line + 1 >= buffer.lineCount())
        return std::nullopt;
    const QString text = buffer.lineAt(line);
    const int opener = openerColumn(text);
    if (opener >= 0) {
        if (const auto match = editing::matchingBracket(buffer, {line, opener}, buffer.lineCount())) {
            if (match->line - 1 > line)
                return match->line - 1;
            return std::nullopt;
        }
    }
    const int base = indentOf(text);
    if (base < 0)
        return std::nullopt;
    int last = line;
    for (int l = line + 1; l < buffer.lineCount(); ++l) {
        const int indent = indentOf(buffer.lineAt(l));
        if (indent < 0)
            continue;
        if (indent <= base)
            break;
        last = l;
    }
    return last > line ? std::optional<int>(last) : std::nullopt;
}

// ── FoldSet ─────────────────────────────────────────────────────────────

void FoldSet::add(Fold fold)
{
    if (fold.last <= fold.line)
        return;
    auto it = std::lower_bound(m_folds.begin(), m_folds.end(), fold.line,
                               [](const Fold &f, int line) { return f.line < line; });
    if (it != m_folds.end() && it->line == fold.line)
        *it = fold;
    else
        m_folds.insert(it, fold);
    rebuild();
}

void FoldSet::assign(QVector<Fold> folds)
{
    m_folds = std::move(folds);
    rebuild();
}

bool FoldSet::remove(int line)
{
    const auto it = std::find_if(m_folds.begin(), m_folds.end(),
                                 [line](const Fold &f) { return f.line == line; });
    if (it == m_folds.end())
        return false;
    m_folds.erase(it);
    rebuild();
    return true;
}

void FoldSet::clear()
{
    m_folds.clear();
    rebuild();
}

bool FoldSet::isFolded(int line) const
{
    return std::any_of(m_folds.cbegin(), m_folds.cend(),
                       [line](const Fold &f) { return f.line == line; })
           && !isHidden(line);
}

bool FoldSet::isHidden(int line) const
{
    const int i = rangeBefore(line);
    return i >= 0 && line <= m_hidden.at(i).last;
}

void FoldSet::reveal(int line)
{
    const auto end = std::remove_if(m_folds.begin(), m_folds.end(),
                                    [line](const Fold &f) { return f.line < line && line <= f.last; });
    if (end == m_folds.end())
        return;
    m_folds.erase(end, m_folds.end());
    rebuild();
}

int FoldSet::rowCount(int lineCount) const
{
    return lineCount - (m_hiddenBefore.isEmpty() ? 0 : m_hiddenBefore.last());
}

int FoldSet::rowOf(int line) const
{
    const int i = rangeBefore(line);
    if (i < 0)
        return line;
    const Range &r = m_hidden.at(i);
    if (line <= r.last) // скрыта — ряд заголовка
        return r.first - 1 - m_hiddenBefore.at(i);
    return line - m_hiddenBefore.at(i + 1);
}

int FoldSet::lineAt(int row) const
{
    // Диапазон i лежит выше ряда row, если его заголовок выше: first - 1 - скрыто_до < row
    int lo = 0, hi = int(m_hidden.size());
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (m_hidden.at(mid).first - 1 - m_hiddenBefore.at(mid) < row)
            lo = mid + 1;
        else
            hi = mid;
    }
    return row + (lo > 0 ? m_hiddenBefore.at(lo) : 0);
}

void FoldSet::applyEdit(const TextBuffer::Edit &edit)
{
    const int start = edit.start.line, oldEnd = edit.oldEnd.line;
    const int delta = edit.newEnd.line - oldEnd;
    if (start == oldEnd && delta == 0)
        return; // правка внутри строки номеров не меняет

    QVector<Fold> kept;
    for (Fold f : std::as_const(m_folds)) {
        if (start > f.last) {
            kept.append(f);
        } else if (oldEnd < f.line
                   || (oldEnd == f.line && (start < f.line || edit.oldEnd.column == 0))) {
            // Правка выше блока, перед текстом заголовка или подтянула его к строке выше — сдвиг
            f.line += delta;
            f.last += delta;
            kept.append(f);
        }
        // Иначе правка задела заголовок изнутри или скрытые строки — разворачиваем
    }
    m_folds = kept;
    rebuild();
}

void FoldSet::rebuild()
{
    m_hidden.clear();
    for (const Fold &f : std::as_const(m_folds)) { // m_folds отсортированы по line
        const Range r{f.line + 1, f.last};
        if (!m_hidden.isEmpty() && r.first <= m_hidden.last().last + 1)
            m_hidden.last().last = qMax(m_hidden.last().last, r.last);
        else
            m_hidden.append(r);
    }
    m_hiddenBefore.resize(m_hidden.size() + 1);
    int total = 0;
    for (int i = 0; i < m_hidden.size(); ++i) {
        m_hiddenBefore[i] = total;
        total += m_hidden.at(i).last - m_hidden.at(i).first + 1;
    }
    m_hiddenBefore.last() = total;
}

int FoldSet::rangeBefore(int line) const
{
    const auto it = std::upper_bound(m_hidden.cbegin(), m_hidden.cend(), line,
                                     [](int l, const Range &r) { return l < r.first; });
    return int(it - m_hidden.cbegin()) - 1;
}

} // namespace core::folding
