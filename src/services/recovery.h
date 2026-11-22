#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

namespace core {
class Document;
}

// Журнал несохранённых правок на диске (горячий выход + восстановление после
// сбоя). На каждый изменённый документ — файл <каталог>/<хэш пути>.log:
// заголовок (путь, размер и время изменения файла на диске) и записи
// правок буфера. Восстановление: файл с диска + проигрывание записей.
// Записи копятся в памяти и дописываются в файл раз в секунду и при выходе
// (правки маленькие — это дешевле, чем писать весь текст). Сохранение
// документа или возврат отменой к версии с диска журнал удаляет.
class RecoveryManager : public QObject {
    Q_OBJECT
public:
    explicit RecoveryManager(const QString &directory, QObject *parent = nullptr);
    ~RecoveryManager() override;

    static QString defaultDirectory();

    // Файлы, для которых на диске лежат журналы (после сбоя или горячего выхода)
    QStringList unsavedFiles() const;

    void watch(core::Document *doc);
    // Перестать следить (документ переходит в другое окно): журнал на диске
    // сохраняется — восстановит новый владелец, когда станет его документ
    void unwatch(core::Document *doc);
    // Проиграть журнал поверх только что загруженного с диска документа.
    // false — журнала нет или файл на диске с тех пор изменился (журнал удаляется)
    bool restore(core::Document *doc);
    // Документ закрыт без сохранения — журнал больше не нужен
    void discard(core::Document *doc);
    void flush();

private:
    struct Journal {
        QString logPath;
        QByteArray pending; // записи, ещё не дописанные в файл
        qint64 committedSize = 0; // размер журнала до следующей записи
    };

    QString logPathFor(const QString &filePath) const;
    void record(core::Document *doc, int offset, int count, const QString &text);
    void drop(core::Document *doc);

    QString m_directory;
    QHash<core::Document *, Journal> m_journals;
    QTimer m_flushTimer;
    core::Document *m_restoring = nullptr;
};
