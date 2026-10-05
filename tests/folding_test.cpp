#include <QtTest>

#include "src/core/folding.h"

using namespace core;
using namespace core::folding;

class FoldingTest : public QObject {
    Q_OBJECT

private slots:
    void bracketBlocks();
    void indentBlocks();
    void rowMapping();
    void nestedFolds();
    void editsShiftOrUnfold();

private:
    static TextBuffer buffer(const QByteArray &text)
    {
        TextBuffer b;
        b.loadFromData(text);
        return b;
    }
};

void FoldingTest::bracketBlocks()
{
    const TextBuffer b = buffer("void f() {\n"     // 0
                                "    if (x) {\n"   // 1
                                "        y();\n"   // 2
                                "    } else {\n"   // 3
                                "        z();\n"   // 4
                                "    }\n"          // 5
                                "}\n"              // 6
                                "int a[] = {1,\n"  // 7
                                "};\n"             // 8
                                "s = \"{\";\n");   // 9
    QVERIFY(canFold(b, 0));
    QCOMPARE(foldEnd(b, 0), std::optional<int>(5)); // строка с } остаётся видимой
    QCOMPARE(foldEnd(b, 1), std::optional<int>(2));
    QCOMPARE(foldEnd(b, 3), std::optional<int>(4)); // `} else {` — по своей {
    QVERIFY(!canFold(b, 2));
    QVERIFY(!canFold(b, 7)); // пара на следующей строке — нечего прятать
    QCOMPARE(foldEnd(b, 7), std::nullopt);
    QVERIFY(!canFold(b, 9)); // скобка в строке не считается
}

void FoldingTest::indentBlocks()
{
    const TextBuffer b = buffer("def f(x):\n"   // 0
                                "    if x:\n"   // 1
                                "        a()\n" // 2
                                "\n"            // 3
                                "    b()\n"     // 4
                                "\n"            // 5
                                "c = 1\n");     // 6
    QVERIFY(canFold(b, 0));
    QCOMPARE(foldEnd(b, 0), std::optional<int>(4)); // пустая строка в хвосте не прячется
    QCOMPARE(foldEnd(b, 1), std::optional<int>(2));
    QVERIFY(!canFold(b, 2));
    QVERIFY(!canFold(b, 3));
    QVERIFY(!canFold(b, 6));
}

void FoldingTest::rowMapping()
{
    FoldSet s;
    s.add({0, 3}); // скрыты 1..3
    s.add({4, 7}); // скрыты 5..7
    QCOMPARE(s.rowCount(10), 4);
    const QVector<int> lines{0, 4, 8, 9};
    for (int row = 0; row < lines.size(); ++row) {
        QCOMPARE(s.lineAt(row), lines.at(row));
        QCOMPARE(s.rowOf(lines.at(row)), row);
    }
    QVERIFY(s.isHidden(2) && !s.isHidden(4));
    QCOMPARE(s.rowOf(6), 1); // скрытая — ряд заголовка
    QVERIFY(s.isFolded(4));

    s.reveal(6);
    QVERIFY(!s.isFolded(4) && s.isFolded(0));
    QCOMPARE(s.rowCount(10), 7);
    QCOMPARE(s.lineAt(1), 4);
}

void FoldingTest::nestedFolds()
{
    FoldSet s;
    s.add({2, 4});
    s.add({0, 8}); // внешний поверх уже свёрнутого внутреннего
    QCOMPARE(s.rowCount(10), 2);
    QVERIFY(!s.isFolded(2)); // заголовок скрыт
    s.remove(0);
    QVERIFY(s.isFolded(2)); // внутренний остался свёрнутым
    QCOMPARE(s.rowCount(10), 8);
}

void FoldingTest::editsShiftOrUnfold()
{
    auto edit = [](int startLine, int oldEndLine, int newEndLine) {
        TextBuffer::Edit e;
        e.start = {startLine, 0};
        e.oldEnd = {oldEndLine, 0};
        e.newEnd = {newEndLine, 0};
        return e;
    };
    FoldSet s;
    s.add({5, 8});
    s.applyEdit(edit(1, 1, 3)); // две строки выше
    QCOMPARE(s.folds(), (QVector<FoldSet::Fold>{{7, 10}}));
    s.applyEdit(edit(2, 4, 2)); // две строки выше удалены
    QCOMPARE(s.folds(), (QVector<FoldSet::Fold>{{5, 8}}));
    s.applyEdit(edit(9, 9, 12)); // ниже блока
    QCOMPARE(s.folds(), (QVector<FoldSet::Fold>{{5, 8}}));
    s.applyEdit(edit(5, 5, 5)); // внутри строки заголовка
    QCOMPARE(s.folds().size(), 1);
    s.applyEdit(edit(4, 5, 4)); // заголовок склеен с предыдущей строкой
    QCOMPARE(s.folds(), (QVector<FoldSet::Fold>{{4, 7}}));
    s.applyEdit(edit(4, 4, 5)); // Enter в начале заголовка — блок уезжает вниз
    QCOMPARE(s.folds(), (QVector<FoldSet::Fold>{{5, 8}}));
    TextBuffer::Edit inside = edit(5, 5, 6);
    inside.start.column = inside.oldEnd.column = 3;
    s.applyEdit(inside); // Enter посреди заголовка — блок разворачивается
    QVERIFY(s.isEmpty());
}

QTEST_GUILESS_MAIN(FoldingTest)
#include "folding_test.moc"
