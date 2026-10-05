#include "lspclient.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QTimer>
#include <QUrl>
#include <spdlog/spdlog.h>

namespace {
constexpr int kMaxCrashes = 3;            // столько падений подряд — и сервер больше не поднимаем
constexpr qint64 kCrashWindowMs = 180000; // «подряд» — в пределах трёх минут
constexpr int kMethodNotFound = -32601;

QJsonObject errorObject(const QString &message)
{
    return {{QStringLiteral("message"), message}};
}
} // namespace

LspClient::LspClient(Config config, QString rootPath, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_root(std::move(rootPath))
{
    m_clock.start();
}

LspClient::~LspClient()
{
    m_stopping = true;
    m_pending.clear(); // владелец разрушается — ответы ему не нужны
    if (!m_process)
        return;
    // Иначе waitForFinished синхронно вызовет processFinished и обнулит m_process
    disconnect(m_process, nullptr, this, nullptr);
    if (m_state == State::Ready) {
        send({{QStringLiteral("id"), m_nextId++}, {QStringLiteral("method"), QStringLiteral("shutdown")}});
        send({{QStringLiteral("method"), QStringLiteral("exit")}});
        m_process->waitForFinished(300);
    }
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(300);
    }
}

void LspClient::setState(State state)
{
    if (state != m_state) {
        m_state = state;
        emit stateChanged();
    }
}

bool LspClient::supports(const QString &capability) const
{
    const QJsonValue value = m_capabilities.value(capability);
    return value.isObject() || value.toBool();
}

void LspClient::start()
{
    setState(m_crashTimes.isEmpty() ? State::Starting : State::Restarting);
    m_input.clear();
    m_capabilities = {};

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_root);
    m_process->setStandardErrorFile(QProcess::nullDevice()); // журналы серверов огромны; непрочитанный stderr остановил бы сервер
    connect(m_process, &QProcess::readyReadStandardOutput, this, &LspClient::readOutput);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            spdlog::warn("lsp: {} не запускается: {}", m_config.name.toStdString(), m_config.program.toStdString());
            m_process->deleteLater();
            m_process = nullptr;
            setState(State::NotFound);
        }
    });
    connect(m_process, &QProcess::finished, this, &LspClient::processFinished);
    connect(m_process, &QProcess::started, this, [this] {
        const QString rootUri = QUrl::fromLocalFile(m_root).toString();
        const QJsonObject params{
            {QStringLiteral("processId"), int(QCoreApplication::applicationPid())},
            {QStringLiteral("rootUri"), rootUri},
            {QStringLiteral("workspaceFolders"), QJsonArray{QJsonObject{
                {QStringLiteral("uri"), rootUri}, {QStringLiteral("name"), QDir(m_root).dirName()}}}},
            {QStringLiteral("capabilities"), clientCapabilities()},
            {QStringLiteral("initializationOptions"), m_config.initializationOptions},
        };
        const int id = m_nextId++;
        m_pending.insert(id, [this](const QJsonValue &result, const QJsonObject &error) {
            if (!error.isEmpty()) {
                spdlog::warn("lsp: {} initialize: {}", m_config.name.toStdString(),
                             error.value(QStringLiteral("message")).toString().toStdString());
                return; // процесс, скорее всего, сам завершится — дальше решает processFinished
            }
            m_capabilities = result.toObject().value(QStringLiteral("capabilities")).toObject();
            send({{QStringLiteral("method"), QStringLiteral("initialized")}, {QStringLiteral("params"), QJsonObject()}});
            if (!m_config.settings.isEmpty())
                send({{QStringLiteral("method"), QStringLiteral("workspace/didChangeConfiguration")},
                      {QStringLiteral("params"), QJsonObject{{QStringLiteral("settings"), m_config.settings}}}});
            setState(State::Ready);
            for (const QJsonObject &message : std::exchange(m_queue, {}))
                send(message);
            emit ready();
        });
        send({{QStringLiteral("id"), id}, {QStringLiteral("method"), QStringLiteral("initialize")},
              {QStringLiteral("params"), params}});
    });
    m_process->start(m_config.program, m_config.arguments);
}

int LspClient::request(const QString &method, const QJsonObject &params, Callback callback)
{
    if (m_state == State::NotFound || m_state == State::Failed) {
        callback({}, errorObject(QStringLiteral("сервер недоступен")));
        return 0;
    }
    const int id = m_nextId++;
    m_pending.insert(id, std::move(callback));
    const QJsonObject message{{QStringLiteral("id"), id}, {QStringLiteral("method"), method},
                              {QStringLiteral("params"), params}};
    if (m_state == State::Ready)
        send(message);
    else
        m_queue.append(message);
    return id;
}

void LspClient::cancel(int id)
{
    if (m_pending.remove(id) && m_state == State::Ready)
        notify(QStringLiteral("$/cancelRequest"), QJsonObject{{QStringLiteral("id"), id}});
}

void LspClient::notify(const QString &method, const QJsonValue &params)
{
    const QJsonObject message{{QStringLiteral("method"), method}, {QStringLiteral("params"), params}};
    if (m_state == State::Ready)
        send(message);
    else if (m_state == State::Starting || m_state == State::Restarting)
        m_queue.append(message);
}

void LspClient::send(QJsonObject message)
{
    if (!m_process)
        return;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    m_process->write("Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
}

void LspClient::readOutput()
{
    m_input += m_process->readAllStandardOutput();
    while (true) {
        const qsizetype headerEnd = m_input.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        qsizetype length = -1;
        for (const QByteArray &header : m_input.left(headerEnd).split('\n')) {
            const QByteArray line = header.trimmed().toLower();
            if (line.startsWith("content-length:"))
                length = line.mid(15).trimmed().toLongLong();
        }
        if (length < 0) { // мусор вместо заголовка — пропускаем до следующего
            m_input.remove(0, headerEnd + 4);
            continue;
        }
        if (m_input.size() < headerEnd + 4 + length)
            return;
        const QByteArray body = m_input.mid(headerEnd + 4, length);
        m_input.remove(0, headerEnd + 4 + length);
        handleMessage(QJsonDocument::fromJson(body).object());
    }
}

void LspClient::handleMessage(const QJsonObject &message)
{
    const QString method = message.value(QStringLiteral("method")).toString();
    const bool hasId = message.contains(QStringLiteral("id"));
    if (method.isEmpty() && hasId) {
        if (Callback callback = m_pending.take(message.value(QStringLiteral("id")).toInt()))
            callback(message.value(QStringLiteral("result")), message.value(QStringLiteral("error")).toObject());
        return;
    }
    if (hasId) {
        handleServerRequest(message);
        return;
    }
    const QJsonObject params = message.value(QStringLiteral("params")).toObject();
    if (method == QLatin1String("textDocument/publishDiagnostics"))
        emit diagnostics(params.value(QStringLiteral("uri")).toString(),
                         params.value(QStringLiteral("diagnostics")).toArray());
    else if (method == QLatin1String("$/progress"))
        handleProgress(params);
    else if (method == QLatin1String("window/showMessage") || method == QLatin1String("window/logMessage"))
        spdlog::info("lsp: {}: {}", m_config.name.toStdString(),
                     params.value(QStringLiteral("message")).toString().toStdString());
}

void LspClient::handleServerRequest(const QJsonObject &message)
{
    const QString method = message.value(QStringLiteral("method")).toString();
    QJsonObject reply{{QStringLiteral("id"), message.value(QStringLiteral("id"))}};
    if (method == QLatin1String("workspace/configuration")) {
        QJsonArray result;
        const QJsonArray items = message.value(QStringLiteral("params")).toObject()
                                     .value(QStringLiteral("items")).toArray();
        for (const QJsonValue &item : items)
            result.append(m_config.settings.value(item.toObject().value(QStringLiteral("section")).toString()));
        reply.insert(QStringLiteral("result"), result);
    } else if (method == QLatin1String("workspace/workspaceFolders")) {
        reply.insert(QStringLiteral("result"), QJsonArray{QJsonObject{
            {QStringLiteral("uri"), QUrl::fromLocalFile(m_root).toString()},
            {QStringLiteral("name"), QDir(m_root).dirName()}}});
    } else if (method == QLatin1String("window/workDoneProgress/create")
               || method.startsWith(QLatin1String("client/"))) {
        reply.insert(QStringLiteral("result"), QJsonValue::Null);
    } else {
        reply.insert(QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), kMethodNotFound},
                                                          {QStringLiteral("message"), method}});
    }
    send(reply);
}

void LspClient::handleProgress(const QJsonObject &params)
{
    const QString token = params.value(QStringLiteral("token")).toVariant().toString();
    const QJsonObject value = params.value(QStringLiteral("value")).toObject();
    const QString kind = value.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("end")) {
        m_progressTitles.remove(token);
        emit progress(QString());
        return;
    }
    if (kind == QLatin1String("begin"))
        m_progressTitles.insert(token, value.value(QStringLiteral("title")).toString());
    QString text = m_progressTitles.value(token);
    const QString detail = value.value(QStringLiteral("message")).toString();
    if (!detail.isEmpty())
        text += QStringLiteral(": ") + detail;
    if (value.contains(QStringLiteral("percentage")))
        text += QStringLiteral(" %1%").arg(value.value(QStringLiteral("percentage")).toInt());
    emit progress(text);
}

void LspClient::processFinished()
{
    const auto pending = std::exchange(m_pending, {}); // колбэки могут слать новые запросы
    for (const Callback &callback : pending)
        callback({}, errorObject(QStringLiteral("сервер завершился")));
    m_queue.clear();
    m_progressTitles.clear();
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (m_stopping)
        return;

    const qint64 now = m_clock.elapsed();
    m_crashTimes.append(now);
    m_crashTimes.removeIf([now](qint64 t) { return now - t > kCrashWindowMs; });
    spdlog::warn("lsp: {} завершился, падений подряд: {}", m_config.name.toStdString(), m_crashTimes.size());
    if (m_crashTimes.size() > kMaxCrashes) {
        setState(State::Failed);
        return;
    }
    setState(State::Restarting);
    QTimer::singleShot(1000, this, &LspClient::start);
}

QJsonObject LspClient::clientCapabilities() const
{
    const QJsonArray markup{QStringLiteral("markdown"), QStringLiteral("plaintext")};
    const QJsonObject textDocument{
        {QStringLiteral("synchronization"), QJsonObject{{QStringLiteral("didSave"), false}}},
        {QStringLiteral("completion"), QJsonObject{
            {QStringLiteral("completionItem"), QJsonObject{
                {QStringLiteral("snippetSupport"), true},
                {QStringLiteral("documentationFormat"), markup},
                {QStringLiteral("resolveSupport"), QJsonObject{{QStringLiteral("properties"),
                    QJsonArray{QStringLiteral("documentation"), QStringLiteral("detail")}}}}}},
            {QStringLiteral("contextSupport"), true}}},
        {QStringLiteral("hover"), QJsonObject{{QStringLiteral("contentFormat"), markup}}},
        {QStringLiteral("definition"), QJsonObject{{QStringLiteral("linkSupport"), true}}},
        {QStringLiteral("references"), QJsonObject()},
        {QStringLiteral("rename"), QJsonObject()},
        {QStringLiteral("formatting"), QJsonObject()},
        {QStringLiteral("publishDiagnostics"), QJsonObject{{QStringLiteral("relatedInformation"), false}}},
    };
    return {
        {QStringLiteral("textDocument"), textDocument},
        {QStringLiteral("workspace"), QJsonObject{{QStringLiteral("configuration"), true},
                                                  {QStringLiteral("workspaceFolders"), true}}},
        {QStringLiteral("window"), QJsonObject{{QStringLiteral("workDoneProgress"), true}}},
        {QStringLiteral("general"), QJsonObject{{QStringLiteral("positionEncodings"),
                                                 QJsonArray{QStringLiteral("utf-16")}}}},
    };
}
