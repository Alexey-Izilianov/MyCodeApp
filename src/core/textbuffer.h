#pragma once

#include <QString>
#include "piecetable.h"

namespace core {

enum class Encoding { Utf8, Utf16Le, Utf16Be, Cp1251, Latin1 };
enum class LineEnding { Lf, Crlf, Cr };

QString encodingName(Encoding e);      // "UTF-8", "UTF-16 LE", "Windows-1251", "Latin-1"
QString lineEndingName(LineEnding le); // "LF", "CRLF", "CR"
QString lineEndingString(LineEnding le);

// Буфер: piece table + line index, строки без терминальных переводов.
// Position: line/column, column — индекс QChar.
// C M2-1 хранение — PieceTable (правки не двигают существующий текст);
// публичный API совпадает с буфером M1.
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
    // Декодирование готовых байтов (используется асинхронной загрузкой Document)
    bool loadFromData(const QByteArray &data, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr); // temp + rename

    int lineCount() const { return m_pt.lineCount(); }
    QString lineAt(int i) const { return m_pt.line(i); }
    int lineLength(int line) const { return m_pt.lineLength(line); }

    Encoding encoding() const { return m_encoding; }
    void setEncoding(Encoding e) { m_encoding = e; }
    LineEnding lineEnding() const { return m_lineEnding; }
    void setLineEnding(LineEnding le) { m_lineEnding = le; }

    void insertText(const Position &pos, const QString &text);
    void removeText(const Position &from, const Position &to);

    // Неизменяемый снимок для фоновых потоков (поиск): копирование O(1)
    PieceTable::Snapshot snapshot() const { return m_pt.snapshot(); }

    static Position clampPosition(const Position &pos, const TextBuffer &buf);

private:
    static Encoding detectEncoding(const QByteArray &data);
    static LineEnding detectLineEnding(const QString &text);

    PieceTable m_pt;
    Encoding m_encoding = Encoding::Utf8;
    LineEnding m_lineEnding = LineEnding::Lf;
};

} // namespace core