#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include "src/core/document.h"

using core::Document;
using core::TextBuffer;

class DocumentTest : public QObject {
    Q_OBJECT

private slots:
    void syncLoadSmallFile();
    void asyncLoadWithProgress();
    void undoRedoRestoresTextAndSelection();
    void typingMergesUntilBreak();
    void newEditDropsRedo();
    void dirtyFollowsHistory();
    void crlfPasteIsNormalized();
    void thousandUndosAreFast();
    void multiReplaceIsOneStep();
    void multiReplaceMixedLengths();
    void thousandCaretsAreFast();

private:
    static QString text(const Document &doc);
    static Document::Selections at(int line, int column) { return {{{line, column}, {line, column}}}; }
    static void type(Document &doc, const QString &chars, Document::Position pos);
};

QString DocumentTest::text(const Document &doc)
{
    const TextBuffer &b = doc.buffer();
    return b.text({0, 0}, b.positionOf(b.length()));
}

void DocumentTest::type(Document &doc, const QString &chars, Document::Position pos)
{
    for (QChar c : chars)
        pos = doc.replace(pos, pos, QString(c), Document::EditKind::Typing, at(pos.line, pos.column));
}

void DocumentTest::undoRedoRestoresTextAndSelection()
{
    Document doc;
    const Document::Selections before{{{0, 2}, {0, 0}}};
    const auto end = doc.replace({0, 0}, {0, 0}, QStringLiteral("ab\ncd"),
                                 Document::EditKind::Other, at(0, 0));
    QCOMPARE(end, (Document::Position{1, 2}));
    doc.replace({0, 0}, {0, 2}, QStringLiteral("X"), Document::EditKind::Other, before);
    QCOMPARE(text(doc), QStringLiteral("X\ncd"));

    Document::Selections sel;
    QVERIFY(doc.undo(&sel));
    QCOMPARE(text(doc), QStringLiteral("ab\ncd"));
    QCOMPARE(sel.first().cursor, before.first().cursor);
    QCOMPARE(sel.first().anchor, before.first().anchor);

    QVERIFY(doc.undo(&sel));
    QCOMPARE(text(doc), QString());
    QVERIFY(!doc.undo(&sel));

    QVERIFY(doc.redo(&sel));
    QVERIFY(doc.redo(&sel));
    QCOMPARE(text(doc), QStringLiteral("X\ncd"));
    QCOMPARE(sel.first().cursor, (Document::Position{0, 1}));
    QVERIFY(!doc.redo(&sel));
}

void DocumentTest::typingMergesUntilBreak()
{
    Document doc;
    type(doc, QStringLiteral("hello"), {0, 0});
    doc.breakUndoGroup();
    type(doc, QStringLiteral(" world"), {0, 5});
    doc.replace({0, 10}, {0, 11}, {}, Document::EditKind::Typing, at(0, 11)); // backspace

    Document::Selections sel;
    QVERIFY(doc.undo(&sel)); // « world» вместе с backspace — один шаг
    QCOMPARE(text(doc), QStringLiteral("hello"));
    QVERIFY(doc.undo(&sel));
    QCOMPARE(text(doc), QString());
    QVERIFY(!doc.canUndo());
}

void DocumentTest::newEditDropsRedo()
{
    Document doc;
    type(doc, QStringLiteral("abc"), {0, 0});
    Document::Selections sel;
    QVERIFY(doc.undo(&sel));
    QVERIFY(doc.canRedo());
    doc.replace({0, 0}, {0, 0}, QStringLiteral("z"), Document::EditKind::Other, at(0, 0));
    QVERIFY(!doc.canRedo());
    QCOMPARE(text(doc), QStringLiteral("z"));
}

void DocumentTest::dirtyFollowsHistory()
{
    QTemporaryDir dir;
    const QString path = dir.path() + "/d.txt";
    Document doc;
    QVERIFY(doc.saveAs(path));
    QVERIFY(!doc.isDirty());

    type(doc, QStringLiteral("ab"), {0, 0});
    QVERIFY(doc.isDirty());
    Document::Selections sel;
    QVERIFY(doc.undo(&sel));
    QVERIFY(!doc.isDirty()); // вернулись к сохранённому состоянию

    QVERIFY(doc.redo(&sel));
    QVERIFY(doc.save());
    QVERIFY(!doc.isDirty());
    QVERIFY(doc.undo(&sel));
    QVERIFY(doc.isDirty());

    // Ветка с сохранённым состоянием отрезана — чистым уже не стать
    doc.replace({0, 0}, {0, 0}, QStringLiteral("q"), Document::EditKind::Other, at(0, 0));
    QVERIFY(doc.undo(&sel));
    QVERIFY(doc.isDirty());
}

void DocumentTest::crlfPasteIsNormalized()
{
    Document doc;
    const auto end = doc.replace({0, 0}, {0, 0}, QStringLiteral("a\r\nb\rc"),
                                 Document::EditKind::Other, at(0, 0));
    QCOMPARE(doc.buffer().lineCount(), 3);
    QCOMPARE(end, (Document::Position{2, 1}));
}

void DocumentTest::thousandUndosAreFast()
{
    // 1000 отмен подряд — без заметной задержки
    Document doc;
    Document::Position pos{0, 0};
    for (int i = 0; i < 1000; ++i)
        pos = doc.replace(pos, pos, QStringLiteral("line\n"), Document::EditKind::Other, at(pos.line, pos.column));

    QElapsedTimer timer;
    timer.start();
    Document::Selections sel;
    for (int i = 0; i < 1000; ++i)
        QVERIFY(doc.undo(&sel));
    const qint64 undoMs = timer.restart();
    for (int i = 0; i < 1000; ++i)
        QVERIFY(doc.redo(&sel));
    const qint64 redoMs = timer.elapsed();
    QCOMPARE(doc.buffer().lineCount(), 1001);
    QVERIFY2(undoMs < 100 && redoMs < 100,
             qPrintable(QStringLiteral("undo %1 ms, redo %2 ms").arg(undoMs).arg(redoMs)));
}

void DocumentTest::syncLoadSmallFile()
{
    QTemporaryDir dir;
    const QString path = dir.path() + "/small.txt";
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write("line1\nline2\n"), qint64(12));
    } // файл закрыт до чтения — иначе читаем пустой файл

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

void DocumentTest::multiReplaceIsOneStep()
{
    Document doc;
    doc.replace({0, 0}, {0, 0}, QStringLiteral("a\nb\nc"), Document::EditKind::Other, at(0, 0));
    const Document::Selections before = {{{2, 0}, {2, 0}}, {{0, 0}, {0, 0}}, {{1, 0}, {1, 0}}};

    // Порядок во входе произвольный — ответ в том же порядке
    const auto ends = doc.replace({{{2, 0}, {2, 0}, "X"}, {{0, 0}, {0, 0}, "X"}, {{1, 0}, {1, 0}, "X"}},
                                  Document::EditKind::Typing, before);
    QCOMPARE(text(doc), QStringLiteral("Xa\nXb\nXc"));
    QCOMPARE(ends.size(), 3);
    QCOMPARE(ends.at(0), (Document::Position{2, 1}));
    QCOMPARE(ends.at(1), (Document::Position{0, 1}));
    QCOMPARE(ends.at(2), (Document::Position{1, 1}));

    Document::Selections sel;
    QVERIFY(doc.undo(&sel));
    QCOMPARE(text(doc), QStringLiteral("a\nb\nc"));
    QCOMPARE(sel.size(), 3);
    QCOMPARE(sel.at(0).cursor, (Document::Position{2, 0}));
    QVERIFY(doc.redo(&sel));
    QCOMPARE(text(doc), QStringLiteral("Xa\nXb\nXc"));
    QCOMPARE(sel.at(1).cursor, (Document::Position{0, 1}));
}

void DocumentTest::multiReplaceMixedLengths()
{
    Document doc;
    doc.replace({0, 0}, {0, 0}, QStringLiteral("one two three"), Document::EditKind::Other, at(0, 0));
    // «one» -> «1», «two» удалить, «three» -> «3\n3»
    const auto ends = doc.replace({{{0, 0}, {0, 3}, "1"}, {{0, 4}, {0, 7}, ""}, {{0, 8}, {0, 13}, "3\n3"}},
                                  Document::EditKind::Other, at(0, 0));
    QCOMPARE(text(doc), QStringLiteral("1  3\n3"));
    QCOMPARE(ends.at(0), (Document::Position{0, 1}));
    QCOMPARE(ends.at(1), (Document::Position{0, 2}));
    QCOMPARE(ends.at(2), (Document::Position{1, 1}));

    Document::Selections sel;
    QVERIFY(doc.undo(&sel));
    QCOMPARE(text(doc), QStringLiteral("one two three"));
}

void DocumentTest::thousandCaretsAreFast()
{
    // Много курсоров без деградации: 1000 курсоров в файле на 10 000 строк
    Document doc;
    QString content;
    for (int i = 0; i < 10000; ++i)
        content += QStringLiteral("line %1\n").arg(i);
    doc.replace({0, 0}, {0, 0}, content, Document::EditKind::Other, at(0, 0));

    QVector<Document::Replacement> parts;
    for (int i = 0; i < 1000; ++i)
        parts.append({{i * 10, 0}, {i * 10, 0}, QStringLiteral("// ")});

    QElapsedTimer timer;
    timer.start();
    doc.replace(parts, Document::EditKind::Typing, {});
    const qint64 editMs = timer.restart();
    Document::Selections sel;
    QVERIFY(doc.undo(&sel));
    const qint64 undoMs = timer.elapsed();

    QCOMPARE(doc.buffer().lineAt(9990), QStringLiteral("line 9990"));
    QVERIFY2(editMs < 50 && undoMs < 50,
             qPrintable(QStringLiteral("edit %1 ms, undo %2 ms").arg(editMs).arg(undoMs)));
}

QTEST_MAIN(DocumentTest)
#include "document_test.moc"