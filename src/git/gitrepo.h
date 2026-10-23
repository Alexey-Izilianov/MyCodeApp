#pragma once

#include <memory>

#include "vcs.h"

struct git_object;
struct git_repository;

namespace git {

using vcs::diffLines;
using vcs::FileStatus;
using vcs::Hunk;

// Инициализация libgit2 на время жизни объекта (счётчик внутри libgit2)
class Library {
public:
    Library();
    ~Library();
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;
};

// Репозиторий Git через libgit2
class Repository : public vcs::Repository {
public:
    ~Repository() override;
    Repository(const Repository &) = delete;
    Repository &operator=(const Repository &) = delete;

    // Репозиторий, в который входит path (поиск вверх по папкам)
    static std::unique_ptr<Repository> discover(const QString &path);
    static bool init(const QString &path, QString *error);

    QString kind() const override { return QStringLiteral("Git"); }
    bool hasIndex() const override { return true; }
    QString workdir() const override;
    QString gitDir() const; // папка .git, с '/' в конце
    QStringList watchPaths() const override;
    QString branch() const override;
    QStringList branches() const override;
    QVector<FileStatus> status() const override;

    std::optional<QByteArray> indexBlob(const QString &path) const override;
    std::optional<QByteArray> headBlob(const QString &path) const override;

    bool stage(const QStringList &paths, QString *error) override;
    bool unstage(const QStringList &paths, QString *error) override;
    bool discard(const QStringList &paths, QString *error) override;
    bool commit(const QString &message, QString *error) override;
    bool checkout(const QString &branch, QString *error) override;
    bool createBranch(const QString &name, QString *error) override;

    QVector<vcs::Commit> log(int limit) const override;
    QVector<vcs::CommitFile> commitFiles(const QString &id) const override;
    std::optional<QByteArray> fileAt(const QString &id, const QString &path) const override;
    bool checkoutCommit(const QString &id, QString *error) override;
    bool createBranchAt(const QString &name, const QString &id, QString *error) override;

private:
    explicit Repository(git_repository *repo) : m_repo(repo) {}
    bool checkoutTarget(const git_object *target, QString *error);

    git_repository *m_repo;
};

} // namespace git
