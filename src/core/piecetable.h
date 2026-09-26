#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace core {

// Piece table (M2-1). Текст = неизменяемый оригинал файла + окна (куски)
// в буферы добавок. Вставка разрезает кусок, удаление выбрасывает его часть —
// существующий текст не копируется и не двигается.
//
// Кусок может содержать '\n' внутри: строки вырезаются по line index
// (смещения каждого '\n'), а не по границам кусков.
class PieceTable {
public:
    struct Piece {
        int bufferIndex = 0; // 0 — оригинал, >= 1 — индекс буфера добавок
        int start = 0;       // смещение начала окна в буфере, в QChar
        int length = 0;      // длина окна в QChar
    };

private:
    // Общее состояние таблицы и снимков: чтение через него же.
    struct Core {
        QString original;       // текст файла, после reset не меняется
        QVector<QString> added; // буферы добавок (по одному на вставку)
        QVector<Piece> pieces;
        QVector<int> offsets;   // offsets[i] — абсолютное начало куска i; последний — длина текста
        QVector<int> newlines;  // смещения каждого '\n' по всему тексту, по возрастанию

        int length() const { return offsets.isEmpty() ? 0 : offsets.constLast(); }
        const QString &bufferOf(const Piece &p) const
        { return p.bufferIndex == 0 ? original : added.at(p.bufferIndex - 1); }

        int pieceFor(int offset) const;
        QString textAt(int offset, int count) const;
        QString toString() const;
        int lineCount() const { return newlines.size() + 1; }
        int lineStartOffset(int line) const;
        int lineLength(int line) const;
        QString line(int line) const;
        int offsetToLine(int offset) const;
    };

public:
    // Неизменяемый снимок для фоновых потоков (поиск). Копирование контейнеров
    // неявное (COW), поэтому дёшево; правки живой таблицы детачат свои копии
    // и не задевают снимок. Дорогая материализация строк — на снимке, в фоне.
    class Snapshot {
    public:
        int length() const { return m_core.length(); }
        int lineCount() const { return m_core.lineCount(); }
        int lineLength(int line) const { return m_core.lineLength(line); }
        QString line(int line) const { return m_core.line(line); }
        int offsetToLine(int offset) const { return m_core.offsetToLine(offset); }
        QString textAt(int offset, int count) const { return m_core.textAt(offset, count); }
        QString toString() const { return m_core.toString(); }
        QStringList lines() const;

    private:
        friend class PieceTable;
        explicit Snapshot(const Core &core) : m_core(core) {}
        Core m_core;
    };

    // Сброс: оригинал = расшифрованный текст файла (с '\n' как разделителями).
    void reset(const QString &originalText);

    int length() const { return m_core.length(); }
    int pieceCount() const { return m_core.pieces.size(); }

    // Правки по смещению (в QChar по всему тексту, включая '\n')
    void insert(int offset, const QString &text);
    void remove(int offset, int count);

    QString textAt(int offset, int count) const { return m_core.textAt(offset, count); }
    QString toString() const { return m_core.toString(); }

    // Строковый слой (line index)
    int lineCount() const { return m_core.lineCount(); }
    int lineLength(int line) const { return m_core.lineLength(line); }
    int lineStartOffset(int line) const { return m_core.lineStartOffset(line); }
    QString line(int line) const { return m_core.line(line); }
    int offsetToLine(int offset) const { return m_core.offsetToLine(offset); }

    Snapshot snapshot() const { return Snapshot(m_core); }

    // Проверка инвариантов — вызывается из тестов
    void verify() const;

private:
    void rebuildOffsets();

    Core m_core;
};

} // namespace core