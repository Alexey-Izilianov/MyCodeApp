#include "gitrepo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTimeZone>
#include <git2.h>
#include <vector>

namespace git {

namespace {

struct Free {
    void operator()(git_blob *p) const { git_blob_free(p); }
    void operator()(git_commit *p) const { git_commit_free(p); }
    void operator()(git_diff *p) const { git_diff_free(p); }
    void operator()(git_reference_iterator *p) const { git_reference_iterator_free(p); }
    void operator()(git_revwalk *p) const { git_revwalk_free(p); }
    void operator()(git_index *p) const { git_index_free(p); }
    void operator()(git_object *p) const { git_object_free(p); }
    void operator()(git_reference *p) const { git_reference_free(p); }
    void operator()(git_branch_iterator *p) const { git_branch_iterator_free(p); }
    void operator()(git_signature *p) const { git_signature_free(p); }
    void operator()(git_status_list *p) const { git_status_list_free(p); }
    void operator()(git_tree *p) const { git_tree_free(p); }
    void operator()(git_tree_entry *p) const { git_tree_entry_free(p); }
};
template <class T>
using Owned = std::unique_ptr<T, Free>;

QString lastError()
{
    const git_error *e = git_error_last();
    return e && e->message ? QString::fromUtf8(e->message) : QCoreApplication::translate("git", "Неизвестная ошибка git");
}

bool fail(QString *error, const QString &message = {})
{
    if (error)
        *error = message.isEmpty() ? lastError() : message;
    return false;
}

// Список путей для libgit2: строки живут, пока жив объект
class PathList {
public:
    explicit PathList(const QStringList &paths)
    {
        for (const QString &p : paths)
            m_bytes.push_back(p.toUtf8());
        for (QByteArray &b : m_bytes)
            m_pointers.push_back(b.data());
        m_array = {m_pointers.data(), m_pointers.size()};
    }
    const git_strarray *get() const { return &m_array; }

private:
    std::vector<QByteArray> m_bytes;
    std::vector<char *> m_pointers;
    git_strarray m_array{};
};

Owned<git_commit> headCommit(git_repository *repo)
{
    git_object *object = nullptr;
    if (git_revparse_single(&object, repo, "HEAD^{commit}") != 0)
        return nullptr; // ветка ещё без коммитов
    return Owned<git_commit>(reinterpret_cast<git_commit *>(object));
}

char indexLetter(unsigned flags)
{
    if (flags & GIT_STATUS_INDEX_NEW) return 'A';
    if (flags & GIT_STATUS_INDEX_MODIFIED) return 'M';
    if (flags & GIT_STATUS_INDEX_DELETED) return 'D';
    if (flags & GIT_STATUS_INDEX_RENAMED) return 'R';
    if (flags & GIT_STATUS_INDEX_TYPECHANGE) return 'T';
    return 0;
}

char worktreeLetter(unsigned flags)
{
    if (flags & GIT_STATUS_WT_NEW) return '?';
    if (flags & GIT_STATUS_WT_MODIFIED) return 'M';
    if (flags & GIT_STATUS_WT_DELETED) return 'D';
    if (flags & GIT_STATUS_WT_RENAMED) return 'M';
    if (flags & GIT_STATUS_WT_TYPECHANGE) return 'T';
    return 0;
}

int toZeroBased(int start, int count)
{
    // У libgit2 строки с 1, а у пустого участка start — строка перед ним
    return count == 0 ? start : start - 1;
}

QString idOf(const git_oid *oid)
{
    char text[GIT_OID_SHA1_HEXSIZE + 1] = {};
    git_oid_tostr(text, sizeof text, oid);
    return QString::fromLatin1(text);
}

std::optional<git_oid> oidOf(const QString &id)
{
    git_oid oid;
    if (git_oid_fromstr(&oid, id.toLatin1().constData()) != 0)
        return std::nullopt;
    return oid;
}

std::optional<QByteArray> blobIn(git_repository *repo, git_tree *tree, const QString &path)
{
    git_tree_entry *rawEntry = nullptr;
    if (git_tree_entry_bypath(&rawEntry, tree, path.toUtf8().constData()) != 0)
        return std::nullopt;
    Owned<git_tree_entry> entry(rawEntry);
    git_blob *rawBlob = nullptr;
    if (git_blob_lookup(&rawBlob, repo, git_tree_entry_id(entry.get())) != 0)
        return std::nullopt;
    Owned<git_blob> blob(rawBlob);
    return QByteArray(static_cast<const char *>(git_blob_rawcontent(blob.get())),
                      qsizetype(git_blob_rawsize(blob.get())));
}

} // namespace

Library::Library() { git_libgit2_init(); }
Library::~Library() { git_libgit2_shutdown(); }

} // namespace git

QVector<vcs::Hunk> vcs::diffLines(const QByteArray &oldText, const QByteArray &newText)
{
    git_diff_options options = GIT_DIFF_OPTIONS_INIT;
    options.context_lines = 0;
    options.flags = GIT_DIFF_FORCE_TEXT;
    QVector<Hunk> hunks;
    auto onHunk = [](const git_diff_delta *, const git_diff_hunk *h, void *payload) {
        static_cast<QVector<Hunk> *>(payload)->append(
            {git::toZeroBased(h->old_start, h->old_lines), h->old_lines,
             git::toZeroBased(h->new_start, h->new_lines), h->new_lines});
        return 0;
    };
    git_diff_buffers(oldText.constData(), size_t(oldText.size()), nullptr,
                     newText.constData(), size_t(newText.size()), nullptr,
                     &options, nullptr, nullptr, onHunk, nullptr, &hunks);
    return hunks;
}

namespace git {

Repository::~Repository()
{
    git_repository_free(m_repo);
}

std::unique_ptr<Repository> Repository::discover(const QString &path)
{
    git_repository *repo = nullptr;
    if (git_repository_open_ext(&repo, QDir::toNativeSeparators(path).toUtf8().constData(), 0, nullptr) != 0)
        return nullptr;
    if (git_repository_is_bare(repo)) {
        git_repository_free(repo);
        return nullptr;
    }
    return std::unique_ptr<Repository>(new Repository(repo));
}

bool Repository::init(const QString &path, QString *error)
{
    git_repository *repo = nullptr;
    if (git_repository_init(&repo, path.toUtf8().constData(), 0) != 0)
        return fail(error);
    git_repository_free(repo);
    return true;
}

QString Repository::workdir() const
{
    return QString::fromUtf8(git_repository_workdir(m_repo));
}

QString Repository::gitDir() const
{
    return QString::fromUtf8(git_repository_path(m_repo));
}

QStringList Repository::watchPaths() const
{
    QStringList paths;
    for (const char *name : {"index", "HEAD", "refs/heads"})
        paths.append(gitDir() + QLatin1String(name));
    return paths;
}

QString Repository::branch() const
{
    git_reference *raw = nullptr;
    const int rc = git_repository_head(&raw, m_repo);
    Owned<git_reference> head(raw);
    if (rc == GIT_EUNBORNBRANCH) {
        // Новый репозиторий: HEAD указывает на ветку, которой ещё нет
        git_reference *symbolic = nullptr;
        if (git_reference_lookup(&symbolic, m_repo, "HEAD") != 0)
            return {};
        Owned<git_reference> owned(symbolic);
        const QString target = QString::fromUtf8(git_reference_symbolic_target(symbolic));
        return target.mid(target.lastIndexOf(QLatin1Char('/')) + 1);
    }
    if (rc != 0)
        return {};
    if (git_repository_head_detached(m_repo) == 1) {
        char id[8] = {};
        git_oid_tostr(id, sizeof id, git_reference_target(head.get()));
        return QStringLiteral("(%1)").arg(QString::fromLatin1(id));
    }
    return QString::fromUtf8(git_reference_shorthand(head.get()));
}

QStringList Repository::branches() const
{
    QStringList names;
    git_branch_iterator *raw = nullptr;
    if (git_branch_iterator_new(&raw, m_repo, GIT_BRANCH_LOCAL) != 0)
        return names;
    Owned<git_branch_iterator> it(raw);
    git_reference *ref = nullptr;
    git_branch_t type;
    while (git_branch_next(&ref, &type, it.get()) == 0) {
        Owned<git_reference> owned(ref);
        const char *name = nullptr;
        if (git_branch_name(&name, ref) == 0)
            names.append(QString::fromUtf8(name));
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

QVector<FileStatus> Repository::status() const
{
    git_status_options options = GIT_STATUS_OPTIONS_INIT;
    options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
                    | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX | GIT_STATUS_OPT_EXCLUDE_SUBMODULES;
    git_status_list *raw = nullptr;
    QVector<FileStatus> result;
    if (git_status_list_new(&raw, m_repo, &options) != 0)
        return result;
    Owned<git_status_list> list(raw);
    const size_t count = git_status_list_entrycount(list.get());
    result.reserve(int(count));
    for (size_t i = 0; i < count; ++i) {
        const git_status_entry *e = git_status_byindex(list.get(), i);
        if (e->status == GIT_STATUS_CURRENT || (e->status & GIT_STATUS_IGNORED))
            continue;
        const git_diff_delta *delta = e->index_to_workdir ? e->index_to_workdir : e->head_to_index;
        FileStatus s;
        s.path = QString::fromUtf8(delta->new_file.path);
        s.conflict = e->status & GIT_STATUS_CONFLICTED;
        s.index = indexLetter(e->status);
        s.worktree = worktreeLetter(e->status);
        result.append(s);
    }
    return result;
}

std::optional<QByteArray> Repository::indexBlob(const QString &path) const
{
    git_index *rawIndex = nullptr;
    if (git_repository_index(&rawIndex, m_repo) != 0)
        return std::nullopt;
    Owned<git_index> index(rawIndex);
    git_index_read(index.get(), 0); // индекс мог поменяться на диске
    const git_index_entry *entry = git_index_get_bypath(index.get(), path.toUtf8().constData(), 0);
    if (!entry)
        return std::nullopt;
    git_blob *rawBlob = nullptr;
    if (git_blob_lookup(&rawBlob, m_repo, &entry->id) != 0)
        return std::nullopt;
    Owned<git_blob> blob(rawBlob);
    return QByteArray(static_cast<const char *>(git_blob_rawcontent(blob.get())),
                      qsizetype(git_blob_rawsize(blob.get())));
}

std::optional<QByteArray> Repository::headBlob(const QString &path) const
{
    const Owned<git_commit> commit = headCommit(m_repo);
    if (!commit)
        return std::nullopt;
    git_tree *rawTree = nullptr;
    if (git_commit_tree(&rawTree, commit.get()) != 0)
        return std::nullopt;
    Owned<git_tree> tree(rawTree);
    return blobIn(m_repo, tree.get(), path);
}

bool Repository::stage(const QStringList &paths, QString *error)
{
    git_index *raw = nullptr;
    if (git_repository_index(&raw, m_repo) != 0)
        return fail(error);
    Owned<git_index> index(raw);
    git_index_read(index.get(), 0);
    for (const QString &path : paths) {
        const QByteArray utf8 = path.toUtf8();
        const int rc = QFileInfo::exists(workdir() + path)
                           ? git_index_add_bypath(index.get(), utf8.constData())
                           : git_index_remove_bypath(index.get(), utf8.constData());
        if (rc != 0)
            return fail(error);
    }
    return git_index_write(index.get()) == 0 || fail(error);
}

bool Repository::unstage(const QStringList &paths, QString *error)
{
    // Без коммитов target = null: файлы просто убираются из индекса
    const Owned<git_commit> head = headCommit(m_repo);
    const PathList list(paths);
    return git_reset_default(m_repo, reinterpret_cast<const git_object *>(head.get()), list.get()) == 0
           || fail(error);
}

bool Repository::discard(const QStringList &paths, QString *error)
{
    QStringList tracked;
    for (const QString &path : paths) {
        unsigned flags = 0;
        if (git_status_file(&flags, m_repo, path.toUtf8().constData()) == 0 && (flags & GIT_STATUS_WT_NEW)) {
            if (!QFile::remove(workdir() + path))
                return fail(error, QCoreApplication::translate("git", "Не удалось удалить %1").arg(path));
        } else {
            tracked.append(path);
        }
    }
    if (tracked.isEmpty())
        return true;
    git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
    options.checkout_strategy = GIT_CHECKOUT_FORCE | GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH;
    const PathList list(tracked);
    options.paths = *list.get();
    return git_checkout_index(m_repo, nullptr, &options) == 0 || fail(error);
}

bool Repository::commit(const QString &message, QString *error)
{
    if (git_repository_state(m_repo) != GIT_REPOSITORY_STATE_NONE)
        return fail(error, QCoreApplication::translate("git", "Идёт слияние или перебазирование — завершите его в git"));
    git_signature *rawSignature = nullptr;
    if (git_signature_default(&rawSignature, m_repo) != 0)
        return fail(error, QCoreApplication::translate("git", "Не заданы имя и почта автора: git config --global user.name / user.email"));
    Owned<git_signature> signature(rawSignature);

    git_index *rawIndex = nullptr;
    if (git_repository_index(&rawIndex, m_repo) != 0)
        return fail(error);
    Owned<git_index> index(rawIndex);
    git_index_read(index.get(), 0);
    git_oid treeId;
    if (git_index_write_tree(&treeId, index.get()) != 0)
        return fail(error);
    git_tree *rawTree = nullptr;
    if (git_tree_lookup(&rawTree, m_repo, &treeId) != 0)
        return fail(error);
    Owned<git_tree> tree(rawTree);

    const Owned<git_commit> parent = headCommit(m_repo);
    const git_commit *parents[] = {parent.get()};
    git_oid commitId;
    return git_commit_create(&commitId, m_repo, "HEAD", signature.get(), signature.get(), nullptr,
                             message.toUtf8().constData(), tree.get(), parent ? 1 : 0, parents) == 0
           || fail(error);
}

bool Repository::checkoutTarget(const git_object *target, QString *error)
{
    // SAFE: не трогать файлы с незакоммиченными правками — тогда ошибка
    git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
    options.checkout_strategy = GIT_CHECKOUT_SAFE;
    return git_checkout_tree(m_repo, target, &options) == 0 || fail(error);
}

bool Repository::checkout(const QString &branch, QString *error)
{
    git_reference *raw = nullptr;
    if (git_branch_lookup(&raw, m_repo, branch.toUtf8().constData(), GIT_BRANCH_LOCAL) != 0)
        return fail(error);
    Owned<git_reference> ref(raw);
    git_object *rawTarget = nullptr;
    if (git_reference_peel(&rawTarget, ref.get(), GIT_OBJECT_COMMIT) != 0)
        return fail(error);
    Owned<git_object> target(rawTarget);
    if (!checkoutTarget(target.get(), error))
        return false;
    return git_repository_set_head(m_repo, git_reference_name(ref.get())) == 0 || fail(error);
}

bool Repository::createBranch(const QString &name, QString *error)
{
    const Owned<git_commit> head = headCommit(m_repo);
    if (!head)
        return fail(error, QCoreApplication::translate("git", "Сначала нужен хотя бы один коммит"));
    git_reference *raw = nullptr;
    if (git_branch_create(&raw, m_repo, name.toUtf8().constData(), head.get(), 0) != 0)
        return fail(error);
    Owned<git_reference> ref(raw);
    return git_repository_set_head(m_repo, git_reference_name(ref.get())) == 0 || fail(error);
}

QVector<vcs::Commit> Repository::log(int limit) const
{
    QVector<vcs::Commit> commits;
    // Метки: ветки, внешние ветки и теги по коммиту, на который указывают
    QHash<QString, QVector<vcs::Ref>> refs;
    const QString current = git_repository_head_detached(m_repo) == 1 ? QString() : branch();
    git_reference_iterator *rawIt = nullptr;
    if (git_reference_iterator_new(&rawIt, m_repo) == 0) {
        Owned<git_reference_iterator> it(rawIt);
        git_reference *rawRef = nullptr;
        while (git_reference_next(&rawRef, it.get()) == 0) {
            Owned<git_reference> ref(rawRef);
            vcs::Ref r;
            if (git_reference_is_branch(rawRef))
                r.kind = vcs::Ref::Branch;
            else if (git_reference_is_remote(rawRef))
                r.kind = vcs::Ref::Remote;
            else if (git_reference_is_tag(rawRef))
                r.kind = vcs::Ref::Tag;
            else
                continue;
            r.name = QString::fromUtf8(git_reference_shorthand(rawRef));
            if (r.kind == vcs::Ref::Remote && r.name.endsWith(QLatin1String("/HEAD")))
                continue;
            r.current = r.kind == vcs::Ref::Branch && r.name == current;
            git_object *peeled = nullptr;
            if (git_reference_peel(&peeled, rawRef, GIT_OBJECT_COMMIT) != 0)
                continue;
            Owned<git_object> target(peeled);
            refs[idOf(git_object_id(peeled))].append(r);
        }
    }
    if (current.isEmpty())
        if (const Owned<git_commit> head = headCommit(m_repo))
            refs[idOf(git_commit_id(head.get()))].prepend({QStringLiteral("HEAD"), vcs::Ref::Branch, true});

    git_revwalk *rawWalk = nullptr;
    if (git_revwalk_new(&rawWalk, m_repo) != 0)
        return commits;
    Owned<git_revwalk> walk(rawWalk);
    git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    git_revwalk_push_head(walk.get());
    git_revwalk_push_glob(walk.get(), "refs/heads");
    git_revwalk_push_glob(walk.get(), "refs/remotes");
    git_revwalk_push_glob(walk.get(), "refs/tags");
    git_oid oid;
    while (commits.size() < limit && git_revwalk_next(&oid, walk.get()) == 0) {
        git_commit *rawCommit = nullptr;
        if (git_commit_lookup(&rawCommit, m_repo, &oid) != 0)
            continue;
        Owned<git_commit> commit(rawCommit);
        vcs::Commit c;
        c.id = idOf(&oid);
        for (unsigned i = 0; i < git_commit_parentcount(rawCommit); ++i)
            c.parents.append(idOf(git_commit_parent_id(rawCommit, i)));
        const git_signature *author = git_commit_author(rawCommit);
        c.author = QString::fromUtf8(author->name);
        c.email = QString::fromUtf8(author->email);
        c.time = QDateTime::fromSecsSinceEpoch(author->when.time, QTimeZone::fromSecondsAheadOfUtc(author->when.offset * 60));
        c.summary = QString::fromUtf8(git_commit_summary(rawCommit));
        c.message = QString::fromUtf8(git_commit_message(rawCommit)).trimmed();
        c.refs = refs.value(c.id);
        commits.append(std::move(c));
    }
    return commits;
}

QVector<vcs::CommitFile> Repository::commitFiles(const QString &id) const
{
    QVector<vcs::CommitFile> files;
    const auto oid = oidOf(id);
    git_commit *rawCommit = nullptr;
    if (!oid || git_commit_lookup(&rawCommit, m_repo, &*oid) != 0)
        return files;
    Owned<git_commit> commit(rawCommit);
    git_tree *rawTree = nullptr;
    if (git_commit_tree(&rawTree, rawCommit) != 0)
        return files;
    Owned<git_tree> tree(rawTree);
    // Сравнение с первым родителем; у первого коммита — с пустым деревом
    Owned<git_tree> parentTree;
    git_commit *rawParent = nullptr;
    if (git_commit_parent(&rawParent, rawCommit, 0) == 0) {
        Owned<git_commit> parent(rawParent);
        git_tree *raw = nullptr;
        if (git_commit_tree(&raw, rawParent) == 0)
            parentTree.reset(raw);
    }
    git_diff *rawDiff = nullptr;
    if (git_diff_tree_to_tree(&rawDiff, m_repo, parentTree.get(), tree.get(), nullptr) != 0)
        return files;
    Owned<git_diff> diff(rawDiff);
    git_diff_find_similar(rawDiff, nullptr);
    for (size_t i = 0; i < git_diff_num_deltas(rawDiff); ++i) {
        const git_diff_delta *d = git_diff_get_delta(rawDiff, i);
        char status = 'M';
        switch (d->status) {
        case GIT_DELTA_ADDED: status = 'A'; break;
        case GIT_DELTA_DELETED: status = 'D'; break;
        case GIT_DELTA_RENAMED: status = 'R'; break;
        default: break;
        }
        files.append({QString::fromUtf8(status == 'D' ? d->old_file.path : d->new_file.path), status});
    }
    return files;
}

std::optional<QByteArray> Repository::fileAt(const QString &id, const QString &path) const
{
    const auto oid = oidOf(id);
    git_commit *rawCommit = nullptr;
    if (!oid || git_commit_lookup(&rawCommit, m_repo, &*oid) != 0)
        return std::nullopt;
    Owned<git_commit> commit(rawCommit);
    git_tree *rawTree = nullptr;
    if (git_commit_tree(&rawTree, rawCommit) != 0)
        return std::nullopt;
    Owned<git_tree> tree(rawTree);
    return blobIn(m_repo, tree.get(), path);
}

bool Repository::checkoutCommit(const QString &id, QString *error)
{
    const auto oid = oidOf(id);
    git_object *rawTarget = nullptr;
    if (!oid || git_object_lookup(&rawTarget, m_repo, &*oid, GIT_OBJECT_COMMIT) != 0)
        return fail(error);
    Owned<git_object> target(rawTarget);
    if (!checkoutTarget(target.get(), error))
        return false;
    return git_repository_set_head_detached(m_repo, &*oid) == 0 || fail(error);
}

bool Repository::createBranchAt(const QString &name, const QString &id, QString *error)
{
    const auto oid = oidOf(id);
    git_commit *rawCommit = nullptr;
    if (!oid || git_commit_lookup(&rawCommit, m_repo, &*oid) != 0)
        return fail(error);
    Owned<git_commit> commit(rawCommit);
    git_reference *raw = nullptr;
    if (git_branch_create(&raw, m_repo, name.toUtf8().constData(), rawCommit, 0) != 0)
        return fail(error);
    Owned<git_reference> ref(raw);
    return checkout(name, error);
}

} // namespace git
