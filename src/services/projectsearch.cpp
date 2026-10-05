#include "projectsearch.h"

#include <QElapsedTimer>
#include <QFile>
#include <QStringDecoder>

#include "workspace.h"

namespace {

constexpr int kPreviewContext = 20; // символов до совпадения в превью

QString decode(const QByteArray &bytes)
{
    if (bytes.startsWith("\xFF\xFE"))
        return QStringDecoder(QStringConverter::Utf16LE)(bytes.mid(2));
    if (bytes.startsWith("\xFE\xFF"))
        return QStringDecoder(QStringConverter::Utf16BE)(bytes.mid(2));
    return QString::fromUtf8(bytes);
}

QString previewHtml(const QString &line, int column, int length)
{
    auto piece = [&](int start, int count) {
        return line.mid(start, count).replace(u'\t', u' ').toHtmlEscaped();
    };
    int indent = 0;
    while (indent < column && line.at(indent).isSpace())
        ++indent;
    const int from = qMax(indent, column - kPreviewContext);
    return (from > indent ? QStringLiteral("…") : QString()) + piece(from, column - from)
           + QStringLiteral("<span style=\"background-color:#6a5520\">") + piece(column, length)
           + QStringLiteral("</span>") + piece(column + length, 200);
}

} // namespace

ProjectSearch::ProjectSearch(QObject *parent)
    : QAbstractListModel(parent)
{
    m_pool.setMaxThreadCount(1);
}

ProjectSearch::~ProjectSearch()
{
    ++m_generation;
    m_pool.waitForDone();
}

QVector<ProjectSearch::Match> ProjectSearch::searchText(const QString &text, const QString &needle,
                                                        Qt::CaseSensitivity cs, int limit)
{
    QVector<Match> matches;
    int line = 0;
    qsizetype lineStart = 0;
    for (qsizetype found = text.indexOf(needle, 0, cs); found >= 0 && matches.size() < limit;
         found = text.indexOf(needle, found + needle.size(), cs)) {
        // Считаем переводы строк только между совпадениями — без разбиения текста на строки
        for (qsizetype nl = text.indexOf(u'\n', lineStart); nl >= 0 && nl < found;
             nl = text.indexOf(u'\n', lineStart)) {
            ++line;
            lineStart = nl + 1;
        }
        const qsizetype lineEnd = text.indexOf(u'\n', lineStart);
        QString lineText = text.mid(lineStart, lineEnd < 0 ? -1 : lineEnd - lineStart);
        if (lineText.endsWith(u'\r'))
            lineText.chop(1);
        const int column = int(found - lineStart);
        matches.append({line, column, int(needle.size()), previewHtml(lineText, column, int(needle.size()))});
    }
    return matches;
}

void ProjectSearch::start(const QString &needle, bool caseSensitive)
{
    clear();
    if (needle.isEmpty() || m_root.isEmpty())
        return;
    const int generation = ++m_generation;
    m_running = true;
    emit progressChanged();

    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    m_pool.start([this, generation, needle, cs, root = m_root] {
        auto cancelled = [&] { return m_generation != generation; };
        const QStringList files = Workspace::scanFiles(root, cancelled);

        QVector<FileResult> batch;
        int matches = 0, done = 0;
        bool truncated = false;
        QElapsedTimer sinceFlush;
        sinceFlush.start();
        auto flush = [&] {
            QMetaObject::invokeMethod(this, [=, this] {
                if (m_generation == generation)
                    append(batch, done, int(files.size()), truncated);
            }, Qt::QueuedConnection);
            batch.clear();
            sinceFlush.restart();
        };

        for (const QString &relative : files) {
            if (cancelled())
                return;
            ++done;
            QFile file(root + u'/' + relative);
            if (file.size() > kMaxFileSize || !file.open(QIODevice::ReadOnly))
                continue;
            const QByteArray bytes = file.readAll();
            const bool utf16 = bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF");
            if (!utf16 && bytes.left(8192).contains('\0'))
                continue; // двоичный файл

            FileResult result{file.fileName(), relative,
                              searchText(decode(bytes), needle, cs, kMaxMatches - matches)};
            if (!result.matches.isEmpty()) {
                matches += int(result.matches.size());
                batch.append(std::move(result));
            }
            truncated = matches >= kMaxMatches;
            if (truncated)
                break;
            if (sinceFlush.elapsed() > 100)
                flush();
        }
        flush();
        QMetaObject::invokeMethod(this, [this, generation] { finish(generation); }, Qt::QueuedConnection);
    });
}

void ProjectSearch::append(const QVector<FileResult> &batch, int filesDone, int filesTotal, bool truncated)
{
    m_filesDone = filesDone;
    m_filesTotal = filesTotal;
    m_truncated = truncated;
    if (!batch.isEmpty()) {
        int added = 0;
        for (const FileResult &r : batch)
            added += 1 + int(r.matches.size());
        beginInsertRows({}, int(m_rows.size()), int(m_rows.size()) + added - 1);
        for (const FileResult &r : batch) {
            const int file = int(m_results.size());
            m_results.append(r);
            m_rows.append({file, -1});
            for (int i = 0; i < r.matches.size(); ++i)
                m_rows.append({file, i});
            m_matchCount += int(r.matches.size());
        }
        endInsertRows();
    }
    emit progressChanged();
}

void ProjectSearch::finish(int generation)
{
    if (generation != m_generation)
        return;
    m_running = false;
    emit progressChanged();
}

void ProjectSearch::cancel()
{
    ++m_generation;
    if (m_running) {
        m_running = false;
        emit progressChanged();
    }
}

void ProjectSearch::clear()
{
    cancel();
    beginResetModel();
    m_results.clear();
    m_rows.clear();
    endResetModel();
    m_matchCount = m_filesDone = m_filesTotal = 0;
    m_truncated = false;
    emit progressChanged();
}

int ProjectSearch::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ProjectSearch::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const auto [file, matchIndex] = m_rows.at(index.row());
    const FileResult &r = m_results.at(file);
    const Match *m = matchIndex >= 0 ? &r.matches.at(matchIndex) : nullptr;
    switch (role) {
    case IsFileRole: return !m;
    case PathRole: return r.path;
    case RelativeRole: return r.relative;
    case CountRole: return int(r.matches.size());
    case LineRole: return m ? m->line : 0;
    case ColumnRole: return m ? m->column : 0;
    case LengthRole: return m ? m->length : 0;
    case PreviewRole: return m ? m->previewHtml : QString();
    default: return {};
    }
}

QHash<int, QByteArray> ProjectSearch::roleNames() const
{
    return {{IsFileRole, "isFile"}, {PathRole, "path"}, {RelativeRole, "relative"},
            {LineRole, "line"}, {ColumnRole, "column"}, {LengthRole, "length"},
            {PreviewRole, "preview"}, {CountRole, "count"}};
}
