#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>
#include <functional>
#include <memory>

#include "gitrepo.h"
#include "src/gui/editerview.h"
#include "src/services/documentmanager.h"

// Git для папки проекта: статусы файлов (дерево, панель), ветки, операции
// индекса и коммит, полоски изменений в редакторе и просмотр изменений файла.
// libgit2 работает в одном фоновом потоке; результаты приходят в GUI-поток.
class GitService : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath NOTIFY rootPathChanged)
    Q_PROPERTY(DocumentManager *documents MEMBER m_documents NOTIFY documentsChanged)
    Q_PROPERTY(bool available READ isAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString branch READ branch NOTIFY stateChanged)
    Q_PROPERTY(QStringList branches READ branches NOTIFY stateChanged)
    // [{path, relative, name, folder, status}] — изменения рабочей копии и индекса
    Q_PROPERTY(QVariantList changes READ changes NOTIFY stateChanged)
    Q_PROPERTY(QVariantList stagedChanges READ stagedChanges NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)

public:
    explicit GitService(QObject *parent = nullptr);
    ~GitService() override;

    QString rootPath() const { return m_root; }
    void setRootPath(const QString &path);
    bool isAvailable() const { return !m_workdir.isEmpty(); }
    QString branch() const { return m_branch; }
    QStringList branches() const { return m_branches; }
    QVariantList changes() const { return m_changes; }
    QVariantList stagedChanges() const { return m_staged; }
    bool isBusy() const { return m_running > 0; }

    // Буква для дерева файлов: M, A, U (новый), D, R, C (конфликт); у папки — по детям
    QString decorationOf(const QString &path, bool isDir) const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void stage(const QStringList &paths);
    Q_INVOKABLE void unstage(const QStringList &paths);
    Q_INVOKABLE void stageAll();
    Q_INVOKABLE void unstageAll();
    Q_INVOKABLE void discard(const QStringList &paths);
    Q_INVOKABLE void commit(const QString &message);
    Q_INVOKABLE void checkout(const QString &branch);
    Q_INVOKABLE void createBranch(const QString &name);
    Q_INVOKABLE void initRepository();

    // Пересчитать полоски изменений открытого в редакторе файла
    Q_INVOKABLE void updateMarks(EditorView *editor);
    // Показать изменения файла в target: staged — индекс против HEAD,
    // иначе текущий текст (открытая вкладка или диск) против индекса
    Q_INVOKABLE void showDiff(const QString &path, bool staged, EditorView *target);

signals:
    void rootPathChanged();
    void documentsChanged();
    void stateChanged();
    void busyChanged();
    void decorationsChanged(const QStringList &paths); // для дерева: чьи буквы поменялись
    void message(const QString &text);
    void committed();
    void diffReady(const QString &title, int changeCount);

private:
    struct Snapshot; // результат обновления статуса

    using Job = std::function<void(git::Repository &)>;
    // Задача в фоновом потоке над репозиторием проекта; done — в GUI-потоке
    void run(Job job, std::function<void()> done = {});
    // Операция, после которой статус надо перечитать; ошибка — в message
    void runOperation(std::function<bool(git::Repository &, QString *)> operation,
                      std::function<void()> onSuccess = {});
    void applySnapshot(const Snapshot &snapshot);
    void watchGitDir(const QString &gitDir);
    QString relative(const QString &path) const;
    core::Document *openDocument(const QString &path) const;

    git::Library m_library;
    QThreadPool m_pool;
    QString m_root;
    QString m_workdir;  // корень рабочей копии ('/' в конце), пусто — не репозиторий
    QString m_branch;
    QStringList m_branches;
    QVariantList m_changes;
    QVariantList m_staged;
    QHash<QString, QChar> m_fileMarks; // ключ — путь в нижнем регистре
    QHash<QString, QChar> m_dirMarks;
    QStringList m_decorated;           // пути с буквой — как их видит дерево
    QPointer<DocumentManager> m_documents;
    QFileSystemWatcher m_gitWatcher; // .git/index, HEAD, refs — правки извне
    QTimer m_refreshDelay;
    QTimer m_poll;                    // рабочая копия без слежения: опрос
    qint64 m_lastRefreshMs = 0;
    int m_generation = 0;             // смена папки отменяет устаревшие результаты
    int m_running = 0;
    bool m_refreshRunning = false;
    bool m_refreshQueued = false;
    int m_marksRequest = 0;
    int m_diffRequest = 0;
    QPointer<core::Document> m_diffDocument;
};
