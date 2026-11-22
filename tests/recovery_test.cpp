#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "src/core/document.h"
#include "src/services/recovery.h"

using core::Document;

class RecoveryTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void editsSurviveRestart();
    void saveRemovesJournal();
    void undoToDiskRemovesJournal();
    void changedOnDiskIsSkipped();
    void truncatedTailIsIgnored();
    void writeFailureKeepsPendingEdits();
    void caseDistinctPathsHaveIndependentJournals();
    void discardOnClose();

private:
    QString writeFile(const QByteArray &content);
    static QString text(const Document &doc);
    static void type(Document &doc, const QString &chars, Document::Position pos);
    QStringList logs() const { return QDir(m_logs.path()).entryList({"*.log"}, QDir::Files); }

    QTemporaryDir m_files;
    QTemporaryDir m_logs;
};

void RecoveryTest::writeFailureKeepsPendingEdits()
{
    const QString path = writeFile("abc");
    const QString journalDir = m_logs.filePath("blocked");
    RecoveryManager recovery(journalDir);
    QVERIFY(QDir().rmdir(journalDir));
    QFile blocked(journalDir);
    QVERIFY(blocked.open(QIODevice::WriteOnly));
    blocked.close();

    Document doc;
    QVERIFY(doc.load(path));
    recovery.watch(&doc);
    type(doc, QStringLiteral("x"), {0, 0});
    recovery.flush();
    QVERIFY(blocked.remove());
    QVERIFY(QDir().mkdir(journalDir));
    type(doc, QStringLiteral("y"), {0, 1});
    recovery.flush();

    Document restored;
    QVERIFY(restored.load(path));
    QVERIFY(recovery.restore(&restored));
    QCOMPARE(text(restored), QStringLiteral("xyabc"));
}

void RecoveryTest::init()
{
    for (const QString &log : logs())
        QFile::remove(m_logs.path() + u'/' + log);
}

QString RecoveryTest::writeFile(const QByteArray &content)
{
    static int n = 0;
    const QString path = m_files.path() + QStringLiteral("/file%1.txt").arg(++n);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(content);
    return path;
}

void RecoveryTest::caseDistinctPathsHaveIndependentJournals()
{
    const QString lower = writeFile("same");
    const QString upper = m_files.filePath(QFileInfo(lower).fileName().toUpper());
    if (QFileInfo::exists(upper))
        QSKIP("The test filesystem treats the two paths as aliases");
    QVERIFY(QFile::copy(lower, upper));

    {
        RecoveryManager recovery(m_logs.path());
        Document first;
        Document second;
        QVERIFY(first.load(lower));
        QVERIFY(second.load(upper));
        recovery.watch(&first);
        recovery.watch(&second);
        type(first, QStringLiteral("lower "), {0, 0});
        type(second, QStringLiteral("upper "), {0, 0});
        recovery.flush();
        QCOMPARE(logs().size(), 2);
    }

    RecoveryManager recovery(m_logs.path());
    Document first;
    Document second;
    QVERIFY(first.load(lower));
    QVERIFY(second.load(upper));
    QVERIFY(recovery.restore(&first));
    QVERIFY(recovery.restore(&second));
    QCOMPARE(text(first), QStringLiteral("lower same"));
    QCOMPARE(text(second), QStringLiteral("upper same"));
}

QString RecoveryTest::text(const Document &doc)
{
    const auto &b = doc.buffer();
    return b.text({0, 0}, b.positionOf(b.length()));
}

void RecoveryTest::type(Document &doc, const QString &chars, Document::Position pos)
{
    for (QChar c : chars)
        pos = doc.replace(pos, pos, QString(c), Document::EditKind::Typing, {});
}

void RecoveryTest::editsSurviveRestart()
{
    const QString path = writeFile("hello\nworld");
    {
        RecoveryManager recovery(m_logs.path());
        Document doc;
        QVERIFY(doc.load(path));
        recovery.watch(&doc);
        type(doc, QStringLiteral(" there"), {0, 5});
        doc.replace({1, 0}, {1, 5}, QStringLiteral("мир"), Document::EditKind::Other, {});
        QCOMPARE(text(doc), QStringLiteral("hello there\nмир"));
    } // «сбой»: менеджер и документ исчезли, журнал остался на диске

    RecoveryManager recovery(m_logs.path());
    QCOMPARE(recovery.unsavedFiles(), QStringList{path});
    Document doc;
    QVERIFY(doc.load(path));
    recovery.watch(&doc);
    QVERIFY(recovery.restore(&doc));
    QCOMPARE(text(doc), QStringLiteral("hello there\nмир"));
    QVERIFY(doc.isDirty());

    Document::Selections sel;
    QVERIFY(doc.undo(&sel)); // восстановление — один шаг, отмена ведёт к версии с диска
    QCOMPARE(text(doc), QStringLiteral("hello\nworld"));
    QVERIFY(!doc.isDirty());
}

void RecoveryTest::saveRemovesJournal()
{
    const QString path = writeFile("abc");
    RecoveryManager recovery(m_logs.path());
    Document doc;
    QVERIFY(doc.load(path));
    recovery.watch(&doc);
    type(doc, QStringLiteral("x"), {0, 0});
    recovery.flush();
    QCOMPARE(logs().size(), 1);

    QVERIFY(doc.save());
    recovery.flush();
    QVERIFY(logs().isEmpty());
}

void RecoveryTest::undoToDiskRemovesJournal()
{
    const QString path = writeFile("abc");
    RecoveryManager recovery(m_logs.path());
    Document doc;
    QVERIFY(doc.load(path));
    recovery.watch(&doc);
    type(doc, QStringLiteral("x"), {0, 0});
    recovery.flush();
    Document::Selections sel;
    QVERIFY(doc.undo(&sel));
    recovery.flush();
    QVERIFY(logs().isEmpty());

    type(doc, QStringLiteral("y"), {0, 0}); // новая правка — новый журнал от версии с диска
    recovery.flush();
    Document again;
    QVERIFY(again.load(path));
    QVERIFY(recovery.restore(&again));
    QCOMPARE(text(again), QStringLiteral("yabc"));
}

void RecoveryTest::changedOnDiskIsSkipped()
{
    const QString path = writeFile("abc");
    {
        RecoveryManager recovery(m_logs.path());
        Document doc;
        QVERIFY(doc.load(path));
        recovery.watch(&doc);
        type(doc, QStringLiteral("x"), {0, 0});
    }
    QFile f(path); // файл поменяли снаружи, пока редактор был закрыт
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("changed elsewhere, longer");
    f.close();

    RecoveryManager recovery(m_logs.path());
    Document doc;
    QVERIFY(doc.load(path));
    QVERIFY(!recovery.restore(&doc));
    QCOMPARE(text(doc), QStringLiteral("changed elsewhere, longer"));
    QVERIFY(logs().isEmpty());
}

void RecoveryTest::truncatedTailIsIgnored()
{
    const QString path = writeFile("abc");
    {
        RecoveryManager recovery(m_logs.path());
        Document doc;
        QVERIFY(doc.load(path));
        recovery.watch(&doc);
        type(doc, QStringLiteral("12"), {0, 3});
    }
    // Сбой посреди записи: в конце журнала обрывок следующей записи
    const QString log = m_logs.path() + u'/' + logs().constFirst();
    QFile f(log);
    QVERIFY(f.open(QIODevice::Append));
    f.write("\x00\x00\x00", 3);
    f.close();

    RecoveryManager recovery(m_logs.path());
    Document doc;
    QVERIFY(doc.load(path));
    QVERIFY(recovery.restore(&doc));
    QCOMPARE(text(doc), QStringLiteral("abc12"));
}

void RecoveryTest::discardOnClose()
{
    const QString path = writeFile("abc");
    RecoveryManager recovery(m_logs.path());
    Document doc;
    QVERIFY(doc.load(path));
    recovery.watch(&doc);
    type(doc, QStringLiteral("x"), {0, 0});
    recovery.flush();
    QCOMPARE(logs().size(), 1);
    recovery.discard(&doc);
    QVERIFY(logs().isEmpty());
    type(doc, QStringLiteral("y"), {0, 0}); // больше не отслеживается
    recovery.flush();
    QVERIFY(logs().isEmpty());
}

QTEST_GUILESS_MAIN(RecoveryTest)
#include "recovery_test.moc"
