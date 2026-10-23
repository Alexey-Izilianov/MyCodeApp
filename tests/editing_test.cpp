#include <QtTest>

#include "src/core/editing.h"

using namespace core;
using namespace core::editing;

class EditingTest : public QObject {
    Q_OBJECT

private slots:
    void visualColumns();
    void columnFromVisual();
    void expandTabsMap();
    void wrapStarts();
    void detectSpacesAndTabs();
    void bracketMatching();

private:
    static TextBuffer buffer(const QByteArray &text)
    {
        TextBuffer b;
        b.loadFromData(text);
        return b;
    }
};

void EditingTest::visualColumns()
{
    QCOMPARE(visualColumn(QStringLiteral("abc"), 2), 2);
    QCOMPARE(visualColumn(QStringLiteral("\tx"), 1), 4);
    QCOMPARE(visualColumn(QStringLiteral("ab\tx"), 3), 4); // табуляция добивает до кратной 4
    QCOMPARE(visualColumn(QStringLiteral("abcd\tx"), 5), 8);
    QCOMPARE(visualColumn(QStringLiteral("\t\tx"), 3), 9);
}

void EditingTest::columnFromVisual()
{
    const QString line = QStringLiteral("\tab");
    QCOMPARE(columnAtVisual(line, 0), 0);
    QCOMPARE(columnAtVisual(line, 1), 0); // ближе к началу табуляции
    QCOMPARE(columnAtVisual(line, 3), 1); // ближе к её концу
    QCOMPARE(columnAtVisual(line, 4), 1);
    QCOMPARE(columnAtVisual(line, 5), 2);
    QCOMPARE(columnAtVisual(line, 9), 6); // ширина строки 6; за концом счёт продолжается: 3 + 3
}

void EditingTest::expandTabsMap()
{
    QVector<int> map;
    QCOMPARE(expandTabs(QStringLiteral("a\tb"), &map), QStringLiteral("a   b"));
    QCOMPARE(map, (QVector<int>{0, 1, 4, 5}));
    QCOMPARE(expandTabs(QStringLiteral("plain"), &map), QStringLiteral("plain"));
    QCOMPARE(map.size(), 6);
}

void EditingTest::detectSpacesAndTabs()
{
    IndentStyle s = detectIndent(buffer("int f() {\n  if (x) {\n    y();\n  }\n}\n"));
    QVERIFY(!s.tabs);
    QCOMPARE(s.width, 2);
    QCOMPARE(s.unit(), QStringLiteral("  "));

    s = detectIndent(buffer("void f() {\n\tx();\n\tif (y) {\n\t\tz();\n\t}\n}\n"));
    QVERIFY(s.tabs);
    QCOMPARE(s.unit(), QStringLiteral("\t"));

    s = detectIndent(buffer("no indent at all\n"));
    QVERIFY(!s.tabs);
    QCOMPARE(s.width, 4);
}

void EditingTest::bracketMatching()
{
    const TextBuffer b = buffer("f(a[1], {\n  g(x)\n})");
    QCOMPARE(matchingBracket(b, {0, 1}), std::optional<TextBuffer::Position>({2, 1}));
    QCOMPARE(matchingBracket(b, {2, 1}), std::optional<TextBuffer::Position>({0, 1}));
    QCOMPARE(matchingBracket(b, {0, 3}), std::optional<TextBuffer::Position>({0, 5}));
    QCOMPARE(matchingBracket(b, {0, 8}), std::optional<TextBuffer::Position>({2, 0}));
    QVERIFY(!matchingBracket(b, {0, 0}).has_value()); // не скобка
    QVERIFY(!matchingBracket(buffer("(("), {0, 0}).has_value()); // без пары
}

void EditingTest::wrapStarts()
{
    using V = QVector<int>;
    QCOMPARE(editing::wrapStarts(QStringLiteral("short"), 10), V{0});
    // Рвём после пробела: "aaaa bbbb" | "cccc"
    QCOMPARE(editing::wrapStarts(QStringLiteral("aaaa bbbb cccc"), 10), (V{0, 10}));
    QCOMPARE(editing::wrapStarts(QStringLiteral("aaaa bbbb cccc"), 9), (V{0, 10})); // пробел свисает
    QCOMPARE(editing::wrapStarts(QStringLiteral("aaaa bbbb cccc"), 7), (V{0, 5, 10}));
    // Слово длиннее ширины — посимвольно
    QCOMPARE(editing::wrapStarts(QStringLiteral("abcdefghij"), 4), (V{0, 4, 8}));
    // Продолжения сдвинуты на отступ и становятся уже
    int indent = -1;
    QCOMPARE(editing::wrapStarts(QStringLiteral("    aaaa bbbb cccc"), 12, &indent), (V{0, 9, 14}));
    QCOMPARE(indent, 4);
    // Отступ и одно длинное слово: первый ряд не пустой, продолжения не шире окна
    const QVector<int> word = editing::wrapStarts(QString(8, u' ') + QString(100, u'x'), 40);
    QCOMPARE(word.at(1), 40);
    for (int i = 2; i < word.size(); ++i)
        QVERIFY(word.at(i) - word.at(i - 1) <= 40 - 8);
    // Отступ больше половины ширины не повторяем
    editing::wrapStarts(QStringLiteral("        aaaa"), 10, &indent);
    QCOMPARE(indent, 0);
}

QTEST_GUILESS_MAIN(EditingTest)
#include "editing_test.moc"
