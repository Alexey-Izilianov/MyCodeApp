#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <optional>

// Общий интерфейс систем контроля версий (Git, Mercurial) для GitService
namespace vcs {

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

struct Ref {
    enum Kind { Branch, Remote, Tag, Bookmark };
    QString name;
    Kind kind = Branch;
    bool current = false; // ветка (закладка), на которой стоит рабочая копия
};

struct Commit {
    QString id;          // полный хэш
    QStringList parents; // первый — основной родитель
    QString author;
    QString email;
    QDateTime time;
    QString summary;     // первая строка сообщения
    QString message;
    QString branch;      // Mercurial: именованная ветка коммита
    QVector<Ref> refs;
};

struct CommitFile {
    QString path;
    char status = 'M'; // 'A', 'M', 'D', 'R'
};

// Синхронная обёртка над репозиторием. Вызовы бывают долгими — только из фонового
// потока; объект не делится между потоками.
class Repository {
public:
    virtual ~Repository() = default;

    virtual QString kind() const = 0;    // "Git", "Mercurial"
    virtual bool hasIndex() const = 0;   // у Mercurial индекс эмулируется
    virtual QString workdir() const = 0; // абсолютный, с '/' в конце
    // Что отслеживать на диске, чтобы заметить коммит или смену ветки извне
    virtual QStringList watchPaths() const = 0;
    virtual QString branch() const = 0;  // имя ветки или «(abc1234)» без ветки
    virtual QStringList branches() const = 0;
    virtual QVector<FileStatus> status() const = 0;

    virtual std::optional<QByteArray> indexBlob(const QString &path) const = 0;
    virtual std::optional<QByteArray> headBlob(const QString &path) const = 0;

    virtual bool stage(const QStringList &paths, QString *error) = 0;
    virtual bool unstage(const QStringList &paths, QString *error) = 0;
    // Вернуть файлы к версии из индекса; новые (неотслеживаемые) — удалить
    virtual bool discard(const QStringList &paths, QString *error) = 0;
    virtual bool commit(const QString &message, QString *error) = 0;
    virtual bool checkout(const QString &branch, QString *error) = 0;
    virtual bool createBranch(const QString &name, QString *error) = 0; // и переключиться на неё

    // История всех веток, от новых к старым, родители всегда ниже детей; limit — не больше
    virtual QVector<Commit> log(int limit) const = 0;
    virtual QVector<CommitFile> commitFiles(const QString &id) const = 0;
    virtual std::optional<QByteArray> fileAt(const QString &id, const QString &path) const = 0;
    // Перевести рабочую копию на коммит (без ветки)
    virtual bool checkoutCommit(const QString &id, QString *error) = 0;
    virtual bool createBranchAt(const QString &name, const QString &id, QString *error) = 0;
};

// Граф истории: коммит — точка на дорожке (колонке), линии ряда идут между его верхом,
// точкой и низом. Through: дорожка проходит мимо (from — сверху, to — снизу);
// In: сверху (from) в точку; Out: из точки вниз (to) — к родителю
struct GraphEdge {
    enum Kind : quint8 { Through, In, Out };
    int from = 0;
    int to = 0;
    int color = 0;
    Kind kind = Through;
};
struct GraphRow {
    int lane = 0;  // дорожка точки коммита
    int color = 0; // её цвет (номер; палитру выбирает интерфейс)
    int width = 1; // дорожек в ряду
    QVector<GraphEdge> edges;
};
// Раскладка для истории log(): родители ниже детей
QVector<GraphRow> layoutGraph(const QVector<Commit> &commits);

// Ближайший вверх по папкам репозиторий Git или Mercurial, в который входит path
std::unique_ptr<Repository> discover(const QString &path);
enum class Kind { Git, Mercurial };
bool init(Kind kind, const QString &path, QString *error);

} // namespace vcs
