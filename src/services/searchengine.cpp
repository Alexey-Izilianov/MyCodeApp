#include "searchengine.h"

#include <QThreadPool>
#include <QVector>

SearchEngine::SearchEngine(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<SearchEngine::Match>();
}

void SearchEngine::start(const core::PieceTable::Snapshot &snapshot,
                         const QString &needle, bool caseSensitive)
{
    const int id = ++m_runId;
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QThreadPool::globalInstance()->start([this, id, snapshot, needle, cs] {
        search([&](const LineVisitor &visit) { snapshot.forEachLine(visit); }, id, needle, cs);
    });
}

void SearchEngine::start(const QStringList &lines, const QString &needle, bool caseSensitive)
{
    const int id = ++m_runId;
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QThreadPool::globalInstance()->start([this, id, lines, needle, cs] {
        search([&](const LineVisitor &visit) {
            for (int i = 0; i < lines.size() && visit(i, lines.at(i)); ++i) {}
        }, id, needle, cs);
    });
}

void SearchEngine::search(const std::function<void(const LineVisitor &)> &forEachLine, int id,
                          const QString &needle, Qt::CaseSensitivity cs)
{
    constexpr int kMaxMatches = 1'000'000; // предохранитель памяти
    QVector<Match> matches;
    bool cancelled = false;
    forEachLine([&](int line, const QString &text) {
        if ((line & 255) == 0 && m_runId.load(std::memory_order_acquire) != id) {
            cancelled = true; // запущен новый поиск
            return false;
        }
        for (int from = 0;;) {
            const int idx = int(text.indexOf(needle, from, cs));
            if (idx < 0)
                break;
            matches.append({line, idx, int(needle.size())});
            from = idx + int(needle.size());
        }
        return matches.size() < kMaxMatches;
    });
    if (!cancelled && m_runId.load(std::memory_order_acquire) == id)
        emit resultsReady(matches); // queued: придёт в GUI-поток
}
