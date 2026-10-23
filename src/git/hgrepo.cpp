#include "hgrepo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTimeZone>
#include <QtEndian>
#include <algorithm>
#include <cctype>

namespace hg {

namespace {

constexpr int kTimeoutMs = 120000;
const QString kNullNode(40, u'0');

QString tr(const char *text)
{
    return QCoreApplication::translate("hg", text);
}

} // namespace

// Командный сервер hg (hg serve --cmdserver pipe): один процесс на репозиторий,
// команды по протоколу каналов (o — вывод, e — ошибки, r — код возврата)
struct Session {
    QProcess process;
    bool utf8 = true;          // кодировка сервера; иначе — локальная (Windows: cp1251)
    QSet<QString> staged;      // «индекс»: файлы следующего коммита

    ~Session()
    {
        if (process.state() == QProcess::NotRunning)
            return;
        process.closeWriteChannel(); // конец ввода — сервер завершается сам
        if (!process.waitForFinished(2000)) {
            process.kill();
            process.waitForFinished(2000);
        }
    }

    QByteArray encode(const QString &text) const { return utf8 ? text.toUtf8() : text.toLocal8Bit(); }
    QString decode(const QByteArray &bytes) const
    {
        return utf8 ? QString::fromUtf8(bytes) : QString::fromLocal8Bit(bytes);
    }

    bool readExactly(char *data, qint64 size)
    {
        while (size > 0) {
            if (process.bytesAvailable() == 0 && !process.waitForReadyRead(kTimeoutMs))
                return false;
            const qint64 got = process.read(data, size);
            if (got < 0)
                return false;
            data += got;
            size -= got;
        }
        return true;
    }
    // Сообщение сервера: канал и данные (у I/L — запрошенный размер ввода)
    bool readMessage(char *channel, QByteArray *data, quint32 *length)
    {
        char header[5];
        if (!readExactly(header, 5))
            return false;
        *channel = header[0];
        *length = qFromBigEndian<quint32>(header + 1);
        if (*channel == 'I' || *channel == 'L')
            return true;
        data->resize(*length);
        return readExactly(data->data(), *length);
    }

    bool start(const QString &root)
    {
        const QString hg = Repository::executable();
        if (hg.isEmpty())
            return false;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("HGPLAIN"), QStringLiteral("1")); // вывод без локализации и алиасов
        process.setProcessEnvironment(env);
        process.setWorkingDirectory(root);
        process.start(hg, {QStringLiteral("serve"), QStringLiteral("--cmdserver"), QStringLiteral("pipe"),
                           QStringLiteral("--config"), QStringLiteral("ui.interactive=False")});
        if (!process.waitForStarted(kTimeoutMs))
            return false;
        char channel = 0;
        QByteArray hello;
        quint32 length = 0;
        if (!readMessage(&channel, &hello, &length) || channel != 'o')
            return false;
        utf8 = hello.contains("encoding: UTF-8") || hello.contains("encoding: utf-8");
        return true;
    }
};

namespace {

// Сессии живут в потоке, который их создал: QProcess нельзя переносить между потоками
thread_local QHash<QString, std::shared_ptr<Session>> t_sessions;

std::shared_ptr<Session> sessionFor(const QString &root)
{
    auto &session = t_sessions[root];
    if (session && session->process.state() == QProcess::Running)
        return session;
    session = std::make_shared<Session>();
    if (!session->start(root)) {
        t_sessions.remove(root);
        return nullptr;
    }
    return session;
}

QString relativePath(const QString &path)
{
    return QDir::fromNativeSeparators(path);
}

} // namespace

Repository::Repository(QString root, std::shared_ptr<Session> session)
    : m_root(std::move(root)), m_session(std::move(session))
{
}

Repository::~Repository() = default;

QString Repository::executable()
{
    static const QString path = [] {
        QString found = QStandardPaths::findExecutable(QStringLiteral("hg"));
#ifdef Q_OS_WIN
        // Установщик Mercurial/TortoiseHg дописывает PATH, но уже запущенная программа его не видит
        for (const QString candidate : {QStringLiteral("C:/Program Files/Mercurial/hg.exe"),
                                        QStringLiteral("C:/Program Files/TortoiseHg/hg.exe")})
            if (found.isEmpty() && QFileInfo::exists(candidate))
                found = candidate;
#endif
        return found;
    }();
    return path;
}

void Repository::closeSessions()
{
    t_sessions.clear();
}

std::unique_ptr<Repository> Repository::open(const QString &root)
{
    QString clean = QDir::fromNativeSeparators(QDir::cleanPath(root));
    if (!clean.endsWith(u'/'))
        clean += u'/';
    auto session = sessionFor(clean);
    if (!session)
        return nullptr;
    return std::unique_ptr<Repository>(new Repository(clean, std::move(session)));
}

bool Repository::init(const QString &path, QString *error)
{
    if (executable().isEmpty()) {
        if (error)
            *error = tr("Mercurial не установлен (нет программы hg)");
        return false;
    }
    QProcess process;
    process.setWorkingDirectory(path);
    process.start(executable(), {QStringLiteral("init")});
    process.waitForFinished(kTimeoutMs);
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0)
        return true;
    if (error)
        *error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    return false;
}

Repository::Result Repository::run(const QStringList &args) const
{
    Result result;
    Session &s = *m_session;
    const QByteArray data = s.encode(args.join(QChar(u'\0')));
    QByteArray message("runcommand\n");
    char length[4];
    qToBigEndian<quint32>(quint32(data.size()), length);
    message.append(length, 4);
    message.append(data);
    if (s.process.write(message) != message.size()) {
        result.err = "hg: write failed";
        return result;
    }
    for (;;) {
        char channel = 0;
        QByteArray payload;
        quint32 size = 0;
        if (!s.readMessage(&channel, &payload, &size)) {
            s.process.kill(); // сервер завис или упал — следующий вызов поднимет новый
            result.err = "hg: no answer from command server";
            return result;
        }
        switch (channel) {
        case 'o': result.out += payload; break;
        case 'e': result.err += payload; break;
        case 'r': result.code = payload.size() == 4 ? qFromBigEndian<qint32>(payload.constData()) : -1; return result;
        case 'I': case 'L': { // ввода нет: пустой ответ (как Ctrl+D)
            const char zero[4] = {};
            s.process.write(zero, 4);
            break;
        }
        default:
            if (std::isupper(uchar(channel))) { // обязательный канал, которого мы не знаем
                s.process.kill();
                return result;
            }
            break;
        }
    }
}

bool Repository::runOrFail(const QStringList &args, QString *error) const
{
    const Result r = run(args);
    if (r.ok())
        return true;
    if (error) {
        *error = m_session->decode(r.err).trimmed();
        if (error->isEmpty())
            *error = m_session->decode(r.out).trimmed();
    }
    return false;
}

QString Repository::workdir() const
{
    return m_root;
}

QStringList Repository::watchPaths() const
{
    QStringList paths;
    for (const char *name : {"dirstate", "bookmarks", "bookmarks.current", "branch", "store/00changelog.i"})
        paths.append(m_root + QLatin1String(".hg/") + QLatin1String(name));
    return paths;
}

QString Repository::branch() const
{
    const QStringList parts = m_session->decode(run({QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("."),
                                                     QStringLiteral("-T"), QStringLiteral("{activebookmark}\n{branch}")})
                                                    .out).split(u'\n');
    if (parts.size() < 2)
        return {};
    return parts.at(0).isEmpty() ? parts.at(1) : parts.at(0);
}

QStringList Repository::branches() const
{
    QStringList names;
    for (const QStringList &args : {QStringList{QStringLiteral("bookmarks"), QStringLiteral("-T"), QStringLiteral("{bookmark}\n")},
                                    QStringList{QStringLiteral("branches"), QStringLiteral("-T"), QStringLiteral("{branch}\n")}})
        for (const QString &name : m_session->decode(run(args).out).split(u'\n', Qt::SkipEmptyParts))
            if (!names.contains(name))
                names.append(name);
    names.sort(Qt::CaseInsensitive);
    return names;
}

QHash<QString, char> Repository::rawStatus() const
{
    QHash<QString, char> files;
    const QString out = m_session->decode(run({QStringLiteral("status"), QStringLiteral("-T"),
                                               QStringLiteral("{status} {path}\n")}).out);
    for (const QString &line : out.split(u'\n', Qt::SkipEmptyParts))
        if (line.size() > 2)
            files.insert(relativePath(line.mid(2)), line.at(0).toLatin1());
    return files;
}

QVector<vcs::FileStatus> Repository::status() const
{
    QVector<vcs::FileStatus> result;
    const QHash<QString, char> files = rawStatus();
    QSet<QString> &staged = m_session->staged;
    for (auto it = staged.begin(); it != staged.end();) // закоммичено или отменено снаружи
        it = files.contains(*it) ? std::next(it) : staged.erase(it);
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        vcs::FileStatus s;
        s.path = it.key();
        const bool inIndex = staged.contains(it.key());
        switch (it.value()) {
        case 'A': s.index = 'A'; break;          // hg add снаружи — уже «в индексе»
        case 'R': s.index = 'D'; break;          // hg remove снаружи
        case 'M': (inIndex ? s.index : s.worktree) = 'M'; break;
        case '!': (inIndex ? s.index : s.worktree) = 'D'; break;
        case '?': inIndex ? void(s.index = 'A') : void(s.worktree = '?'); break;
        default: continue;
        }
        result.append(s);
    }
    return result;
}

std::optional<QByteArray> Repository::headBlob(const QString &path) const
{
    return fileAt(QStringLiteral("."), path);
}

std::optional<QByteArray> Repository::indexBlob(const QString &path) const
{
    // В «индексе» лежит рабочая версия файла, иначе — версия последнего коммита
    const char state = rawStatus().value(path, 'C');
    if (m_session->staged.contains(path) || state == 'A') {
        QFile file(m_root + path);
        if (!file.open(QIODevice::ReadOnly))
            return std::nullopt;
        return file.readAll();
    }
    return headBlob(path);
}

bool Repository::stage(const QStringList &paths, QString *)
{
    for (const QString &path : paths)
        m_session->staged.insert(path);
    return true;
}

bool Repository::unstage(const QStringList &paths, QString *error)
{
    const QHash<QString, char> files = rawStatus();
    QStringList forget, restore;
    for (const QString &path : paths) {
        m_session->staged.remove(path);
        if (files.value(path) == 'A')
            forget.append(path); // добавлен снаружи: снова неотслеживаемый
        else if (files.value(path) == 'R')
            restore.append(path); // удалён через hg remove: вернуть под контроль
    }
    if (!forget.isEmpty() && !runOrFail(QStringList{QStringLiteral("forget"), QStringLiteral("--")} + forget, error))
        return false;
    return restore.isEmpty()
           || runOrFail(QStringList{QStringLiteral("revert"), QStringLiteral("--no-backup"), QStringLiteral("--")} + restore, error);
}

bool Repository::discard(const QStringList &paths, QString *error)
{
    const QHash<QString, char> files = rawStatus();
    QStringList tracked;
    for (const QString &path : paths) {
        m_session->staged.remove(path);
        if (files.value(path) == '?') {
            if (!QFile::remove(m_root + path)) {
                if (error)
                    *error = tr("Не удалось удалить %1").arg(path);
                return false;
            }
        } else {
            tracked.append(path);
        }
    }
    return tracked.isEmpty()
           || runOrFail(QStringList{QStringLiteral("revert"), QStringLiteral("--no-backup"), QStringLiteral("--")} + tracked, error);
}

bool Repository::commit(const QString &message, QString *error)
{
    const QHash<QString, char> files = rawStatus();
    QStringList paths;
    for (auto it = files.cbegin(); it != files.cend(); ++it)
        if (m_session->staged.contains(it.key()) || it.value() == 'A' || it.value() == 'R')
            paths.append(it.key());
    if (paths.isEmpty()) {
        if (error)
            *error = tr("Нет файлов для коммита: отметьте их кнопкой «+»");
        return false;
    }
    // --addremove: новые файлы из списка добавляются, пропавшие — удаляются
    if (!runOrFail(QStringList{QStringLiteral("commit"), QStringLiteral("--addremove"), QStringLiteral("-m"), message,
                               QStringLiteral("--")} + paths, error))
        return false;
    m_session->staged.clear();
    return true;
}

bool Repository::checkout(const QString &branch, QString *error)
{
    // --check: при незакоммиченных правках — ошибка, а не слияние с ними
    return runOrFail({QStringLiteral("update"), QStringLiteral("--check"), QStringLiteral("--"), branch}, error);
}

bool Repository::createBranch(const QString &name, QString *error)
{
    // Закладка — лёгкая ветка в духе git; именованные ветки hg постоянны
    return runOrFail({QStringLiteral("bookmark"), QStringLiteral("--"), name}, error);
}

QVector<vcs::Commit> Repository::log(int limit) const
{
    QVector<vcs::Commit> commits;
    const QString current = m_session->decode(run({QStringLiteral("log"), QStringLiteral("-r"), QStringLiteral("."),
                                                   QStringLiteral("-T"), QStringLiteral("{node}\n{activebookmark}")})
                                                  .out);
    const QString currentNode = current.section(u'\n', 0, 0);
    const QString activeBookmark = current.section(u'\n', 1, 1);

    // Поля через \x1f, записи через \x1e; номера ревизий hg уже упорядочены топологически
    const QString tmpl = QStringLiteral("{node}\x1f{p1.node} {p2.node}\x1f{author}\x1f{date|hgdate}\x1f"
                                        "{branch}\x1f{bookmarks}\x1f{tags}\x1f{desc}\x1e");
    const QString out = m_session->decode(run({QStringLiteral("log"), QStringLiteral("-l"), QString::number(limit),
                                               QStringLiteral("-T"), tmpl}).out);
    QSet<QString> branchesSeen;
    for (const QString &record : out.split(u'\x1e', Qt::SkipEmptyParts)) {
        const QStringList f = record.split(u'\x1f');
        if (f.size() < 8)
            continue;
        vcs::Commit c;
        c.id = f.at(0).trimmed();
        for (const QString &parent : f.at(1).split(u' ', Qt::SkipEmptyParts))
            if (parent != kNullNode)
                c.parents.append(parent);
        const QString author = f.at(2);
        const qsizetype lt = author.indexOf(u'<');
        c.author = (lt > 0 ? author.left(lt) : author).trimmed();
        c.email = lt > 0 ? author.mid(lt + 1).chopped(author.endsWith(u'>') ? 1 : 0).trimmed() : QString();
        const QStringList date = f.at(3).split(u' ');
        if (date.size() == 2)
            c.time = QDateTime::fromSecsSinceEpoch(date.at(0).toLongLong(),
                                                   QTimeZone::fromSecondsAheadOfUtc(-date.at(1).toInt()));
        c.branch = f.at(4);
        c.message = f.mid(7).join(u'\x1f').trimmed();
        c.summary = c.message.section(u'\n', 0, 0);
        // Метки: имя ветки — у её самого нового коммита, закладки, теги (кроме tip)
        if (!branchesSeen.contains(c.branch)) {
            branchesSeen.insert(c.branch);
            c.refs.append({c.branch, vcs::Ref::Branch, c.id == currentNode && activeBookmark.isEmpty()});
        }
        for (const QString &bookmark : f.at(5).split(u' ', Qt::SkipEmptyParts))
            c.refs.append({bookmark, vcs::Ref::Bookmark, bookmark == activeBookmark});
        for (const QString &tag : f.at(6).split(u' ', Qt::SkipEmptyParts))
            if (tag != QLatin1String("tip"))
                c.refs.append({tag, vcs::Ref::Tag, false});
        if (c.id == currentNode && std::none_of(c.refs.cbegin(), c.refs.cend(), [](const vcs::Ref &r) { return r.current; }))
            c.refs.prepend({QStringLiteral("HEAD"), vcs::Ref::Branch, true});
        commits.append(std::move(c));
    }
    return commits;
}

QVector<vcs::CommitFile> Repository::commitFiles(const QString &id) const
{
    QVector<vcs::CommitFile> files;
    const QString out = m_session->decode(run({QStringLiteral("status"), QStringLiteral("--change"), id,
                                               QStringLiteral("-T"), QStringLiteral("{status} {path}\n")}).out);
    for (const QString &line : out.split(u'\n', Qt::SkipEmptyParts)) {
        if (line.size() < 3)
            continue;
        const char status = line.at(0).toLatin1() == 'R' ? 'D' : line.at(0).toLatin1();
        files.append({relativePath(line.mid(2)), status});
    }
    return files;
}

std::optional<QByteArray> Repository::fileAt(const QString &id, const QString &path) const
{
    const Result r = run({QStringLiteral("cat"), QStringLiteral("-r"), id, QStringLiteral("--"), path});
    if (!r.ok())
        return std::nullopt;
    return r.out;
}

bool Repository::checkoutCommit(const QString &id, QString *error)
{
    return runOrFail({QStringLiteral("update"), QStringLiteral("--check"), QStringLiteral("-r"), id}, error);
}

bool Repository::createBranchAt(const QString &name, const QString &id, QString *error)
{
    return checkoutCommit(id, error) && createBranch(name, error);
}

} // namespace hg
