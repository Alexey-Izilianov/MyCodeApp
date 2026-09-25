#include <QSignalSpy>
#include <QtTest>
#include "src/services/searchengine.h"

class SearchEngineTest : public QObject {
    Q_OBJECT

private slots:
    void findsAllMatches();
    void caseInsensitive();
    void cancelDropsResults();
};

void SearchEngineTest::findsAllMatches()
{
    SearchEngine engine;
    QSignalSpy spy(&engine, &SearchEngine::resultsReady);

    engine.start({QStringLiteral("abc abc"), QStringLiteral("xyz")},
                 QStringLiteral("abc"), true);
    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.size(), 1);
    const auto matches
        = spy.first().first().value<QVector<SearchEngine::Match>>();
    QCOMPARE(matches.size(), 2);
    QCOMPARE(matches.at(0).line, 0);
    QCOMPARE(matches.at(0).column, 0);
    QCOMPARE(matches.at(1).line, 0);
    QCOMPARE(matches.at(1).column, 4);
}

void SearchEngineTest::caseInsensitive()
{
    SearchEngine engine;
    QSignalSpy spy(&engine, &SearchEngine::resultsReady);

    engine.start({QStringLiteral("Hello HELLO hello")}, QStringLiteral("hello"),
                 false);
    QVERIFY(spy.wait(2000));
    const auto matches
        = spy.first().first().value<QVector<SearchEngine::Match>>();
    QCOMPARE(matches.size(), 3);
}

void SearchEngineTest::cancelDropsResults()
{
    SearchEngine engine;
    QSignalSpy spy(&engine, &SearchEngine::resultsReady);

    QStringList lines;
    for (int i = 0; i < 2000; ++i)
        lines.append(QString(5000, 'a')); // скан на десятки миллисекунд
    engine.start(lines, QStringLiteral("a"), true);
    engine.cancel(); // стартовавший позже номер запуска гасит первый
    QTest::qWait(500);
    QCOMPARE(spy.size(), 0);
}

QTEST_MAIN(SearchEngineTest)
#include "searchengine_test.moc"