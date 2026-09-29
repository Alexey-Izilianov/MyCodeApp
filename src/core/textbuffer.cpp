#include "textbuffer.h"

#include <QFile>
#include <QSaveFile>
#include <QStringDecoder>
#include <QStringEncoder>
#include <limits>
#include <utility>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace core {

namespace {
#ifdef Q_OS_WIN
// QStringConverter без ICU знает только UTF/latin1/system, поэтому cp1251
// делаем через WinAPI (кодировка 1251).
QString cp1251ToString(const QByteArray &raw)
{
    if (raw.isEmpty())
        return {};
    const int len = MultiByteToWideChar(1251, 0, raw.constData(), raw.size(),
                                        nullptr, 0);
    QString out(len, QChar());
    if (len > 0)
        MultiByteToWideChar(1251, 0, raw.constData(), raw.size(),
                            reinterpret_cast<wchar_t *>(out.data()), len);
    return out;
}

QByteArray stringToCp1251(const QString &s)
{
    if (s.isEmpty())
        return {};
    const int len = WideCharToMultiByte(1251, 0, reinterpret_cast<const wchar_t *>(s.constData()), s.size(),
                                        nullptr, 0, nullptr, nullptr);
    QByteArray out(len, 0);
    if (len > 0)
        WideCharToMultiByte(1251, 0, reinterpret_cast<const wchar_t *>(s.constData()), s.size(),
                            out.data(), len, nullptr, nullptr);
    return out;
}
#endif

// Ручная проверка: QStringDecoder::hasError() в этой конфигурации не
// срабатывает (invalid-байты молча заменяются U+FFFD), надёжнее посчитать сам.
bool isValidUtf8(const QByteArray &d)
{
    const int n = d.size();
    for (int i = 0; i < n; ) {
        const unsigned char c = d[i];
        if (c < 0x80) {
            ++i;
            continue;
        }
        int len = 0;
        if ((c & 0xE0) == 0xC0) len = 1;
        else if ((c & 0xF0) == 0xE0) len = 2;
        else if ((c & 0xF8) == 0xF0) len = 3;
        else return false;
        if (i + len >= n)
            return false;
        for (int j = 1; j <= len; ++j)
            if ((d[i + j] & 0xC0) != 0x80)
                return false;
        i += len + 1;
    }
    return true;
}
} // namespace

QString encodingName(Encoding e)
{
    switch (e) {
    case Encoding::Utf8:    return QStringLiteral("UTF-8");
    case Encoding::Utf16Le: return QStringLiteral("UTF-16 LE");
    case Encoding::Utf16Be: return QStringLiteral("UTF-16 BE");
    case Encoding::Cp1251:  return QStringLiteral("Windows-1251");
    case Encoding::Latin1:  return QStringLiteral("Latin-1");
    }
    return {};
}

QString lineEndingName(LineEnding le)
{
    switch (le) {
    case LineEnding::Lf:   return QStringLiteral("LF");
    case LineEnding::Crlf: return QStringLiteral("CRLF");
    case LineEnding::Cr:   return QStringLiteral("CR");
    }
    return {};
}

QString lineEndingString(LineEnding le)
{
    switch (le) {
    case LineEnding::Crlf: return QStringLiteral("\r\n");
    case LineEnding::Cr:   return QStringLiteral("\r");
    case LineEnding::Lf:   break;
    }
    return QStringLiteral("\n");
}

bool TextBuffer::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return loadFromData(file.readAll(), error);
}

bool TextBuffer::loadFromData(const QByteArray &raw, QString *error)
{
    if (raw.size() > std::numeric_limits<int>::max()) { // смещения piece table — int
        if (error)
            *error = QStringLiteral("файл больше 2 ГБ");
        return false;
    }

    m_encoding = detectEncoding(raw);

    // Декодирование. Utf16 — с учётом BOM; Cp1251/Latin1 — однобайтовые.
    QString text;
    switch (m_encoding) {
    case Encoding::Utf16Le: {
        QStringDecoder dec(QStringConverter::Utf16LE);
        text = dec(raw.mid(2));
        break;
    }
    case Encoding::Utf16Be: {
        QStringDecoder dec(QStringConverter::Utf16BE);
        text = dec(raw.mid(2));
        break;
    }
    case Encoding::Cp1251:
#ifdef Q_OS_WIN
        text = cp1251ToString(raw);
#else
        text = QString::fromLatin1(raw);
#endif
        break;
    case Encoding::Latin1: {
        text = QString::fromLatin1(raw);
        break;
    }
    case Encoding::Utf8: {
        text = QString::fromUtf8(raw);
        break;
    }
    }

    m_lineEnding = detectLineEnding(text);

    text.replace("\r\n", "\n").replace('\r', '\n');
    // Piece table хранит текст с '\n' как разделителями; строковый слой
    // вырезает строки без терминальных переводов («hi\n» — 2 строки).
    m_pt.reset(text);
    m_edits.clear(); // новый текст: старые правки к нему не относятся
    m_editsOverflow = false;
    return true;
}

bool TextBuffer::save(const QString &path, QString *error)
{
    // Разделители в piece table всегда '\n'
    QString text = m_pt.toString();
    if (m_lineEnding != LineEnding::Lf)
        text.replace(u'\n', lineEndingString(m_lineEnding));

    QByteArray encoded;
    switch (m_encoding) {
    case Encoding::Utf16Le:
        encoded = QByteArray("\xFF\xFE", 2) + QStringEncoder(QStringConverter::Utf16LE)(text);
        break;
    case Encoding::Utf16Be:
        encoded = QByteArray("\xFE\xFF", 2) + QStringEncoder(QStringConverter::Utf16BE)(text);
        break;
    case Encoding::Cp1251:
#ifdef Q_OS_WIN
        encoded = stringToCp1251(text);
#else
        encoded = text.toLatin1();
#endif
        break;
    case Encoding::Latin1:
        encoded = text.toLatin1();
        break;
    case Encoding::Utf8:
        encoded = text.toUtf8();
        break;
    }

    QSaveFile file(path); // temp + атомарная замена при commit()
    if (!file.open(QIODevice::WriteOnly) || file.write(encoded) != encoded.size()
        || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

TextBuffer::Position TextBuffer::clampPosition(const Position &pos,
                                               const TextBuffer &buf)
{
    Position p;
    p.line = qBound(0, pos.line, buf.lineCount() - 1);
    p.column = qBound(0, pos.column, buf.lineLength(p.line));
    return p;
}

QString TextBuffer::normalizeNewlines(QString text)
{
    return text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(u'\r', u'\n');
}

int TextBuffer::offsetOf(const Position &pos) const
{
    const Position p = clampPosition(pos, *this);
    return m_pt.lineStartOffset(p.line) + p.column;
}

TextBuffer::Position TextBuffer::positionOf(int offset) const
{
    offset = qBound(0, offset, m_pt.length());
    const int line = m_pt.offsetToLine(offset);
    return {line, offset - m_pt.lineStartOffset(line)};
}

QString TextBuffer::text(const Position &from, const Position &to) const
{
    const int a = offsetOf(qMin(from, to));
    return m_pt.textAt(a, offsetOf(qMax(from, to)) - a);
}

void TextBuffer::replace(int offset, int count, const QString &text)
{
    offset = qBound(0, offset, m_pt.length());
    count = qBound(0, count, m_pt.length() - offset);
    if (count == 0 && text.isEmpty())
        return;
    const Position start = positionOf(offset);
    const Position oldEnd = positionOf(offset + count);
    if (count > 0)
        m_pt.remove(offset, count);
    if (!text.isEmpty())
        m_pt.insert(offset, text);
    const int newEnd = offset + int(text.size());
    recordEdit({offset, offset + count, newEnd, start, oldEnd, positionOf(newEnd)});
}

void TextBuffer::insertText(const Position &pos, const QString &text)
{
    replace(offsetOf(pos), 0, normalizeNewlines(text));
}

void TextBuffer::removeText(const Position &from, const Position &to)
{
    const int a = offsetOf(qMin(from, to));
    replace(a, offsetOf(qMax(from, to)) - a, {});
}

void TextBuffer::recordEdit(const Edit &e)
{
    // Если журнал никто не забирает, не копим его бесконечно
    constexpr int kMaxEdits = 10000;
    if (m_edits.size() >= kMaxEdits) {
        m_edits.clear();
        m_editsOverflow = true;
    }
    m_edits.append(e);
}

QVector<TextBuffer::Edit> TextBuffer::takeEdits(bool *overflow)
{
    if (overflow)
        *overflow = m_editsOverflow;
    m_editsOverflow = false;
    return std::exchange(m_edits, {});
}

Encoding TextBuffer::detectEncoding(const QByteArray &data)
{
    // BOM сравниваем с явной длиной: startsWith(const char*) усекает по NUL,
    // из-за чего "\xFF\xFE\x00\x00" даёт длину 2.
    static const QByteArray bomUtf8("\xEF\xBB\xBF", 3);
    static const QByteArray bom16le("\xFF\xFE", 2);
    static const QByteArray bom16le32("\xFF\xFE\x00\x00", 4);
    static const QByteArray bom16be("\xFE\xFF", 2);

    if (data.startsWith(bomUtf8))
        return Encoding::Utf8;
    if (data.startsWith(bom16le) && !data.startsWith(bom16le32))
        return Encoding::Utf16Le;
    if (data.startsWith(bom16be))
        return Encoding::Utf16Be;

    if (isValidUtf8(data))
        return Encoding::Utf8;

    // cp1251 vs Latin-1: русский текст в cp1251 — почти все старшие байты
    // это 0xC0-0xFF (кириллица); Latin-1 текст обычно редкие акценты.
    int highC0 = 0, high80 = 0;
    for (unsigned char c : data) {
        if (c >= 0xC0)
            ++highC0;
        else if (c >= 0x80)
            ++high80;
    }
    const double highFraction = double(highC0 + high80) / data.size();
    if (highC0 > high80 && highFraction > 0.25)
        return Encoding::Cp1251;
    return Encoding::Latin1;
}

LineEnding TextBuffer::detectLineEnding(const QString &text)
{
    int crlf = 0, lf = 0, cr = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == '\r') {
            if (i + 1 < text.size() && text.at(i + 1) == '\n')
                ++crlf;
            else
                ++cr;
        } else if (c == '\n') {
            if (i == 0 || text.at(i - 1) != '\r')
                ++lf;
        }
    }
    if (crlf >= lf && crlf >= cr && crlf > 0)
        return LineEnding::Crlf;
    if (cr >= lf && cr > 0)
        return LineEnding::Cr;
    return LineEnding::Lf;
}

} // namespace core