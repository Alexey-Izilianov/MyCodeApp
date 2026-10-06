#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include "src/gui/editerview.h"
#include "src/services/documentmanager.h"

class LspClient;

// Языковые серверы для открытых документов: какой сервер по расширению
// (assets/lsp.json + %LOCALAPPDATA%/appMyCodeApp/lsp.json), синхронизация
// текста (целиком, с задержкой; перед каждым запросом — немедленно),
// диагностика и запросы редактора. Позиции LSP — UTF-16, как QChar.
class LspManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(DocumentManager *documents READ documents WRITE setDocuments NOTIFY documentsChanged)
    Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath NOTIFY rootPathChanged)
    Q_PROPERTY(QVariantList problems READ problems NOTIFY problemsChanged)
    Q_PROPERTY(QVariantList references READ references NOTIFY referencesChanged)

public:
    explicit LspManager(QObject *parent = nullptr);
    ~LspManager() override;

    DocumentManager *documents() const { return m_documents; }
    void setDocuments(DocumentManager *documents);
    QString rootPath() const { return m_root; }
    void setRootPath(const QString &path);
    QVariantList problems() const { return m_problems; }
    QVariantList references() const { return m_references; }

    // "" — для файла нет сервера в настройках
    Q_INVOKABLE QString statusFor(const QString &path) const;
    // [{startLine, startColumn, endLine, endColumn, severity, message}]; severity 1 — ошибка
    Q_INVOKABLE QVariantList diagnosticsFor(const QString &path) const;

    Q_INVOKABLE void requestCompletion(EditorView *editor);
    // [{index, label, detail, kind}] — отфильтровано по уже набранному
    Q_INVOKABLE QVariantList completionItems(const QString &prefix) const;
    Q_INVOKABLE void requestDocumentation(int index);
    Q_INVOKABLE void acceptCompletion(EditorView *editor, int index);
    Q_INVOKABLE void cancelCompletion();

    Q_INVOKABLE void hover(EditorView *editor, int line, int column);
    Q_INVOKABLE void gotoDefinition(EditorView *editor, int line, int column);
    Q_INVOKABLE void findReferences(EditorView *editor);
    Q_INVOKABLE void rename(EditorView *editor, const QString &newName);
    Q_INVOKABLE void format(EditorView *editor);


signals:
    void documentsChanged();
    void rootPathChanged();
    void statusChanged();
    void problemsChanged();
    void diagnosticsChanged(const QString &path);
    void completionReady();
    void documentationReady(int index, const QString &markdown);
    void hoverReady(const QString &markdown, int line, int column);
    void navigate(const QString &path, int line, int column);
    void referencesChanged();
    void message(const QString &text);

private:
    struct ServerConfig {
        QString name;
        QStringList extensions;
        QStringList command;
        QStringList candidates;
        QJsonObject settings;
        QJsonObject initializationOptions;
        bool compileCommands = false;
    };
    struct Tracked {
        LspClient *client = nullptr;
        QString path;
        QString uri;
        QString languageId;
        int version = 1;
        bool changed = false;
    };
    struct Diagnostic {
        int startLine, startColumn, endLine, endColumn, severity;
        QString message;
    };
    struct FileDiagnostics {
        QString path;
        QVector<Diagnostic> items;
    };
    struct Completion {
        QPointer<core::Document> document;
        LspClient *client = nullptr;
        QJsonArray items;
        int requestId = 0;
    };

    void loadConfig();
    const ServerConfig *configFor(const QString &path) const;
    LspClient *clientFor(const ServerConfig &config, const QString &fallbackRoot);
    QString resolveProgram(const ServerConfig &config) const;
    void syncDocuments();
    void track(core::Document *doc);
    void sendOpen(core::Document *doc, Tracked &tracked);
    void flush(core::Document *doc);
    void flushAll();
    void resetClients();
    void setDiagnostics(const QString &uri, const QJsonArray &items);
    void rebuildProblems();

    // Документ редактора с живым сервером; текст уже отправлен
    Tracked *prepare(EditorView *editor);
    QJsonObject positionParams(const Tracked &tracked, int line, int column) const;
    void applyWorkspaceEdit(EditorView *editor, const QJsonObject &edit);
    core::Document *findDocument(const QString &path) const;

    QPointer<DocumentManager> m_documents;
    QString m_root;
    QVector<ServerConfig> m_configs;
    QHash<QString, LspClient *> m_clients; // имя сервера -> клиент
    QHash<QString, QString> m_progress;    // имя сервера -> фоновая работа
    QHash<core::Document *, Tracked> m_tracked;
    QTimer m_changeTimer;
    QHash<QString, FileDiagnostics> m_diagnostics; // ключ — путь без учёта регистра
    QVariantList m_problems;
    QVariantList m_references;
    Completion m_completion;
    int m_hoverRequest = 0;
};
