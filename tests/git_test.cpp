#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

#include "src/git/gitrepo.h"
#include "src/git/hgrepo.h"

using git::Repository;

class GitTest : public QObject {
    Q_OBJECT

private:
    git::Library m_library;

    static void write(const QString &path, const QByteArray &data)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(data);
    }
    static vcs::FileStatus statusOf(const vcs::Repository &repo, const QString &path)
    {
        for (const vcs::FileStatus &s : repo.status())
            if (s.path == path)
                return s;
        return {};
    }

private slots:
    void diffHunks()
    {
        // Две замены — два участка без контекста
        const auto hunks = git::diffLines("a\nb\nc\nd\ne\n", "a\nB\nc\nd\nX\n");
        QCOMPARE(hunks.size(), 2);
        QCOMPARE(hunks[0].newStart, 1);
        QCOMPARE(hunks[0].newCount, 1);
        QCOMPARE(hunks[1].newStart, 4);
        QCOMPARE(hunks[1].oldCount, 1);

        const auto inserted = git::diffLines("a\nb\n", "a\nnew\nb\n");
        QCOMPARE(inserted.size(), 1);
        QCOMPARE(inserted[0].oldCount, 0);
        QCOMPARE(inserted[0].newStart, 1);
        QCOMPARE(inserted[0].newCount, 1);

        const auto removed = git::diffLines("a\nb\nc\n", "a\nc\n");
        QCOMPARE(removed.size(), 1);
        QCOMPARE(removed[0].newCount, 0);
        QCOMPARE(removed[0].newStart, 1); // удалено перед строкой «c»
        QCOMPARE(removed[0].oldStart, 1);
        QCOMPARE(removed[0].oldCount, 1);
    }

    void stageCommitDiscard()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString error;
        QVERIFY2(Repository::init(dir.path(), &error), qPrintable(error));
        auto repo = Repository::discover(dir.path());
        QVERIFY(repo);

        write(dir.filePath(QStringLiteral("a.txt")), "one\ntwo\n");
        QCOMPARE(statusOf(*repo, QStringLiteral("a.txt")).worktree, '?');

        QVERIFY(repo->stage({QStringLiteral("a.txt")}, &error));
        QCOMPARE(statusOf(*repo, QStringLiteral("a.txt")).index, 'A');
        QCOMPARE(repo->indexBlob(QStringLiteral("a.txt")).value_or(QByteArray()), QByteArray("one\ntwo\n"));

        QVERIFY(repo->unstage({QStringLiteral("a.txt")}, &error)); // без коммитов — просто из индекса
        QCOMPARE(statusOf(*repo, QStringLiteral("a.txt")).worktree, '?');
        QVERIFY(repo->stage({QStringLiteral("a.txt")}, &error));

        // Подпись берётся из конфигурации репозитория — тест не зависит от глобальной
        {
            QFile config(dir.filePath(QStringLiteral(".git/config")));
            QVERIFY(config.open(QIODevice::Append));
            config.write("[user]\n\tname = Test\n\temail = test@example.com\n");
        }
        QVERIFY2(repo->commit(QStringLiteral("первый"), &error), qPrintable(error));
        QVERIFY(repo->status().isEmpty());
        QVERIFY(repo->headBlob(QStringLiteral("a.txt")).has_value());

        write(dir.filePath(QStringLiteral("a.txt")), "one\nTWO\n");
        QCOMPARE(statusOf(*repo, QStringLiteral("a.txt")).worktree, 'M');
        QVERIFY(repo->discard({QStringLiteral("a.txt")}, &error));
        QVERIFY(repo->status().isEmpty());

        write(dir.filePath(QStringLiteral("new.txt")), "x");
        QVERIFY(repo->discard({QStringLiteral("new.txt")}, &error)); // новый файл удаляется
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("new.txt"))));

        const QString first = repo->branch();
        QVERIFY(repo->createBranch(QStringLiteral("feature"), &error));
        QCOMPARE(repo->branch(), QStringLiteral("feature"));
        QCOMPARE(repo->branches().size(), 2);
        QVERIFY2(repo->checkout(first, &error), qPrintable(error));
        QCOMPARE(repo->branch(), first);
    }

    void gitHistory()
    {
        QTemporaryDir dir;
        QString error;
        QVERIFY2(Repository::init(dir.path(), &error), qPrintable(error));
        {
            QFile config(dir.filePath(QStringLiteral(".git/config")));
            QVERIFY(config.open(QIODevice::Append));
            config.write("[user]\n\tname = Test\n\temail = test@example.com\n");
        }
        auto repo = vcs::discover(dir.path());
        QVERIFY(repo);
        QCOMPARE(repo->kind(), QStringLiteral("Git"));

        write(dir.filePath(QStringLiteral("a.txt")), "one\n");
        QVERIFY(repo->stage({QStringLiteral("a.txt")}, &error));
        QVERIFY2(repo->commit(QStringLiteral("первый"), &error), qPrintable(error));
        write(dir.filePath(QStringLiteral("a.txt")), "two\n");
        write(dir.filePath(QStringLiteral("b.txt")), "b\n");
        QVERIFY(repo->stage({QStringLiteral("a.txt"), QStringLiteral("b.txt")}, &error));
        QVERIFY2(repo->commit(QStringLiteral("второй\n\nподробности"), &error), qPrintable(error));

        const auto log = repo->log(10);
        QCOMPARE(log.size(), 2);
        QCOMPARE(log[0].summary, QStringLiteral("второй"));
        QCOMPARE(log[0].parents, QStringList{log[1].id});
        QVERIFY(log[1].parents.isEmpty());
        QCOMPARE(log[0].author, QStringLiteral("Test"));
        QVERIFY(std::any_of(log[0].refs.cbegin(), log[0].refs.cend(), [](const vcs::Ref &r) { return r.current; }));

        const auto files = repo->commitFiles(log[0].id);
        QCOMPARE(files.size(), 2);
        for (const vcs::CommitFile &f : files)
            QCOMPARE(f.status, f.path == QLatin1String("a.txt") ? 'M' : 'A');
        QCOMPARE(repo->fileAt(log[1].id, QStringLiteral("a.txt")).value_or(""), QByteArray("one\n"));
        QVERIFY(!repo->fileAt(log[1].id, QStringLiteral("b.txt")).has_value());

        QVERIFY2(repo->createBranchAt(QStringLiteral("old"), log[1].id, &error), qPrintable(error));
        QCOMPARE(repo->branch(), QStringLiteral("old"));
        QCOMPARE(repo->headBlob(QStringLiteral("a.txt")).value_or(""), QByteArray("one\n"));
    }

    void graphLinear()
    {
        const QVector<vcs::Commit> commits{{.id = "c", .parents = {"b"}}, {.id = "b", .parents = {"a"}}, {.id = "a"}};
        const auto rows = vcs::layoutGraph(commits);
        QCOMPARE(rows.size(), 3);
        for (const vcs::GraphRow &row : rows) {
            QCOMPARE(row.lane, 0);
            QCOMPARE(row.width, 1);
            QCOMPARE(row.color, rows[0].color);
        }
        QCOMPARE(rows[0].edges.size(), 1); // вершина: только вниз
        QCOMPARE(rows[0].edges[0].kind, vcs::GraphEdge::Out);
        QCOMPARE(rows[2].edges.size(), 1); // корень: только сверху
        QCOMPARE(rows[2].edges[0].kind, vcs::GraphEdge::In);
    }

    void graphBranchAndMerge()
    {
        // m — слияние a и b, обе ветки растут из base
        const QVector<vcs::Commit> commits{{.id = "m", .parents = {"a", "b"}}, {.id = "b", .parents = {"base"}},
                                           {.id = "a", .parents = {"base"}}, {.id = "base"}};
        const auto rows = vcs::layoutGraph(commits);
        QCOMPARE(rows[0].lane, 0);
        QCOMPARE(rows[0].width, 2);
        const auto toSecondParent = std::find_if(rows[0].edges.cbegin(), rows[0].edges.cend(), [](const vcs::GraphEdge &e) {
            return e.kind == vcs::GraphEdge::Out && e.to == 1;
        });
        QVERIFY(toSecondParent != rows[0].edges.cend());
        QCOMPARE(rows[1].lane, 1); // b — на дорожке второго родителя
        QCOMPARE(rows[1].color, toSecondParent->color);
        QCOMPARE(rows[2].lane, 0);
        QCOMPARE(rows[3].lane, 0); // обе дорожки сходятся в base
        QCOMPARE(std::count_if(rows[3].edges.cbegin(), rows[3].edges.cend(),
                               [](const vcs::GraphEdge &e) { return e.kind == vcs::GraphEdge::In && e.to == 0; }),
                 2);
    }

    void mercurial()
    {
        if (hg::Repository::executable().isEmpty())
            QSKIP("Mercurial не установлен");
        QTemporaryDir dir;
        QString error;
        QVERIFY2(vcs::init(vcs::Kind::Mercurial, dir.path(), &error), qPrintable(error));
        {
            QFile config(dir.filePath(QStringLiteral(".hg/hgrc")));
            QVERIFY(config.open(QIODevice::Append));
            config.write("[ui]\nusername = Test <test@example.com>\n");
        }
        auto repo = vcs::discover(dir.path());
        QVERIFY(repo);
        QCOMPARE(repo->kind(), QStringLiteral("Mercurial"));
        QVERIFY(!repo->hasIndex());

        const QString name = QStringLiteral("папка/файл.txt");
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("папка")));
        write(dir.filePath(name), "один\n");
        QCOMPARE(statusOf(*repo, name).worktree, '?');
        QVERIFY(repo->stage({name}, &error));
        QCOMPARE(statusOf(*repo, name).index, 'A');
        QVERIFY2(repo->commit(QStringLiteral("первый"), &error), qPrintable(error));
        QVERIFY(repo->status().isEmpty());

        write(dir.filePath(name), "два\n");
        QCOMPARE(statusOf(*repo, name).worktree, 'M');
        QCOMPARE(repo->headBlob(name).value_or(""), QStringLiteral("один\n").toUtf8());
        QVERIFY2(repo->discard({name}, &error), qPrintable(error));
        QVERIFY(repo->status().isEmpty());

        const auto log = repo->log(10);
        QCOMPARE(log.size(), 1);
        QCOMPARE(log[0].summary, QStringLiteral("первый"));
        QCOMPARE(log[0].author, QStringLiteral("Test"));
        QCOMPARE(log[0].email, QStringLiteral("test@example.com"));
        const auto files = repo->commitFiles(log[0].id);
        QCOMPARE(files.size(), 1);
        QCOMPARE(files[0].path, name);
        QCOMPARE(files[0].status, 'A');

        QVERIFY2(repo->createBranch(QStringLiteral("feature"), &error), qPrintable(error));
        QCOMPARE(repo->branch(), QStringLiteral("feature"));
        QVERIFY(repo->branches().contains(QStringLiteral("default")));

        repo.reset();
        hg::Repository::closeSessions(); // иначе hg держит временную папку
    }
};

QTEST_GUILESS_MAIN(GitTest)
#include "git_test.moc"
