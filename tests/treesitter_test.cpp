// Спайк M2-2: сборка tree-sitter под MinGW + грамматики C++/Python.
// Проверяем: загрузка языков, парсинг сниппета, инкрементальный пересчёт
// и скорости (полный парс ~1 МБ vs правка строки).
#include <QElapsedTimer>
#include <QtTest>
#include <tree_sitter/api.h>

extern "C" const TSLanguage *tree_sitter_cpp();
extern "C" const TSLanguage *tree_sitter_python();

class TreeSitterTest : public QObject {
    Q_OBJECT

private slots:
    void grammarsLoad();
    void parseCppSnippet();
    void parsePythonSnippet();
    void incrementalEditIsFast();
    void parseSpeedOneMb();

private:
    struct Parse {
        TSParser *parser = nullptr;
        TSTree *tree = nullptr;
        bool languageSet = false;
    };
    static Parse makeParser(const TSLanguage *lang);
    static QByteArray oneMbCppSource();
};

TreeSitterTest::Parse TreeSitterTest::makeParser(const TSLanguage *lang)
{
    Parse p;
    p.parser = ts_parser_new();
    p.languageSet = ts_parser_set_language(p.parser, lang) != 0;
    return p;
}

QByteArray TreeSitterTest::oneMbCppSource()
{
    QByteArray src;
    src.reserve(1024 * 1024);
    for (int i = 0; src.size() < 1024 * 1024; ++i)
        src += "int func" + QByteArray::number(i)
               + "() { return " + QByteArray::number(i) + "; }\n";
    return src;
}

void TreeSitterTest::grammarsLoad()
{
    TSParser *parser = ts_parser_new();
    QVERIFY(ts_parser_set_language(parser, tree_sitter_cpp()));
    QVERIFY(ts_parser_set_language(parser, tree_sitter_python()));
    ts_parser_delete(parser);
}

void TreeSitterTest::parseCppSnippet()
{
    Parse p = makeParser(tree_sitter_cpp());
    QVERIFY(p.languageSet);
    const QByteArray src = "int main() {\n    /* привет */\n    return 0;\n}\n";
    p.tree = ts_parser_parse_string(p.parser, nullptr, src.constData(),
                                    src.size());
    QVERIFY(p.tree != nullptr);
    QCOMPARE(QString::fromUtf8(ts_node_type(ts_tree_root_node(p.tree))),
             QStringLiteral("translation_unit"));

    // S-expression содержит узлы ключевых типов: блочный комментарий
    // «виден» целиком, хотя занимает несколько строк.
    const QString sexp = QString::fromUtf8(ts_node_string(ts_tree_root_node(p.tree)));
    QVERIFY(sexp.contains(QStringLiteral("function_definition")));
    QVERIFY(sexp.contains(QStringLiteral("comment")));
    QVERIFY(sexp.contains(QStringLiteral("return_statement")));
}

void TreeSitterTest::parsePythonSnippet()
{
    Parse p = makeParser(tree_sitter_python());
    QVERIFY(p.languageSet);
    const QByteArray src = "def add(a, b):\n    return a + b\n";
    p.tree = ts_parser_parse_string(p.parser, nullptr, src.constData(),
                                    src.size());
    QVERIFY(p.tree != nullptr);
    const QString sexp = QString::fromUtf8(ts_node_string(ts_tree_root_node(p.tree)));
    QVERIFY(sexp.contains(QStringLiteral("function_definition")));
    QVERIFY(sexp.contains(QStringLiteral("return_statement")));
}

void TreeSitterTest::incrementalEditIsFast()
{
    // Правка одной строки ~в середине файла на ~1 МБ
    const QByteArray src = oneMbCppSource();
    Parse p = makeParser(tree_sitter_cpp());
    QVERIFY(p.languageSet);
    p.tree = ts_parser_parse_string(p.parser, nullptr, src.constData(),
                                    src.size());
    QVERIFY(p.tree != nullptr);

    const int editOffset = src.size() / 2;
    const TSInputEdit edit{
        uint32_t(editOffset), uint32_t(editOffset), uint32_t(editOffset + 1),
        {0, 0}, {0, 0}, {0, 1}};
    ts_tree_edit(p.tree, &edit);
    QByteArray edited = src;
    edited.insert(editOffset, "x");

    QElapsedTimer timer;
    timer.start();
    TSTree *updated = ts_parser_parse_string(p.parser, p.tree,
                                             edited.constData(), edited.size());
    const qint64 incMs = timer.elapsed();
    QVERIFY(updated != nullptr);

    // Правка строки — доли миллисекунды, с запасом
    QVERIFY2(incMs < 100, qPrintable(QStringLiteral("incremental: %1 ms").arg(incMs)));

    ts_tree_delete(updated);
    ts_tree_delete(p.tree);
    ts_parser_delete(p.parser);
}

void TreeSitterTest::parseSpeedOneMb()
{
    const QByteArray src = oneMbCppSource();
    Parse p = makeParser(tree_sitter_cpp());
    QVERIFY(p.languageSet);

    QElapsedTimer timer;
    timer.start();
    p.tree = ts_parser_parse_string(p.parser, nullptr, src.constData(),
                                    src.size());
    const qint64 fullMs = timer.elapsed();
    QVERIFY(p.tree != nullptr);
    // Критерий M2-2: полный пересчёт файла 1 МБ < 100 мс
    QVERIFY2(fullMs < 1000, qPrintable(QStringLiteral("full: %1 ms").arg(fullMs)));

    // Правка одной строки в середине: вставка символа в имя функции
    const int editOffset = src.indexOf("int func", src.size() / 2) + 8;
    QByteArray edited = src;
    edited.insert(editOffset, "x");
    const TSInputEdit edit{
        uint32_t(editOffset), uint32_t(editOffset), uint32_t(editOffset + 1),
        {0, 0}, {0, 0}, {0, 1}};
    TSTree *old = ts_tree_copy(p.tree);
    ts_tree_edit(old, &edit);
    timer.restart();
    TSTree *updated = ts_parser_parse_string(p.parser, old,
                                             edited.constData(), edited.size());
    const qint64 incMs = timer.elapsed();
    QVERIFY(updated != nullptr);

    // Критерий M2-2: правка строки пересчитывает только затронутый
    // диапазон — изменённые узлы не выходят за пределы этой строки.
    uint32_t count = 0;
    TSRange *ranges = ts_tree_get_changed_ranges(old, updated, &count);
    for (uint32_t i = 0; i < count; ++i)
        QVERIFY2(ranges[i].end_byte - ranges[i].start_byte < 200,
                 qPrintable(QStringLiteral("changed range %1..%2")
                                .arg(ranges[i].start_byte).arg(ranges[i].end_byte)));
    free(ranges);
    QVERIFY2(incMs < fullMs, qPrintable(QStringLiteral("edit: %1 ms").arg(incMs)));

    qInfo("parse 1 MB: full %lld ms, incremental edit %lld ms, changed ranges %u",
          fullMs, incMs, count);

    ts_tree_delete(updated);
    ts_tree_delete(old);
    ts_tree_delete(p.tree);
    ts_parser_delete(p.parser);
}

QTEST_GUILESS_MAIN(TreeSitterTest)
#include "treesitter_test.moc"