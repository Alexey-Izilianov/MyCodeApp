#pragma once

#include <QAtomicInteger>
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

    const TextBuffer &buffer() const { return m_buffer; }

    // Заменяет [from, to) на text; before — выделение до правки (для undo).
    // Возвращает позицию сразу за вставленным текстом.
    Position replace(Position from, Position to, const QString &text,
                     EditKind kind, const Selection &before);
    bool undo(Selection *selection);
    bool redo(Selection *selection);
    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index < m_history.size(); }
    // Курсор сдвинули, кликнули мышью и т.п. — следующий ввод начнёт новый шаг
    void breakUndoGroup() { m_groupOpen = false; }

    // Правки буфера для инкрементальных потребителей (подсветка)
    QVector<TextBuffer::Edit> takeEdits(bool *overflow = nullptr)
    { return m_buffer.takeEdits(overflow); }

signals:
    void dirtyChanged(bool dirty);
    void loadProgress(int percent); // 0..100
    void loadFinished(bool ok);

private:
    struct Change {
        int offset = 0;
        QString removed;
        QString inserted;
    };
    struct Step {
        QVector<Change> changes;
        Selection before;
        Selection after;
        EditKind kind = EditKind::Other;
    };

    void resetHistory();
    void markClean();
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
};

} // namespace core
