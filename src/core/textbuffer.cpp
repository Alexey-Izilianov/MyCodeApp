#include "textbuffer.h"

#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>
#include <QStringEncoder>
#include <QTemporaryFile>
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

    const QByteArray raw = file.readAll();
    if (raw.size() >= 8192 * 1024 * 1024LL) { // предел ТЗ — 1 ГБ
        if (error)
            *error = QStringLiteral("файл больше 1 ГБ");
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
    // Терминальный \n — отдельная пустая строка (как в Scintilla): «hi\n» — 2 строки.
    m_lines = text.split('\n');
    if (m_lines.isEmpty())
        m_lines.append(QString());
    return true;
}

bool TextBuffer::save(const QString &path, QString *error)
{
    QString tempName;
    QByteArray encoded;
    {
        QTemporaryFile temp(QFileInfo(path).absolutePath() + "/XXXXXX.tmp");
        temp.setAutoRemove(false);
        if (!temp.open()) {
            if (error)
                *error = temp.errorString();
            return false;
        }
        tempName = temp.fileName();

        QString text = m_lines.join(lineEndingString(m_lineEnding));

        switch (m_encoding) {
        case Encoding::Utf16Le: {
            QStringEncoder enc(QStringConverter::Utf16LE);
            encoded = ("\xFF\xFE") + enc(text);
            break;
        }
        case Encoding::Utf16Be: {
            QStringEncoder enc(QStringConverter::Utf16BE);
            encoded = ("\xFE\xFF") + enc(text);
            break;
        }
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

        if (temp.write(encoded) != encoded.size()) {
            if (error)
                *error = temp.errorString();
            temp.remove();
            return false;
        }
    } // QTemporaryFile уничтожен — хэндл закрыт, иначе rename на Windows падает

    QFile target(path);
    if (target.exists() && !target.remove()) {
        if (error)
            *error = target.errorString();
        QFile::remove(tempName);
        return false;
    }
    if (!QFile::rename(tempName, path)) {
        if (error)
            *error = QStringLiteral("rename не удался");
        QFile::remove(tempName);
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

void TextBuffer::insertText(const Position &pos, const QString &text)
{
    QString normalized = text;
    normalized.replace('\r', '\n');

    Position p = clampPosition(pos, *this);
    QString &line = m_lines[p.line];
    const int col = p.column;

    if (!normalized.contains('\n')) {
        line.insert(col, normalized);
        return;
    }

    const QStringList parts = normalized.split('\n');
    const QString tail = line.mid(col);
    line = line.left(col) + parts.first();
    for (int i = 1; i < parts.size(); ++i)
        m_lines.insert(p.line + i, parts.at(i));
    m_lines[p.line + parts.size() - 1] += tail;
}

void TextBuffer::removeText(const Position &from, const Position &to)
{
    Position a = from, b = to;
    if (b < a)
        std::swap(a, b);
    a = clampPosition(a, *this);
    b = clampPosition(b, *this);
    if (a == b)
        return;

    if (a.line == b.line) {
        m_lines[a.line].remove(a.column, b.column - a.column);
        return;
    }

    m_lines[a.line] = m_lines[a.line].left(a.column)
                      + m_lines[b.line].mid(b.column);
    m_lines.remove(a.line + 1, b.line - a.line);
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