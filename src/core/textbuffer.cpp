#include "textbuffer.h"

#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QStringDecoder>
#include <QStringEncoder>
#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

namespace core {

namespace {
// Windows-1251 своей таблицей: QStringConverter без ICU её не знает, а ICU
// есть не во всех сборках Qt. Байты 0x00–0x7F — ASCII, 0xC0–0xFF — А–я.
constexpr char16_t kCp1251High[64] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90
    0x0098, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, // 0xA0
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, // 0xB0
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
};

QString cp1251ToString(const QByteArray &raw)
{
    QString out(raw.size(), Qt::Uninitialized);
    QChar *dst = out.data();
    for (const char ch : raw) {
        const auto b = static_cast<unsigned char>(ch);
        *dst++ = b < 0x80 ? QChar(b) : b < 0xC0 ? QChar(kCp1251High[b - 0x80]) : QChar(char16_t(0x0410 + b - 0xC0));
    }
    return out;
}

QByteArray stringToCp1251(const QString &s)
{
    QByteArray out(s.size(), Qt::Uninitialized);
    char *dst = out.data();
    for (const QChar c : s) {
        const char16_t u = c.unicode();
        char byte = '?'; // символа нет в кодировке
        if (u < 0x80)
            byte = char(u);
        else if (u >= 0x0410 && u <= 0x044F)
            byte = char(0xC0 + u - 0x0410);
        else if (const auto *it = std::find(std::begin(kCp1251High), std::end(kCp1251High), u); it != std::end(kCp1251High))
            byte = char(0x80 + (it - std::begin(kCp1251High)));
        *dst++ = byte;
    }
    return out;
}

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
            *error = QCoreApplication::translate("TextBuffer", "Файл больше 2 ГБ");
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
        text = cp1251ToString(raw);
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

bool TextBuffer::loadMapped(const QString &path, const std::function<void(int)> &progress)
{
    // Кодировка — по началу файла, обрезанному до конца строки, чтобы не
    // разрезать многобайтовый символ UTF-8
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QByteArray head = file.read(1 << 16);
    if (const int nl = int(head.lastIndexOf('\n')); nl > 0)
        head.truncate(nl + 1);
    const Encoding encoding = detectEncoding(head);
    if (encoding != Encoding::Utf8 && encoding != Encoding::Latin1)
        return false;

    auto mapped = MappedText::open(path, encoding == Encoding::Utf8, progress);
    if (!mapped)
        return false;
    m_encoding = encoding;
    m_lineEnding = mapped->crlf() ? LineEnding::Crlf : LineEnding::Lf;
    m_pt.reset(std::move(mapped));
    m_edits.clear();
    m_editsOverflow = false;
    return true;
}

bool TextBuffer::save(const QString &path, QString *error)
{
    if (isMapped()) {
        if (error)
            *error = QCoreApplication::translate("TextBuffer", "Файл открыт только для чтения");
        return false;
    }

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
        encoded = stringToCp1251(text);
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

bool TextBuffer::looksBinary(const QByteArray &data)
{
    const bool utf16 = data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF");
    return !utf16 && data.left(8192).contains('\0');
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