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
    bool save(const QString &path, QString *error = nullptr); // атомарно (QSaveFile)

    int length() const { return m_pt.length(); }
    int lineCount() const { return m_pt.lineCount(); }
    QString lineAt(int i) const { return m_pt.line(i); }
    int lineLength(int line) const { return m_pt.lineLength(line); }

    int offsetOf(const Position &pos) const; // позиция зажимается в границы текста
    Position positionOf(int offset) const;
    QString text(const Position &from, const Position &to) const;

    Encoding encoding() const { return m_encoding; }
    void setEncoding(Encoding e) { m_encoding = e; }
    LineEnding lineEnding() const { return m_lineEnding; }
    void setLineEnding(LineEnding le) { m_lineEnding = le; }

    // Базовая правка: [offset, offset + count) -> text (переводы строк — только '\n')
    void replace(int offset, int count, const QString &text);
    void insertText(const Position &pos, const QString &text); // CRLF/CR -> LF
    void removeText(const Position &from, const Position &to);
    static QString normalizeNewlines(QString text);

    // Журнал правок для инкрементальных потребителей (tree-sitter):
    // смещения — в QChar по всему тексту.
    struct Edit {
        int startOffset = 0;
        int oldEndOffset = 0;
        int newEndOffset = 0;
        Position start;
        Position oldEnd;
        Position newEnd;
    };
    // overflow = true: журнал переполнялся и был сброшен — потребителю
    // нужен полный пересчёт вместо применения правок.
    QVector<Edit> takeEdits(bool *overflow = nullptr);

    // Неизменяемый снимок для фоновых потоков (поиск): копирование O(1)
    PieceTable::Snapshot snapshot() const { return m_pt.snapshot(); }

    static Position clampPosition(const Position &pos, const TextBuffer &buf);

private:
    static Encoding detectEncoding(const QByteArray &data);
    static LineEnding detectLineEnding(const QString &text);

    void recordEdit(const Edit &e);

    PieceTable m_pt;
    QVector<Edit> m_edits;
    bool m_editsOverflow = false;
    Encoding m_encoding = Encoding::Utf8;
    LineEnding m_lineEnding = LineEnding::Lf;
};

} // namespace core