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
#include "historymodel.h"
#include "src/gui/editerview.h"
#include "src/services/documentmanager.h"

// Контроль версий папки проекта (Git или Mercurial): статусы файлов (дерево, панель),
// ветки, операции индекса и коммит, полоски изменений в редакторе, просмотр изменений
// и история. Репозиторий работает в одном фоновом потоке; результаты приходят в GUI-поток.
class GitService : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath NOTIFY rootPathChanged)
    Q_PROPERTY(DocumentManager *documents MEMBER m_documents NOTIFY documentsChanged)
    Q_PROPERTY(bool available READ isAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY stateChanged)       // "Git", "Mercurial"
    Q_PROPERTY(bool hasIndex READ hasIndex NOTIFY stateChanged) // false — «индекс» эмулируется
    Q_PROPERTY(QString branch READ branch NOTIFY stateChanged)
    Q_PROPERTY(QStringList branches READ branches NOTIFY stateChanged)
    // [{path, relative, name, folder, status}] — изменения рабочей копии и индекса
    Q_PROPERTY(QVariantList changes READ changes NOTIFY stateChanged)
    Q_PROPERTY(QVariantList stagedChanges READ stagedChanges NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(HistoryModel *history READ history CONSTANT)
    Q_PROPERTY(bool historyLoading READ isHistoryLoading NOTIFY historyLoadingChanged)

public:
    explicit GitService(QObject *parent = nullptr);
    ~GitService() override;

    QString rootPath() const { return m_root; }
    void setRootPath(const QString &path);
    bool isAvailable() const { return !m_workdir.isEmpty(); }
    QString kind() const { return m_kind; }
    bool hasIndex() const { return m_hasIndex; }
    QString branch() const { return m_branch; }
    QStringList branches() const { return m_branches; }
    QVariantList changes() const { return m_changes; }
    QVariantList stagedChanges() const { return m_staged; }
    bool isBusy() const { return m_running > 0; }
    HistoryModel *history() { return &m_history; }
    bool isHistoryLoading() const { return m_historyLoading; }

    // Буква для дерева файлов: M, A, U (новый), D, R, C (конфликт); у папки — по детям
    QString decorationOf(const QString &path, bool isDir) const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void stage(const QStringList &paths);
    Q_INVOKABLE void unstage(const QStringList &paths);
    Q_INVOKABLE void stageAll();
    Q_INVOKABLE void unstageAll();
    Q_INVOKABLE void discard(const QStringList &paths);
    Q_INVOKABLE void commit(const QString &message);
    // Изменения будущего коммита (отмеченные, а без них — все) в формате diff не длиннее
    // budget символов — для сообщения от ассистента; ответ — commitDiffReady
    Q_INVOKABLE void requestCommitDiff(int budget);
    Q_INVOKABLE void checkout(const QString &branch);
    Q_INVOKABLE void createBranch(const QString &name);
    Q_INVOKABLE void initRepository();

    // Пересчитать полоски изменений открытого в редакторе файла
    Q_INVOKABLE void updateMarks(EditorView *editor);
    // Показать изменения файла в target: staged — индекс против HEAD,
    // иначе текущий текст (открытая вкладка или диск) против индекса
    Q_INVOKABLE void showDiff(const QString &path, bool staged, EditorView *target);

    // История: загрузить (и дальше перечитывать при коммитах и смене ветки)
    Q_INVOKABLE void loadHistory();
    Q_INVOKABLE void loadCommitFiles(const QString &id); // ответ — commitFilesReady
    // Изменения файла в коммите: против первого родителя
    Q_INVOKABLE void showCommitDiff(const QString &id, const QString &relativePath, EditorView *target);
    Q_INVOKABLE void checkoutCommit(const QString &id);
    Q_INVOKABLE void createBranchAt(const QString &name, const QString &id);

signals:
    void rootPathChanged();
    void documentsChanged();
    void stateChanged();
    void busyChanged();
    void historyLoadingChanged();
    void decorationsChanged(const QStringList &paths); // для дерева: чьи буквы поменялись
    void message(const QString &text);
    void committed();
    void diffReady(const QString &title, int changeCount);
    void commitFilesReady(const QString &id, const QVariantList &files); // как changes
    void commitDiffReady(const QString &diff);

private:
    struct Snapshot; // результат обновления статуса

    using Job = std::function<void(vcs::Repository &)>;
    // Задача в фоновом потоке над репозиторием проекта; done — в GUI-потоке
    void run(Job job, std::function<void()> done = {});
    // Операция, после которой статус надо перечитать; ошибка — в message.
    // changesHistory — после успеха перечитать и историю
    void runOperation(std::function<bool(vcs::Repository &, QString *)> operation,
                      std::function<void()> onSuccess = {}, bool changesHistory = false);
    // Старый и новый текст для просмотра изменений (в фоновом потоке)
    using DiffTexts = std::function<std::pair<QString, QString>(vcs::Repository &)>;
    void runDiff(DiffTexts texts, const QString &path, const QString &title, EditorView *target);
    void applySnapshot(const Snapshot &snapshot);
    void watch(const QStringList &paths);
    QVariantMap fileEntry(const QString &relativePath) const;
    QString relative(const QString &path) const;
    QStringList relatives(const QStringList &paths) const;
    core::Document *openDocument(const QString &path) const;

    git::Library m_library;
    QThreadPool m_pool;
    QString m_root;
    QString m_workdir;  // корень рабочей копии ('/' в конце), пусто — не репозиторий
    QString m_kind;
    bool m_hasIndex = true;
    QString m_branch;
    QStringList m_branches;
    QVariantList m_changes;
    QVariantList m_staged;
    QHash<QString, QChar> m_fileMarks; // ключ — путь в нижнем регистре
    QHash<QString, QChar> m_dirMarks;
    QStringList m_decorated;           // пути с буквой — как их видит дерево
    QPointer<DocumentManager> m_documents;
    QFileSystemWatcher m_watcher;      // служебные файлы репозитория — правки извне
    QTimer m_refreshDelay;
    QTimer m_poll;                     // рабочая копия без слежения: опрос
    qint64 m_lastRefreshMs = 0;
    int m_generation = 0;              // смена папки отменяет устаревшие результаты
    int m_running = 0;
    bool m_refreshRunning = false;
    bool m_refreshQueued = false;
    int m_marksRequest = 0;
    int m_diffRequest = 0;
    QPointer<core::Document> m_diffDocument;

    HistoryModel m_history;
    bool m_historyActive = false; // история открыта — держать актуальной
    bool m_historyStale = false;
    bool m_historyLoading = false;
    int m_historyRequest = 0;
    int m_filesRequest = 0;
};
