#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include "src/services/highlighter.h"

using Span = Highlighter::Span;

namespace {
// Записывает минимальный набор правил во временный JSON и возвращает путь
// (пустая строка — не удалось записать). JSON строится программно:
// moc спотыкается о сырые строки с регэкспами и молча не генерирует класс.
QJsonObject makeRule(const QString &type, const QString &re, const QString &color)
{
    QJsonObject r;
    r.insert("type", type);
    r.insert("re", re);
    r.insert("color", color);
    return r;
}

QString writeTestRules(QTemporaryDir &dir)
{
    const QString path = dir.path() + "/rules.json";

    QJsonObject cpp;
    cpp.insert("extensions", QJsonArray{QStringLiteral("cpp"),
                                        QStringLiteral("h")});
    cpp.insert("rules", QJsonArray{
        makeRule("comment", QStringLiteral("//.*$"), QStringLiteral("#6a9955")),
        makeRule("string", QStringLiteral("\"[^\"]*\""), QStringLiteral("#ce9178")),
        makeRule("number", QStringLiteral("\\b\\d+\\b"), QStringLiteral("#b5cea8")),
        makeRule("keyword", QStringLiteral("\\b(?:int|return)\\b"),
                 QStringLiteral("#569cd6"))});

    QJsonObject json;
    json.insert("extensions", QJsonArray{QStringLiteral("json")});
    json.insert("rules", QJsonArray{
        makeRule("key",
                 QStringLiteral("\"(?:[^\\\"\\\\]|\\\\.)*\"(?=\\s*:)"),
                 QStringLiteral("#9cdcfe")),
        makeRule("string", QStringLiteral("\"(?:[^\\\"\\\\]|\\\\.)*\""),
                 QStringLiteral("#ce9178"))});

    QJsonObject langs;
    langs.insert("cpp", cpp);
    langs.insert("json", json);
    QJsonObject root;
    root.insert("languages", langs);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    f.write(QJsonDocument(root).toJson());
    return path;
}

bool hasSpan(const QVector<Span> &spans, int start, int length, int rule)
{
    for (const Span &s : spans)
        if (s.start == start && s.length == length && s.rule == rule)
            return true;
    return false;
}
} // namespace

class HighlighterTest : public QObject {
    Q_OBJECT

private slots:
    void loadFailsOnMissingFile();
    void loadAndDetectLanguage();
    void cppLine();
    void earlierRuleWins();
    void jsonKeyBeatsString();
    void plainLanguageGivesNoSpans();
};

void HighlighterTest::loadFailsOnMissingFile()
{
    Highlighter hl;
    QVERIFY(!hl.loadRules(QStringLiteral("no/such/file.json")));
}

void HighlighterTest::loadAndDetectLanguage()
{
    QTemporaryDir dir;
    Highlighter hl;
    { const QString p = writeTestRules(dir); QVERIFY(!p.isEmpty()); QVERIFY(hl.loadRules(p)); }
    QVERIFY(!hl.hasLanguage());

    hl.setLanguageForFile(QStringLiteral("/tmp/main.cpp"));
    QVERIFY(hl.hasLanguage());
    hl.setLanguageForFile(QStringLiteral("C:/x/y/Header.H")); // регистр не важен
    QVERIFY(hl.hasLanguage());
    hl.setLanguageForFile(QStringLiteral("file.unknown"));
    QVERIFY(!hl.hasLanguage());
}

void HighlighterTest::cppLine()
{
    QTemporaryDir dir;
    Highlighter hl;
    { const QString p = writeTestRules(dir); QVERIFY(!p.isEmpty()); QVERIFY(hl.loadRules(p)); }
    hl.setLanguageForFile("a.cpp");

    QVector<Span> spans;
    hl.highlightLine(QStringLiteral("int x = 42; // init"), spans);
    QVERIFY(hasSpan(spans, 0, 3, 3));            // int — keyword
    QVERIFY(hasSpan(spans, 8, 2, 2));            // 42 — number
    QVERIFY(hasSpan(spans, 12, 7, 0));           // // init — comment
    // фрагменты не перекрываются и отсортированы
    for (int i = 1; i < spans.size(); ++i)
        QVERIFY(spans.at(i - 1).start + spans.at(i - 1).length
                <= spans.at(i).start);
}

void HighlighterTest::earlierRuleWins()
{
    QTemporaryDir dir;
    Highlighter hl;
    { const QString p = writeTestRules(dir); QVERIFY(!p.isEmpty()); QVERIFY(hl.loadRules(p)); }
    hl.setLanguageForFile("a.cpp");

    QVector<Span> spans;
    // 42 внутри комментария не подсвечивается числом: комментарий начинается раньше
    hl.highlightLine(QStringLiteral("// use 42 now"), spans);
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans.first().rule, 0);
}

void HighlighterTest::jsonKeyBeatsString()
{
    QTemporaryDir dir;
    Highlighter hl;
    { const QString p = writeTestRules(dir); QVERIFY(!p.isEmpty()); QVERIFY(hl.loadRules(p)); }
    hl.setLanguageForFile("data.json");

    QVector<Span> spans;
    hl.highlightLine(QStringLiteral("{\"name\": \"val\", \"n\": 1}"), spans);
    // ключ "name" — правило key (с lookahead ':'), значение — string
    QVERIFY(hasSpan(spans, 1, 6, 0));
    QVERIFY(hasSpan(spans, 9, 5, 1));
}

void HighlighterTest::plainLanguageGivesNoSpans()
{
    QTemporaryDir dir;
    Highlighter hl;
    { const QString p = writeTestRules(dir); QVERIFY(!p.isEmpty()); QVERIFY(hl.loadRules(p)); }
    hl.setLanguageForFile("readme.txt");

    QVector<Span> spans;
    hl.highlightLine(QStringLiteral("int x = 42; // init"), spans);
    QVERIFY(spans.isEmpty());
}

QTEST_MAIN(HighlighterTest)
#include "highlighter_test.moc"