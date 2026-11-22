#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>
#include <memory>
#include "mappedtext.h"
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
    ~Document() override;

    // Порог отображения в память вместо полного чтения; для тестов — чтобы
    // получить крупный путь на маленьком файле
    static void setMappedThreshold(qint64 bytes) { s_mappedThreshold = bytes; }

    bool load(const QString &path);
    // Чтение и декодирование в QThreadPool; готовность — loadFinished
    bool loadAsync(const QString &path);
    bool isLoading() const { return m_loading; }

    bool save(QString *error = nullptr);
    bool saveAsUtf8(QString *error);
    bool saveWithLineEnding(LineEnding ending, QString *error);
    bool saveAs(const QString &path, QString *error = nullptr);
    // Потоковое сохранение крупного файла (отображён в память): запись в temp
    // в фоновой потоке, затем переименование и перенацеливание кусков.
    // false — обычный файл (сохранять синхронно) или уже идёт сохранение
    bool saveAsync(QString *error = nullptr);
    bool saveAsyncAt(const QString &target, bool sameFile, QString *error = nullptr);
    void cancelSave();
    bool isSaving() const { return m_saving; }

    QString filePath() const { return m_filePath; }
    QString displayName() const;
    bool isDirty() const { return m_dirty; }
    quint64 revision() const { return m_revision; }
    bool isReadOnly() const { return m_virtual; }
    // Текст не с диска (просмотр изменений): только чтение, путь — для подсветки.
    // Буфер готовится в фоне, сюда передаётся целиком
    void loadVirtual(TextBuffer buffer, const QString &path);
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
    void fileFormatChanged();
    // Каждое изменение буфера (правка, undo, redo) — для журнала восстановления
    void edited(int offset, int count, const QString &text);
    void loadProgress(int percent); // 0..100
    void loadFinished(bool ok);
    void reloaded();
    // Потоковое сохранение крупного файла
    void saveProgress(int percent);
    void saveStateChanged(bool saving);
    void saveFinished(bool ok, const QString &error);
    // После каждой правки (replace, undo, redo, восстановление) — её журнал для всех видов
    // документа: свёртка, перенос, tree-sitter. overflow — журнал переполнился, пересчитать всё
    void changed(const QVector<TextBuffer::Edit> &edits, bool overflow);

private:
    using MappedSharedPtr = std::shared_ptr<const MappedText>;
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
    void flushEdits();
    void pushStep(QVector<Change> changes, const Selections &before, const Selections &after,
                  EditKind kind);
    void resetHistory();
    void markClean();
    void stampDisk();
    void updateDirty();
    // Завершение saveAsync в GUI-потоке: temp уже записан и отсканирован
    void finishSave(bool ok, const QString &error, const MappedSharedPtr &newBase,
                    const QString &temp, const QString &target, bool sameFile);

    QString m_filePath;
    TextBuffer m_buffer;
    quint64 m_revision = 0;
    bool m_loading = false;
    bool m_virtual = false;
    bool m_saving = false;
    std::shared_ptr<std::atomic_bool> m_saveCancel;
    static qint64 s_mappedThreshold;

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
