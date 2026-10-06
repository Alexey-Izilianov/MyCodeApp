#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <optional>

struct git_repository;

namespace git {

// Инициализация libgit2 на время жизни объекта (счётчик внутри libgit2)
class Library {
public:
    Library();
    ~Library();
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;
};

struct FileStatus {
    QString path;       // относительно корня рабочей копии, через '/'
    char index = 0;     // изменение в индексе: 'A', 'M', 'D', 'R', 'T' или 0
    char worktree = 0;  // в рабочей копии: 'M', 'D', 'T', '?' (новый файл) или 0
    bool conflict = false;
};

// Отличающийся участок, строки с 0. Count == 0 — пустой участок перед строкой start
struct Hunk {
    int oldStart = 0;
    int oldCount = 0;
    int newStart = 0;
    int newCount = 0;
};

// Построчное сравнение двух текстов (переводы строк — '\n')
QVector<Hunk> diffLines(const QByteArray &oldText, const QByteArray &newText);

// Синхронная обёртка над репозиторием libgit2. Вызовы бывают долгими —
// только из фонового потока; объект не делится между потоками.
class Repository {
public:
    ~Repository();
    Repository(const Repository &) = delete;
    Repository &operator=(const Repository &) = delete;

    // Репозиторий, в который входит path (поиск вверх по папкам)
    static std::unique_ptr<Repository> discover(const QString &path);
    static bool init(const QString &path, QString *error);

    QString workdir() const; // абсолютный, с '/' в конце
    QString gitDir() const;  // папка .git, с '/' в конце
    QString branch() const;  // имя ветки или «(abc1234)» без ветки
    QStringList branches() const;
    QVector<FileStatus> status() const;

    std::optional<QByteArray> indexBlob(const QString &path) const;
    std::optional<QByteArray> headBlob(const QString &path) const;

    bool stage(const QStringList &paths, QString *error);
    bool unstage(const QStringList &paths, QString *error);
    // Вернуть файлы к версии из индекса; новые (неотслеживаемые) — удалить
    bool discard(const QStringList &paths, QString *error);
    bool commit(const QString &message, QString *error);
    bool checkout(const QString &branch, QString *error);
    bool createBranch(const QString &name, QString *error); // и переключиться на неё

private:
    explicit Repository(git_repository *repo) : m_repo(repo) {}

    git_repository *m_repo;
};

} // namespace git
