#pragma once

#include <QVector>
#include <optional>

#include "textbuffer.h"

// Сворачивание блоков: где блок начинается и кончается (по скобкам, иначе
// по отступам) и какие строки скрыты. Строки и ряды — с нуля; ряд — номер
// строки на экране после вычета скрытых.
namespace core::folding {

// Дёшево, для маркеров в gutter: строка открывает многострочный блок
// (незакрытая в строке скобка или следующая непустая строка глубже)
bool canFold(const TextBuffer &buffer, int line);
// Последняя скрываемая строка блока, открытого строкой line. По скобкам
// строка с закрывающей остаётся видимой (`} else {`); по отступам пустые
// строки в хвосте блока тоже
std::optional<int> foldEnd(const TextBuffer &buffer, int line);

class FoldSet {
public:
    struct Fold {
        int line = 0; // заголовок, видим
        int last = 0; // скрыты line+1..last
        bool operator==(const Fold &o) const { return line == o.line && last == o.last; }
    };

    bool isEmpty() const { return m_folds.isEmpty(); }
    const QVector<Fold> &folds() const { return m_folds; }
    void add(Fold fold);
    void assign(QVector<Fold> folds); // по возрастанию line
    bool remove(int line); // свёрнутый блок с заголовком line
    void clear();
    bool isFolded(int line) const; // строка — заголовок свёрнутого блока
    bool isHidden(int line) const;
    void reveal(int line); // развернуть всё, что прячет строку

    int rowCount(int lineCount) const;
    int rowOf(int line) const; // скрытая строка — ряд её заголовка
    int lineAt(int row) const; // строка, видимая в ряду row

    // Сдвинуть блоки под правку; блоки, задетые многострочной правкой, — развернуть
    void applyEdit(const TextBuffer::Edit &edit);

private:
    struct Range {
        int first = 0;
        int last = 0;
    };
    void rebuild();
    int rangeBefore(int line) const; // индекс последнего диапазона с first <= line, иначе -1

    QVector<Fold> m_folds;   // по line, без повторов; вложенные допустимы
    QVector<Range> m_hidden; // объединённые скрытые диапазоны
    QVector<int> m_hiddenBefore; // скрыто строк до m_hidden[i]; последний — всего
};

} // namespace core::folding
