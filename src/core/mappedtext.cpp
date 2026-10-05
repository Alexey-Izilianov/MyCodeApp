#include "mappedtext.h"

#include <cstring>
#include <limits>

namespace core {

std::shared_ptr<const MappedText> MappedText::open(const QString &path, bool utf8,
                                                   const std::function<void(int)> &progress)
{
    std::shared_ptr<MappedText> t(new MappedText);
    t->m_file.setFileName(path);
    if (!t->m_file.open(QIODevice::ReadOnly) || t->m_file.size() == 0)
        return nullptr;
    t->m_size = t->m_file.size();
    t->m_data = t->m_file.map(0, t->m_size);
    if (!t->m_data)
        return nullptr;
    t->m_utf8 = utf8;
    const uchar *d = t->m_data;
    if (utf8 && t->m_size >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF)
        t->m_begin = 3;

    // Длина в QChar: каждый байт, кроме продолжений UTF-8; 4-байтовая
    // последовательность — суррогатная пара, ещё +1. '\r' перед '\n' не считается.
    constexpr qint64 kChunk = 16 << 20;
    qint64 pos = 0;
    t->m_checkpoints.append(t->m_begin);
    for (qint64 chunk = t->m_begin; chunk < t->m_size; chunk += kChunk) {
        const qint64 end = qMin(t->m_size, chunk + kChunk);
        for (qint64 i = chunk; i < end; ++i) {
            const uchar b = d[i];
            if (b == '\n') {
                if (i > t->m_begin && d[i - 1] == '\r') {
                    --pos;
                    t->m_crlf = true;
                }
                t->m_newlines.append(int(qMin<qint64>(pos, std::numeric_limits<int>::max())));
                ++pos;
                if (t->m_newlines.size() % kCheckpoint == 0)
                    t->m_checkpoints.append(i + 1);
            } else {
                pos += utf8 ? ((b & 0xC0) != 0x80) + (b >= 0xF0) : 1;
            }
        }
        if (progress)
            progress(int(end * 100 / t->m_size));
    }
    if (pos > std::numeric_limits<int>::max())
        return nullptr;
    t->m_length = int(pos);
    return t;
}

qint64 MappedText::lineByteStart(int line) const
{
    qint64 byte = m_checkpoints.at(line / kCheckpoint);
    for (int i = line / kCheckpoint * kCheckpoint; i < line; ++i)
        byte = lineByteEnd(byte) + 1;
    return byte;
}

qint64 MappedText::lineByteEnd(qint64 start) const
{
    const void *nl = std::memchr(m_data + start, '\n', size_t(m_size - start));
    return nl ? static_cast<const uchar *>(nl) - m_data : m_size;
}

QString MappedText::decode(qint64 from, qint64 to) const
{
    if (to < m_size && to > from && m_data[to - 1] == '\r')
        --to;
    const auto *bytes = reinterpret_cast<const char *>(m_data + from);
    return m_utf8 ? QString::fromUtf8(bytes, to - from) : QString::fromLatin1(bytes, to - from);
}

QString MappedText::text(int offset, int count) const
{
    if (offset < 0 || count <= 0 || offset >= m_length)
        return {};
    count = qMin(count, m_length - offset);
    const int line = int(std::lower_bound(m_newlines.cbegin(), m_newlines.cend(), offset)
                         - m_newlines.cbegin());
    const int skip = offset - (line == 0 ? 0 : m_newlines.at(line - 1) + 1);

    QString out;
    for (qint64 byte = lineByteStart(line); out.size() < skip + count;) {
        const qint64 end = lineByteEnd(byte);
        out += decode(byte, end);
        if (end >= m_size)
            break;
        out += u'\n';
        byte = end + 1;
    }
    return out.mid(skip, count);
}

void MappedText::forEachLine(const std::function<bool(int, const QString &)> &fn) const
{
    qint64 byte = m_begin;
    for (int line = 0; line <= m_newlines.size(); ++line) {
        const qint64 end = lineByteEnd(byte);
        if (!fn(line, decode(byte, end)))
            return;
        byte = end + 1;
    }
}

} // namespace core
