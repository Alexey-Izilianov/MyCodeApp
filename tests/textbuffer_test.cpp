#include <QtTest>
#include "src/core/textbuffer.h"
#include "src/core/document.h"

using namespace core;

class TextBufferTest : public QObject {
    Q_OBJECT

private slots:
    void detectEncodingUtf8Bom();
    void detectEncodingUtf16Le();
    void detectEncodingCp1251();
    void detectLatin1();
    void lineEndingsCrlfLfCr();
    void insertSimple();
    void insertMultiline();
    void removeWithinLine();
    void removeAcrossLines();
    void editJournal();
    void saveLoadRoundTrip();
    void documentDirty();
    void mappedMatchesLoaded();
    void mappedEditLikeLoaded();
    void mappedStreamSave();
    void mappedStreamSaveLatin1Error();
    void mappedRetarget();

private:
    static QString writeTemp(const QByteArray &bytes, const QString &suffix);
};

QString TextBufferTest::writeTemp(const QByteArray &bytes, const QString &suffix)
{
    const QString path = QDir::tempPath() + "/mca_test_" + suffix;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(bytes);
    return path;
}

void TextBufferTest::detectEncodingUtf8Bom()
{
    const QString path = writeTemp("\xEF\xBB\xBF/* -*- C++ -*- */\nint x;\n", "u8");
    TextBuffer b;
    QVERIFY(b.load(path));
    QCOMPARE(b.encoding(), Encoding::Utf8);
    QCOMPARE(b.lineCount(), 3);
}

void TextBufferTest::detectEncodingUtf16Le()
{
    // Строковый литерал с \x00 усекается strlen'ом — собираем байты явно.
    QByteArray bytes;
    bytes.append(char(0xFF)).append(char(0xFE));
    bytes.append('h').append(char(0)).append('i').append(char(0));
    bytes.append('\n').append(char(0));
    const QString path = writeTemp(bytes, "u16");
    TextBuffer b;
    QVERIFY(b.load(path));
    QCOMPARE(b.encoding(), Encoding::Utf16Le);
    QCOMPARE(b.lineCount(), 2);
    QCOMPARE(b.lineAt(0), QStringLiteral("hi"));
}

void TextBufferTest::detectEncodingCp1251()
{
    // «Привет» в cp1251: CF F0 E8 E2 E5 F2 — валидный UTF-8 из этого не выходит.
    const QString path = writeTemp("\xCF\xF0\xE8\xE2\xE5\xF2\r\n", "cp1251");
    TextBuffer b;
    QVERIFY(b.load(path));
    QCOMPARE(b.encoding(), Encoding::Cp1251);
    QCOMPARE(b.lineAt(0), QStringLiteral("Привет"));
    QCOMPARE(b.lineEnding(), LineEnding::Crlf);
}

void TextBufferTest::detectLatin1()
{
    // «café» в Latin-1: 63 61 66 E9 — валидный UTF-8 не получается,
    // старший байт один и меньше порога cp1251-эвристики.
    const QString path = writeTemp("caf\xE9\ncaf\xE9\n", "l1");
    TextBuffer b;
    QVERIFY(b.load(path));
    QCOMPARE(b.encoding(), Encoding::Latin1);
    QCOMPARE(b.lineAt(0), QString::fromLatin1("caf\xE9"));
}

void TextBufferTest::lineEndingsCrlfLfCr()
{
    {
        const QString path = writeTemp("a\r\nb\r\n", "crlf");
        TextBuffer b;
        QVERIFY(b.load(path));
        QCOMPARE(b.lineEnding(), LineEnding::Crlf);
        QCOMPARE(b.lineCount(), 3);
    }
    {
        const QString path = writeTemp("a\nb", "lf");
        TextBuffer b;
        QVERIFY(b.load(path));
        QCOMPARE(b.lineEnding(), LineEnding::Lf);
        QCOMPARE(b.lineCount(), 2);
    }
    {
        const QString path = writeTemp("a\rb\r", "cr");
        TextBuffer b;
        QVERIFY(b.load(path));
        QCOMPARE(b.lineEnding(), LineEnding::Cr);
        QCOMPARE(b.lineCount(), 3);
    }
}

void TextBufferTest::insertSimple()
{
    TextBuffer b;
    b.load(QDir::tempPath() + "/mca_test_lf"); // "a\nb"
    TextBuffer::Position pos{0, 1};
    b.insertText(pos, "bc");
    QCOMPARE(b.lineAt(0), QStringLiteral("abc"));
}

void TextBufferTest::insertMultiline()
{
    const QString path = writeTemp("one\ntwo\n", "ml");
    TextBuffer b;
    QVERIFY(b.load(path));
    b.insertText(TextBuffer::Position{0, 3}, "x\ny");
    QCOMPARE(b.lineCount(), 4);
    QCOMPARE(b.lineAt(0), QStringLiteral("onex"));
    QCOMPARE(b.lineAt(1), QStringLiteral("y"));
    QCOMPARE(b.lineAt(2), QStringLiteral("two"));
}

void TextBufferTest::removeWithinLine()
{
    const QString path = writeTemp("hello\n", "rm1");
    TextBuffer b;
    QVERIFY(b.load(path));
    b.removeText(TextBuffer::Position{0, 1}, TextBuffer::Position{0, 4});
    QCOMPARE(b.lineAt(0), QStringLiteral("ho"));
}

void TextBufferTest::removeAcrossLines()
{
    const QString path = writeTemp("first\nsecond\nthird\n", "rm2");
    TextBuffer b;
    QVERIFY(b.load(path));
    b.removeText(TextBuffer::Position{0, 2}, TextBuffer::Position{1, 3});
    QCOMPARE(b.lineCount(), 3);
    QCOMPARE(b.lineAt(0), QStringLiteral("fiond"));
    QCOMPARE(b.lineAt(1), QStringLiteral("third"));
}

void TextBufferTest::editJournal()
{
    TextBuffer b;
    QVERIFY(b.loadFromData("ab\ncd"));
    QVERIFY(b.takeEdits().isEmpty());

    b.insertText({0, 1}, QStringLiteral("X\nYZ"));    // aX / YZb / cd
    b.removeText({1, 1}, {2, 1});                       // aX / Yd
    bool overflow = true;
    const QVector<TextBuffer::Edit> edits = b.takeEdits(&overflow);
    QVERIFY(!overflow);
    QCOMPARE(edits.size(), 2);

    const TextBuffer::Edit &ins = edits.at(0);
    QCOMPARE(ins.startOffset, 1);
    QCOMPARE(ins.oldEndOffset, 1);
    QCOMPARE(ins.newEndOffset, 5);
    QCOMPARE(ins.newEnd, (TextBuffer::Position{1, 2}));

    const TextBuffer::Edit &rem = edits.at(1);
    QCOMPARE(rem.startOffset, 4);
    QCOMPARE(rem.oldEndOffset, 8);
    QCOMPARE(rem.newEndOffset, 4);
    QCOMPARE(rem.start, (TextBuffer::Position{1, 1}));
    QCOMPARE(rem.oldEnd, (TextBuffer::Position{2, 1}));
    QCOMPARE(rem.newEnd, rem.start);
    QVERIFY(b.takeEdits().isEmpty());
}

void TextBufferTest::saveLoadRoundTrip()
{
    const QString path = writeTemp("", "rt");
    TextBuffer b;
    QVERIFY(b.load(writeTemp("one\ntwo\n", "rt_in")));
    QCOMPARE(b.lineCount(), 3);
    QString saveError;
    QVERIFY2(b.save(path, &saveError), qPrintable(saveError));

    TextBuffer b2;
    QVERIFY(b2.load(path));
    QCOMPARE(b2.lineCount(), 3);
    QCOMPARE(b2.lineAt(0), QStringLiteral("one"));
    QCOMPARE(b2.lineAt(2), QString());
}

void TextBufferTest::documentDirty()
{
    const QString path = writeTemp("x\n", "doc");
    Document doc;
    QVERIFY(doc.load(path));
    QCOMPARE(doc.isDirty(), false);

    doc.replace({0, 1}, {0, 1}, QStringLiteral("y"), Document::EditKind::Other, {});
    QCOMPARE(doc.isDirty(), true);
    QCOMPARE(doc.displayName(), QFileInfo(path).fileName());

    QString docSaveError;
    QVERIFY2(doc.save(&docSaveError), qPrintable(docSaveError));
    QCOMPARE(doc.isDirty(), false);
}

void TextBufferTest::mappedMatchesLoaded()
{
    // Больше kCheckpoint строк, CRLF, кириллица, суррогатная пара, BOM
    QByteArray bytes = "\xEF\xBB\xBF";
    for (int i = 0; i < 40; ++i)
        bytes += "line " + QByteArray::number(i) + (i % 3 ? " \xD0\xBF\xD1\x80\xD0\xB8 \xF0\x9F\x98\x80" : "") + "\r\n";
    bytes += "tail";
    const QString path = writeTemp(bytes, "mapped.txt");

    TextBuffer loaded, mapped;
    QVERIFY(loaded.load(path));
    QVERIFY(mapped.loadMapped(path));
    QVERIFY(mapped.isMapped());
    QCOMPARE(mapped.lineEnding(), LineEnding::Crlf);
    QCOMPARE(mapped.length(), loaded.length());
    QCOMPARE(mapped.lineCount(), loaded.lineCount());
    for (int i = 0; i < loaded.lineCount(); ++i)
        QCOMPARE(mapped.lineAt(i), loaded.lineAt(i));
    QCOMPARE(mapped.textAt(100, 300), loaded.textAt(100, 300)); // через несколько строк
    QCOMPARE(mapped.snapshot().toString(), loaded.snapshot().toString());

    int visited = 0;
    mapped.snapshot().forEachLine([&](int line, const QString &text) {
        visited += text == loaded.lineAt(line);
        return true;
    });
    QCOMPARE(visited, loaded.lineCount());
    // С середины, между контрольными точками (каждая 16-я строка)
    visited = 0;
    mapped.snapshot().forEachLine([&](int line, const QString &text) {
        visited += line >= 19 && text == loaded.lineAt(line);
        return true;
    }, 19);
    QCOMPARE(visited, loaded.lineCount() - 19);
    QVERIFY(!mapped.save(path));
}

void TextBufferTest::mappedEditLikeLoaded()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("edit_source.txt");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        for (int i = 0; i < 30; ++i)
            file.write(("строка " + QByteArray::number(i) + " \xF0\x9F\x98\x80\r\n").constData());
    }

    TextBuffer mapped, loaded;
    QVERIFY(mapped.loadMapped(path));
    QVERIFY(loaded.load(path));
    QVERIFY(mapped.isMapped());

    // Одинаковые правки: вставка с переносом, замена в середине, удаление поперёк строк, конец
    const auto edit = [](TextBuffer &b) {
        b.insertText({0, 0}, QStringLiteral("первая\n"));
        b.insertText({2, 0}, QStringLiteral("[середина] "));
        b.removeText({3, 0}, {5, 0});
        b.insertText({b.lineCount() - 1, 0}, QStringLiteral("хвост "));
    };
    edit(mapped);
    edit(loaded);
    QCOMPARE(mapped.length(), loaded.length());
    QCOMPARE(mapped.lineCount(), loaded.lineCount());
    for (int i = 0; i < loaded.lineCount(); ++i)
        QCOMPARE(mapped.lineAt(i), loaded.lineAt(i));
    QCOMPARE(mapped.textAt(0, mapped.length()), loaded.textAt(0, loaded.length()));
    QCOMPARE(mapped.snapshot().toString(), loaded.snapshot().toString());

    // Снимок сделан до правок — правки его не трогают
    TextBuffer mapped2, loaded2;
    QVERIFY(mapped2.loadMapped(path));
    QVERIFY(loaded2.load(path));
    const auto snapshot = mapped2.snapshot();
    edit(mapped2);
    QCOMPARE(snapshot.toString(), loaded2.textAt(0, loaded2.length()));
}

void TextBufferTest::mappedStreamSave()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("crlf_source.txt");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("\xEF\xBB\xBF");
        for (int i = 0; i < 25; ++i)
            file.write("row " + QByteArray::number(i) + "\r\n");
    }
    TextBuffer b;
    QVERIFY(b.loadMapped(path));
    QVERIFY(b.mappedBaseHasBom());
    b.insertText({0, 0}, QStringLiteral("голова\n"));

    const QString out = dir.filePath("saved.txt");
    QString error;
    int sawProgress = -1;
    QVERIFY2(core::streamSave(b.snapshot(), b.encoding(), b.lineEnding(), b.mappedBaseHasBom(),
                              out,
                              [&](int percent) { sawProgress = percent; return true; },
                              &error),
             qPrintable(error));
    QVERIFY(sawProgress >= 0);

    // Побайтово: BOM, CRLF, правка
    QFile written(out);
    QVERIFY(written.open(QIODevice::ReadOnly));
    const QByteArray bytes = written.readAll();
    QVERIFY(bytes.startsWith("\xEF\xBB\xBFголова\r\nrow 0\r\n"));
    QVERIFY(bytes.endsWith("row 24\r\n"));

    // Логический текст после перечитывания совпадает
    TextBuffer check;
    QVERIFY(check.load(out));
    QCOMPARE(check.lineCount(), b.lineCount());
    for (int i = 0; i < b.lineCount(); ++i)
        QCOMPARE(check.lineAt(i), b.lineAt(i));

    // Отмена: файл не остаётся на диске
    QVERIFY(!core::streamSave(b.snapshot(), b.encoding(), b.lineEnding(), true,
                              dir.filePath("cancelled.txt"),
                              [](int) { return false; }, &error));
    QVERIFY(!QFile::exists(dir.filePath("cancelled.txt")));
}

void TextBufferTest::mappedStreamSaveLatin1Error()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("latin.txt");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("caf\xE9 latin\nbytes\n");
    }

    TextBuffer b;
    QVERIFY(b.loadMapped(path)); // один старший байт не в 0xC0.. — Latin-1
    QCOMPARE(b.encoding(), Encoding::Latin1);
    b.insertText({1, 0}, QStringLiteral("Ω")); // символа в Latin-1 нет

    QString error;
    QVERIFY(!core::streamSave(b.snapshot(), b.encoding(), b.lineEnding(), false,
                              dir.filePath("latin_out.txt"), {}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFile::exists(dir.filePath("latin_out.txt")));
}

void TextBufferTest::mappedRetarget()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("retarget_source.txt");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        for (int i = 0; i < 25; ++i)
            file.write("line " + QByteArray::number(i) + "\n");
    }
    TextBuffer b;
    QVERIFY(b.loadMapped(path));
    b.insertText({0, 0}, QStringLiteral("start\n"));
    b.insertText({10, 3}, QStringLiteral(" mid"));
    const QString snapshotText = b.snapshot().toString();

    const QString out = dir.filePath("retarget_saved.txt");
    QString error;
    QVERIFY2(core::streamSave(b.snapshot(), b.encoding(), b.lineEnding(), false, out, {}, &error),
             qPrintable(error));
    const auto newBase = core::MappedText::open(out, true);
    QVERIFY(newBase);
    b.retargetMappedBase(newBase);

    // Куски базы теперь ссылаются на новое отображение — текст тот же
    QCOMPARE(b.textAt(0, b.length()), snapshotText);
    QCOMPARE(b.snapshot().toString(), snapshotText);
    for (int i = 0; i < b.lineCount(); ++i)
        QCOMPARE(b.lineAt(i), b.snapshot().line(i));
}

QTEST_GUILESS_MAIN(TextBufferTest)
#include "textbuffer_test.moc"