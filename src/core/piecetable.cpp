#include "piecetable.h"

#include <algorithm>

namespace core {

int PieceTable::Core::pieceFor(int offset) const
{
    // Первый кусок, чей конец > offset (offsets[0] == 0 при непустом тексте).
    // offset == length() даёт pieces.size() — метку «за концом» для вставки.
    const auto it = std::upper_bound(offsets.cbegin() + 1, offsets.cend(), offset);
    return int(it - offsets.cbegin()) - 1;
}

QString PieceTable::Core::textAt(int offset, int count) const
{
    if (offset < 0 || count <= 0)
        return {};
    count = qMin(count, length() - offset);
    if (count <= 0)
        return {};

    QString out(count, QChar());
    QChar *dst = out.data();
    int written = 0;
    for (int i = pieceFor(offset); i < pieces.size() && written < count; ++i) {
        const Piece &p = pieces.at(i);
        const int local = qMax(0, offset + written - offsets.at(i));
        const int take = qMin(p.length - local, count - written);
        std::memcpy(dst + written,
                    bufferOf(p).constData() + p.start + local,
                    size_t(take) * sizeof(QChar));
        written += take;
    }
    return out;
}

QString PieceTable::Core::toString() const
{
    if (pieces.isEmpty())
        return {};
    QString out(length(), QChar());
    QChar *dst = out.data();
    int written = 0;
    for (const Piece &p : pieces) {
        std::memcpy(dst + written,
                    bufferOf(p).constData() + p.start,
                    size_t(p.length) * sizeof(QChar));
        written += p.length;
    }
    return out;
}

int PieceTable::Core::lineStartOffset(int line) const
{
    return line == 0 ? 0 : newlines.at(line - 1) + 1;
}

int PieceTable::Core::lineLength(int line) const
{
    const int end = line == newlines.size() ? length() : newlines.at(line);
    return end - lineStartOffset(line);
}

QString PieceTable::Core::line(int line) const
{
    return textAt(lineStartOffset(line), lineLength(line));
}

int PieceTable::Core::offsetToLine(int offset) const
{
    // Число '\n' строго раньше offset (позиция на самом '\n' — конец строки)
    const auto it = std::upper_bound(newlines.cbegin(), newlines.cend(), offset);
    return int(it - newlines.cbegin());
}

QStringList PieceTable::Snapshot::lines() const
{
    QStringList out;
    out.reserve(m_core.lineCount());
    for (int i = 0; i < m_core.lineCount(); ++i)
        out.append(m_core.line(i));
    return out;
}

void PieceTable::reset(const QString &originalText)
{
    m_core.original = originalText;
    m_core.added.clear();
    m_core.pieces.clear();
    m_core.offsets = {0};
    m_core.newlines.clear();

    if (originalText.isEmpty())
        return;

    m_core.pieces.append(Piece{0, 0, originalText.size()});
    m_core.offsets.append(originalText.size());

    // Смещения всех '\n': QString::indexOf заметно быстрее посимвольного
    // прохода по ~100 МБ (simd-поиск вместо цикла).
    int from = 0;
    while (true) {
        const int idx = m_core.original.indexOf(u'\n', from);
        if (idx < 0)
            break;
        m_core.newlines.append(idx);
        from = idx + 1;
    }
}

void PieceTable::rebuildOffsets()
{
    // Пересчёт всех начал кусков: кусков на практике тысячи-десятки тысяч,
    // это дешевле и надёжнее, чем инкрементальное сдвигание префиксных сумм.
    m_core.offsets.resize(m_core.pieces.size() + 1);
    int *offsets = m_core.offsets.data();
    int acc = 0;
    for (int i = 0; i < m_core.pieces.size(); ++i) {
        offsets[i] = acc;
        acc += m_core.pieces.at(i).length;
    }
    offsets[m_core.pieces.size()] = acc;
}

void PieceTable::insert(int offset, const QString &text)
{
    if (text.isEmpty())
        return;
    offset = qBound(0, offset, m_core.length());

    // Вставленный текст — новый буфер добавок; существующие куски не трогаем,
    // при необходимости разрезаем кусок, накрывший точку вставки.
    m_core.added.append(text);
    const Piece piece{int(m_core.added.size()), 0, text.size()};

    const int i = m_core.pieceFor(offset);
    if (i == m_core.pieces.size()) {
        m_core.pieces.append(piece);
    } else {
        const int local = offset - m_core.offsets.at(i);
        if (local == 0) {
            m_core.pieces.insert(i, piece);
        } else {
            Piece tail = m_core.pieces.at(i);
            tail.start += local;
            tail.length -= local;
            m_core.pieces[i].length = local;
            m_core.pieces.insert(i + 1, piece);
            m_core.pieces.insert(i + 2, tail);
        }
    }
    rebuildOffsets();

    // Line index: '\n' правее точки вставки сдвигаются на длину вставленного,
    // вставленные '\n' занимают offset + позиция внутри вставленного текста.
    QVector<int> inserted;
    int p = text.indexOf(u'\n');
    while (p >= 0) {
        inserted.append(offset + p);
        p = text.indexOf(u'\n', p + 1);
    }
    const int idx = int(std::lower_bound(m_core.newlines.cbegin(),
                                         m_core.newlines.cend(), offset)
                        - m_core.newlines.cbegin());
    const int oldCount = m_core.newlines.size();
    const int tail = oldCount - idx;
    m_core.newlines.resize(oldCount + inserted.size());
    int *nl = m_core.newlines.data();
    std::move_backward(nl + idx, nl + idx + tail, nl + m_core.newlines.size());
    // Хвост после move_backward уже на правильных местах — прибавляем сдвиг
    for (int j = idx + inserted.size(); j < m_core.newlines.size(); ++j)
        nl[j] += text.size();
    for (int k = 0; k < inserted.size(); ++k)
        nl[idx + k] = inserted.at(k);
}

void PieceTable::remove(int offset, int count)
{
    if (count <= 0 || offset < 0 || offset >= m_core.length())
        return;
    count = qMin(count, m_core.length() - offset);
    const int end = offset + count;

    // Идём по кускам в старых offsets (они выровнены с pieces): полностью
    // покрытые выбрасываем, крайние подрезаем, кусок с удалением внутри —
    // режем. Смещения кусков пересчитываются один раз в конце.
    int i = m_core.pieceFor(offset);
    while (i < m_core.pieces.size() && m_core.offsets.at(i) < end) {
        const Piece p = m_core.pieces.at(i);
        const int pStart = m_core.offsets.at(i);
        const int pEnd = pStart + p.length;
        const int keepHead = qBound(0, offset - pStart, p.length);
        const int keepTail = qBound(0, pEnd - end, p.length);

        if (keepHead > 0 && keepTail > 0) {
            Piece tail = p;
            tail.start += p.length - keepTail; // голова + выброшенная середина
            tail.length = keepTail;
            m_core.pieces[i].length = keepHead;
            m_core.pieces.insert(i + 1, tail);
            ++i;
        } else if (keepHead > 0) {
            m_core.pieces[i].length = keepHead;
            ++i;
        } else if (keepTail > 0) {
            Piece trimmed = p;
            trimmed.start += p.length - keepTail;
            trimmed.length = keepTail;
            m_core.pieces[i] = trimmed;
            ++i;
        } else {
            m_core.pieces.removeAt(i);
            m_core.offsets.removeAt(i); // выравнивание пары pieces/offsets
        }
    }
    rebuildOffsets();

    // Line index: '\n' внутри диапазона пропадают, правее — сдвигаются влево.
    const int lo = int(std::lower_bound(m_core.newlines.cbegin(),
                                        m_core.newlines.cend(), offset)
                       - m_core.newlines.cbegin());
    const int hi = int(std::lower_bound(m_core.newlines.cbegin(),
                                        m_core.newlines.cend(), end)
                       - m_core.newlines.cbegin());
    m_core.newlines.erase(m_core.newlines.begin() + lo,
                          m_core.newlines.begin() + hi);
    int *nl = m_core.newlines.data();
    for (int j = lo; j < m_core.newlines.size(); ++j)
        nl[j] -= count;
}

void PieceTable::verify() const
{
    Q_ASSERT(m_core.offsets.size() == m_core.pieces.size() + 1);
    for (const Piece &p : m_core.pieces)
        Q_ASSERT(p.length > 0);
    for (int i = 1; i < m_core.offsets.size(); ++i)
        Q_ASSERT(m_core.offsets.at(i) >= m_core.offsets.at(i - 1));
    Q_ASSERT(m_core.offsets.isEmpty() || m_core.offsets.constLast() == m_core.length());

    // Полная сверка line index с текстом — дорого, только для тестов
    const QString text = m_core.toString();
    Q_ASSERT(text.size() == m_core.length());
    QVector<int> actual;
    for (int i = 0; i < text.size(); ++i)
        if (text.at(i) == u'\n')
            actual.append(i);
    Q_ASSERT(actual == m_core.newlines);
}

} // namespace core