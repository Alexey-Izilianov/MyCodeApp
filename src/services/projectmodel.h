#pragma once

#include <QPointer>
#include <QSortFilterProxyModel>
#include <QtQmlIntegration/qqmlintegration.h>
#include <memory>

#include "ignorerules.h"
#include "src/git/gitservice.h"

class QFileSystemModel;

// Дерево файлов проекта для TreeView: QFileSystemModel (асинхронная загрузка
// папок, сам следит за изменениями на диске) без игнорируемого .gitignore,
// папки выше файлов.
class ProjectModel : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath NOTIFY rootPathChanged)
    Q_PROPERTY(QModelIndex rootIndex READ rootIndex NOTIFY rootPathChanged)
    Q_PROPERTY(GitService *git READ git WRITE setGit NOTIFY gitChanged)

public:
    enum Role { PathRole = Qt::UserRole + 100, IsDirRole, GitStatusRole };

    explicit ProjectModel(QObject *parent = nullptr);
    ~ProjectModel() override;

    QString rootPath() const { return m_rules->root(); }
    void setRootPath(const QString &path);
    QModelIndex rootIndex() const;
    GitService *git() const { return m_git; }
    void setGit(GitService *git);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Индекс файла в дереве (для подсветки текущего), невалидный — если папки не загружены
    Q_INVOKABLE QModelIndex indexForPath(const QString &path) const;

signals:
    void rootPathChanged();
    void gitChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    void decorationsChanged(const QStringList &paths);

    QFileSystemModel *m_files = nullptr;
    QPointer<GitService> m_git;
    std::unique_ptr<IgnoreRules> m_rules = std::make_unique<IgnoreRules>();
};
