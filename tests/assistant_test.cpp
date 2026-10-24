#include <QTest>

#include "src/assistant/prompting.h"

using namespace assistant;

// JSON в тестах пишется с одинарными кавычками — так читается легче
static QByteArray json(const char *text)
{
    return QByteArray(text).replace('\'', '"');
}

class AssistantTest : public QObject {
    Q_OBJECT

private slots:
    void ollamaStreamInPieces()
    {
        StreamParser parser(Protocol::Ollama);
        // Строки JSON приходят разорванными на произвольных местах
        QVector<Delta> d = parser.feed(json("{'message':{'role':'assistant','content':'При'},'done':false}\n"
                                            "{'message':{'content':'в"));
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].content, QStringLiteral("При"));
        d = parser.feed(json("ет','thinking':''},'done':false}\n"
                             "{'message':{'content':''},'done':true,'prompt_eval_count':12,'eval_count':34}"));
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].content, QStringLiteral("вет"));
        d = parser.finish(); // последняя строка — без перевода строки
        QCOMPARE(d.size(), 1);
        QVERIFY(d[0].done);
        QCOMPARE(d[0].promptTokens, 12);
        QCOMPARE(d[0].outputTokens, 34);
    }

    void ollamaErrorAndThinking()
    {
        StreamParser parser(Protocol::Ollama);
        const QVector<Delta> d = parser.feed(json("{'message':{'content':'','thinking':'хм'},'done':false}\n"
                                                  "{'error':'model x not found'}\n"));
        QCOMPARE(d.size(), 2);
        QCOMPARE(d[0].thinking, QStringLiteral("хм"));
        QCOMPARE(d[1].error, QStringLiteral("model x not found"));
        QVERIFY(d[1].done);
    }

    void openAiServerSentEvents()
    {
        StreamParser parser(Protocol::OpenAi);
        const QVector<Delta> d = parser.feed(json(
            ": ping\r\n\r\n"
            "data: {'choices':[{'delta':{'reasoning_content':'r'}}]}\r\n\r\n"
            "data: {'choices':[{'delta':{'content':'Да'},'finish_reason':null}]}\n\n"
            "data: {'choices':[],'usage':{'prompt_tokens':5,'completion_tokens':7}}\n\n"
            "data: [DONE]\n\n"));
        QCOMPARE(d.size(), 4);
        QCOMPARE(d[0].thinking, QStringLiteral("r"));
        QCOMPARE(d[1].content, QStringLiteral("Да"));
        QCOMPARE(d[2].outputTokens, 7);
        QVERIFY(d[3].done);
    }

    void errorBodies()
    {
        QCOMPARE(errorFromBody(json("{'error':'boom'}")), QStringLiteral("boom"));
        QCOMPARE(errorFromBody(json("{'error':{'message':'bad key','type':'auth'}}")), QStringLiteral("bad key"));
        QCOMPARE(errorFromBody("Not Found"), QStringLiteral("Not Found"));
    }

    void excerptWholeOrWindow()
    {
        const QStringList lines = {"aaaa", "bbbb", "cccc", "dddd", "eeee", "ffff"};
        auto at = [&](int i) { return lines.at(i); };
        Excerpt e = excerpt(at, 1, 3, 1, 100);
        QCOMPARE(e.text, QStringLiteral("bbbb\ncccc\ndddd"));
        QVERIFY(!e.truncated);
        // 3 строки по 5 символов: окно вокруг центра, вниз и вверх поровну
        e = excerpt(at, 0, 5, 3, 15);
        QVERIFY(e.truncated);
        QCOMPARE(e.firstLine, 2);
        QCOMPARE(e.lastLine, 4);
        QCOMPARE(e.text, QStringLiteral("cccc\ndddd\neeee"));
        // Одна строка длиннее бюджета — обрезается
        e = excerpt([](int) { return QStringLiteral("0123456789"); }, 0, 0, 0, 4);
        QCOMPARE(e.text, QStringLiteral("0123"));
    }

    void logDigestSmallIsWhole()
    {
        LogDigest digest(1000);
        for (const char *line : {"start", "error: x", "end"})
            digest.add(QString::fromLatin1(line));
        QCOMPARE(digest.text(), QStringLiteral("start\nerror: x\nend"));
        QCOMPARE(digest.errorCount(), 1);
    }

    void logDigestCollapsesRepeats()
    {
        LogDigest digest(3000);
        digest.add(QStringLiteral("app started"));
        for (int i = 0; i < 500; ++i) {
            digest.add(QStringLiteral("12:00:%1 info: tick %2").arg(i % 60).arg(i));
            if (i % 50 == 0)
                digest.add(QStringLiteral("12:00:%1 ERROR: connection 0x%2 refused").arg(i % 60).arg(i * 977, 0, 16));
        }
        digest.add(QStringLiteral("Предупреждение: диск почти заполнен"));
        digest.add(QStringLiteral("app stopped"));
        const QString text = digest.text();
        QVERIFY(text.size() <= 3000 + 200);
        QVERIFY(text.contains(QStringLiteral("app started")));
        QVERIFY(text.contains(QStringLiteral("ERROR: connection")));
        QVERIFY(text.contains(QStringLiteral("(x10)")));          // 10 ошибок — одной строкой
        QVERIFY(text.contains(QStringLiteral("диск почти заполнен")));
        QVERIFY(text.endsWith(QStringLiteral("app stopped")));    // конец журнала
        QCOMPARE(digest.errorCount(), 10);
        QCOMPARE(digest.warningCount(), 1);
    }

    void codeBlocks()
    {
        QCOMPARE(firstCodeBlock(QStringLiteral("Вот:\n```cpp\n/** Сумма. */\n```\nГотово")),
                 QStringLiteral("/** Сумма. */"));
        QCOMPARE(firstCodeBlock(QStringLiteral("  просто текст  ")), QStringLiteral("просто текст"));
        QCOMPARE(firstCodeBlock(QStringLiteral("```\nоборван")), QStringLiteral("оборван"));
        QCOMPARE(cleanCommitMessage(QStringLiteral("«Исправлен вывод»")), QStringLiteral("Исправлен вывод"));
        QCOMPARE(cleanCommitMessage(QStringLiteral("```\nДобавлен поиск\n\n- регистр\n```")),
                 QStringLiteral("Добавлен поиск\n\n- регистр"));
        QCOMPARE(cleanCommitMessage(QStringLiteral("Добавлен файл.  \n- новый.txt  \n- правка")),
                 QStringLiteral("Добавлен файл\n\n- новый.txt\n- правка"));
    }

    void commentAboveCode()
    {
        const QStringList code = {"", "    int sum(int a, int b)", "    {", "        return a + b;", "    }"};
        const Placement p = placeComment(code, 10, QStringLiteral("C++"),
                                         QStringLiteral("  /**\n   * Сумма.\n   */\n"));
        QCOMPARE(p.line, 11); // перед первой непустой строкой
        QCOMPARE(p.text, QStringLiteral("    /**\n     * Сумма.\n     */\n"));
    }

    void pythonDocstringInsideFunction()
    {
        const QStringList code = {"def load(path,", "         strict=False):", "    data = read(path)", "    return data"};
        Placement p = placeComment(code, 0, QStringLiteral("Python"), QStringLiteral("Читает файл."));
        QCOMPARE(p.line, 2); // после многострочного заголовка
        QCOMPARE(p.text, QStringLiteral("    \"\"\"Читает файл.\"\"\"\n"));
        p = placeComment(code, 0, QStringLiteral("Python"), QStringLiteral("\"\"\"\nЧитает файл.\n\nArgs: путь.\n\"\"\""));
        QCOMPARE(p.text, QStringLiteral("    \"\"\"\n    Читает файл.\n\n    Args: путь.\n    \"\"\"\n"));
    }
};

QTEST_GUILESS_MAIN(AssistantTest)
#include "assistant_test.moc"
