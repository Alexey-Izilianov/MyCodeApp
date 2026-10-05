#include "projectmodel.h"

#include <QFileSystemModel>

ProjectModel::ProjectModel(QObject *parent)
    : QSortFilterProxyModel(parent)
    , m_files(new QFileSystemModel(this))
{
    m_files->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    m_files->setReadOnly(true);
    setSourceModel(m_files);
    setDynamicSortFilter(true);
    setRecursiveFilteringEnabled(false);
    sort(0);
}

ProjectModel::~ProjectModel() = default;

void ProjectModel::setRootPath(const QString &path)
{
    if (path == m_rules->root())
        return;
    beginFilterChange();
    m_rules = std::make_unique<IgnoreRules>(path);
    m_files->setRootPath(path); // без корня QFileSystemModel не следит за изменениями
    endFilterChange(Direction::Rows);
    emit rootPathChanged();
}

QModelIndex ProjectModel::rootIndex() const
{
    return m_rules->root().isEmpty() ? QModelIndex() : mapFromSource(m_files->index(m_rules->root()));
}

QModelIndex ProjectModel::indexForPath(const QString &path) const
{
    return mapFromSource(m_files->index(path));
}

QVariant ProjectModel::data(const QModelIndex &index, int role) const
{
    switch (role) {
    case PathRole:
        return m_files->filePath(mapToSource(index));
    case IsDirRole:
        return m_files->isDir(mapToSource(index));
    case GitStatusRole:
        return QString(); // заполнится в M5 (libgit2)
    default:
        return QSortFilterProxyModel::data(index, role);
    }
}

QHash<int, QByteArray> ProjectModel::roleNames() const
{
    QHash<int, QByteArray> roles = QSortFilterProxyModel::roleNames();
    roles.insert(PathRole, "path");
    roles.insert(IsDirRole, "isDir");
    roles.insert(GitStatusRole, "gitStatus");
    return roles;
}

bool ProjectModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (m_rules->root().isEmpty())
        return true;
    const QModelIndex index = m_files->index(sourceRow, 0, sourceParent);
    const QString path = m_files->filePath(index);
    // Предки корня (диск, папки над проектом) нужны, чтобы до корня дойти
    if (!path.startsWith(m_rules->root()) || path.size() == m_rules->root().size())
        return true;
    return !m_rules->isIgnored(path, m_files->isDir(index));
}

bool ProjectModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const bool leftDir = m_files->isDir(left), rightDir = m_files->isDir(right);
    if (leftDir != rightDir)
        return leftDir;
    return m_files->fileName(left).compare(m_files->fileName(right), Qt::CaseInsensitive) < 0;
}
