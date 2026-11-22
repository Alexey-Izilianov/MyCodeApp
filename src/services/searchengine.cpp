#include "searchengine.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThreadPool>
#include <QVector>

namespace {
// РеDoS-защита regex-сканирования: строки длиннее этого пропускаются,
// а весь сканирование прекращается при превышении бюджета времени
constexpr qsizetype kMaxRegexLine = 1 << 20;
constexpr qint64 kRegexBudgetMs = 5000;
constexpr int kMaxPatternLength = 4096;

bool wordChar(QChar ch)
{
    return ch.isLetterOrNumber() || ch == u'_';
}

void collectPlainOrWord(const QString &text, int line, const QString &needle,
                        Qt::CaseSensitivity cs, bool wholeWord, QVector<SearchEngine::Match> &matches,
                        int maxMatches)
{
    for (int from = 0;;) {
        const int idx = int(text.indexOf(needle, from, cs));
        if (idx < 0)
            return;
        const bool hit = !wholeWord
            || SearchEngine::wholeWordHit(text, idx, int(needle.size()));
        if (hit) {
            matches.append({line, idx, int(needle.size())});
            if (matches.size() == maxMatches)
                return;
            from = idx + int(needle.size());
        } else {
            from = idx + 1; // пропустили суррогатный кандидат — идём дальше
        }
    }
}
} // namespace

bool SearchEngine::wholeWordHit(const QString &text, int from, int length)
{
    const bool before = from > 0 && wordChar(text.at(from - 1));
    const int end = from + length;
    const bool after = end < text.size() && wordChar(text.at(end));
    return !before && !after;
}

QRegularExpression SearchEngine::buildRegex(const QString &needle, bool caseSensitive, QString *error)
{
    if (int(needle.size()) > kMaxPatternLength) {
        if (error)
            *error = QCoreApplication::translate("SearchEngine",
                "Выражение слишком длинное (больше %1 символов)").arg(kMaxPatternLength);
        return {};
    }
    // Multiline: «$» ловит конец строки («перед \n»); по одной строке за
    // сканирование других «\n» внутри нет
    const auto options = caseSensitive ? QRegularExpression::MultilineOption
                                       : QRegularExpression::MultilineOption
                                             | QRegularExpression::CaseInsensitiveOption;
    QRegularExpression re(needle, options);
    if (!re.isValid()) {
        if (error)
            *error = re.errorString();
        return {};
    }
    if (error)
        error->clear();
    return re;
}

SearchEngine::SearchEngine(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<SearchEngine::Match>();
}

SearchEngine::~SearchEngine()
{
    cancel();
    m_pool.waitForDone();
}

void SearchEngine::start(const core::PieceTable::Snapshot &snapshot,
                         const QString &needle, bool caseSensitive, Mode mode)
{
    const int id = ++m_runId;
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    m_pool.start([this, id, snapshot, needle, cs, mode] {
        search([&](const LineVisitor &visit) { snapshot.forEachLine(visit); }, id, needle, cs, mode);
    });
}

void SearchEngine::start(const QStringList &lines, const QString &needle,
                         bool caseSensitive, Mode mode)
{
    const int id = ++m_runId;
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    m_pool.start([this, id, lines, needle, cs, mode] {
        search([&](const LineVisitor &visit) {
            for (int i = 0; i < lines.size() && visit(i, lines.at(i)); ++i) {}
        }, id, needle, cs, mode);
    });
}

void SearchEngine::search(const std::function<void(const LineVisitor &)> &forEachLine, int id,
                          const QString &needle, Qt::CaseSensitivity cs, Mode mode)
{
    constexpr int kMaxMatches = 1'000'000; // предохранитель памяти
    QVector<Match> matches;
    const auto current = [&] { return m_runId.load(std::memory_order_acquire) == id; };
    QElapsedTimer budget;
    QRegularExpression regex;
    if (mode == Mode::Regex && !needle.isEmpty()) {
        QString error;
        regex = buildRegex(needle, cs == Qt::CaseSensitive, &error);
        if (!error.isEmpty()) {
            emit errorOccurred(error);
            return;
        }
        budget.start();
    }
    if (!needle.isEmpty()) {
        forEachLine([&](int line, const QString &text) {
            if (!current())
                return false;
            if (mode == Mode::Regex) {
                if (text.size() > kMaxRegexLine)
                    return true; // слишком длинная строка — см. комментарий в заголовке
                if (budget.elapsed() > kRegexBudgetMs)
                    return false; // бюджет сканирования исчерпан — покажем накопленное
                // Хвостовой '\n': «$» у PCRE2 на конце view не срабатывает,
                // а «перед финальным \n» — да. Символ за строкой не учитываем.
                const QString lineWithNl = text + u'\n';
                auto it = regex.globalMatchView(QStringView(lineWithNl));
                while (it.hasNext()) {
                    const auto m = it.next();
                    const int start = int(m.capturedStart());
                    const int end = qMin(int(m.capturedEnd()), text.size());
                    if (end <= start)
                        continue; // нулевая длина или захват '\n'-хвоста целиком
                    matches.append({line, start, end - start});
                    if (matches.size() == kMaxMatches)
                        return false;
                }
                return true;
            }
            collectPlainOrWord(text, line, needle, cs, mode == Mode::WholeWord, matches, kMaxMatches);
            if (matches.size() == kMaxMatches)
                return false;
            return true;
        });
    }
    if (current())
        emit resultsReady(matches); // queued: придёт в GUI-поток
}
