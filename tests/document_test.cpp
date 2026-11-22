#include <QSignalSpy>
#include <QStandardPaths>
#include <QThreadPool>
#include "perfcheck.h"
#include <QTemporaryDir>
#include <QtTest>
#include "src/core/document.h"
#include "src/services/documentmanager.h"

using core::Document;
using core::TextBuffer;

class DocumentTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void syncLoadSmallFile();
    void asyncLoadWithProgress();
    void destroyedDuringAsyncLoad();
    void duplicateAsyncOpen();
    void failedSaveKeepsTab();
    void encodingFailurePreservesFile();
    void savesWithSelectedLineEnding();
    void undoRedoRestoresTextAndSelection();
    void typingMergesUntilBreak();
    void newEditDropsRedo();
    void dirtyFollowsHistory();
    void crlfPasteIsNormalized();
    void thousandUndosAreFast();
    void multiReplaceIsOneStep();
    void multiReplaceMixedLengths();
    void thousandCaretsAreFast();
    void asyncSaveRewritesMappedFile();
    void asyncSaveAsRedirectsMappedFile();
    void asyncSaveCancelKeepsFile();
    void takeAndAdoptMovesDocument();

private:
    static QString text(const Document &doc);
    static Document::Selections at(int line, int column) { return {{{line, column}, {line, column}}}; }
    static void type(Document &doc, const QString &chars, Document::Position pos);
};

void DocumentTest::destroyedDuringAsyncLoad()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("loading.txt");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(4 * 1024 * 1024));
    file.close();
    {
        Document doc;
        QVERIFY(doc.loadAsync(path));
    }
    QThreadPool::globalInstance()->waitForDone();
}

void DocumentTest::duplicateAsyncOpen()
{
    QTemporaryDir dir;
    QVERIFY(QDir(dir.path()).mkdir("alias"));
    const QString path = dir.filePath("large.txt");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(21 * 1024 * 1024));
    file.close();

    DocumentManager manager;
    QSignalSpy completed(&manager, &DocumentManager::asyncOpenFinished);
    QCOMPARE(manager.openPath(path), -1);
    QVERIFY(manager.isLoading());
    QVERIFY(!manager.pendingOpenKey(path).isEmpty());
    QCOMPARE(manager.openPath(dir.filePath("alias/../large.txt")), -1);
    QTRY_VERIFY(!manager.isLoading());
    QCOMPARE(completed.size(), 1);
    QCOMPARE(completed.first().at(1).value<QObject *>(), manager.documentAt(0));
    QCOMPARE(manager.documents().size(), 1);
    QCOMPARE(manager.openPath(path), 0);
#ifdef Q_OS_WIN
    QCOMPARE(manager.openPath(dir.filePath("LARGE.txt")), 0);
#endif
    QCOMPARE(manager.documents().size(), 1);
}

void DocumentTest::failedSaveKeepsTab()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("document.txt");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("original"), 8);
    file.close();

    DocumentManager manager;
    QCOMPARE(manager.openPath(path), 0);
    auto *doc = manager.documents().constFirst();
    type(*doc, QStringLiteral("edited "), {0, 0});
    QVERIFY(file.remove());
    QVERIFY(QDir().mkdir(path));
    QSignalSpy errors(&manager, &DocumentManager::errorOccurred);
    manager.saveAndClose(doc);
    QCOMPARE(errors.size(), 1);
    QCOMPARE(manager.documentAt(0), doc);
    QVERIFY(doc->isDirty());
    QCOMPARE(text(*doc), QStringLiteral("edited original"));
    QVERIFY(QDir().rmdir(path));
    manager.saveAndClose(doc);
    QVERIFY(manager.documents().isEmpty());
}

void DocumentTest::encodingFailurePreservesFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("encoded.txt");
    const QByteArray original = QByteArray::fromHex("cff0e8e2e5f2");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(original), original.size());
    file.close();

    Document doc;
    QVERIFY(doc.load(path));
    QCOMPARE(doc.buffer().encoding(), core::Encoding::Cp1251);
    type(doc, QStringLiteral("\u4f60"), {0, 6});
    QString error;
    QVERIFY(!doc.save(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(doc.isDirty());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
    file.close();

    QVERIFY(doc.saveAsUtf8(&error));
    QVERIFY(!doc.isDirty());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), text(doc).toUtf8());
}

void DocumentTest::savesWithSelectedLineEnding()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("line-endings.txt");
    Document doc;
    type(doc, QStringLiteral("first\nsecond"), {0, 0});
    QVERIFY(doc.saveAs(path));
    QVERIFY(doc.saveWithLineEnding(core::LineEnding::Crlf, nullptr));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("first\r\nsecond"));
    QCOMPARE(doc.buffer().lineEnding(), core::LineEnding::Crlf);
    QCOMPARE(text(doc), QStringLiteral("first\nsecond"));
}

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
    PERF_VERIFY2(undoMs < 100 && redoMs < 100,
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
    PERF_VERIFY2(editMs < 50 && undoMs < 50,
             qPrintable(QStringLiteral("edit %1 ms, undo %2 ms").arg(editMs).arg(undoMs)));
}

void DocumentTest::asyncSaveRewritesMappedFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("big.txt");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        for (int i = 0; i < 42000; ++i) // ~300 КБ — больше порога отображения
            file.write("строка " + QByteArray::number(i) + "\r\n");
    }

    Document::setMappedThreshold(256 * 1024);
    Document doc;
    QSignalSpy loadFinished(&doc, &Document::loadFinished);
    QVERIFY(doc.loadAsync(path));
    QVERIFY(loadFinished.wait(20000));
    QVERIFY(doc.buffer().isMapped());
    QCOMPARE(doc.buffer().lineCount(), 42001); // завершающий CRLF даёт пустую последнюю строку

    doc.replace({0, 0}, {0, 0}, QStringLiteral("ХЕДЕР"), Document::EditKind::Other, {});
    QSignalSpy saving(&doc, &Document::saveStateChanged);
    QSignalSpy progress(&doc, &Document::saveProgress);
    QSignalSpy finished(&doc, &Document::saveFinished);
    QObject::connect(&doc, &Document::saveFinished, &doc, [](bool ok, const QString &error) {
        if (!ok)
            qWarning().noquote() << "saveAsync error:" << error;
    });
    QVERIFY(doc.saveAsync());
    QVERIFY(finished.wait(30000));
    QTRY_COMPARE(saving.last().first().toBool(), false);
    QVERIFY(progress.size() > 0);
    QCOMPARE(finished.first().first().toBool(), true);
    QVERIFY(!doc.isSaving());
    QVERIFY(!doc.isDirty());

    // На диске: заголовок, CRLF сохранены, temp убран
    QFile written(path);
    QVERIFY(written.open(QIODevice::ReadOnly));
    const QByteArray bytes = written.readAll();
    QVERIFY(bytes.startsWith("ХЕДЕРстрока 0\r\n"));
    QVERIFY(bytes.endsWith("\r\n"));
    QVERIFY(bytes.contains("строка 41999\r\n"));
    QVERIFY(!QFile::exists(path + ".mca-save"));

    // Undo переживает сохранение: ретаргет базы на новое отображение
    Document::Selections sel;
    QVERIFY(doc.undo(&sel));
    QVERIFY(text(doc).startsWith("строка 0"));
    QVERIFY(!doc.changedOnDisk()); // отмена не делает файл «чужим»

    QThreadPool::globalInstance()->waitForDone();
    Document::setMappedThreshold(256 << 20);
}

void DocumentTest::asyncSaveAsRedirectsMappedFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("source.txt");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        for (int i = 0; i < 42000; ++i)
            file.write("row " + QByteArray::number(i) + "\n");
    }

    Document::setMappedThreshold(256 * 1024);
    Document doc;
    QSignalSpy loadFinished(&doc, &Document::loadFinished);
    QVERIFY(doc.loadAsync(path));
    QVERIFY(loadFinished.wait(20000));
    doc.replace({0, 0}, {0, 0}, QStringLiteral("::"), Document::EditKind::Other, {});

    const QString other = dir.filePath("copy.txt");
    QSignalSpy finished(&doc, &Document::saveFinished);
    QVERIFY(doc.saveAsyncAt(other, false));
    QVERIFY(finished.wait(30000));
    QCOMPARE(finished.first().first().toBool(), true);
    QCOMPARE(doc.filePath(), other);
    QVERIFY(!QFile::exists(other + ".mca-save"));

    // Исходный файл не тронут, в новом — правка
    QFile source(path);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QVERIFY(source.readAll().startsWith("row 0\n"));
    QFile saved(other);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QVERIFY(saved.readAll().startsWith("::row 0\n"));
    QVERIFY(!doc.isDirty());

    QThreadPool::globalInstance()->waitForDone();
    Document::setMappedThreshold(256 << 20);
}

void DocumentTest::asyncSaveCancelKeepsFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("data.txt");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        for (int i = 0; i < 42000; ++i)
            file.write("item " + QByteArray::number(i) + "\n");
    }

    Document::setMappedThreshold(256 * 1024);
    {
        // Отмена: документ уничтожен во время записи — файл не переименовывается,
        // temp убирается, вкладка не теряет журнал (протестировано сохранением вкладок)
        Document doc;
        QSignalSpy loadFinished(&doc, &Document::loadFinished);
        QVERIFY(doc.loadAsync(path));
        QVERIFY(loadFinished.wait(20000));
        doc.replace({0, 0}, {0, 0}, QStringLiteral("###"), Document::EditKind::Other, {});
        QVERIFY(doc.isDirty());
        QVERIFY(doc.saveAsync());
    } // деструктор — cancelSave()
    QThreadPool::globalInstance()->waitForDone();
    QTRY_VERIFY(!QFile::exists(path + ".mca-save"));

    QFile original(path);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QVERIFY(original.readAll().startsWith("item 0\n"));

    Document::setMappedThreshold(256 << 20);
}

void DocumentTest::takeAndAdoptMovesDocument()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("detach.txt");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("first\nsecond\n");
    }

    DocumentManager windowA, windowB;
    const int index = windowA.openPath(path);
    QVERIFY(index >= 0);
    Document *doc = qobject_cast<Document *>(windowA.documentAt(index));
    QVERIFY(doc);
    QCOMPARE(doc->parent(), &windowA);
    doc->replace({0, 0}, {0, 0}, QStringLiteral("X"), Document::EditKind::Other, {});
    QVERIFY(windowA.isDirty(index));

    QSignalSpy tabsA(&windowA, &DocumentManager::tabsChanged);
    QObject *moved = windowA.takeDocument(index);
    QCOMPARE(moved, doc);
    QCOMPARE(doc->parent(), nullptr); // владелец сменился
    QCOMPARE(tabsA.size(), 1);
    QCOMPARE(windowA.tabs().size(), 0);
    QCOMPARE(windowA.currentIndex(), -1);

    QSignalSpy tabsB(&windowB, &DocumentManager::tabsChanged);
    windowB.adopt(doc);
    QCOMPARE(doc->parent(), &windowB);
    QCOMPARE(tabsB.size(), 1);
    QCOMPARE(windowB.tabs().size(), 1);
    QCOMPARE(windowB.currentIndex(), 0);
    QVERIFY(windowB.isDirty(0));
    QCOMPARE(static_cast<Document *>(windowB.documentAt(0)), doc);
    QCOMPARE(text(*doc), QStringLiteral("Xfirst\nsecond\n")); // правки переехали

    // Пустой менеджер: take несуществующей вкладки безопасен
    QVERIFY(windowA.takeDocument(0) == nullptr);

    // Обратно: забрать и вернуть в первое окно
    QCOMPARE(windowB.takeDocument(0), doc);
    QCOMPARE(windowB.tabs().size(), 0);
    windowA.adopt(doc);
    QCOMPARE(windowA.tabs().size(), 1);
    QCOMPARE(windowA.currentIndex(), 0);
    QCOMPARE(windowA.documentAt(0), doc);

    // Повторный adopt того же документа не плодит вкладки
    windowA.adopt(doc);
    QCOMPARE(windowA.tabs().size(), 1);

    windowA.close(0);
    QTRY_COMPARE(windowA.tabs().size(), 0);
}

QTEST_MAIN(DocumentTest)
#include "document_test.moc"
