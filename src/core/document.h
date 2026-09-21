#pragma once

#include <QObject>
#include <QString>
#include "textbuffer.h"

namespace core {

// Документ = буфер + путь к файлу + dirty-флаг. Живёт в GUI-треде.
class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject *parent = nullptr);

    bool load(const QString &path);
    bool save(QString *error = nullptr); // сохраняет в тот же путь
    bool saveAs(const QString &path, QString *error = nullptr);

    QString filePath() const { return m_filePath; }
    QString displayName() const; // имя файла без каталога

    bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty);

    const TextBuffer &buffer() const { return m_buffer; }
    TextBuffer &buffer() { return m_buffer; }

signals:
    void dirtyChanged(bool dirty);

private:
    QString m_filePath;
    bool m_dirty = false;
    TextBuffer m_buffer;
};

} // namespace core