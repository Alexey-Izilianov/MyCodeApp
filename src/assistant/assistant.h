#pragma once

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>
#include <atomic>
#include <functional>
#include <memory>

#include "chatmodel.h"
#include "prompting.h"
#include "src/gui/editerview.h"
#include "src/services/documentmanager.h"

// Офлайн-ассистент: локальная модель через Ollama или OpenAI-совместимый сервер
// (llama.cpp server, vLLM…) на этой машине или в локальной сети. Задачи — объяснить код,
// написать документирующий комментарий, объяснить ошибку, пересказать журнал, предложить
// сообщение коммита; свободные вопросы — в том же разговоре. Ответ приходит потоком.
// Подсказки модели переводятся вместе с интерфейсом: язык ответа = язык интерфейса.
class AssistantService : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Assistant)

    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY settingsChanged) // "ollama", "openai"
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY settingsChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY settingsChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY settingsChanged)
    Q_PROPERTY(int contextSize READ contextSize WRITE setContextSize NOTIFY settingsChanged) // токенов
    Q_PROPERTY(DocumentManager *documents MEMBER m_documents NOTIFY documentsChanged)
    Q_PROPERTY(ChatModel *messages READ messages CONSTANT)
    // "" — свободен; "reading" — готовит текст; "waiting" — модель загружается и читает
    // запрос; "thinking" — рассуждает; "writing" — пишет ответ
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY phaseChanged)
    Q_PROPERTY(QString commitDraft READ commitDraft NOTIFY commitDraftChanged)
    Q_PROPERTY(bool commitBusy READ isCommitBusy NOTIFY commitBusyChanged)
    // Проверка сервера: "", "checking", "ok", "error"
    Q_PROPERTY(QString serverState READ serverState NOTIFY serverChanged)
    Q_PROPERTY(QString serverText READ serverText NOTIFY serverChanged)
    Q_PROPERTY(QStringList models READ models NOTIFY serverChanged) // модели на сервере
    // Сколько символов текста можно отдать модели в одном запросе
    Q_PROPERTY(int inputBudget READ inputBudget NOTIFY settingsChanged)

public:
    explicit AssistantService(QObject *parent = nullptr);
    ~AssistantService() override;

    QString provider() const { return m_provider; }
    void setProvider(const QString &provider);
    QString endpoint() const { return m_endpoint; }
    void setEndpoint(const QString &endpoint);
    QString model() const { return m_model; }
    void setModel(const QString &model);
    QString apiKey() const { return m_apiKey; }
    void setApiKey(const QString &key);
    int contextSize() const { return m_contextSize; }
    void setContextSize(int tokens);
    ChatModel *messages() { return &m_chat; }
    QString phase() const { return m_phase; }
    bool isBusy() const { return !m_phase.isEmpty(); }
    QString commitDraft() const { return m_commitDraft; }
    bool isCommitBusy() const { return m_commitReply != nullptr; }
    QString serverState() const { return m_serverState; }
    QString serverText() const { return m_serverText; }
    QStringList models() const { return m_models; }
    int inputBudget() const;
    // Сколько символов diff отдать для сообщения коммита
    Q_INVOKABLE int diffBudget() const;

    // Свободный вопрос; withSelection — приложить выделенный в editor код
    Q_INVOKABLE void ask(const QString &question, EditorView *editor, bool withSelection);
    // Объяснить выделенное (без выделения — файл, вокруг курсора, если не влезает целиком)
    Q_INVOKABLE void explainCode(EditorView *editor);
    // Документирующий комментарий к выделенному; вставка — insertComment
    Q_INVOKABLE void documentCode(EditorView *editor);
    Q_INVOKABLE void explainProblem(const QString &path, int line, const QString &message);
    // Пересказ журнала: выделенное или весь файл (выжимка готовится в фоне)
    Q_INVOKABLE void summarizeLog(EditorView *editor);
    Q_INVOKABLE void suggestCommitMessage(const QString &diff);
    Q_INVOKABLE void stopCommitMessage();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void checkServer();

    Q_INVOKABLE QString codeOf(int row) const; // первый блок кода ответа
    Q_INVOKABLE void copy(const QString &text) const;
    // Вставить комментарий из ответа row в editor (там должен быть открыт тот же файл)
    Q_INVOKABLE bool insertComment(int row, EditorView *editor);
    Q_INVOKABLE QString commentPath(int row) const;

signals:
    void settingsChanged();
    void documentsChanged();
    void phaseChanged();
    void commitDraftChanged();
    void commitBusyChanged();
    void serverChanged();
    void message(const QString &text); // короткое сообщение для строки состояния
    void taskStarted();                // показать панель ассистента

private:
    struct Handler {
        std::function<void(const assistant::Delta &)> delta;
        std::function<void(const QString &error, bool stopped)> finished; // error пустая — успех
    };
    QNetworkReply *post(const QJsonArray &messages, int maxTokens, Handler handler);
    bool isOllama() const { return m_provider != QLatin1String("openai"); }
    // Запрос к API выбранного сервера (путь у Ollama и у OpenAI-совместимого свой) с ключом доступа
    QNetworkRequest request(const QString &ollamaPath, const QString &openAiPath) const;
    QString describe(QNetworkReply *reply, const QByteArray &body) const;
    QString systemPrompt() const;
    // Добавить задачу в разговор и отправить всю историю, что помещается в контекст
    void send(ChatModel::Message request);
    QJsonArray history() const;
    void setPhase(const QString &phase);
    void setServer(const QString &state, const QString &text);
    // Строковая настройка: сохранить в settings.json под key и оповестить
    void assign(QString &field, const QString &value, const QString &key);
    int codeBudget() const { return inputBudget() * 3 / 4; } // на код в задаче, остальное — подсказка

    QString m_provider;
    QString m_endpoint;
    QString m_model;
    QString m_apiKey;
    int m_contextSize = 8192;
    QPointer<DocumentManager> m_documents;

    QNetworkAccessManager m_network;
    ChatModel m_chat;
    QPointer<QNetworkReply> m_reply;
    int m_replyRow = -1;
    QString m_phase;
    // Фоновая подготовка текста (выжимка журнала); false — отменена новой задачей или «Стоп»
    std::shared_ptr<std::atomic_bool> m_preparing;

    QPointer<QNetworkReply> m_commitReply;
    QString m_commitDraft;

    QPointer<QNetworkReply> m_checkReply;
    QString m_serverState;
    QString m_serverText;
    QStringList m_models;
};
