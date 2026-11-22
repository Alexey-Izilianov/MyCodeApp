#include <QSignalSpy>
#include <QtTest>
#include "src/core/piecetable.h"
#include "src/services/searchengine.h"

class SearchEngineTest : public QObject {
    Q_OBJECT

private slots:
    void findsAllMatches();
    void caseInsensitive();
    void cancelDropsResults();
    void searchSnapshot();
    void capsMatchesWithinOneLine();
    void wholeWordRespectsBorders();
    void regexFindsAnchoredMatches();
    void regexCaseInsensitive();
    void invalidRegexSignalsError();
    void regexSkipsHugeLine();
};

void SearchEngineTest::capsMatchesWithinOneLine()
{
    SearchEngine engine;
    QSignalSpy spy(&engine, &SearchEngine::resultsReady);
    engine.start(QStringList{QString(1'100'000, u'a')}, QStringLiteral("a"), true);
    QVERIFY(spy.wait(5000));
    const auto matches = spy.first().first().value<QVector<SearchEngine::Match>>();
    QCOMPARE(matches.size(), 1'000'000);
}

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

void SearchEngineTest::searchSnapshot()
{
    // Путь из редактора: снимок piece table (COW) вместо списка строк
    core::PieceTable pt;
    pt.reset(QStringLiteral("alpha beta\ngamma beta"));
    const auto snap = pt.snapshot();
    // Правки после снимка не должны влиять на прогон
    pt.remove(0, 6);

    SearchEngine engine;
    QSignalSpy spy(&engine, &SearchEngine::resultsReady);
    engine.start(snap, QStringLiteral("beta"), true);
    QVERIFY(spy.wait(2000));
    const auto matches
        = spy.first().first().value<QVector<SearchEngine::Match>>();
    QCOMPARE(matches.size(), 2);
    QCOMPARE(matches.at(0).line, 0);
    QCOMPARE(matches.at(0).column, 6);
    QCOMPARE(matches.at(1).line, 1);
    QCOMPARE(matches.at(1).column, 6);
}

void SearchEngineTest::wholeWordRespectsBorders()
{
    SearchEngine engine;
    QSignalSpy spy(&engine, &SearchEngine::resultsReady);

    // Целые вхождения: 0, 19, в скобках (39). «_word_» — не целое: подчёркивание
    // считается частью слова (как в VS Code), subword/words/word2 — тоже не целые
    engine.start({QStringLiteral("word words subword word; _word_ word2 (word)")},
                  QStringLiteral("word"), true, SearchEngine::Mode::WholeWord);
    QVERIFY(spy.wait(2000));
    const auto matches = spy.first().first().value<QVector<SearchEngine::Match>>();
    QCOMPARE(matches.size(), 3);
    QCOMPARE(matches.at(0).column, 0);
    QCOMPARE(matches.at(1).column, 19);
    QCOMPARE(matches.at(2).column, 39);
}

void SearchEngineTest::regexFindsAnchoredMatches()
{
    SearchEngine engine;
    QSignalSpy spy(&engine, &SearchEngine::resultsReady);

    // Каждая строка — отдельный мир: «^» и «$» работают внутри строки
    engine.start({QStringLiteral("return foo();"), QStringLiteral("foos"), QStringLiteral("x foo")},
                 QStringLiteral("^foo|\\);$"), true, SearchEngine::Mode::Regex);
    QVERIFY(spy.wait(2000));
    const auto matches = spy.first().first().value<QVector<SearchEngine::Match>>();
    QCOMPARE(matches.size(), 2);
    QCOMPARE(matches.at(0).line, 0);
    QCOMPARE(matches.at(0).column, 11);
    QCOMPARE(matches.at(0).length, 2); // «);» — конец строки («$» ловится перед \n-хвостом)
    QCOMPARE(matches.at(1).line, 1);
    QCOMPARE(matches.at(1).column, 0);
    QCOMPARE(matches.at(1).length, 3); // «^foo»
}

void SearchEngineTest::regexCaseInsensitive()
{
    SearchEngine engine;
    QSignalSpy spy(&engine, &SearchEngine::resultsReady);

    engine.start({QStringLiteral("Error ERROR error")},
                 QStringLiteral("error"), false, SearchEngine::Mode::Regex);
    QVERIFY(spy.wait(2000));
    const auto matches = spy.first().first().value<QVector<SearchEngine::Match>>();
    QCOMPARE(matches.size(), 3);
}

void SearchEngineTest::invalidRegexSignalsError()
{
    SearchEngine engine;
    QSignalSpy errors(&engine, &SearchEngine::errorOccurred);
    QSignalSpy results(&engine, &SearchEngine::resultsReady);

    engine.start({QStringLiteral("abc")}, QStringLiteral("a("), true,
                 SearchEngine::Mode::Regex);
    QVERIFY(errors.wait(2000));
    QVERIFY(!errors.first().first().toString().isEmpty());
    QVERIFY(!results.wait(500)); // результата при ошибке нет
}

void SearchEngineTest::regexSkipsHugeLine()
{
    SearchEngine engine;
    QSignalSpy spy(&engine, &SearchEngine::resultsReady);
    // Строка 1,2 млн символов — длиннее предохранителя — пропускается
    const QString huge(1'200'000, u'x');
    engine.start({huge, QStringLiteral("needle")}, QStringLiteral("needle"), true,
                 SearchEngine::Mode::Regex);
    QVERIFY(spy.wait(5000));
    const auto matches = spy.first().first().value<QVector<SearchEngine::Match>>();
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.at(0).line, 1);
}

QTEST_MAIN(SearchEngineTest)
#include "searchengine_test.moc"
