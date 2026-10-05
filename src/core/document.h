#pragma once

#include <QAtomicInteger>
#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVector>
#include "textbuffer.h"

namespace core {

// Документ = буфер + путь + история правок (undo/redo). Живёт в GUI-потоке.
// Все правки текста идут через replace(), чтобы попасть в историю;
// dirty — «текущий шаг истории не тот, что был при сохранении».
class Document : public QObject {
    Q_OBJECT
public:
    using Position = TextBuffer::Position;

    struct Selection {
        Position cursor;
        Position anchor;
    };
    using Selections = QVector<Selection>; // по одной на курсор

    struct Replacement {
        Position from;
        Position to;
        QString text;
    };

    // Typing — посимвольный ввод и удаление: подряд идущие правки
    // сливаются в один шаг отмены, пока группу не разорвут.
    enum class EditKind { Typing, Other };

    explicit Document(QObject *parent = nullptr);

    bool load(const QString &path);
    // Чтение и декодирование в QThreadPool; готовность — loadFinished
    bool loadAsync(const QString &path);
    bool isLoading() const { return m_loading.loadRelaxed(); }

    bool save(QString *error = nullptr);
    bool saveAs(const QString &path, QString *error = nullptr);

    QString filePath() const { return m_filePath; }
    QString displayName() const;
    bool isDirty() const { return m_dirty; }
    bool isReadOnly() const { return m_buffer.isMapped(); }
    // Файл на диске поменяли не мы (время или размер не те, что при загрузке/сохранении)
    bool changedOnDisk() const;
    // Перечитать с диска; история правок сбрасывается
    bool reload();
    // Оставить свою версию: изменение на диске считается увиденным
    void acceptDiskVersion();

    const TextBuffer &buffer() const { return m_buffer; }

    // Заменяет непересекающиеся диапазоны одним шагом истории; before —
    // выделения до правки (для undo). Возвращает позиции сразу за каждой
    // вставкой — в порядке parts.
    QVector<Position> replace(const QVector<Replacement> &parts, EditKind kind,
                              const Selections &before);
    Position replace(Position from, Position to, const QString &text, EditKind kind,
                     const Selections &before)
    { return replace({{from, to, text}}, kind, before).constFirst(); }
    // Уточнить выделения «после» у только что записанного шага: курсор
    // оказался не за вставкой (внутри автоскобок, отступ строк и т.п.)
    void setLastStepSelections(const Selections &after);
    bool undo(Selections *selections);
    bool redo(Selections *selections);
    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index < m_history.size(); }
    // Курсор сдвинули, кликнули мышью и т.п. — следующий ввод начнёт новый шаг
    void breakUndoGroup() { m_groupOpen = false; }

    // Правки буфера для инкрементальных потребителей (подсветка)
    QVector<TextBuffer::Edit> takeEdits(bool *overflow = nullptr)
    { return m_buffer.takeEdits(overflow); }

    // Правка буфера в «сыром» виде: [offset, offset + count) -> text
    struct RawEdit {
        int offset = 0;
        int count = 0;
        QString text;
    };
    // Проиграть правки поверх текста с диска (восстановление после сбоя)
    // одним шагом истории: отмена вернёт версию с диска
    void replayEdits(const QVector<RawEdit> &edits);

signals:
    void dirtyChanged(bool dirty);
    // Каждое изменение буфера (правка, undo, redo) — для журнала восстановления
    void edited(int offset, int count, const QString &text);
    void loadProgress(int percent); // 0..100
    void loadFinished(bool ok);
    void reloaded();

private:
    struct Change {
        int offset = 0;
        QString removed;
        QString inserted;
    };
    struct Step {
        QVector<Change> changes; // в порядке применения
        Selections before;
        Selections after;
        EditKind kind = EditKind::Other;
    };

    void apply(int offset, int count, const QString &text);
    void pushStep(QVector<Change> changes, const Selections &before, const Selections &after,
                  EditKind kind);
    void resetHistory();
    void markClean();
    void stampDisk();
    void updateDirty();

    QString m_filePath;
    TextBuffer m_buffer;
    QAtomicInteger<bool> m_loading{false};

    QVector<Step> m_history;
    int m_index = 0;      // сколько шагов истории применено
    int m_cleanIndex = 0; // m_index на момент сохранения; -1 — недостижим
    bool m_groupOpen = false;
    QElapsedTimer m_lastTyping;
    bool m_dirty = false;
    QDateTime m_diskTime;
    qint64 m_diskSize = -1;
};

} // namespace core
