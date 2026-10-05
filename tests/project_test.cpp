#include <QtTest>

#include "src/services/ignorerules.h"
#include "src/services/workspace.h"

class ProjectTest : public QObject {
    Q_OBJECT

private slots:
    void gitignorePatterns();
    void scanSkipsIgnored();
    void fuzzyRanking();
};

void ProjectTest::gitignorePatterns()
{
    const auto rules = IgnoreRules::parse("# comment\nbuild/\n*.log\n!keep.log\n/root.txt\ndocs/**/*.tmp\n");
    auto ignored = [&](const char *path, bool isDir = false) {
        return IgnoreRules::match(rules, QString::fromLatin1(path), isDir).value_or(false);
    };
    QVERIFY(ignored("build", true));
    QVERIFY(!ignored("build")); // build/ — только папка
    QVERIFY(ignored("src/build", true));
    QVERIFY(ignored("a/b/x.log"));
    QVERIFY(!ignored("a/keep.log")); // последнее правило побеждает
    QVERIFY(ignored("root.txt"));
    QVERIFY(!ignored("sub/root.txt")); // со слэшем — только от корня
    QVERIFY(ignored("docs/a/b/c.tmp"));
    QVERIFY(ignored("docs/c.tmp"));
    QVERIFY(!ignored("other/c.tmp"));
}

void ProjectTest::scanSkipsIgnored()
{
    QTemporaryDir dir;
    auto touch = [&](const QString &relative, const QByteArray &content = {}) {
        QDir(dir.path()).mkpath(QFileInfo(relative).path());
        QFile f(dir.path() + u'/' + relative);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(content);
    };
    touch(".gitignore", "out/\n*.o\n");
    touch("src/main.cpp");
    touch("src/main.o");
    touch("src/lib/.gitignore", "!special.o\ngen.h\n");
    touch("src/lib/special.o");
    touch("src/lib/gen.h");
    touch("out/app.exe");
    touch(".git/HEAD");
    const QStringList files = Workspace::scanFiles(dir.path());
    QCOMPARE(files, (QStringList{".gitignore", "src/lib/.gitignore", "src/lib/special.o", "src/main.cpp"}));
}

void ProjectTest::fuzzyRanking()
{
    QCOMPARE(Workspace::fuzzyScore(u"xyz", u"src/main.cpp"), -1);
    // Имя файла важнее совпадения, размазанного по папкам
    QVERIFY(Workspace::fuzzyScore(u"doc", u"src/core/document.h")
            > Workspace::fuzzyScore(u"doc", u"docs/plan/readme.md"));
    QVERIFY(Workspace::fuzzyScore(u"edv", u"src/gui/editerview.cpp") >= 0);
    QVERIFY(Workspace::fuzzyScore(u"main", u"main.cpp") > Workspace::fuzzyScore(u"main", u"src/domain/xmainx.cpp"));
    QVERIFY(Workspace::fuzzyScore(u"core/doc", u"src/core/document.h") >= 0); // со слэшем — по всему пути
}

QTEST_GUILESS_MAIN(ProjectTest)
#include "project_test.moc"
