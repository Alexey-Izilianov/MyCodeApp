#include "gitservice.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>

#include "src/core/document.h"
#include "src/gui/editerview.h"
#include "src/services/documentmanager.h"

namespace {

constexpr int kMaxListed = 5000;          // строк в панели на каждый список
constexpr int kMaxMarkedChars = 4000000;  // больше — полоски изменений не считаем
constexpr int kMinPollMs = 3000;

QString key(const QString &path)
{
    return QDir::cleanPath(path).toLower();
}

// Байты из git/с диска -> текст с '\n' (кодировка определяется как при открытии файла)
QString decode(const QByteArray &bytes)
{
    core::TextBuffer buffer;
    buffer.loadFromData(bytes);
    return buffer.snapshot().toString();
}

QChar treeLetter(const git::FileStatus &s)
{
    if (s.conflict)
        return u'C';
    switch (s.worktree) {
    case '?': return u'U';
    case 'M': case 'T': return u'M';
    case 'D': return u'D';
    default: break;
    }
    switch (s.index) {
    case 'T': return u'M';
    case 0: return {};
    default: return QChar::fromLatin1(s.index);
    }
}

int priority(QChar letter)
{
    switch (letter.unicode()) {
    case u'C': return 4;
    case u'M': return 3;
    case u'D': return 2;
    default: return letter.isNull() ? 0 : 1;
    }
}

} // namespace

struct GitService::Snapshot {
    QString workdir;
    QString gitDir;
    QString branch;
    QStringList branches;
    QVector<git::FileStatus> files;
};

GitService::GitService(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1); // операции git строго по очереди
    m_refreshDelay.setSingleShot(true);
    m_refreshDelay.setInterval(300);
    connect(&m_refreshDelay, &QTimer::timeout, this, &GitService::refresh);
    connect(&m_gitWatcher, &QFileSystemWatcher::fileChanged, &m_refreshDelay, qOverload<>(&QTimer::start));
    connect(&m_gitWatcher, &QFileSystemWatcher::directoryChanged, &m_refreshDelay, qOverload<>(&QTimer::start));
    // Правки рабочей копии другими программами не отслеживаются — опрос, пока окно активно
    connect(&m_poll, &QTimer::timeout, this, [this] {
        if (QGuiApplication::applicationState() == Qt::ApplicationActive)
            refresh();
    });
}

GitService::~GitService()
{
    m_pool.clear();
    m_pool.waitForDone();
}

void GitService::setRootPath(const QString &path)
{
    if (path == m_root)
        return;
    m_root = path;
    ++m_generation;
    m_refreshRunning = m_refreshQueued = false;
    applySnapshot({});
    emit rootPathChanged();
    refresh();
}

void GitService::run(Job job, std::function<void()> done)
{
    if (m_root.isEmpty())
        return;
    const QString root = m_root;
    const int generation = m_generation;
    m_pool.start([this, root, generation, job = std::move(job), done = std::move(done)] {
        if (const auto repo = git::Repository::discover(root))
            job(*repo);
        QMetaObject::invokeMethod(this, [this, generation, done] {
            if (generation == m_generation && done)
                done();
        }, Qt::QueuedConnection);
    });
}

void GitService::runOperation(std::function<bool(git::Repository &, QString *)> operation,
                              std::function<void()> onSuccess)
{
    if (!isAvailable())
        return;
    struct Result {
        bool ok = false;
        QString error;
    };
    auto result = std::make_shared<Result>();
    ++m_running;
    emit busyChanged();
    run([result, operation = std::move(operation)](git::Repository &repo) {
        result->ok = operation(repo, &result->error);
    }, [this, result, onSuccess = std::move(onSuccess)] {
        --m_running;
        emit busyChanged();
        if (!result->ok)
            emit message(result->error);
        else if (onSuccess)
            onSuccess();
        refresh();
    });
}

void GitService::refresh()
{
    if (m_root.isEmpty())
        return;
    if (m_refreshRunning) {
        m_refreshQueued = true;
        return;
    }
    m_refreshRunning = true;
    auto snapshot = std::make_shared<Snapshot>();
    auto elapsed = std::make_shared<qint64>(0);
    run([snapshot, elapsed](git::Repository &repo) {
        QElapsedTimer timer;
        timer.start();
        snapshot->workdir = repo.workdir();
        snapshot->gitDir = repo.gitDir();
        snapshot->branch = repo.branch();
        snapshot->branches = repo.branches();
        snapshot->files = repo.status();
        *elapsed = timer.elapsed();
    }, [this, snapshot, elapsed] {
        m_refreshRunning = false;
        m_lastRefreshMs = *elapsed;
        applySnapshot(*snapshot);
        if (m_refreshQueued) {
            m_refreshQueued = false;
            refresh();
        }
    });
}

void GitService::applySnapshot(const Snapshot &snapshot)
{
    QStringList touched = m_decorated; // пути, у которых могла поменяться буква в дереве
    m_decorated.clear();

    m_workdir = snapshot.workdir;
    m_branch = snapshot.branch;
    m_branches = snapshot.branches;
    m_fileMarks.clear();
    m_dirMarks.clear();
    m_changes.clear();
    m_staged.clear();

    const QString root = QDir::cleanPath(m_workdir);
    for (const git::FileStatus &s : snapshot.files) {
        const QString path = m_workdir + s.path;
        const QChar letter = treeLetter(s);
        if (!letter.isNull()) {
            m_fileMarks.insert(key(path), letter);
            m_decorated.append(path);
            // Папки до корня рабочей копии — по самому важному изменению внутри
            for (QString dir = QFileInfo(path).path(); dir.size() >= root.size(); dir = QFileInfo(dir).path()) {
                QChar &mark = m_dirMarks[key(dir)];
                if (priority(letter) <= priority(mark))
                    break;
                mark = letter;
                m_decorated.append(dir);
                if (dir.size() == root.size())
                    break;
            }
        }

        const int slash = int(s.path.lastIndexOf(u'/'));
        QVariantMap entry{{QStringLiteral("path"), path},
                          {QStringLiteral("relative"), s.path},
                          {QStringLiteral("name"), s.path.mid(slash + 1)},
                          {QStringLiteral("folder"), slash < 0 ? QString() : s.path.left(slash)}};
        if ((s.worktree || s.conflict) && m_changes.size() < kMaxListed) {
            entry.insert(QStringLiteral("status"),
                         QString(s.conflict ? QChar(u'C') : s.worktree == '?' ? QChar(u'U') : QChar::fromLatin1(s.worktree)));
            m_changes.append(entry);
        }
        if (s.index && !s.conflict && m_staged.size() < kMaxListed) {
            entry.insert(QStringLiteral("status"), QString(QChar::fromLatin1(s.index)));
            m_staged.append(entry);
        }
    }

    watchGitDir(snapshot.gitDir);
    m_poll.setInterval(int(qMax<qint64>(kMinPollMs, m_lastRefreshMs * 10)));
    if (isAvailable())
        m_poll.start();
    else
        m_poll.stop();

    touched += m_decorated;
    touched.removeDuplicates();
    emit decorationsChanged(touched);
    emit stateChanged();
}

void GitService::watchGitDir(const QString &gitDir)
{
    // Атомарная замена index/HEAD снимает слежение — добавляем пути заново
    if (!m_gitWatcher.files().isEmpty())
        m_gitWatcher.removePaths(m_gitWatcher.files());
    if (!m_gitWatcher.directories().isEmpty())
        m_gitWatcher.removePaths(m_gitWatcher.directories());
    if (gitDir.isEmpty())
        return;
    QStringList paths;
    for (const char *name : {"index", "HEAD", "refs/heads"})
        if (QFileInfo::exists(gitDir + QLatin1String(name)))
            paths.append(gitDir + QLatin1String(name));
    if (!paths.isEmpty())
        m_gitWatcher.addPaths(paths);
}

QString GitService::decorationOf(const QString &path, bool isDir) const
{
    const QChar letter = (isDir ? m_dirMarks : m_fileMarks).value(key(path));
    return letter.isNull() ? QString() : QString(letter);
}

QString GitService::relative(const QString &path) const
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (m_workdir.isEmpty() || !clean.startsWith(m_workdir, Qt::CaseInsensitive))
        return {};
    return clean.mid(m_workdir.size());
}

core::Document *GitService::openDocument(const QString &path) const
{
    if (!m_documents)
        return nullptr;
    const QString wanted = key(path);
    for (core::Document *doc : m_documents->documents())
        if (key(doc->filePath()) == wanted)
            return doc;
    return nullptr;
}

void GitService::stage(const QStringList &paths)
{
    QStringList relatives;
    for (const QString &path : paths)
        if (const QString r = relative(path); !r.isEmpty())
            relatives.append(r);
    runOperation([relatives](git::Repository &repo, QString *error) { return repo.stage(relatives, error); });
}

void GitService::unstage(const QStringList &paths)
{
    QStringList relatives;
    for (const QString &path : paths)
        if (const QString r = relative(path); !r.isEmpty())
            relatives.append(r);
    runOperation([relatives](git::Repository &repo, QString *error) { return repo.unstage(relatives, error); });
}

void GitService::stageAll()
{
    QStringList paths;
    for (const QVariant &entry : std::as_const(m_changes))
        paths.append(entry.toMap().value(QStringLiteral("path")).toString());
    stage(paths);
}

void GitService::unstageAll()
{
    QStringList paths;
    for (const QVariant &entry : std::as_const(m_staged))
        paths.append(entry.toMap().value(QStringLiteral("path")).toString());
    unstage(paths);
}

void GitService::discard(const QStringList &paths)
{
    QStringList relatives;
    for (const QString &path : paths)
        if (const QString r = relative(path); !r.isEmpty())
            relatives.append(r);
    runOperation([relatives](git::Repository &repo, QString *error) { return repo.discard(relatives, error); });
}

void GitService::commit(const QString &message)
{
    const QString text = message.trimmed();
    if (text.isEmpty()) {
        emit this->message(tr("Введите сообщение коммита"));
        return;
    }
    if (m_staged.isEmpty()) {
        emit this->message(tr("Нет изменений в индексе: добавьте файлы кнопкой «+»"));
        return;
    }
    runOperation([text](git::Repository &repo, QString *error) { return repo.commit(text, error); },
                 [this] { emit committed(); });
}

void GitService::checkout(const QString &branch)
{
    if (branch == m_branch)
        return;
    runOperation([branch](git::Repository &repo, QString *error) { return repo.checkout(branch, error); });
}

void GitService::createBranch(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return;
    runOperation([trimmed](git::Repository &repo, QString *error) { return repo.createBranch(trimmed, error); });
}

void GitService::initRepository()
{
    if (m_root.isEmpty() || isAvailable())
        return;
    const QString root = m_root;
    const int generation = m_generation;
    m_pool.start([this, root, generation] {
        QString error;
        const bool ok = git::Repository::init(root, &error);
        QMetaObject::invokeMethod(this, [this, generation, ok, error] {
            if (generation != m_generation)
                return;
            if (!ok)
                emit message(error);
            refresh();
        }, Qt::QueuedConnection);
    });
}

void GitService::updateMarks(EditorView *editor)
{
    if (!editor)
        return;
    auto *doc = qobject_cast<core::Document *>(editor->document());
    if (!doc)
        return;
    const QString path = relative(doc->filePath());
    if (path.isEmpty() || doc->isReadOnly() || doc->buffer().length() > kMaxMarkedChars) {
        editor->setChangeMarks(doc, {});
        return;
    }
    const auto text = doc->buffer().snapshot();
    const int request = ++m_marksRequest;
    auto marks = std::make_shared<QVector<EditorView::ChangeMark>>();
    run([path, text, marks](git::Repository &repo) {
        const auto base = repo.indexBlob(path);
        if (!base)
            return; // файла нет в индексе — новый, полосок нет
        for (const git::Hunk &h : git::diffLines(decode(*base).toUtf8(), text.toString().toUtf8())) {
            using Mark = EditorView::ChangeMark;
            if (h.newCount == 0)
                marks->append({h.newStart, 0, Mark::Removed});
            else
                marks->append({h.newStart, h.newCount, h.oldCount == 0 ? Mark::Added : Mark::Modified});
        }
    }, [this, request, marks, editor = QPointer<EditorView>(editor), doc = QPointer<core::Document>(doc)] {
        if (request == m_marksRequest && editor && doc)
            editor->setChangeMarks(doc, *marks);
    });
}

void GitService::showDiff(const QString &path, bool staged, EditorView *target)
{
    const QString rel = relative(path);
    if (rel.isEmpty() || !target)
        return;
    std::optional<core::PieceTable::Snapshot> current;
    if (!staged)
        if (core::Document *doc = openDocument(path))
            current = doc->buffer().snapshot();

    struct Result {
        core::TextBuffer buffer;
        QVector<qint8> kinds;
        QVector<int> numbers;
        int changes = 0;
    };
    auto result = std::make_shared<Result>();
    const int request = ++m_diffRequest;
    ++m_running;
    emit busyChanged();
    run([rel, staged, current, result](git::Repository &repo) {
        const QString oldText = decode((staged ? repo.headBlob(rel) : repo.indexBlob(rel)).value_or(QByteArray()));
        QString newText;
        if (staged) {
            newText = decode(repo.indexBlob(rel).value_or(QByteArray()));
        } else if (current) {
            newText = current->toString();
        } else {
            QFile file(repo.workdir() + rel);
            if (file.open(QIODevice::ReadOnly))
                newText = decode(file.readAll()); // удалённый файл — пустой текст
        }
        const QStringList oldLines = oldText.split(u'\n');
        const QStringList newLines = newText.split(u'\n');

        // Один текст: новые строки, а перед изменёнными — удалённые старые
        QString merged;
        merged.reserve(oldText.size() + newText.size());
        auto add = [&](const QString &line, qint8 kind, int number) {
            if (!result->kinds.isEmpty())
                merged += u'\n';
            merged += line;
            result->kinds.append(kind);
            result->numbers.append(number);
        };
        int next = 0;
        for (const git::Hunk &h : git::diffLines(oldText.toUtf8(), newText.toUtf8())) {
            for (; next < h.newStart && next < newLines.size(); ++next)
                add(newLines.at(next), EditorView::Unchanged, next);
            for (int i = 0; i < h.oldCount && h.oldStart + i < oldLines.size(); ++i)
                add(oldLines.at(h.oldStart + i), EditorView::Deleted, -1);
            for (int i = 0; i < h.newCount && h.newStart + i < newLines.size(); ++i)
                add(newLines.at(h.newStart + i), EditorView::Inserted, h.newStart + i);
            next = h.newStart + h.newCount;
            ++result->changes;
        }
        for (; next < newLines.size(); ++next)
            add(newLines.at(next), EditorView::Unchanged, next);
        result->buffer.loadFromData(merged.toUtf8()); // разбор строк — тоже в фоне
    }, [this, path, staged, request, result, target = QPointer<EditorView>(target)] {
        --m_running;
        emit busyChanged();
        if (request != m_diffRequest || !target)
            return;
        auto *doc = new core::Document(this);
        doc->loadVirtual(std::move(result->buffer), path);
        target->setDiffLines(doc, std::move(result->kinds), std::move(result->numbers));
        target->setDocument(doc);
        if (m_diffDocument)
            m_diffDocument->deleteLater();
        m_diffDocument = doc;
        const QString name = QFileInfo(path).fileName();
        emit diffReady(staged ? tr("%1 — индекс и HEAD").arg(name) : tr("%1 — изменения").arg(name),
                       result->changes);
    });
}
