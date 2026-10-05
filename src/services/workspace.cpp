#include "workspace.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "ignorerules.h"

namespace {

QString dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object()
                                          : QJsonObject();
}

void writeJson(const QString &path, const QJsonObject &object)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(object).toJson());
}

QString settingsPath()
{
    return dataDir() + QStringLiteral("/settings.json");
}

void rememberRoot(const QString &root)
{
    QJsonObject settings = readJson(settingsPath());
    settings.insert(QStringLiteral("lastWorkspace"), root);
    writeJson(settingsPath(), settings);
}

} // namespace

Workspace::Workspace(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}

Workspace::~Workspace()
{
    ++m_generation;
    m_pool.waitForDone();
}

QString Workspace::rootName() const
{
    return QFileInfo(m_root).fileName();
}

bool Workspace::open(const QUrl &folder)
{
    const QString path = QDir::cleanPath(folder.isLocalFile() ? folder.toLocalFile() : folder.toString());
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return false;
    m_root = path;
    m_files.clear();
    rememberRoot(m_root);
    emit rootChanged();
    emit filesChanged();
    rescan();
    return true;
}

void Workspace::close()
{
    if (m_root.isEmpty())
        return;
    ++m_generation;
    m_root.clear();
    m_files.clear();
    rememberRoot({});
    setScanning(false);
    emit rootChanged();
    emit filesChanged();
}

void Workspace::setScanning(bool scanning)
{
    if (scanning != m_scanning) {
        m_scanning = scanning;
        emit scanningChanged();
    }
}

void Workspace::rescan()
{
    if (m_root.isEmpty())
        return;
    const int generation = ++m_generation;
    setScanning(true);
    m_pool.start([this, generation, root = m_root] {
        QStringList files = scanFiles(root, [&] { return m_generation != generation; });
        QMetaObject::invokeMethod(this, [this, generation, files = std::move(files)] {
            if (m_generation != generation)
                return;
            m_files = files;
            setScanning(false);
            emit filesChanged();
        }, Qt::QueuedConnection);
    });
}

QStringList Workspace::scanFiles(const QString &root, const std::function<bool()> &cancelled, int limit)
{
    const IgnoreRules rules(root);
    const QDir base(root);
    QStringList files;
    QStringList pending{root};
    while (!pending.isEmpty() && files.size() < limit) {
        if (cancelled && cancelled())
            return {};
        QDirIterator it(pending.takeLast(), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        while (it.hasNext()) {
            const QFileInfo info = it.nextFileInfo();
            const bool isDir = info.isDir();
            if ((isDir && info.isSymLink()) || rules.isIgnored(info.filePath(), isDir))
                continue;
            if (isDir)
                pending.append(info.filePath());
            else
                files.append(base.relativeFilePath(info.filePath()));
        }
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

int Workspace::fuzzyScore(QStringView query, QStringView path)
{
    if (query.isEmpty())
        return 0;
    // Жадно слева направо: бонус за подряд идущие символы и начала слов
    auto matchFrom = [&](qsizetype from) {
        int score = 0;
        qsizetype q = 0, previous = -2;
        for (qsizetype i = from; i < path.size() && q < query.size(); ++i) {
            if (path.at(i).toLower() != query.at(q).toLower())
                continue;
            const QChar before = i > 0 ? path.at(i - 1) : QChar(u'/');
            score += 1;
            if (i == previous + 1)
                score += 5;
            if (QStringView(u"/\\_-. ").contains(before) || (before.isLower() && path.at(i).isUpper()))
                score += 4;
            previous = i;
            ++q;
        }
        return q == query.size() ? score : -1;
    };
    // Совпадение в имени файла важнее совпадения в папках
    const qsizetype nameStart = path.lastIndexOf(u'/') + 1;
    if (const int inName = matchFrom(nameStart); inName >= 0)
        return 1000 + inName * 4 - int(path.size() - nameStart);
    const int anywhere = matchFrom(0);
    return anywhere < 0 ? -1 : anywhere * 2 - int(path.size()) / 4;
}

QVariantList Workspace::findFiles(const QString &query, int limit) const
{
    const QString needle = QString(query).remove(u' ');
    QVector<QPair<int, int>> scored; // {оценка, индекс файла}
    for (int i = 0; i < m_files.size(); ++i)
        if (const int score = fuzzyScore(needle, m_files.at(i)); score >= 0)
            scored.append({score, i});
    const auto top = scored.begin() + qMin<qsizetype>(limit, scored.size());
    std::partial_sort(scored.begin(), top, scored.end(), [&](const auto &a, const auto &b) {
        return a.first != b.first ? a.first > b.first : m_files.at(a.second) < m_files.at(b.second);
    });

    QVariantList out;
    for (auto it = scored.begin(); it != top; ++it) {
        const QString &relative = m_files.at(it->second);
        out.append(QVariantMap{{QStringLiteral("path"), m_root + u'/' + relative},
                               {QStringLiteral("relative"), relative},
                               {QStringLiteral("name"), relative.mid(relative.lastIndexOf(u'/') + 1)}});
    }
    return out;
}

QString Workspace::relativePath(const QString &path) const
{
    return m_root.isEmpty() ? path : QDir(m_root).relativeFilePath(path);
}

QUrl Workspace::lastRoot() const
{
    const QString root = readJson(settingsPath()).value(QStringLiteral("lastWorkspace")).toString();
    return !root.isEmpty() && QFileInfo(root).isDir() ? QUrl::fromLocalFile(root) : QUrl();
}

QString Workspace::sessionPath() const
{
    const QByteArray key = QCryptographicHash::hash(m_root.toLower().toUtf8(), QCryptographicHash::Sha1);
    return dataDir() + QStringLiteral("/sessions/") + QString::fromLatin1(key.toHex()) + QStringLiteral(".json");
}

QVariantMap Workspace::loadSession() const
{
    return m_root.isEmpty() ? QVariantMap() : readJson(sessionPath()).toVariantMap();
}

void Workspace::saveSession(const QVariantMap &session) const
{
    if (!m_root.isEmpty())
        writeJson(sessionPath(), QJsonObject::fromVariantMap(session));
}
