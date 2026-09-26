#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

#include "src/core/piecetable.h"

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

    // Снимок piece table: на GUI-потоке копирование O(1), материализация
    // строк происходит в фоновом потоке.
    void start(const core::PieceTable::Snapshot &snapshot, const QString &needle,
               bool caseSensitive);
    // Готовый список строк (для тестов и маленьких документов)
    void start(const QStringList &lines, const QString &needle,
               bool caseSensitive);
    void cancel() { ++m_runId; }

signals:
    void resultsReady(const QVector<SearchEngine::Match> &matches);

private:
    void searchLines(const QStringList &lines, int id, const QString &needle,
                     Qt::CaseSensitivity cs);

    std::atomic<int> m_runId{0};
};