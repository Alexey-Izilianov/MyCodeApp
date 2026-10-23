#pragma once

#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

#include "vcs.h"

// История для вкладки «История»: коммиты и раскладка графа. Заполняет GitService
class HistoryModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("История даёт GitService.history")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int maxLanes READ maxLanes NOTIFY countChanged) // ширина колонки графа

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        ShortIdRole,
        SummaryRole,
        MessageRole,
        AuthorRole,
        EmailRole,
        DateRole,     // строка для колонки
        RefsRole,     // [{name, kind: "branch"|"remote"|"tag"|"bookmark", current}]
        GraphRole,    // {lane, color, width, edges: [from, to, color, kind, ...]}
        BranchRole,   // Mercurial: именованная ветка
    };

    explicit HistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int maxLanes() const { return m_maxLanes; }

    void reset(QVector<vcs::Commit> commits, QVector<vcs::GraphRow> rows);
    // Номер ряда первого коммита, у которого сообщение, автор или хэш содержат text, начиная с from
    Q_INVOKABLE int find(const QString &text, int from) const;
    // Первый родитель коммита; пусто — корневой коммит или коммита нет в истории
    QString parentOf(const QString &id) const;

signals:
    void countChanged();

private:
    QVector<vcs::Commit> m_commits;
    QVector<vcs::GraphRow> m_rows;
    int m_maxLanes = 1;
};
