#include "searchengine.h"

#include <QThreadPool>
#include <QVector>

SearchEngine::SearchEngine(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<SearchEngine::Match>();
}

void SearchEngine::start(const QStringList &lines, const QString &needle,
                         bool caseSensitive)
{
    const int id = ++m_runId;
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive
                                                 : Qt::CaseInsensitive;
    QThreadPool::globalInstance()->start([this, id, lines, needle, cs]() {
        QVector<Match> matches;
        matches.reserve(256);
        const int n = needle.size();
        for (int i = 0; i < lines.size(); ++i) {
            if ((i & 255) == 0 && m_runId.load(std::memory_order_acquire) != id)
                return; // запущен новый поиск — этот прогон не нужен
            int from = 0;
            while (true) {
                const int idx = lines.at(i).indexOf(needle, from, cs);
                if (idx < 0)
                    break;
                matches.append({i, idx, n});
                from = idx + n;
                if (matches.size() >= 1'000'000) // предохранитель памяти
                    break;
            }
        }
        if (m_runId.load(std::memory_order_acquire) != id)
            return;
        emit resultsReady(matches); // auto->queued: придёт в GUI-поток
    });
}