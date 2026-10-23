#pragma once

#include <memory>

#include "vcs.h"

namespace hg {

struct Session;

// Репозиторий Mercurial через командный сервер hg (один процесс на репозиторий и поток).
// Индекса у Mercurial нет: «добавленные в индекс» файлы запоминаются в сессии и
// уходят в коммит списком (hg commit --addremove <файлы>)
class Repository : public vcs::Repository {
public:
    ~Repository() override;

    static std::unique_ptr<Repository> open(const QString &root);
    static bool init(const QString &path, QString *error);
    static QString executable(); // путь к hg; пусто — не установлен
    // Завершить командные серверы текущего потока: они держат папки репозиториев открытыми
    static void closeSessions();

    QString kind() const override { return QStringLiteral("Mercurial"); }
    bool hasIndex() const override { return false; }
    QString workdir() const override;
    QStringList watchPaths() const override;
    QString branch() const override;
    QStringList branches() const override;
    QVector<vcs::FileStatus> status() const override;

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
    struct Result {
        int code = -1;
        QByteArray out;
        QByteArray err;
        bool ok() const { return code == 0; }
    };
    Repository(QString root, std::shared_ptr<Session> session);
    Result run(const QStringList &args) const;
    bool runOrFail(const QStringList &args, QString *error) const;
    QHash<QString, char> rawStatus() const; // путь -> буква hg status

    QString m_root; // с '/' в конце
    std::shared_ptr<Session> m_session;
};

} // namespace hg
