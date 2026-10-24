#include "chatmodel.h"

ChatModel::ChatModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(kRefreshMs);
    connect(&m_refresh, &QTimer::timeout, this, [this] { refresh(m_pendingRow, {TextRole}); });
}

int ChatModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_messages.size());
}

const ChatModel::Message *ChatModel::find(int row) const
{
    return row >= 0 && row < m_messages.size() ? &m_messages.at(row) : nullptr;
}

QVariant ChatModel::data(const QModelIndex &index, int role) const
{
    const Message *m = find(index.row());
    if (!m || !index.isValid())
        return {};
    switch (role) {
    case UserRole: return m->user;
    case TextRole: return m->text;
    case ErrorRole: return m->error;
    case StatsRole: return m->stats;
    case DoneRole: return m->done;
    case CommentRole: return m->comment.has_value();
    case HasCodeRole: return m->done && m->text.contains(QLatin1String("```"));
    default: return {};
    }
}

QHash<int, QByteArray> ChatModel::roleNames() const
{
    return {{UserRole, "user"}, {TextRole, "text"},       {ErrorRole, "error"},     {StatsRole, "stats"},
            {DoneRole, "done"}, {CommentRole, "comment"}, {HasCodeRole, "hasCode"}};
}

int ChatModel::append(Message message)
{
    const int row = int(m_messages.size());
    beginInsertRows({}, row, row);
    m_messages.append(std::move(message));
    endInsertRows();
    return row;
}

void ChatModel::appendText(int row, const QString &text)
{
    if (!find(row) || text.isEmpty())
        return;
    m_messages[row].text += text;
    m_pendingRow = row;
    if (!m_refresh.isActive())
        m_refresh.start();
}

void ChatModel::finish(int row, const QString &error, const QString &stats)
{
    if (!find(row))
        return;
    Message &m = m_messages[row];
    m.done = true;
    m.error = error;
    m.stats = stats;
    if (m_pendingRow == row)
        m_refresh.stop();
    refresh(row, {TextRole, ErrorRole, StatsRole, DoneRole, HasCodeRole});
}

void ChatModel::refresh(int row, const QList<int> &roles)
{
    if (!find(row))
        return;
    const QModelIndex i = index(row);
    emit dataChanged(i, i, roles);
}

void ChatModel::clear()
{
    m_refresh.stop();
    m_pendingRow = -1;
    beginResetModel();
    m_messages.clear();
    endResetModel();
}
