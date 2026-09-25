#include "document.h"
#include <QFile>
#include <QFileInfo>
#include <QThreadPool>

namespace core {

namespace {
constexpr qint64 kReadChunk = 1 << 20; // 1 МБ — шаг прогресса чтения
} // namespace

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

bool Document::loadAsync(const QString &path)
{
    if (m_loading.fetchAndStoreRelaxed(true))
        return false; // загрузка уже идёт
    m_filePath = path; // документ ещё не опубликован — можно сразу
    Document *doc = this; // документ держит DocumentManager до loadFinished
    QThreadPool::globalInstance()->start([doc, path]() {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMetaObject::invokeMethod(
                doc,
                [doc]() { doc->m_loading.storeRelaxed(false); emit doc->loadFinished(false); },
                Qt::QueuedConnection);
            return;
        }
        const qint64 total = qMax<qint64>(1, file.size());
        QByteArray raw;
        raw.reserve(int(qMin<qint64>(total, 8192 * 1024 * 1024LL)));
        char chunk[kReadChunk];
        int lastPercent = -1;
        while (!file.atEnd()) {
            const qint64 n = file.read(chunk, kReadChunk);
            if (n <= 0)
                break;
            raw.append(chunk, int(n));
            const int percent = int(raw.size() * 100 / total);
            if (percent != lastPercent) {
                lastPercent = percent;
                QMetaObject::invokeMethod(
                    doc,
                    [doc, percent]() { emit doc->loadProgress(percent); },
                    Qt::QueuedConnection);
            }
        }
        // Буфер ещё никем не виден (документ не опубликован) — грузим в фоне.
        QString error;
        const bool ok = doc->m_buffer.loadFromData(raw, &error);
        QMetaObject::invokeMethod(
            doc,
            [doc, ok, path]() {
                doc->m_loading.storeRelaxed(false);
                if (ok) {
                    doc->m_filePath = path;
                    doc->setDirty(false);
                }
                emit doc->loadFinished(ok);
            },
            Qt::QueuedConnection);
    });
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