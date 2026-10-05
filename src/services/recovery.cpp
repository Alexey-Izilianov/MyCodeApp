#include "recovery.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "src/core/document.h"

using core::Document;

namespace {
constexpr quint32 kMagic = 0x4d434152; // "MCAR"
constexpr quint32 kVersion = 1;
constexpr int kFlushMs = 1000;
constexpr QDataStream::Version kStreamVersion = QDataStream::Qt_6_0;

struct Header {
    QString path;
    qint64 size = -1;
    qint64 modified = 0;
};

QDataStream &operator<<(QDataStream &out, const Header &h)
{
    return out << kMagic << kVersion << h.path << h.size << h.modified;
}

bool readHeader(QDataStream &in, Header *h)
{
    quint32 magic = 0, version = 0;
    in >> magic >> version >> h->path >> h->size >> h->modified;
    return in.status() == QDataStream::Ok && magic == kMagic && version == kVersion;
}

Header diskState(const QString &path)
{
    const QFileInfo info(path);
    return {path, info.size(), info.lastModified().toMSecsSinceEpoch()};
}
} // namespace

RecoveryManager::RecoveryManager(const QString &directory, QObject *parent)
    : QObject(parent)
    , m_directory(directory)
{
    QDir().mkpath(m_directory);
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(kFlushMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &RecoveryManager::flush);
    if (QCoreApplication::instance())
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
                &RecoveryManager::flush);
}

RecoveryManager::~RecoveryManager()
{
    flush();
}

QString RecoveryManager::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/recovery");
}

QString RecoveryManager::logPathFor(const QString &filePath) const
{
    const QByteArray key = QFileInfo(filePath).absoluteFilePath().toLower().toUtf8();
    return m_directory + u'/'
           + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex())
           + QStringLiteral(".log");
}

QStringList RecoveryManager::unsavedFiles() const
{
    QStringList files;
    const QStringList logs = QDir(m_directory).entryList({QStringLiteral("*.log")}, QDir::Files);
    for (const QString &name : logs) {
        QFile file(m_directory + u'/' + name);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QDataStream in(&file);
        in.setVersion(kStreamVersion);
        Header header;
        if (readHeader(in, &header))
            files.append(header.path);
    }
    return files;
}

void RecoveryManager::watch(Document *doc)
{
    if (m_journals.contains(doc))
        return;
    m_journals.insert(doc, {logPathFor(doc->filePath()), {}, false});
    connect(doc, &Document::edited, this,
            [this, doc](int offset, int count, const QString &text) { record(doc, offset, count, text); });
    // Документ снова совпадает с диском (сохранили или отменили всё) — журнал не нужен
    connect(doc, &Document::dirtyChanged, this, [this, doc](bool dirty) {
        if (!dirty)
            drop(doc);
    });
    connect(doc, &QObject::destroyed, this, [this, doc] {
        flush(); // выход из приложения: несохранённое остаётся в журнале
        m_journals.remove(doc);
    });
}

bool RecoveryManager::restore(Document *doc)
{
    const QString logPath = logPathFor(doc->filePath());
    QFile file(logPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QDataStream in(&file);
    in.setVersion(kStreamVersion);
    Header header;
    const Header disk = diskState(doc->filePath());
    if (!readHeader(in, &header) || header.size != disk.size || header.modified != disk.modified) {
        file.close();
        QFile::remove(logPath); // файл на диске изменился — правки к нему не применимы
        return false;
    }

    QVector<Document::RawEdit> edits;
    while (!in.atEnd()) {
        Document::RawEdit e;
        in >> e.offset >> e.count >> e.text;
        if (in.status() != QDataStream::Ok)
            break; // запись оборвана сбоем — всё до неё уже прочитано
        edits.append(e);
    }
    file.close();
    QFile::remove(logPath); // перепишется заново: проигрывание снова попадёт в журнал

    if (m_journals.contains(doc))
        m_journals[doc] = {logPath, {}, false};
    doc->replayEdits(edits);
    return !edits.isEmpty();
}

void RecoveryManager::discard(Document *doc)
{
    drop(doc);
    m_journals.remove(doc);
}

void RecoveryManager::drop(Document *doc)
{
    auto it = m_journals.find(doc);
    if (it == m_journals.end())
        return;
    it->pending.clear();
    it->started = false;
    QFile::remove(it->logPath);
}

void RecoveryManager::record(Document *doc, int offset, int count, const QString &text)
{
    auto it = m_journals.find(doc);
    if (it == m_journals.end())
        return;
    QDataStream out(&it->pending, QIODevice::Append);
    out.setVersion(kStreamVersion);
    if (!it->started && it->pending.isEmpty())
        out << diskState(doc->filePath()); // первая правка после совпадения с диском
    out << qint32(offset) << qint32(count) << text;
    if (!m_flushTimer.isActive())
        m_flushTimer.start();
}

void RecoveryManager::flush()
{
    for (auto it = m_journals.begin(); it != m_journals.end(); ++it) {
        if (it->pending.isEmpty())
            continue;
        QFile file(it->logPath);
        const QIODevice::OpenMode mode = it->started ? QIODevice::Append
                                                     : QIODevice::WriteOnly | QIODevice::Truncate;
        if (!file.open(mode))
            continue; // диск недоступен — попробуем при следующей правке
        file.write(it->pending);
        it->pending.clear();
        it->started = true;
    }
}
