#include "assistant.h"

#include <QClipboard>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QThreadPool>

#include "src/core/document.h"
#include "src/services/appsettings.h"

using assistant::Delta;

namespace {

const QString kOllamaEndpoint = QStringLiteral("http://127.0.0.1:11434");
const QString kOpenAiEndpoint = QStringLiteral("http://127.0.0.1:8080"); // llama.cpp server
const QString kDefaultModel = QStringLiteral("qwen3:4b-instruct");
const QString kProviderKey = QStringLiteral("assistant.provider");
const QString kEndpointKey = QStringLiteral("assistant.endpoint");
const QString kModelKey = QStringLiteral("assistant.model");
const QString kApiKeyKey = QStringLiteral("assistant.apiKey");
const QString kContextKey = QStringLiteral("assistant.contextSize");
constexpr int kAnswerTokens = 1024;
constexpr int kCommitTokens = 256;
constexpr int kMaxCommentLines = 400;
constexpr qint64 kMaxProblemFileBytes = 16 * 1024 * 1024;
// На процессоре модель читает запрос ~50 токенов/с: без явного выделения и в выжимках
// берём меньше текста, чтобы первый ответ приходил за полминуты-минуту
constexpr int kAroundCursorChars = 6000;
constexpr int kLogChars = 9000;
constexpr int kDiffChars = 8000;

QString defaultEndpoint(const QString &provider)
{
    return provider == QLatin1String("openai") ? kOpenAiEndpoint : kOllamaEndpoint;
}

// Блок кода в запросе; язык — тот же, что видит пользователь в строке состояния
QString fence(const QString &code, const QString &path)
{
    QString language = EditorView::languageOf(path).toLower();
    language.replace(QLatin1String("++"), QLatin1String("pp"));
    return QStringLiteral("```%1\n%2\n```").arg(language, code);
}

assistant::Excerpt excerptOf(const core::TextBuffer &buffer, int from, int to, int center, int budget)
{
    return assistant::excerpt([&](int i) { return buffer.lineAt(i); }, from, to, center, budget);
}

// «main.cpp, строки 10–24»; строки — с 0
QString where(const QString &path, int first, int last)
{
    return AssistantService::tr("%1, строки %2–%3").arg(QFileInfo(path).fileName()).arg(first + 1).arg(last + 1);
}

// Время ответа: от отправки, первого слова и до конца
struct Timing {
    QElapsedTimer clock;
    qint64 firstTokenMs = -1;
    int outputTokens = -1;
};

} // namespace

AssistantService::AssistantService(QObject *parent)
    : QObject(parent)
    , m_chat(this)
{
    m_provider = appSetting(kProviderKey, QStringLiteral("ollama")).toString();
    m_endpoint = appSetting(kEndpointKey, defaultEndpoint(m_provider)).toString();
    m_model = appSetting(kModelKey, kDefaultModel).toString();
    m_apiKey = appSetting(kApiKeyKey).toString();
    m_contextSize = appSetting(kContextKey, 8192).toInt();
}

AssistantService::~AssistantService()
{
    if (m_preparing)
        *m_preparing = false;
    // Обрыв соединения останавливает генерацию и на сервере
    for (QNetworkReply *reply : {m_reply.data(), m_commitReply.data(), m_checkReply.data()})
        if (reply) {
            reply->disconnect(this);
            reply->abort();
        }
}

void AssistantService::assign(QString &field, const QString &value, const QString &key)
{
    const QString trimmed = value.trimmed();
    if (trimmed == field)
        return;
    field = trimmed;
    setAppSetting(key, field);
    emit settingsChanged();
}

void AssistantService::setProvider(const QString &provider)
{
    if (provider == m_provider)
        return;
    // Адрес по умолчанию у каждого сервера свой
    if (m_endpoint == defaultEndpoint(m_provider)) {
        m_endpoint = defaultEndpoint(provider);
        setAppSetting(kEndpointKey, m_endpoint);
    }
    assign(m_provider, provider, kProviderKey);
}

void AssistantService::setEndpoint(const QString &endpoint)
{
    assign(m_endpoint, endpoint, kEndpointKey);
}

void AssistantService::setModel(const QString &model)
{
    assign(m_model, model, kModelKey);
}

void AssistantService::setApiKey(const QString &apiKey)
{
    assign(m_apiKey, apiKey, kApiKeyKey);
}

void AssistantService::setContextSize(int tokens)
{
    tokens = qBound(2048, tokens, 131072);
    if (tokens == m_contextSize)
        return;
    m_contextSize = tokens;
    setAppSetting(kContextKey, m_contextSize);
    emit settingsChanged();
}

int AssistantService::inputBudget() const
{
    return qMax(2000, (m_contextSize - kAnswerTokens - 300) * assistant::kCharsPerToken);
}

int AssistantService::diffBudget() const
{
    return qMin(kDiffChars, inputBudget() * 85 / 100);
}

void AssistantService::setPhase(const QString &phase)
{
    if (phase == m_phase)
        return;
    m_phase = phase;
    emit phaseChanged();
}

void AssistantService::setServer(const QString &state, const QString &text)
{
    m_serverState = state;
    m_serverText = text;
    emit serverChanged();
}

QNetworkRequest AssistantService::request(const QString &ollamaPath, const QString &openAiPath) const
{
    QString base = m_endpoint.trimmed();
    if (!base.contains(QLatin1String("://")))
        base.prepend(QLatin1String("http://"));
    while (base.endsWith(u'/'))
        base.chop(1);
    if (!isOllama() && base.endsWith(QLatin1String("/v1")))
        base.chop(3); // адрес часто копируют вместе с /v1
    QNetworkRequest request(QUrl(base + (isOllama() ? ollamaPath : openAiPath)));
    if (!isOllama() && !m_apiKey.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
    return request;
}

QString AssistantService::describe(QNetworkReply *reply, const QByteArray &body) const
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ollama = isOllama();
    if (status >= 400) {
        const QString text = assistant::errorFromBody(body);
        if (status == 404 && ollama && text.contains(QLatin1String("not found")))
            return tr("Модели «%1» нет на сервере. Скачайте её командой: ollama pull %1").arg(m_model);
        if (status == 401 || status == 403)
            return tr("Сервер отклонил ключ доступа (HTTP %1)").arg(status);
        return tr("Сервер модели ответил ошибкой (HTTP %1): %2").arg(status).arg(text);
    }
    switch (reply->error()) {
    case QNetworkReply::ConnectionRefusedError:
        return ollama ? tr("Ollama не отвечает по адресу %1. Запустите её или проверьте адрес в настройках.").arg(m_endpoint)
                      : tr("Сервер модели не отвечает по адресу %1").arg(m_endpoint);
    case QNetworkReply::HostNotFoundError:
        return tr("Адрес сервера модели не найден: %1").arg(m_endpoint);
    case QNetworkReply::OperationCanceledError:
        return tr("Сервер модели не ответил вовремя");
    default:
        return tr("Нет связи с сервером модели: %1").arg(reply->errorString());
    }
}

QString AssistantService::systemPrompt() const
{
    return tr("Ты — ассистент программиста, встроенный в редактор кода MyCodeApp, и работаешь без интернета. "
              "Отвечай по-русски, кратко и по делу: обычно не больше 10–15 строк, если не просят подробнее; "
              "без вступлений и без эмодзи. Код оформляй блоками ``` "
              "с названием языка. Если по фрагменту чего-то не понять — так и скажи, не выдумывай.");
}

QNetworkReply *AssistantService::post(const QJsonArray &messages, int maxTokens, Handler handler)
{
    const bool ollama = isOllama();
    QJsonObject body{{"model", m_model}, {"messages", messages}, {"stream", true}};
    if (ollama) {
        body.insert(QStringLiteral("keep_alive"), QStringLiteral("15m")); // повторный вопрос — без загрузки модели
        body.insert(QStringLiteral("options"),
                    QJsonObject{{"num_ctx", m_contextSize}, {"num_predict", maxTokens}, {"temperature", 0.2}});
    } else {
        body.insert(QStringLiteral("max_tokens"), maxTokens);
        body.insert(QStringLiteral("temperature"), 0.2);
        body.insert(QStringLiteral("stream_options"), QJsonObject{{"include_usage", true}});
    }
    QNetworkRequest chat = request(QStringLiteral("/api/chat"), QStringLiteral("/v1/chat/completions"));
    chat.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_network.post(chat, QJsonDocument(body).toJson(QJsonDocument::Compact));

    auto parser = std::make_shared<assistant::StreamParser>(ollama ? assistant::Protocol::Ollama
                                                                   : assistant::Protocol::OpenAi);
    auto failure = std::make_shared<QString>(); // ошибка посреди потока
    auto forward = [handler, failure](const QVector<Delta> &deltas) {
        for (const Delta &d : deltas) {
            if (!d.error.isEmpty())
                *failure = d.error;
            else
                handler.delta(d);
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, [reply, parser, forward] {
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400)
            return; // тело ошибки разберём целиком в finished
        forward(parser->feed(reply->readAll()));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, parser, forward, failure, handler] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            handler.finished({}, true);
            return;
        }
        const QByteArray rest = reply->readAll();
        if (reply->error() != QNetworkReply::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400) {
            handler.finished(describe(reply, rest), false);
            return;
        }
        forward(parser->feed(rest));
        forward(parser->finish());
        handler.finished(*failure, false);
    });
    return reply;
}

QJsonArray AssistantService::history() const
{
    // С конца разговора, пока помещается в контекст; последний вопрос — всегда
    const QString system = systemPrompt();
    const int budget = inputBudget();
    int used = int(system.size());
    QVector<QJsonObject> picked;
    const auto &all = m_chat.messages();
    for (qsizetype i = all.size() - 1; i >= 0; --i) {
        const ChatModel::Message &m = all.at(i);
        const QString content = m.user && !m.prompt.isEmpty() ? m.prompt : m.text;
        if (content.isEmpty())
            continue; // ответ оборвался ошибкой, не начавшись
        if (!picked.isEmpty() && used + content.size() > budget)
            break;
        used += int(content.size());
        picked.prepend({{"role", m.user ? "user" : "assistant"}, {"content", content}});
    }
    while (!picked.isEmpty() && picked.first().value(QLatin1String("role")).toString() != QLatin1String("user"))
        picked.removeFirst();

    QJsonArray out{QJsonObject{{"role", "system"}, {"content", system}}};
    for (const QJsonObject &m : picked)
        out.append(m);
    return out;
}

void AssistantService::send(ChatModel::Message request)
{
    stop();
    ChatModel::Message answer;
    answer.done = false;
    answer.comment = std::exchange(request.comment, std::nullopt); // для вставки комментария из ответа
    request.user = true;
    m_chat.append(std::move(request));
    const QJsonArray messages = history();
    const int row = m_chat.append(std::move(answer));
    m_replyRow = row;
    setPhase(QStringLiteral("waiting"));
    emit taskStarted();

    auto timing = std::make_shared<Timing>();
    timing->clock.start();
    m_reply = post(messages, kAnswerTokens, {
        [this, row, timing](const Delta &d) {
            if (!d.thinking.isEmpty() && m_phase == QLatin1String("waiting"))
                setPhase(QStringLiteral("thinking"));
            if (!d.content.isEmpty()) {
                if (timing->firstTokenMs < 0)
                    timing->firstTokenMs = timing->clock.elapsed();
                setPhase(QStringLiteral("writing"));
                m_chat.appendText(row, d.content);
            }
            if (d.outputTokens >= 0)
                timing->outputTokens = d.outputTokens;
        },
        [this, row, timing](const QString &error, bool stopped) {
            const qint64 total = timing->clock.elapsed();
            const qint64 writing = total - timing->firstTokenMs;
            QString stats = tr("%1 с").arg(qRound(total / 1000.0));
            if (stopped)
                stats = tr("остановлено, %1").arg(stats);
            else if (timing->outputTokens > 0 && timing->firstTokenMs >= 0 && writing > 0)
                stats += QStringLiteral(" · ") + tr("%1 ток/с").arg(QLocale().toString(
                    timing->outputTokens * 1000.0 / double(writing), 'f', 1));
            m_chat.finish(row, error, error.isEmpty() ? stats : QString());
            if (row == m_replyRow) {
                m_replyRow = -1;
                setPhase({});
            }
        },
    });
}

void AssistantService::stop()
{
    if (m_preparing) {
        *m_preparing = false;
        m_preparing.reset();
    }
    if (m_reply)
        m_reply->abort(); // finished придёт сразу и завершит ответ
    setPhase({});
}

void AssistantService::clear()
{
    stop();
    m_chat.clear();
}

void AssistantService::copy(const QString &text) const
{
    QGuiApplication::clipboard()->setText(text);
}

QString AssistantService::codeOf(int row) const
{
    const ChatModel::Message *m = m_chat.find(row);
    return m ? assistant::firstCodeBlock(m->text) : QString();
}

QString AssistantService::commentPath(int row) const
{
    const ChatModel::Message *m = m_chat.find(row);
    return m && m->comment ? m->comment->path : QString();
}

void AssistantService::ask(const QString &question, EditorView *editor, bool withSelection)
{
    const QString text = question.trimmed();
    if (text.isEmpty())
        return;
    ChatModel::Message m;
    m.text = text;
    core::Document *doc = editor ? editor->textDocument() : nullptr;
    int from = 0, to = 0;
    if (doc && withSelection && editor->selectedLines(&from, &to)) {
        const assistant::Excerpt code = excerptOf(doc->buffer(), from, to, from, inputBudget() / 2);
        const QString place = where(doc->filePath(), code.firstLine, code.lastLine);
        m.prompt = text + QStringLiteral("\n\n") + tr("Выделенный код (%1):").arg(place) + u'\n'
                   + fence(code.text, doc->filePath());
        m.text += u'\n' + tr("(с выделенным кодом: %1)").arg(place);
    }
    send(std::move(m));
}

void AssistantService::explainCode(EditorView *editor)
{
    core::Document *doc = editor ? editor->textDocument() : nullptr;
    if (!doc)
        return;
    int from = 0, to = doc->buffer().lineCount() - 1;
    const bool selected = editor->selectedLines(&from, &to);
    const assistant::Excerpt code = excerptOf(doc->buffer(), from, to, selected ? from : editor->cursorLine(),
                                              selected ? codeBudget() : qMin(kAroundCursorChars, codeBudget()));
    const QString place = where(doc->filePath(), code.firstLine, code.lastLine);
    ChatModel::Message m;
    m.text = tr("Объясни код: %1").arg(place);
    m.prompt = tr("Объясни, что делает этот код. Сначала одной-двумя фразами — суть, затем важные детали. "
                  "Если видишь ошибки или опасные места — перечисли их отдельно.")
               + QStringLiteral("\n\n") + tr("Файл %1").arg(place)
               + (code.truncated ? QStringLiteral(" ") + tr("(показана часть: весь текст не помещается в контекст модели)")
                                 : QString())
               + QStringLiteral(":\n") + fence(code.text, doc->filePath());
    send(std::move(m));
}

void AssistantService::documentCode(EditorView *editor)
{
    core::Document *doc = editor ? editor->textDocument() : nullptr;
    if (!doc)
        return;
    int from = 0, to = 0;
    if (!editor->selectedLines(&from, &to)) {
        emit message(tr("Выделите функцию или класс, к которому нужен комментарий"));
        return;
    }
    const assistant::Excerpt code = to - from < kMaxCommentLines
                                        ? excerptOf(doc->buffer(), from, to, from, codeBudget())
                                        : assistant::Excerpt{{}, from, to, true};
    if (code.truncated) {
        emit message(tr("Выделение слишком большое: выделите одну функцию или класс"));
        return;
    }
    const QString language = editor->language();
    ChatModel::Message m;
    m.text = tr("Напиши комментарий: %1").arg(where(doc->filePath(), from, to));
    m.prompt = tr("Напиши документирующий комментарий на русском языке к этому коду. Стиль — принятый для "
                  "языка %1: /** … */ в духе Doxygen для C, C++ и QML, docstring для Python. Коротко опиши "
                  "назначение, параметры и возвращаемое значение, если они есть. Верни только комментарий "
                  "в блоке кода, без самого кода и без пояснений.")
                   .arg(language.isEmpty() ? QFileInfo(doc->filePath()).fileName() : language)
               + QStringLiteral("\n\n") + fence(code.text, doc->filePath());
    m.comment = ChatModel::CommentTarget{doc->filePath(), language, from, code.text.split(u'\n')};
    send(std::move(m));
}

bool AssistantService::insertComment(int row, EditorView *editor)
{
    core::Document *doc = editor ? editor->textDocument() : nullptr;
    const ChatModel::Message *m = m_chat.find(row);
    if (!doc || !m || !m->comment || editor->readOnly()
        || DocumentManager::pathKey(doc->filePath()) != DocumentManager::pathKey(m->comment->path))
        return false;
    const ChatModel::CommentTarget &target = *m->comment;
    const QString comment = assistant::firstCodeBlock(m->text);
    if (target.code.isEmpty() || comment.trimmed().isEmpty())
        return false;

    // Код могли сдвинуть правками — ищем его первую непустую строку рядом с прежним местом
    int anchor = 0;
    while (anchor < target.code.size() - 1 && target.code.at(anchor).trimmed().isEmpty())
        ++anchor;
    const QString wanted = target.code.at(anchor).trimmed();
    const core::TextBuffer &buffer = doc->buffer();
    auto matches = [&](int line) {
        return line >= 0 && line < buffer.lineCount() && QStringView(buffer.lineAt(line)).trimmed() == wanted;
    };
    const int origin = target.firstLine + anchor;
    int found = matches(origin) ? origin : -1;
    for (int distance = 1; distance <= 2000 && found < 0; ++distance)
        if (matches(origin + distance))
            found = origin + distance;
        else if (matches(origin - distance))
            found = origin - distance;
    if (found < 0) {
        emit message(tr("Код, к которому писался комментарий, не найден — вставьте комментарий вручную"));
        return false;
    }
    const int first = found - anchor;
    QStringList current;
    for (int i = 0; i < target.code.size() && first + i < buffer.lineCount(); ++i)
        current.append(buffer.lineAt(first + i));
    const assistant::Placement place = assistant::placeComment(current, first, target.language, comment);
    editor->applyTextEdits({QVariantMap{{"startLine", place.line}, {"startColumn", 0},
                                        {"endLine", place.line}, {"endColumn", 0}, {"text", place.text}}});
    editor->goTo(place.line, 0);
    return true;
}

void AssistantService::explainProblem(const QString &path, int line, const QString &problem)
{
    // Открытая вкладка (с несохранёнными правками) или файл с диска
    core::TextBuffer fromDisk;
    const core::TextBuffer *buffer = nullptr;
    if (core::Document *doc = m_documents ? m_documents->find(path) : nullptr) {
        buffer = &doc->buffer();
    } else {
        if (QFileInfo(path).size() > kMaxProblemFileBytes || !fromDisk.load(path))
            return;
        buffer = &fromDisk;
    }
    const int last = buffer->lineCount() - 1;
    line = qBound(0, line, last);
    const assistant::Excerpt code = excerptOf(*buffer, qMax(0, line - 25), qMin(last, line + 25), line,
                                              inputBudget() / 2);
    const int width = int(QString::number(code.lastLine + 1).size());
    QStringList numbered = code.text.split(u'\n');
    for (int i = 0; i < numbered.size(); ++i)
        numbered[i] = QStringLiteral("%1| ").arg(code.firstLine + i + 1, width) + numbered.at(i);

    const QString name = QFileInfo(path).fileName();
    ChatModel::Message m;
    m.text = tr("Объясни ошибку: %1").arg(problem) + u'\n' + tr("(%1, строка %2)").arg(name).arg(line + 1);
    m.prompt = tr("Компилятор или анализатор кода сообщает в строке %1 файла %2:").arg(line + 1).arg(name)
               + u'\n' + problem + QStringLiteral("\n\n")
               + tr("Объясни простыми словами, что это значит и почему возникло, и предложи исправление. "
                    "Код с номерами строк:")
               + u'\n' + fence(numbered.join(u'\n'), path);
    send(std::move(m));
}

void AssistantService::summarizeLog(EditorView *editor)
{
    core::Document *doc = editor ? editor->textDocument() : nullptr;
    if (!doc)
        return;
    stop();
    int from = 0, to = doc->buffer().lineCount() - 1;
    const bool selected = editor->selectedLines(&from, &to);
    const core::PieceTable::Snapshot snapshot = doc->buffer().snapshot();
    const QString name = QFileInfo(doc->filePath()).fileName();
    const int budget = qMin(kLogChars, codeBudget());
    auto alive = std::make_shared<std::atomic_bool>(true);
    m_preparing = alive;
    setPhase(QStringLiteral("reading"));
    emit taskStarted();

    // Журнал бывает в гигабайт — выжимка в фоне, по снимку текста
    QPointer<AssistantService> self(this);
    QThreadPool::globalInstance()->start([=] {
        assistant::LogDigest digest(budget);
        snapshot.forEachLine([&](int i, const QString &line) {
            digest.add(line);
            return i < to && *alive;
        }, from);
        if (!*alive)
            return;
        const QString text = digest.text();
        const int lines = digest.lineCount(), errors = digest.errorCount(), warnings = digest.warningCount();
        QMetaObject::invokeMethod(qApp, [self, alive, text, lines, errors, warnings, name, selected, from] {
            if (!self || !*alive)
                return;
            ChatModel::Message m;
            m.text = tr("Перескажи журнал: %1").arg(selected ? where(name, from, from + lines - 1) : name);
            m.text += u'\n' + tr("(строк: %1, ошибок: %2, предупреждений: %3)").arg(lines).arg(errors).arg(warnings);
            m.prompt = tr("Это журнал работы программы (%1, строк: %2). Кратко, до 10 строк, перескажи: что происходило, "
                          "какие были ошибки и предупреждения, и назови вероятную причину главной ошибки. Большой журнал "
                          "сокращён: начало, все ошибки и предупреждения (повторы схлопнуты, xN — число повторов) "
                          "и конец.").arg(name).arg(lines)
                       + QStringLiteral("\n\n```text\n") + text + QStringLiteral("\n```");
            self->send(std::move(m));
        }, Qt::QueuedConnection);
    });
}

void AssistantService::suggestCommitMessage(const QString &diff)
{
    stopCommitMessage();
    if (diff.trimmed().isEmpty()) {
        emit message(tr("Нет изменений для сообщения коммита"));
        return;
    }
    m_commitDraft.clear();
    emit commitDraftChanged();
    const QJsonArray messages{
        QJsonObject{{"role", "system"}, {"content", systemPrompt()}},
        QJsonObject{{"role", "user"},
                    {"content", tr("Напиши сообщение коммита на русском языке по этим изменениям. Первая строка — "
                                   "суть до 72 символов без точки в конце, например: «Исправлено падение при "
                                   "вставке в пустой документ». Если изменений несколько — после пустой строки "
                                   "добавь короткий список «- …». Верни только текст сообщения, без кавычек, "
                                   "пояснений и блоков кода.")
                                    + QStringLiteral("\n\n```diff\n") + diff + QStringLiteral("\n```")}}};
    m_commitReply = post(messages, kCommitTokens, {
        [this](const Delta &d) {
            if (d.content.isEmpty())
                return;
            m_commitDraft += d.content;
            emit commitDraftChanged();
        },
        [this](const QString &error, bool stopped) {
            m_commitReply = nullptr;
            emit commitBusyChanged();
            if (!error.isEmpty()) {
                emit message(error);
                return;
            }
            if (!stopped) {
                m_commitDraft = assistant::cleanCommitMessage(m_commitDraft);
                emit commitDraftChanged();
            }
        },
    });
    emit commitBusyChanged();
}

void AssistantService::stopCommitMessage()
{
    if (m_commitReply)
        m_commitReply->abort();
}

void AssistantService::checkServer()
{
    if (m_checkReply) {
        QNetworkReply *old = m_checkReply;
        m_checkReply = nullptr;
        old->abort();
    }
    setServer(QStringLiteral("checking"), tr("Проверка…"));
    const bool ollama = isOllama();
    QNetworkRequest models = request(QStringLiteral("/api/tags"), QStringLiteral("/v1/models"));
    models.setTransferTimeout(5000);
    QNetworkReply *reply = m_network.get(models);
    m_checkReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, ollama] {
        reply->deleteLater();
        if (reply != m_checkReply)
            return; // заменён новой проверкой
        m_checkReply = nullptr;
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            setServer(QStringLiteral("error"), describe(reply, body));
            return;
        }
        const QJsonObject json = QJsonDocument::fromJson(body).object();
        m_models.clear();
        for (const QJsonValue &v : json.value(ollama ? QLatin1String("models") : QLatin1String("data")).toArray())
            m_models.append(v.toObject().value(ollama ? QLatin1String("name") : QLatin1String("id")).toString());
        m_models.sort();
        const bool present = m_models.contains(m_model)
                             || (ollama && !m_model.contains(u':') && m_models.contains(m_model + QLatin1String(":latest")));
        if (present)
            setServer(QStringLiteral("ok"), tr("Подключено, модель «%1» на месте").arg(m_model));
        else if (m_models.isEmpty())
            setServer(QStringLiteral("error"), tr("Сервер отвечает, но моделей на нём нет"));
        else if (ollama)
            setServer(QStringLiteral("error"), tr("Модели «%1» на сервере нет. Скачайте её командой: ollama pull %1 "
                                                  "— или выберите другую").arg(m_model));
        else
            setServer(QStringLiteral("error"), tr("Модели «%1» на сервере нет — выберите одну из списка").arg(m_model));
    });
}
