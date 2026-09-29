#include <QSignalSpy>
#include <QtTest>

#include "src/core/textbuffer.h"
#include "src/services/syntaxhighlighter.h"

using core::TextBuffer;
using Spans = QVector<QVector<SyntaxHighlighter::Span>>;

class SyntaxHighlighterTest : public QObject {
    Q_OBJECT

private slots:
    void languages();
    void cppColors();
    void preprocessorLines();
    void multilineComment();
    void pythonColors();
    void incrementalEdit();
    void editsDuringParse();
    void documentSwitchDropsStaleParse();

private:
    static bool parse(SyntaxHighlighter &hl, TextBuffer &buf, const QByteArray &text);
    static bool waitIdle(SyntaxHighlighter &hl);
    static Spans highlightAll(const SyntaxHighlighter &hl, const TextBuffer &buf);
    static QByteArray styleAt(const Spans &spans, int line, int column);
};

bool SyntaxHighlighterTest::parse(SyntaxHighlighter &hl, TextBuffer &buf,
                                  const QByteArray &text)
{
    if (!buf.loadFromData(text))
        return false;
    hl.reset(buf.snapshot());
    return waitIdle(hl);
}

bool SyntaxHighlighterTest::waitIdle(SyntaxHighlighter &hl)
{
    QSignalSpy spy(&hl, &SyntaxHighlighter::updated);
    while (hl.isParsing())
        if (!spy.wait(5000))
            return false;
    return hl.isReady();
}

Spans SyntaxHighlighterTest::highlightAll(const SyntaxHighlighter &hl, const TextBuffer &buf)
{
    QStringList lines;
    for (int i = 0; i < buf.lineCount(); ++i)
        lines.append(buf.lineAt(i));
    Spans out;
    hl.highlightLines(0, lines, out);
    return out;
}

QByteArray SyntaxHighlighterTest::styleAt(const Spans &spans, int line, int column)
{
    for (const SyntaxHighlighter::Span &s : spans.value(line))
        if (column >= s.start && column < s.start + s.length)
            return SyntaxHighlighter::styleName(s.rule);
    return {};
}

namespace {
const QByteArray kKeyword("keyword");
const QByteArray kType("type");
const QByteArray kFunction("function");
const QByteArray kString("string");
const QByteArray kEscape("escape");
const QByteArray kNumber("number");
const QByteArray kComment("comment");
const QByteArray kPreprocessor("preprocessor");
} // namespace

void SyntaxHighlighterTest::languages()
{
    SyntaxHighlighter hl;
    // true — грамматика есть и .scm-запрос скомпилировался
    QVERIFY(hl.setLanguageForFile(QStringLiteral("a.cpp")));
    QVERIFY(hl.setLanguageForFile(QStringLiteral("B.H")));
    QVERIFY(hl.setLanguageForFile(QStringLiteral("x.py")));
    QVERIFY(!hl.setLanguageForFile(QStringLiteral("x.json")));
    QVERIFY(!hl.setLanguageForFile(QString()));
}

void SyntaxHighlighterTest::cppColors()
{
    SyntaxHighlighter hl;
    TextBuffer buf;
    hl.setLanguageForFile(QStringLiteral("a.cpp"));
    QVERIFY(parse(hl, buf,
                  "#include <vector>\n"
                  "int main() { return 42; }\n"
                  "const char *s = \"a\\nb\";\n"));
    const Spans spans = highlightAll(hl, buf);

    QCOMPARE(styleAt(spans, 0, 0), kPreprocessor);
    QCOMPARE(styleAt(spans, 0, 10), kPreprocessor); // <vector> — часть директивы
    QCOMPARE(styleAt(spans, 1, 0), kType);      // int
    QCOMPARE(styleAt(spans, 1, 4), kFunction);  // main
    QCOMPARE(styleAt(spans, 1, 13), kKeyword);  // return
    QCOMPARE(styleAt(spans, 1, 20), kNumber);   // 42
    QCOMPARE(styleAt(spans, 2, 0), kKeyword);   // const
    QCOMPARE(styleAt(spans, 2, 16), kString);   // "a
    QCOMPARE(styleAt(spans, 2, 18), kEscape);   // \n внутри строки
    QCOMPARE(styleAt(spans, 2, 20), kString);   // b"
    QCOMPARE(styleAt(spans, 2, 6), kType);      // char
    QCOMPARE(styleAt(spans, 2, 12), QByteArray());  // s — без подсветки
}

void SyntaxHighlighterTest::preprocessorLines()
{
    SyntaxHighlighter hl;
    TextBuffer buf;
    hl.setLanguageForFile(QStringLiteral("a.cpp"));
    QVERIFY(parse(hl, buf,
                  "#include \"doc.h\"\n"
                  "#define MAX_SIZE (1 + 2)\n"
                  "#ifdef DEBUG\n"
                  "#endif\n"));
    const Spans spans = highlightAll(hl, buf);

    QCOMPARE(styleAt(spans, 0, 0), kPreprocessor);
    QCOMPARE(styleAt(spans, 0, 10), kPreprocessor); // "doc.h"
    QCOMPARE(styleAt(spans, 1, 0), kPreprocessor);
    QCOMPARE(styleAt(spans, 1, 8), kPreprocessor);  // MAX_SIZE
    QCOMPARE(styleAt(spans, 1, 18), QByteArray());  // тело макроса — один неразобранный токен
    QCOMPARE(styleAt(spans, 2, 7), kPreprocessor);  // DEBUG
    QCOMPARE(styleAt(spans, 3, 0), kPreprocessor);
}

void SyntaxHighlighterTest::multilineComment()
{
    SyntaxHighlighter hl;
    TextBuffer buf;
    hl.setLanguageForFile(QStringLiteral("a.cpp"));
    QVERIFY(parse(hl, buf, "int a; /* one\ntwo\nthree */ int b;\n"));
    const Spans spans = highlightAll(hl, buf);

    QCOMPARE(styleAt(spans, 0, 7), kComment);
    QCOMPARE(styleAt(spans, 1, 0), kComment);
    QCOMPARE(styleAt(spans, 1, 2), kComment);
    QCOMPARE(styleAt(spans, 2, 7), kComment);   // закрывающее */
    QCOMPARE(styleAt(spans, 2, 9), kType);      // int после комментария

    // Запрос только середины: комментарий начат выше видимой области
    Spans middle;
    hl.highlightLines(1, {buf.lineAt(1)}, middle);
    QCOMPARE(middle.size(), 1);
    QCOMPARE(styleAt(middle, 0, 0), kComment);
}

void SyntaxHighlighterTest::pythonColors()
{
    SyntaxHighlighter hl;
    TextBuffer buf;
    hl.setLanguageForFile(QStringLiteral("a.py"));
    QVERIFY(parse(hl, buf,
                  "def add(a, b):\n"
                  "    \"\"\"doc\n"
                  "    string\"\"\"\n"
                  "    return a + 1  # sum\n"
                  "bpy.ops.read(x)\n"
                  "print(len(a), 2)\n"));
    const Spans spans = highlightAll(hl, buf);

    QCOMPARE(styleAt(spans, 4, 8), kFunction);   // read — метод через атрибут
    QCOMPARE(styleAt(spans, 4, 0), QByteArray());    // bpy
    QCOMPARE(styleAt(spans, 5, 0), kFunction);   // print
    QCOMPARE(styleAt(spans, 5, 6), kFunction);   // len
    QCOMPARE(styleAt(spans, 5, 14), kNumber);

    QCOMPARE(styleAt(spans, 0, 0), kKeyword);
    QCOMPARE(styleAt(spans, 0, 4), kFunction);
    QCOMPARE(styleAt(spans, 1, 4), kString);
    QCOMPARE(styleAt(spans, 2, 4), kString);    // docstring на второй строке
    QCOMPARE(styleAt(spans, 3, 4), kKeyword);
    QCOMPARE(styleAt(spans, 3, 15), kNumber);
    QCOMPARE(styleAt(spans, 3, 18), kComment);
}

void SyntaxHighlighterTest::incrementalEdit()
{
    SyntaxHighlighter hl;
    TextBuffer buf;
    hl.setLanguageForFile(QStringLiteral("a.cpp"));
    QVERIFY(parse(hl, buf, "int a;\nint b;\nint c; */\n"));
    QCOMPARE(styleAt(highlightAll(hl, buf), 1, 0), kType);

    // Открывающий /* на первой строке — две следующие уходят в комментарий
    // (незакрытый /* tree-sitter комментарием не считает, поэтому */ уже есть)
    buf.insertText({0, 6}, QStringLiteral(" /*"));
    hl.applyEdits(buf.takeEdits(), buf.snapshot());
    QVERIFY(waitIdle(hl));
    Spans spans = highlightAll(hl, buf);
    QCOMPARE(styleAt(spans, 0, 0), kType);
    QCOMPARE(styleAt(spans, 1, 0), kComment);
    QCOMPARE(styleAt(spans, 2, 4), kComment);

    buf.removeText({0, 6}, {0, 9});
    hl.applyEdits(buf.takeEdits(), buf.snapshot());
    QVERIFY(waitIdle(hl));
    spans = highlightAll(hl, buf);
    QCOMPARE(styleAt(spans, 1, 0), kType);
    QCOMPARE(styleAt(spans, 2, 0), kType);
}

void SyntaxHighlighterTest::editsDuringParse()
{
    // Файл покрупнее, чтобы правки прилетали, пока фоновый разбор идёт
    QByteArray text;
    for (int i = 0; i < 20000; ++i)
        text += "int f" + QByteArray::number(i) + "() { return " + QByteArray::number(i) + "; }\n";

    SyntaxHighlighter hl;
    TextBuffer buf;
    hl.setLanguageForFile(QStringLiteral("a.cpp"));
    QVERIFY(buf.loadFromData(text));
    hl.reset(buf.snapshot());

    // Серия правок сразу после старта: часть уйдёт в «докатку»
    for (int i = 0; i < 50; ++i) {
        const int line = (i * 397) % buf.lineCount();
        if (i % 3 == 2)
            buf.removeText({line, 0}, {line, 4});
        else
            buf.insertText({line, 0}, i % 2 ? QStringLiteral("/* c */ ") : QStringLiteral("\n"));
        hl.applyEdits(buf.takeEdits(), buf.snapshot());
    }
    QVERIFY(waitIdle(hl));

    SyntaxHighlighter fresh;
    fresh.setLanguageForFile(QStringLiteral("a.cpp"));
    fresh.reset(buf.snapshot());
    QVERIFY(waitIdle(fresh));

    const Spans a = highlightAll(hl, buf);
    const Spans b = highlightAll(fresh, buf);
    QCOMPARE(a.size(), b.size());
    for (int i = 0; i < a.size(); ++i) {
        QCOMPARE(a.at(i).size(), b.at(i).size());
        for (int k = 0; k < a.at(i).size(); ++k) {
            QCOMPARE(a.at(i).at(k).start, b.at(i).at(k).start);
            QCOMPARE(a.at(i).at(k).length, b.at(i).at(k).length);
            QCOMPARE(a.at(i).at(k).rule, b.at(i).at(k).rule);
        }
    }
}

void SyntaxHighlighterTest::documentSwitchDropsStaleParse()
{
    QByteArray big;
    for (int i = 0; i < 20000; ++i)
        big += "int f" + QByteArray::number(i) + "();\n";

    SyntaxHighlighter hl;
    TextBuffer first, second;
    hl.setLanguageForFile(QStringLiteral("a.cpp"));
    QVERIFY(first.loadFromData(big));
    hl.reset(first.snapshot());
    // Сразу переключаемся на другой документ: старый разбор не должен
    // подменить дерево нового
    QVERIFY(parse(hl, second, "int x;\n/* x */\n"));
    QTest::qWait(300);
    const Spans spans = highlightAll(hl, second);
    QCOMPARE(styleAt(spans, 1, 0), kComment);
    QCOMPARE(styleAt(spans, 0, 0), kType);
}

QTEST_GUILESS_MAIN(SyntaxHighlighterTest)
#include "syntaxhighlighter_test.moc"
