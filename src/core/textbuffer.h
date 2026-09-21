#pragma once

#include <QVector>
#include <QString>

namespace core {

enum class Encoding { Utf8, Utf16Le, Utf16Be, Cp1251, Latin1 };
enum class LineEnding { Lf, Crlf, Cr };

QString encodingName(Encoding e);      // "UTF-8", "UTF-16 LE", "Windows-1251", "Latin-1"
QString lineEndingName(LineEnding le); // "LF", "CRLF", "CR"
QString lineEndingString(LineEnding le);

// Буфер M1: файл в памяти, строки без терминальных переводов.
// Position: line/column, column — индекс QChar. Piece table — M2.
class TextBuffer {
public:
    struct Position {
        int line = 0;
        int column = 0;

        bool operator==(const Position &o) const
        { return line == o.line && column == o.column; }
        bool operator<(const Position &o) const
        { return line != o.line ? line < o.line : column < o.column; }
    };

    bool load(const QString &path, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr); // temp + rename

    int lineCount() const { return m_lines.size(); }
    const QString &lineAt(int i) const { return m_lines.at(i); }
    int lineLength(int line) const { return m_lines.at(line).size(); }

    Encoding encoding() const { return m_encoding; }
    void setEncoding(Encoding e) { m_encoding = e; }
    LineEnding lineEnding() const { return m_lineEnding; }
    void setLineEnding(LineEnding le) { m_lineEnding = le; }

    void insertText(const Position &pos, const QString &text);
    void removeText(const Position &from, const Position &to);

    static Position clampPosition(const Position &pos, const TextBuffer &buf);

private:
    static Encoding detectEncoding(const QByteArray &data);
    static LineEnding detectLineEnding(const QString &text);

    QVector<QString> m_lines;
    Encoding m_encoding = Encoding::Utf8;
    LineEnding m_lineEnding = LineEnding::Lf;
};

} // namespace core