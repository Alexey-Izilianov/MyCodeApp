#include "recovery.h"

#include "appsettings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

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

QString recoveryPathKey(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
#ifdef Q_OS_WIN
    return absolute.toLower();
#else
    return absolute;
#endif
}

QString journalFileName(const QByteArray &key)
{
    return QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex())
           + QStringLiteral(".log");
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
    return appDataDir()
           + QStringLiteral("/recovery");
}

QString RecoveryManager::logPathFor(const QString &filePath) const
{
    const QString name = journalFileName(recoveryPathKey(filePath).toUtf8());
#ifdef Q_OS_WIN
    return m_directory + u'/' + name;
#else
    return m_directory + QStringLiteral("/path-") + name;
#endif
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
    m_journals.insert(doc, {logPathFor(doc->filePath()), {}, 0});
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

void RecoveryManager::unwatch(core::Document *doc)
{
    if (!m_journals.contains(doc))
        return;
    flush();
    // Снимаем только свои подписки на этот документ (получатели — this);
    // журнал остаётся на диске, его продолжит новый владелец документа
    disconnect(doc, &Document::edited, this, nullptr);
    disconnect(doc, &Document::dirtyChanged, this, nullptr);
    disconnect(doc, &QObject::destroyed, this, nullptr);
    m_journals.remove(doc);
}

bool RecoveryManager::restore(Document *doc)
{
    const QString logPath = logPathFor(doc->filePath());
    QFile file(logPath);
    if (!file.open(QIODevice::ReadOnly)) {
#ifndef Q_OS_WIN
        // Старые журналы на Linux и macOS хэшировали путь в нижнем регистре.
        const QByteArray oldKey = QFileInfo(doc->filePath()).absoluteFilePath().toLower().toUtf8();
        file.setFileName(m_directory + u'/' + journalFileName(oldKey));
        if (!file.open(QIODevice::ReadOnly))
            return false;
#else
        return false;
#endif
    }

    QDataStream in(&file);
    in.setVersion(kStreamVersion);
    Header header;
    const Header disk = diskState(doc->filePath());
    if (!readHeader(in, &header) || recoveryPathKey(header.path) != recoveryPathKey(disk.path))
        return false;
    if (header.size != disk.size || header.modified != disk.modified) {
        file.close();
        file.remove(); // файл на диске изменился — правки к нему не применимы
        return false;
    }

    QVector<Document::RawEdit> edits;
    qint64 committedSize = file.pos();
    while (!in.atEnd()) {
        Document::RawEdit e;
        in >> e.offset >> e.count >> e.text;
        if (in.status() != QDataStream::Ok)
            break; // запись оборвана сбоем — всё до неё уже прочитано
        edits.append(e);
        committedSize = file.pos();
    }
    file.close();

    if (auto it = m_journals.find(doc); it != m_journals.end()) {
        if (file.fileName() != logPath && !file.rename(logPath))
            qWarning("Could not migrate recovery journal: %s", qPrintable(file.errorString()));
        it->logPath = file.fileName();
        it->committedSize = committedSize;
    }
    m_restoring = doc;
    doc->replayEdits(edits);
    m_restoring = nullptr;
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
    it->committedSize = 0;
    QFile::remove(it->logPath);
}

void RecoveryManager::record(Document *doc, int offset, int count, const QString &text)
{
    if (doc == m_restoring)
        return;
    auto it = m_journals.find(doc);
    if (it == m_journals.end())
        return;
    QDataStream out(&it->pending, QIODevice::Append);
    out.setVersion(kStreamVersion);
    if (it->committedSize == 0 && it->pending.isEmpty())
        out << diskState(doc->filePath()); // первая правка после совпадения с диском
    out << qint32(offset) << qint32(count) << text;
    if (!m_flushTimer.isActive())
        m_flushTimer.start();
}

void RecoveryManager::flush()
{
    bool retry = false;
    for (auto it = m_journals.begin(); it != m_journals.end(); ++it) {
        if (it->pending.isEmpty())
            continue;
        bool written = false;
        if (it->committedSize == 0) {
            QSaveFile file(it->logPath);
            written = file.open(QIODevice::WriteOnly)
                      && file.write(it->pending) == it->pending.size() && file.commit();
        } else {
            QFile file(it->logPath);
            // Оборванный хвост удаляем до повторной записи того же блока.
            if (file.open(QIODevice::ReadWrite | QIODevice::ExistingOnly)
                && file.size() >= it->committedSize && file.resize(it->committedSize)
                && file.seek(it->committedSize)) {
                written = file.write(it->pending) == it->pending.size() && file.flush();
            }
        }
        if (written) {
            it->committedSize += it->pending.size();
            it->pending.clear();
        } else {
            qWarning("Could not write recovery journal: %s", qPrintable(it->logPath));
            retry = true;
        }
    }
    if (retry && !m_flushTimer.isActive())
        m_flushTimer.start();
}
