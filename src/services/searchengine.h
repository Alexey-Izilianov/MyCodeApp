#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

// Асинхронный plain-поиск по снимку строк документа (M1).
// Снимок делает вызывающий (под мьютексом документа); сам движок ищет
// в QThreadPool, результат — сигналом resultsReady в GUI-поток.
// Повторный start отменяет предыдущий прогон (по номеру запуска).
class SearchEngine : public QObject {
    Q_OBJECT
public:
    struct Match {
        int line = 0;
        int column = 0;
        int length = 0;
    };

    explicit SearchEngine(QObject *parent = nullptr);

    void start(const QStringList &lines, const QString &needle,
               bool caseSensitive);
    void cancel() { ++m_runId; }

signals:
    void resultsReady(const QVector<SearchEngine::Match> &matches);

private:
    std::atomic<int> m_runId{0};
};