#pragma once

#include <QList>
#include <QObject>
#include <QRegularExpression>
#include <QThreadPool>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>

#include "src/core/piecetable.h"

// Асинхронный поиск подстроки по снимку документа: снимок делает вызывающий,
// движок ищет в QThreadPool, результат — сигналом resultsReady в GUI-поток.
// Повторный start отменяет предыдущий прогон (по номеру запуска).
// Режимы: Plain — подстрока; WholeWord — подстрока на границах слова;
// Regex — регулярное выражение (каждая строка текста сканируется отдельно,
// «^»/«$» внутри строки; строки длиннее 1 МБ пропускаются, чтобы взрывная
// правка регэкспа не висела на гигантской строке; сканирование прекращается
// при превышении временного бюджета — частичный результат всё равно приходит).
class SearchEngine : public QObject {
    Q_OBJECT
public:
    struct Match {
        int line = 0;
        int column = 0;
        int length = 0;
    };
    enum class Mode { Plain = 0, WholeWord = 1, Regex = 2 };

    explicit SearchEngine(QObject *parent = nullptr);
    ~SearchEngine() override; // отменяет и дожидается поиска: задачи держат this

    // Снимок piece table: на GUI-потоке копирование O(1), строки
    // читаются по одной в фоновом потоке.
    void start(const core::PieceTable::Snapshot &snapshot, const QString &needle,
               bool caseSensitive, Mode mode = Mode::Plain);
    // Готовый список строк (для тестов и маленьких документов)
    void start(const QStringList &lines, const QString &needle,
               bool caseSensitive, Mode mode = Mode::Plain);
    void cancel() { ++m_runId; }

    // Совпадения на границах слова (пробелы, знаки, скобки)
    static bool wholeWordHit(const QString &text, int from, int length);
    static QRegularExpression buildRegex(const QString &needle, bool caseSensitive, QString *error);

signals:
    void resultsReady(const QVector<SearchEngine::Match> &matches);
    // Regex: ошибка в выражении (пустой результат не сигналит об этом)
    void errorOccurred(const QString &message);

private:
    using LineVisitor = std::function<bool(int, const QString &)>;
    void search(const std::function<void(const LineVisitor &)> &forEachLine, int id,
                const QString &needle, Qt::CaseSensitivity cs, Mode mode);

    std::atomic<int> m_runId{0};
    QThreadPool m_pool;
};
