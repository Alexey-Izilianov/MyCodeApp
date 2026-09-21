#include "document.h"
#include <QFileInfo>

namespace core {

Document::Document(QObject *parent) : QObject(parent) {}

bool Document::load(const QString &path)
{
    QString error;
    if (!m_buffer.load(path, &error))
        return false;
    m_filePath = path;
    setDirty(false);
    return true;
}

bool Document::save(QString *error)
{
    if (m_filePath.isEmpty())
        return false;
    if (!m_buffer.save(m_filePath, error))
        return false;
    setDirty(false);
    return true;
}

bool Document::saveAs(const QString &path, QString *error)
{
    if (!m_buffer.save(path, error))
        return false;
    m_filePath = path;
    setDirty(false);
    return true;
}

QString Document::displayName() const
{
    return QFileInfo(m_filePath).fileName();
}

void Document::setDirty(bool dirty)
{
    if (dirty == m_dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged(m_dirty);
}

} // namespace core