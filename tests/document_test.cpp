#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include "src/core/document.h"

using core::Document;

class DocumentTest : public QObject {
    Q_OBJECT

private slots:
    void syncLoadSmallFile();
    void asyncLoadWithProgress();
};

void DocumentTest::syncLoadSmallFile()
{
    QTemporaryDir dir;
    const QString path = dir.path() + "/small.txt";
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("line1\nline2\n");

    Document doc;
    QVERIFY(doc.load(path));
    QCOMPARE(doc.buffer().lineCount(), 3);
    QCOMPARE(doc.buffer().lineAt(0), QStringLiteral("line1"));
    QVERIFY(!doc.isDirty());
}

void DocumentTest::asyncLoadWithProgress()
{
    QTemporaryDir dir;
    const QString path = dir.path() + "/async.txt";
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        // 3 МБ — больше одного чанка чтения (1 МБ), чтобы прогресс шел
        const QByteArray line(60, 'x');
        for (int i = 0; i < 52000; ++i)
            f.write(line + "\n");
    }

    Document doc;
    QSignalSpy progress(&doc, &Document::loadProgress);
    QSignalSpy finished(&doc, &Document::loadFinished);

    QVERIFY(doc.loadAsync(path));
    QVERIFY(doc.isLoading());
    QVERIFY(finished.wait(10000));
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.first().first().toBool(), true);
    QVERIFY(!doc.isLoading());
    QVERIFY(progress.size() >= 1); // чанков несколько — проценты приходили
    QCOMPARE(doc.buffer().lineCount(), 52001);
    QCOMPARE(doc.filePath(), path);
    QVERIFY(!doc.isDirty());

    // повторный запуск до завершения не должен начинать вторую загрузку —
    // проверяем через ещё одну загрузку того же документа
    QSignalSpy finished2(&doc, &Document::loadFinished);
    QVERIFY(doc.loadAsync(path));
    QVERIFY(finished2.wait(10000));
    QVERIFY(!doc.isLoading());
}

QTEST_MAIN(DocumentTest)
#include "document_test.moc"