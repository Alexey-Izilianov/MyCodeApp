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
    void saveLoadRoundTrip();
    void documentDirty();

private:
    static QString writeTemp(const QByteArray &bytes, const QString &suffix);
};

QString TextBufferTest::writeTemp(const QByteArray &bytes, const QString &suffix)
{
    const QString path = QDir::tempPath() + "/mca_test_" + suffix;
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
    f.close();
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

    doc.buffer().insertText(TextBuffer::Position{0, 1}, "y");
    doc.setDirty(true);
    QCOMPARE(doc.isDirty(), true);
    QCOMPARE(doc.displayName(), QFileInfo(path).fileName());

    QString docSaveError;
    QVERIFY2(doc.save(&docSaveError), qPrintable(docSaveError));
    QCOMPARE(doc.isDirty(), false);
}

QTEST_GUILESS_MAIN(TextBufferTest)
#include "textbuffer_test.moc"