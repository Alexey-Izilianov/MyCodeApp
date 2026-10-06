#include <QTest>

#include "src/core/snippet.h"

using namespace core::snippet;

class SnippetTest : public QObject {
    Q_OBJECT

private slots:
    void tabStopsAndFinal()
    {
        const Expansion e = expand(QStringLiteral("for ($1; $2) {\n\t$0\n}"));
        QCOMPARE(e.text, QStringLiteral("for (; ) {\n\t\n}"));
        QCOMPARE(e.stops.size(), 3);
        QCOMPARE(e.stops[0].number, 1);
        QCOMPARE(e.stops[0].ranges[0].start, 5);
        QCOMPARE(e.stops[1].ranges[0].start, 7);
        QCOMPARE(e.stops[2].number, 0); // финальная — последней
        QCOMPARE(e.stops[2].ranges[0].start, 12);
    }

    void placeholdersAndMirrors()
    {
        const Expansion e = expand(QStringLiteral("${1:name} = $1; ${2|int,long|}"));
        QCOMPARE(e.text, QStringLiteral("name = name; int"));
        QCOMPARE(e.stops[0].ranges.size(), 2);
        QCOMPARE(e.stops[0].ranges[1].start, 7);
        QCOMPARE(e.stops[0].ranges[1].length, 4);
        QCOMPARE(e.stops[1].ranges[0].length, 3);
        QCOMPARE(e.stops.last().ranges[0].start, int(e.text.size())); // $0 нет — в конец
    }

    void nestedVariablesEscapes()
    {
        const Expansion e = expand(QStringLiteral("${1:a${2:b}c} $TM_FILENAME ${UNKNOWN:x} \\$1 5$"),
                                   [](const QString &name) -> std::optional<QString> {
                                       if (name == QStringLiteral("TM_FILENAME"))
                                           return QStringLiteral("f.cpp");
                                       return std::nullopt;
                                   });
        QCOMPARE(e.text, QStringLiteral("abc f.cpp x $1 5$"));
        QCOMPARE(e.stops[0].ranges[0].length, 3);
        QCOMPARE(e.stops[1].ranges[0].start, 1);
        QCOMPARE(e.stops[1].ranges[0].length, 1);
    }
};

QTEST_GUILESS_MAIN(SnippetTest)
#include "snippet_test.moc"
