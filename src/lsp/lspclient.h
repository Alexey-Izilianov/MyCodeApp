#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <functional>

// Один языковой сервер: процесс, JSON-RPC 2.0 поверх stdio (заголовок
// Content-Length), рукопожатие initialize и перезапуск после падения.
// Запросы до готовности сервера копятся в очереди. После каждого
// (пере)запуска — сигнал ready: открытые документы надо переоткрыть.
class LspClient : public QObject {
    Q_OBJECT
public:
    enum class State { Starting, Ready, Restarting, Failed, NotFound };

    struct Config {
        QString name;
        QString program;
        QStringList arguments;
        QJsonObject initializationOptions;
        QJsonObject settings; // ответы на workspace/configuration по секциям
    };
    // error пустой — успех
    using Callback = std::function<void(const QJsonValue &result, const QJsonObject &error)>;

    LspClient(Config config, QString rootPath, QObject *parent = nullptr);
    ~LspClient() override;

    void start();
    State state() const { return m_state; }
    QString name() const { return m_config.name; }
    QString rootPath() const { return m_root; }
    const QJsonObject &capabilities() const { return m_capabilities; }
    bool supports(const QString &capability) const;

    int request(const QString &method, const QJsonObject &params, Callback callback);
    void cancel(int id);
    void notify(const QString &method, const QJsonValue &params);

signals:
    void stateChanged();
    void ready();
    void diagnostics(const QString &uri, const QJsonArray &items);
    void progress(const QString &text); // пустая строка — фоновая работа закончилась

private:
    void send(QJsonObject message);
    void readOutput();
    void handleMessage(const QJsonObject &message);
    void handleServerRequest(const QJsonObject &message);
    void handleProgress(const QJsonObject &params);
    void processFinished();
    void setState(State state);
    QJsonObject clientCapabilities() const;

    Config m_config;
    QString m_root;
    QProcess *m_process = nullptr;
    QByteArray m_input;
    State m_state = State::Starting;
    QJsonObject m_capabilities;
    int m_nextId = 1;
    QHash<int, Callback> m_pending;
    QVector<QJsonObject> m_queue; // до ответа на initialize
    QHash<QString, QString> m_progressTitles; // token -> заголовок
    QVector<qint64> m_crashTimes;
    QElapsedTimer m_clock;
    bool m_stopping = false;
};
