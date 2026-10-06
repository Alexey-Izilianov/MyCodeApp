#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "src/git/gitrepo.h"

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
    static git::FileStatus statusOf(const Repository &repo, const QString &path)
    {
        for (const git::FileStatus &s : repo.status())
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
};

QTEST_GUILESS_MAIN(GitTest)
#include "git_test.moc"
