#include <QtTest>
#include "src/core/piecetable.h"

using namespace core;

class PieceTableTest : public QObject {
    Q_OBJECT

private slots:
    void resetAndRead();
    void insertAtStart();
    void insertInsidePiece();
    void insertAtEnd();
    void insertMultiline();
    void removeWholePiece();
    void removeWithinPiece();
    void removeSpanningPieces();
    void removeMiddleOfPiece();
    void editNewlineShift();
    void sequenceOfEdits();
    void emptyDocument();
    void snapshotIsolated();
    void snapshotLines();

private:
    static QString textOf(const QVector<int> &lines);
};

QString PieceTableTest::textOf(const QVector<int> &lines)
{
    QString out;
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0)
            out += u'\n';
        out += QStringLiteral("L%1").arg(lines[i]);
    }
    return out;
}

void PieceTableTest::resetAndRead()
{
    PieceTable pt;
    pt.reset(QStringLiteral("one\ntwo\nthree"));
    pt.verify();
    QCOMPARE(pt.length(), 13);
    QCOMPARE(pt.lineCount(), 3);
    QCOMPARE(pt.line(0), QStringLiteral("one"));
    QCOMPARE(pt.line(1), QStringLiteral("two"));
    QCOMPARE(pt.line(2), QStringLiteral("three"));
    QCOMPARE(pt.toString(), QStringLiteral("one\ntwo\nthree"));
}

void PieceTableTest::insertAtStart()
{
    PieceTable pt;
    pt.reset(QStringLiteral("one\ntwo\n"));
    pt.insert(0, QStringLiteral("zero "));
    pt.verify();
    QCOMPARE(pt.toString(), QStringLiteral("zero one\ntwo\n"));
    QCOMPARE(pt.line(0), QStringLiteral("zero one"));
    QCOMPARE(pt.lineCount(), 3);
}

void PieceTableTest::insertInsidePiece()
{
    PieceTable pt;
    pt.reset(QStringLiteral("abcdef\nghijkl"));
    pt.insert(3, QStringLiteral("XY"));
    pt.verify();
    QCOMPARE(pt.toString(), QStringLiteral("abcXYdef\nghijkl"));
    QCOMPARE(pt.lineCount(), 2);
    QCOMPARE(pt.line(0), QStringLiteral("abcXYdef"));
}

void PieceTableTest::insertAtEnd()
{
    PieceTable pt;
    pt.reset(QStringLiteral("abc"));
    pt.insert(3, QStringLiteral("def"));
    pt.verify();
    QCOMPARE(pt.toString(), QStringLiteral("abcdef"));
    QCOMPARE(pt.lineCount(), 1);
}

void PieceTableTest::insertMultiline()
{
    PieceTable pt;
    pt.reset(QStringLiteral("one\ntwo\n"));
    pt.insert(3, QStringLiteral("X\nY")); // в конец первой строки
    pt.verify();
    QCOMPARE(pt.toString(), QStringLiteral("oneX\nY\ntwo\n"));
    QCOMPARE(pt.lineCount(), 4);
    QCOMPARE(pt.line(0), QStringLiteral("oneX"));
    QCOMPARE(pt.line(1), QStringLiteral("Y"));
    QCOMPARE(pt.line(2), QStringLiteral("two"));
    QCOMPARE(pt.line(3), QString());
}

void PieceTableTest::removeWholePiece()
{
    PieceTable pt;
    pt.reset(QStringLiteral("abcdef"));
    pt.insert(6, QStringLiteral("XYZ"));
    pt.remove(6, 3); // только что вставленный кусок целиком
    pt.verify();
    QCOMPARE(pt.toString(), QStringLiteral("abcdef"));
    QCOMPARE(pt.line(0), QStringLiteral("abcdef"));
}

void PieceTableTest::removeWithinPiece()
{
    PieceTable pt;
    pt.reset(QStringLiteral("abcdef\nghijkl"));
    pt.remove(2, 2); // "cd" из первой строки
    pt.verify();
    QCOMPARE(pt.toString(), QStringLiteral("abef\nghijkl"));
    QCOMPARE(pt.line(0), QStringLiteral("abef"));
    QCOMPARE(pt.line(1), QStringLiteral("ghijkl"));
}

void PieceTableTest::removeSpanningPieces()
{
    PieceTable pt;
    pt.reset(QStringLiteral("aaa\nbbb\nccc\nddd"));
    // Удаляем через границу строк: "c\ndd" → текст "aaa\nbbb\ncccddd"
    // точнее: offset = 8 ("aaa\nbbb\n" = 8), count = 3 ("c\nd")
    pt.insert(8, QStringLiteral("XX"));   // "aaa\nbbb\nXXccc\nddd"
    pt.verify();
    pt.remove(8, 5);                      // "XXccc" → "aaa\nbbb\n\nddd"
    pt.verify();
    QCOMPARE(pt.toString(), QStringLiteral("aaa\nbbb\n\nddd"));
    QCOMPARE(pt.lineCount(), 4);
    QCOMPARE(pt.line(2), QString());
    QCOMPARE(pt.line(3), QStringLiteral("ddd"));
}

void PieceTableTest::removeMiddleOfPiece()
{
    PieceTable pt;
    pt.reset(QStringLiteral("abcdefgh"));
    pt.remove(2, 3); // "cde" внутри одного куска — разрез
    pt.verify();
    QCOMPARE(pt.toString(), QStringLiteral("abfgh"));
    QCOMPARE(pt.line(0), QStringLiteral("abfgh"));
}

void PieceTableTest::editNewlineShift()
{
    PieceTable pt;
    pt.reset(QStringLiteral("one\ntwo\nthree"));
    pt.insert(0, QStringLiteral("z\n")); // новая строка сверху
    pt.verify();
    QCOMPARE(pt.lineCount(), 4);
    QCOMPARE(pt.line(0), QStringLiteral("z"));
    QCOMPARE(pt.line(1), QStringLiteral("one"));
    QCOMPARE(pt.line(2), QStringLiteral("two"));
    QCOMPARE(pt.line(3), QStringLiteral("three"));
}

void PieceTableTest::sequenceOfEdits()
{
    PieceTable pt;
    pt.reset(QStringLiteral("hello world"));
    // Серия правок в разных местах — как при наборе
    pt.insert(11, QStringLiteral("!"));   // "hello world!"
    pt.insert(5, QStringLiteral(","));    // "hello, world!"
    pt.insert(0, QStringLiteral("> "));   // "> hello, world!"
    pt.verify();
    QCOMPARE(pt.toString(), QStringLiteral("> hello, world!"));

    pt.remove(0, 2); // "hello, world!"
    pt.remove(5, 1); // запятая
    pt.verify();
    QCOMPARE(pt.toString(), QStringLiteral("hello world!"));

    // Правки не должны ломать line index: проверим каждую строку
    const QString joined = pt.toString();
    for (int i = 0; i < pt.lineCount(); ++i)
        QCOMPARE(pt.line(i).size(), pt.lineLength(i));
    QCOMPARE(joined.split(u'\n').size(), pt.lineCount());
}

void PieceTableTest::emptyDocument()
{
    PieceTable pt;
    pt.reset(QString());
    pt.verify();
    QCOMPARE(pt.length(), 0);
    QCOMPARE(pt.lineCount(), 1);
    QCOMPARE(pt.line(0), QString());

    pt.insert(0, QStringLiteral("text"));
    pt.verify();
    QCOMPARE(pt.toString(), QStringLiteral("text"));

    pt.remove(0, 4);
    pt.verify();
    QCOMPARE(pt.toString(), QString());
    QCOMPARE(pt.lineCount(), 1);
}

void PieceTableTest::snapshotIsolated()
{
    PieceTable pt;
    pt.reset(QStringLiteral("one\ntwo"));
    const PieceTable::Snapshot snap = pt.snapshot();
    QCOMPARE(snap.lineCount(), 2);
    QCOMPARE(snap.line(0), QStringLiteral("one"));

    // Правки после снимка не видны в снимке
    pt.insert(7, QStringLiteral(" three"));
    pt.remove(0, 4);
    QCOMPARE(snap.toString(), QStringLiteral("one\ntwo"));
    QCOMPARE(snap.lineCount(), 2);
    QCOMPARE(pt.toString(), QStringLiteral("two three"));
}

void PieceTableTest::snapshotLines()
{
    PieceTable pt;
    pt.reset(QStringLiteral("a\nbb\nccc"));
    const PieceTable::Snapshot snap = pt.snapshot();
    const QStringList lines = snap.lines();
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(0), QStringLiteral("a"));
    QCOMPARE(lines.at(1), QStringLiteral("bb"));
    QCOMPARE(lines.at(2), QStringLiteral("ccc"));
}

QTEST_GUILESS_MAIN(PieceTableTest)
#include "piecetable_test.moc"