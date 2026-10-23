#include "historymodel.h"

#include <QLocale>

HistoryModel::HistoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_commits.size());
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_commits.size())
        return {};
    const vcs::Commit &c = m_commits.at(index.row());
    switch (role) {
    case IdRole: return c.id;
    case ShortIdRole: return c.id.left(8);
    case SummaryRole: return c.summary;
    case MessageRole: return c.message;
    case AuthorRole: return c.author;
    case EmailRole: return c.email;
    case BranchRole: return c.branch;
    case DateRole: return QLocale().toString(c.time.toLocalTime(), QStringLiteral("dd.MM.yyyy HH:mm"));
    case RefsRole: {
        static const char *kinds[] = {"branch", "remote", "tag", "bookmark"};
        QVariantList refs;
        for (const vcs::Ref &r : c.refs)
            refs.append(QVariantMap{{QStringLiteral("name"), r.name},
                                    {QStringLiteral("kind"), QLatin1String(kinds[r.kind])},
                                    {QStringLiteral("current"), r.current}});
        return refs;
    }
    case GraphRole: {
        const vcs::GraphRow &row = m_rows.at(index.row());
        QVariantList edges;
        edges.reserve(row.edges.size() * 4);
        for (const vcs::GraphEdge &e : row.edges)
            edges << e.from << e.to << e.color << int(e.kind);
        return QVariantMap{{QStringLiteral("lane"), row.lane}, {QStringLiteral("color"), row.color},
                           {QStringLiteral("width"), row.width}, {QStringLiteral("edges"), edges}};
    }
    default: return {};
    }
}

QHash<int, QByteArray> HistoryModel::roleNames() const
{
    return {{IdRole, "commitId"},   {ShortIdRole, "shortId"}, {SummaryRole, "summary"},
            {MessageRole, "message"}, {AuthorRole, "author"},  {EmailRole, "email"},
            {DateRole, "date"},     {RefsRole, "refs"},       {GraphRole, "graph"},
            {BranchRole, "branch"}};
}

void HistoryModel::reset(QVector<vcs::Commit> commits, QVector<vcs::GraphRow> rows)
{
    beginResetModel();
    m_commits = std::move(commits);
    m_rows = std::move(rows);
    m_maxLanes = 1;
    for (const vcs::GraphRow &row : std::as_const(m_rows))
        m_maxLanes = qMax(m_maxLanes, row.width);
    endResetModel();
    emit countChanged();
}

int HistoryModel::find(const QString &text, int from) const
{
    if (text.isEmpty())
        return -1;
    for (int n = 0; n < m_commits.size(); ++n) {
        const int i = (qMax(0, from) + n) % int(m_commits.size()); // по кругу
        const vcs::Commit &c = m_commits.at(i);
        if (c.message.contains(text, Qt::CaseInsensitive) || c.author.contains(text, Qt::CaseInsensitive)
            || c.id.startsWith(text, Qt::CaseInsensitive))
            return i;
    }
    return -1;
}

QString HistoryModel::parentOf(const QString &id) const
{
    for (const vcs::Commit &c : m_commits)
        if (c.id == id)
            return c.parents.value(0);
    return {};
}
