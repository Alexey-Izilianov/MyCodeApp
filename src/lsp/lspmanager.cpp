#include "lspmanager.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLibraryInfo>
#include <QStandardPaths>
#include <QUrl>
#include <spdlog/spdlog.h>

#include "lspclient.h"
#include "src/services/workspace.h"

using core::Document;

namespace {

constexpr int kMaxChars = 4 * 1024 * 1024; // больше — серверу не отправляем (секунды на разбор)
constexpr int kMaxCompletionItems = 200;
constexpr int kChangeDelayMs = 300;

QString pathKey(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    return clean.toLower();
#else
    return clean;
#endif
}

QString pathFromUri(const QString &uri)
{
    QString path = QUrl(uri).toLocalFile();
    if (path.size() > 1 && path.at(1) == u':') // c:/ от сервера -> C:/, как у открытых файлов
        path[0] = path.at(0).toUpper();
    return QDir::cleanPath(path);
}

QString languageIdFor(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("c"))
        return QStringLiteral("c");
    if (suffix.startsWith(QLatin1String("py")))
        return QStringLiteral("python");
    if (suffix == QLatin1String("rs"))
        return QStringLiteral("rust");
    if (suffix == QLatin1String("qml"))
        return QStringLiteral("qml");
    return QStringLiteral("cpp");
}

QJsonObject position(int line, int column)
{
    return {{QStringLiteral("line"), line}, {QStringLiteral("character"), column}};
}

// Hover и документация: MarkupContent, MarkedString или их массив -> markdown
QString markdownOf(const QJsonValue &contents)
{
    if (contents.isString())
        return contents.toString();
    if (contents.isArray()) {
        QStringList parts;
        for (const QJsonValue &part : contents.toArray())
            parts.append(markdownOf(part));
        parts.removeAll(QString());
        return parts.join(QStringLiteral("\n\n"));
    }
    const QJsonObject object = contents.toObject();
    const QString value = object.value(QStringLiteral("value")).toString();
    if (object.contains(QStringLiteral("language")))
        return QStringLiteral("```%1\n%2\n```").arg(object.value(QStringLiteral("language")).toString(), value);
    return value;
}

QVariantMap editFromJson(const QJsonObject &edit)
{
    const QJsonObject range = edit.value(QStringLiteral("range")).toObject();
    const QJsonObject start = range.value(QStringLiteral("start")).toObject();
    const QJsonObject end = range.value(QStringLiteral("end")).toObject();
    return {{QStringLiteral("startLine"), start.value(QStringLiteral("line")).toInt()},
            {QStringLiteral("startColumn"), start.value(QStringLiteral("character")).toInt()},
            {QStringLiteral("endLine"), end.value(QStringLiteral("line")).toInt()},
            {QStringLiteral("endColumn"), end.value(QStringLiteral("character")).toInt()},
            {QStringLiteral("text"), edit.value(QStringLiteral("newText")).toString()}};
}

QVariantList editsFromJson(const QJsonArray &edits)
{
    QVariantList out;
    for (const QJsonValue &edit : edits)
        out.append(editFromJson(edit.toObject()));
    return out;
}

// Путь с '*' в компонентах (C:/Qt/*/mingw_64/bin/qmlls.exe); из нескольких
// совпадений — последнее по имени, то есть обычно самая новая версия
QString firstMatch(const QString &pattern)
{
    QStringList paths{QString()};
    for (const QString &part : pattern.split(u'/')) {
        QStringList next;
        for (const QString &base : std::as_const(paths)) {
            if (!part.contains(u'*')) {
                next.append(base.isEmpty() ? part : base + u'/' + part);
                continue;
            }
            QStringList names = QDir(base.endsWith(u':') ? base + u'/' : base).entryList({part}, QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
            std::reverse(names.begin(), names.end());
            for (const QString &name : std::as_const(names))
                next.append(base + u'/' + name);
        }
        paths = next;
    }
    for (const QString &path : std::as_const(paths))
        if (QFileInfo(path).isFile())
            return path;
    return {};
}

// Location | Location[] | LocationLink[] -> {путь, строка, колонка} каждой
QVector<std::tuple<QString, int, int>> locationsOf(const QJsonValue &result)
{
    const QJsonArray array = result.isArray() ? result.toArray() : QJsonArray{result};
    QVector<std::tuple<QString, int, int>> out;
    for (const QJsonValue &value : array) {
        const QJsonObject location = value.toObject();
        const bool link = location.contains(QStringLiteral("targetUri"));
        const QString uri = location.value(link ? QStringLiteral("targetUri") : QStringLiteral("uri")).toString();
        const QJsonObject start = location.value(link ? QStringLiteral("targetSelectionRange") : QStringLiteral("range"))
                                      .toObject().value(QStringLiteral("start")).toObject();
        if (!uri.isEmpty())
            out.append({pathFromUri(uri), start.value(QStringLiteral("line")).toInt(),
                        start.value(QStringLiteral("character")).toInt()});
    }
    return out;
}

} // namespace

LspManager::LspManager(QObject *parent)
    : QObject(parent)
{
    loadConfig();
    m_changeTimer.setSingleShot(true);
    m_changeTimer.setInterval(kChangeDelayMs);
    connect(&m_changeTimer, &QTimer::timeout, this, &LspManager::flushAll);
}

LspManager::~LspManager()
{
    qDeleteAll(m_clients); // shutdown/exit серверам
}

// ── Настройки и серверы ─────────────────────────────────────────────────

void LspManager::loadConfig()
{
    auto read = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly)
                   ? QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("servers")).toObject()
                   : QJsonObject();
    };
    QJsonObject servers = read(QStringLiteral(":/assets/lsp.json"));
    // Пользовательский файл заменяет описание сервера целиком
    const QJsonObject user = read(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                                  + QStringLiteral("/lsp.json"));
    for (auto it = user.begin(); it != user.end(); ++it)
        servers.insert(it.key(), it.value());

    auto strings = [](const QJsonValue &value) {
        QStringList out;
        for (const QJsonValue &v : value.toArray())
            out.append(v.toString());
        return out;
    };
    for (auto it = servers.begin(); it != servers.end(); ++it) {
        const QJsonObject s = it.value().toObject();
        m_configs.append({it.key(), strings(s.value(QStringLiteral("extensions"))),
                          strings(s.value(QStringLiteral("command"))), strings(s.value(QStringLiteral("candidates"))),
                          s.value(QStringLiteral("settings")).toObject(),
                          s.value(QStringLiteral("initializationOptions")).toObject(),
                          s.value(QStringLiteral("compileCommands")).toBool()});
    }
}

const LspManager::ServerConfig *LspManager::configFor(const QString &path) const
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    for (const ServerConfig &config : m_configs)
        if (config.extensions.contains(suffix) && !config.command.isEmpty())
            return &config;
    return nullptr;
}

QString LspManager::resolveProgram(const ServerConfig &config) const
{
    const QString command = config.command.first();
    if (QFileInfo(command).isAbsolute())
        return QFileInfo::exists(command) ? command : QString();
    if (const QString found = QStandardPaths::findExecutable(command); !found.isEmpty())
        return found;
    const QString qtBin = QLibraryInfo::path(QLibraryInfo::BinariesPath);
    const QString qtRoot = QDir(QLibraryInfo::path(QLibraryInfo::PrefixPath) + QStringLiteral("/../..")).absolutePath();
    for (QString candidate : config.candidates) {
        candidate.replace(QStringLiteral("${QT_BIN}"), qtBin).replace(QStringLiteral("${QT_ROOT}"), qtRoot);
        if (const QString found = firstMatch(candidate); !found.isEmpty())
            return found;
    }
    return {};
}

LspClient *LspManager::clientFor(const ServerConfig &config, const QString &fallbackRoot)
{
    if (LspClient *client = m_clients.value(config.name))
        return client;

    const QString root = m_root.isEmpty() ? fallbackRoot : m_root;
    LspClient::Config clientConfig{config.name, resolveProgram(config), config.command.mid(1),
                                   config.initializationOptions, config.settings};
    if (clientConfig.program.isEmpty())
        clientConfig.program = config.command.first(); // не найден — QProcess сообщит, статус «не найден»
    if (clientConfig.program.endsWith(QLatin1String(".cmd")) || clientConfig.program.endsWith(QLatin1String(".bat"))) {
        clientConfig.arguments.prepend(clientConfig.program); // npm-обёртки запускаются только через cmd
        clientConfig.arguments.prepend(QStringLiteral("/c"));
        clientConfig.program = QStringLiteral("cmd.exe");
    }
    if (config.compileCommands) {
        QStringList dirs{root, root + QStringLiteral("/build")};
        for (QDirIterator it(root + QStringLiteral("/build"), QDir::Dirs | QDir::NoDotAndDotDot); it.hasNext();)
            dirs.append(it.next());
        for (const QString &dir : std::as_const(dirs)) {
            if (QFileInfo::exists(dir + QStringLiteral("/compile_commands.json"))) {
                clientConfig.arguments.append(QStringLiteral("--compile-commands-dir=") + dir);
                break;
            }
        }
    }

    auto *client = new LspClient(clientConfig, root, this);
    m_clients.insert(config.name, client);
    connect(client, &LspClient::stateChanged, this, &LspManager::statusChanged);
    connect(client, &LspClient::progress, this, [this, name = config.name](const QString &text) {
        m_progress.insert(name, text);
        emit statusChanged();
    });
    connect(client, &LspClient::diagnostics, this, &LspManager::setDiagnostics);
    connect(client, &LspClient::ready, this, [this, client] {
        for (auto it = m_tracked.begin(); it != m_tracked.end(); ++it)
            if (it->client == client)
                sendOpen(it.key(), *it);
    });
    spdlog::info("lsp: {} -> {} {}", config.name.toStdString(), clientConfig.program.toStdString(),
                 clientConfig.arguments.join(u' ').toStdString());
    client->start();
    return client;
}

QString LspManager::statusFor(const QString &path) const
{
    const ServerConfig *config = configFor(path);
    if (!config)
        return {};
    const LspClient *client = m_clients.value(config->name);
    if (!client)
        return config->name;
    switch (client->state()) {
    case LspClient::State::Starting: return tr("%1: запуск").arg(config->name);
    case LspClient::State::Restarting: return tr("%1: перезапуск").arg(config->name);
    case LspClient::State::Failed: return tr("%1: остановлен после сбоев").arg(config->name);
    case LspClient::State::NotFound: return tr("%1 не найден").arg(config->name);
    case LspClient::State::Ready: break;
    }
    const QString progress = m_progress.value(config->name);
    return progress.isEmpty() ? config->name : config->name + QStringLiteral(": ") + progress;
}

void LspManager::setRootPath(const QString &path)
{
    if (path == m_root)
        return;
    m_root = path;
    resetClients();
    syncDocuments();
    emit rootPathChanged();
}

void LspManager::resetClients()
{
    cancelCompletion();
    for (auto it = m_tracked.cbegin(); it != m_tracked.cend(); ++it)
        disconnect(it.key(), nullptr, this, nullptr);
    m_tracked.clear();
    qDeleteAll(m_clients);
    m_clients.clear();
    m_progress.clear();
    m_diagnostics.clear();
    rebuildProblems();
    emit statusChanged();
}

// ── Документы ───────────────────────────────────────────────────────────

void LspManager::setDocuments(DocumentManager *documents)
{
    if (documents == m_documents)
        return;
    if (m_documents)
        disconnect(m_documents, nullptr, this, nullptr);
    m_documents = documents;
    if (documents)
        connect(documents, &DocumentManager::tabsChanged, this, &LspManager::syncDocuments);
    syncDocuments();
    emit documentsChanged();
}

void LspManager::syncDocuments()
{
    if (!m_documents)
        return;
    for (Document *doc : m_documents->documents())
        if (!m_tracked.contains(doc))
            track(doc);
}

void LspManager::track(Document *doc)
{
    const ServerConfig *config = configFor(doc->filePath());
    if (!config || doc->isReadOnly() || doc->buffer().length() > kMaxChars)
        return;
    LspClient *client = clientFor(*config, QFileInfo(doc->filePath()).absolutePath());
    Tracked &tracked = m_tracked[doc];
    tracked = {client, doc->filePath(), QUrl::fromLocalFile(doc->filePath()).toString(), languageIdFor(doc->filePath())};

    auto changed = [this, doc] {
        if (auto it = m_tracked.find(doc); it != m_tracked.end()) {
            it->changed = true;
            m_changeTimer.start();
        }
    };
    connect(doc, &Document::edited, this, changed);
    connect(doc, &Document::reloaded, this, changed);
    connect(doc, &QObject::destroyed, this, [this, doc] {
        const Tracked closed = m_tracked.take(doc);
        closed.client->notify(QStringLiteral("textDocument/didClose"),
                              QJsonObject{{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), closed.uri}}}});
        if (m_diagnostics.remove(pathKey(closed.path))) {
            rebuildProblems();
            emit diagnosticsChanged(closed.path);
        }
    });
    if (client->state() == LspClient::State::Ready)
        sendOpen(doc, tracked);
}

void LspManager::sendOpen(Document *doc, Tracked &tracked)
{
    tracked.changed = false;
    tracked.client->notify(QStringLiteral("textDocument/didOpen"), QJsonObject{{QStringLiteral("textDocument"), QJsonObject{
        {QStringLiteral("uri"), tracked.uri}, {QStringLiteral("languageId"), tracked.languageId},
        {QStringLiteral("version"), tracked.version}, {QStringLiteral("text"), doc->buffer().snapshot().toString()}}}});
}

void LspManager::flush(Document *doc)
{
    auto it = m_tracked.find(doc);
    if (it == m_tracked.end() || !it->changed)
        return;
    it->changed = false;
    ++it->version;
    it->client->notify(QStringLiteral("textDocument/didChange"), QJsonObject{
        {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), it->uri}, {QStringLiteral("version"), it->version}}},
        {QStringLiteral("contentChanges"), QJsonArray{QJsonObject{{QStringLiteral("text"), doc->buffer().snapshot().toString()}}}}});
}

void LspManager::flushAll()
{
    for (auto it = m_tracked.cbegin(); it != m_tracked.cend(); ++it)
        flush(it.key());
}

Document *LspManager::findDocument(const QString &path) const
{
    if (m_documents)
        for (Document *doc : m_documents->documents())
            if (pathKey(doc->filePath()) == pathKey(path))
                return doc;
    return nullptr;
}

LspManager::Tracked *LspManager::prepare(EditorView *editor)
{
    auto *doc = editor ? qobject_cast<Document *>(editor->document()) : nullptr;
    auto it = m_tracked.find(doc);
    if (it == m_tracked.end())
        return nullptr;
    const LspClient::State state = it->client->state();
    if (state == LspClient::State::NotFound || state == LspClient::State::Failed)
        return nullptr;
    flush(doc);
    return &it.value();
}

QJsonObject LspManager::positionParams(const Tracked &tracked, int line, int column) const
{
    return {{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), tracked.uri}}},
            {QStringLiteral("position"), position(line, column)}};
}

// ── Диагностика ─────────────────────────────────────────────────────────

void LspManager::setDiagnostics(const QString &uri, const QJsonArray &items)
{
    const QString path = pathFromUri(uri);
    FileDiagnostics file{path, {}};
    for (const QJsonValue &value : items) {
        const QJsonObject d = value.toObject();
        const QVariantMap range = editFromJson(d); // у диагностики то же поле range
        file.items.append({range.value(QStringLiteral("startLine")).toInt(), range.value(QStringLiteral("startColumn")).toInt(),
                           range.value(QStringLiteral("endLine")).toInt(), range.value(QStringLiteral("endColumn")).toInt(),
                           d.value(QStringLiteral("severity")).toInt(1), d.value(QStringLiteral("message")).toString()});
    }
    if (file.items.isEmpty())
        m_diagnostics.remove(pathKey(path));
    else
        m_diagnostics.insert(pathKey(path), file);
    rebuildProblems();
    emit diagnosticsChanged(path);
}

QVariantList LspManager::diagnosticsFor(const QString &path) const
{
    QVariantList out;
    for (const Diagnostic &d : m_diagnostics.value(pathKey(path)).items)
        out.append(QVariantMap{{QStringLiteral("startLine"), d.startLine}, {QStringLiteral("startColumn"), d.startColumn},
                               {QStringLiteral("endLine"), d.endLine}, {QStringLiteral("endColumn"), d.endColumn},
                               {QStringLiteral("severity"), d.severity}, {QStringLiteral("message"), d.message}});
    return out;
}

void LspManager::rebuildProblems()
{
    QStringList keys = m_diagnostics.keys();
    keys.sort();
    m_problems.clear();
    for (const QString &key : std::as_const(keys)) {
        const FileDiagnostics &file = m_diagnostics[key];
        QVector<Diagnostic> items = file.items;
        std::stable_sort(items.begin(), items.end(), [](const Diagnostic &a, const Diagnostic &b) {
            return a.severity != b.severity ? a.severity < b.severity : a.startLine < b.startLine;
        });
        for (const Diagnostic &d : std::as_const(items))
            m_problems.append(QVariantMap{{QStringLiteral("path"), file.path}, {QStringLiteral("line"), d.startLine},
                                          {QStringLiteral("column"), d.startColumn}, {QStringLiteral("severity"), d.severity},
                                          {QStringLiteral("message"), d.message}});
    }
    emit problemsChanged();
}

// ── Автодополнение ──────────────────────────────────────────────────────

void LspManager::requestCompletion(EditorView *editor)
{
    cancelCompletion();
    Tracked *tracked = prepare(editor);
    if (!tracked)
        return;
    QPointer<Document> doc = qobject_cast<Document *>(editor->document());
    LspClient *client = tracked->client;
    QElapsedTimer timer;
    timer.start();
    m_completion.client = client;
    m_completion.requestId = client->request(
        QStringLiteral("textDocument/completion"), positionParams(*tracked, editor->cursorLine(), editor->cursorColumn()),
        [this, doc, client, timer](const QJsonValue &result, const QJsonObject &error) {
            m_completion.requestId = 0;
            if (!error.isEmpty() || !doc)
                return;
            m_completion.document = doc;
            m_completion.client = client;
            m_completion.items = result.isArray() ? result.toArray()
                                                  : result.toObject().value(QStringLiteral("items")).toArray();
            spdlog::info("lsp: автодополнение {} мс, вариантов {}", timer.elapsed(), m_completion.items.size());
            emit completionReady();
        });
}

void LspManager::cancelCompletion()
{
    if (m_completion.requestId && m_completion.client)
        m_completion.client->cancel(m_completion.requestId);
    m_completion = {};
}

QVariantList LspManager::completionItems(const QString &prefix) const
{
    struct Scored {
        int score;
        int index;
        QString sortText;
    };
    QVector<Scored> scored;
    for (int i = 0; i < m_completion.items.size(); ++i) {
        const QJsonObject item = m_completion.items.at(i).toObject();
        QString filter = item.value(QStringLiteral("filterText")).toString();
        if (filter.isEmpty())
            filter = item.value(QStringLiteral("label")).toString().trimmed();
        const int score = Workspace::fuzzyScore(prefix, filter);
        if (score >= 0)
            scored.append({score, i, item.value(QStringLiteral("sortText")).toString()});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
        return a.score != b.score ? a.score > b.score : a.sortText < b.sortText;
    });

    QVariantList out;
    for (int i = 0; i < qMin<qsizetype>(scored.size(), kMaxCompletionItems); ++i) {
        const QJsonObject item = m_completion.items.at(scored.at(i).index).toObject();
        out.append(QVariantMap{{QStringLiteral("index"), scored.at(i).index},
                               {QStringLiteral("label"), item.value(QStringLiteral("label")).toString().trimmed()},
                               {QStringLiteral("detail"), item.value(QStringLiteral("detail")).toString()},
                               {QStringLiteral("kind"), item.value(QStringLiteral("kind")).toInt()}});
    }
    return out;
}

void LspManager::requestDocumentation(int index)
{
    if (index < 0 || index >= m_completion.items.size())
        return;
    auto textOf = [](const QJsonObject &item) {
        const QString detail = item.value(QStringLiteral("detail")).toString().trimmed();
        const QString doc = markdownOf(item.value(QStringLiteral("documentation")));
        return (detail.isEmpty() ? QString() : QStringLiteral("```\n%1\n```\n\n").arg(detail)) + doc;
    };
    const QJsonObject item = m_completion.items.at(index).toObject();
    const bool resolvable = m_completion.client && m_completion.client->capabilities()
                                .value(QStringLiteral("completionProvider")).toObject()
                                .value(QStringLiteral("resolveProvider")).toBool();
    if (item.contains(QStringLiteral("documentation")) || !resolvable) {
        emit documentationReady(index, textOf(item));
        return;
    }
    const QJsonArray items = m_completion.items;
    m_completion.client->request(QStringLiteral("completionItem/resolve"), item,
        [this, index, items, textOf](const QJsonValue &result, const QJsonObject &error) {
            if (!error.isEmpty() || m_completion.items != items)
                return; // список уже другой
            m_completion.items[index] = result;
            emit documentationReady(index, textOf(result.toObject()));
        });
}

void LspManager::acceptCompletion(EditorView *editor, int index)
{
    if (!editor || index < 0 || index >= m_completion.items.size()
        || editor->document() != m_completion.document)
        return;
    const QJsonObject item = m_completion.items.at(index).toObject();
    const int line = editor->cursorLine(), caret = editor->cursorColumn();
    int from = caret - int(editor->wordBeforeCursor().size()), to = caret;
    QString text = item.value(QStringLiteral("insertText")).toString();
    if (text.isEmpty())
        text = item.value(QStringLiteral("label")).toString().trimmed();

    if (item.contains(QStringLiteral("textEdit"))) {
        const QJsonObject edit = item.value(QStringLiteral("textEdit")).toObject();
        // InsertReplaceEdit: берём вставку, как по умолчанию в VS Code
        const QJsonObject range = edit.value(edit.contains(QStringLiteral("insert")) ? QStringLiteral("insert")
                                                                                     : QStringLiteral("range")).toObject();
        const QJsonObject start = range.value(QStringLiteral("start")).toObject();
        if (start.value(QStringLiteral("line")).toInt() == line) {
            from = start.value(QStringLiteral("character")).toInt();
            // После запроса могли допечатать — заменяем и допечатанное
            to = qMax(caret, range.value(QStringLiteral("end")).toObject().value(QStringLiteral("character")).toInt());
        }
        text = edit.value(QStringLiteral("newText")).toString();
    }
    int cursor = int(text.size());
    if (item.value(QStringLiteral("insertTextFormat")).toInt() == 2)
        std::tie(text, cursor) = expandSnippet(text);

    QVariantList edits{QVariantMap{{QStringLiteral("startLine"), line}, {QStringLiteral("startColumn"), from},
                                   {QStringLiteral("endLine"), line}, {QStringLiteral("endColumn"), to},
                                   {QStringLiteral("text"), text}}};
    edits += editsFromJson(item.value(QStringLiteral("additionalTextEdits")).toArray());
    editor->applyTextEdits(edits, 0, cursor);
    cancelCompletion();
}

QPair<QString, int> LspManager::expandSnippet(const QString &snippet)
{
    QString out;
    int firstTab = std::numeric_limits<int>::max(), cursor = -1, finalCursor = -1;
    auto tabStop = [&](int number, int at) {
        if (number == 0)
            finalCursor = at;
        else if (number < firstTab) {
            firstTab = number;
            cursor = at;
        }
    };
    for (int i = 0; i < snippet.size(); ++i) {
        const QChar c = snippet.at(i);
        if (c == u'\\' && i + 1 < snippet.size()) {
            out += snippet.at(++i);
            continue;
        }
        if (c != u'$' || i + 1 == snippet.size()) {
            out += c;
            continue;
        }
        const QChar next = snippet.at(i + 1);
        if (next.isDigit()) {
            int j = i + 1, number = 0;
            for (; j < snippet.size() && snippet.at(j).isDigit(); ++j)
                number = number * 10 + snippet.at(j).digitValue();
            tabStop(number, int(out.size()));
            i = j - 1;
        } else if (next == u'{') {
            int depth = 0, j = i + 1;
            for (; j < snippet.size(); ++j) {
                if (snippet.at(j) == u'{')
                    ++depth;
                else if (snippet.at(j) == u'}' && --depth == 0)
                    break;
            }
            const QString inner = snippet.mid(i + 2, j - i - 2); // "1:имя", "2|a,b|" или "1"
            int k = 0, number = 0;
            for (; k < inner.size() && inner.at(k).isDigit(); ++k)
                number = number * 10 + inner.at(k).digitValue();
            tabStop(number, int(out.size()));
            if (k < inner.size() && inner.at(k) == u':')
                out += expandSnippet(inner.mid(k + 1)).first;
            else if (k < inner.size() && inner.at(k) == u'|')
                out += inner.mid(k + 1).section(u',', 0, 0).remove(u'|');
            i = j;
        } else if (next.isLetter() || next == u'_') { // переменные ($TM_FILENAME) не поддерживаем — пропускаем имя
            int j = i + 1;
            while (j < snippet.size() && (snippet.at(j).isLetterOrNumber() || snippet.at(j) == u'_'))
                ++j;
            i = j - 1;
        } else {
            out += c;
        }
    }
    return {out, cursor >= 0 ? cursor : finalCursor >= 0 ? finalCursor : int(out.size())};
}

// ── Навигация и правки ──────────────────────────────────────────────────

void LspManager::hover(EditorView *editor, int line, int column)
{
    QStringList parts;
    auto *doc = editor ? qobject_cast<Document *>(editor->document()) : nullptr;
    if (!doc)
        return;
    for (const Diagnostic &d : m_diagnostics.value(pathKey(doc->filePath())).items) {
        const bool after = line > d.startLine || (line == d.startLine && column >= d.startColumn);
        const bool before = line < d.endLine || (line == d.endLine && column <= qMax(d.endColumn, d.startColumn + 1));
        if (after && before)
            parts.append((d.severity == 1 ? tr("Ошибка: ") : d.severity == 2 ? tr("Предупреждение: ") : QString())
                         + d.message);
    }
    Tracked *tracked = prepare(editor);
    if (!tracked) {
        if (!parts.isEmpty())
            emit hoverReady(parts.join(QStringLiteral("\n\n")), line, column);
        return;
    }
    if (m_hoverRequest)
        tracked->client->cancel(m_hoverRequest);
    m_hoverRequest = tracked->client->request(QStringLiteral("textDocument/hover"), positionParams(*tracked, line, column),
        [this, parts, line, column](const QJsonValue &result, const QJsonObject &) mutable {
            m_hoverRequest = 0;
            const QString contents = markdownOf(result.toObject().value(QStringLiteral("contents")));
            if (!contents.isEmpty())
                parts.append(contents);
            if (!parts.isEmpty())
                emit hoverReady(parts.join(QStringLiteral("\n\n---\n\n")), line, column);
        });
}

void LspManager::gotoDefinition(EditorView *editor, int line, int column)
{
    Tracked *tracked = prepare(editor);
    if (!tracked) {
        emit message(tr("Для этого файла нет языкового сервера"));
        return;
    }
    tracked->client->request(QStringLiteral("textDocument/definition"), positionParams(*tracked, line, column),
        [this](const QJsonValue &result, const QJsonObject &) {
            const auto locations = locationsOf(result);
            if (locations.isEmpty() || std::get<0>(locations.first()).isEmpty())
                emit message(tr("Определение не найдено"));
            else
                emit navigate(std::get<0>(locations.first()), std::get<1>(locations.first()), std::get<2>(locations.first()));
        });
}

void LspManager::findReferences(EditorView *editor)
{
    Tracked *tracked = prepare(editor);
    if (!tracked) {
        emit message(tr("Для этого файла нет языкового сервера"));
        return;
    }
    QJsonObject params = positionParams(*tracked, editor->cursorLine(), editor->cursorColumn());
    params.insert(QStringLiteral("context"), QJsonObject{{QStringLiteral("includeDeclaration"), true}});
    tracked->client->request(QStringLiteral("textDocument/references"), params,
        [this](const QJsonValue &result, const QJsonObject &) {
            auto locations = locationsOf(result);
            std::sort(locations.begin(), locations.end());
            QHash<QString, QStringList> files; // путь -> строки, читаем каждый файл один раз
            m_references.clear();
            for (const auto &[path, line, column] : std::as_const(locations)) {
                if (!files.contains(path)) {
                    QStringList lines;
                    if (Document *doc = findDocument(path)) {
                        lines = doc->buffer().snapshot().lines();
                    } else if (QFile file(path); file.open(QIODevice::ReadOnly)) {
                        lines = QString::fromUtf8(file.readAll()).split(u'\n');
                    }
                    files.insert(path, lines);
                }
                const QString text = files.value(path).value(line).trimmed();
                m_references.append(QVariantMap{{QStringLiteral("path"), path}, {QStringLiteral("line"), line},
                                                {QStringLiteral("column"), column}, {QStringLiteral("preview"), text}});
            }
            emit referencesChanged();
            if (m_references.isEmpty())
                emit message(tr("Ссылки не найдены"));
        });
}

void LspManager::rename(EditorView *editor, const QString &newName)
{
    Tracked *tracked = prepare(editor);
    if (!tracked || newName.isEmpty())
        return;
    QJsonObject params = positionParams(*tracked, editor->cursorLine(), editor->cursorColumn());
    params.insert(QStringLiteral("newName"), newName);
    QPointer<EditorView> view = editor;
    tracked->client->request(QStringLiteral("textDocument/rename"), params,
        [this, view](const QJsonValue &result, const QJsonObject &error) {
            if (!error.isEmpty())
                emit message(error.value(QStringLiteral("message")).toString());
            else if (view)
                applyWorkspaceEdit(view, result.toObject());
        });
}

void LspManager::format(EditorView *editor)
{
    Tracked *tracked = prepare(editor);
    if (!tracked)
        return;
    const QJsonObject params{
        {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), tracked->uri}}},
        {QStringLiteral("options"), QJsonObject{{QStringLiteral("tabSize"), 4}, {QStringLiteral("insertSpaces"), true}}}};
    QPointer<EditorView> view = editor;
    QPointer<QObject> doc = editor->document();
    tracked->client->request(QStringLiteral("textDocument/formatting"), params,
        [this, view, doc](const QJsonValue &result, const QJsonObject &error) {
            if (!error.isEmpty())
                emit message(error.value(QStringLiteral("message")).toString());
            else if (view && view->document() == doc)
                view->applyTextEdits(editsFromJson(result.toArray()));
        });
}

void LspManager::applyWorkspaceEdit(EditorView *editor, const QJsonObject &edit)
{
    QHash<QString, QJsonArray> byPath;
    const QJsonObject changes = edit.value(QStringLiteral("changes")).toObject();
    for (auto it = changes.begin(); it != changes.end(); ++it)
        byPath[pathFromUri(it.key())] = it.value().toArray();
    for (const QJsonValue &change : edit.value(QStringLiteral("documentChanges")).toArray()) {
        const QJsonObject object = change.toObject();
        const QString uri = object.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString();
        if (!uri.isEmpty())
            byPath[pathFromUri(uri)] = object.value(QStringLiteral("edits")).toArray();
    }
    if (byPath.isEmpty() || !m_documents)
        return;

    // Неоткрытые файлы открываются вкладками (правки видны и отменяемы), потом
    // возвращаемся на исходную вкладку
    QObject *original = editor->document();
    for (auto it = byPath.cbegin(); it != byPath.cend(); ++it) {
        Document *doc = findDocument(it.key());
        if (!doc)
            doc = qobject_cast<Document *>(m_documents->documentAt(m_documents->openPath(it.key())));
        if (!doc || doc->isReadOnly())
            continue;
        if (editor->document() == doc) {
            editor->applyTextEdits(editsFromJson(it.value()));
            continue;
        }
        QVector<Document::Replacement> parts;
        for (const QVariant &v : editsFromJson(it.value())) {
            const QVariantMap e = v.toMap();
            parts.append({{e.value(QStringLiteral("startLine")).toInt(), e.value(QStringLiteral("startColumn")).toInt()},
                          {e.value(QStringLiteral("endLine")).toInt(), e.value(QStringLiteral("endColumn")).toInt()},
                          e.value(QStringLiteral("text")).toString()});
        }
        doc->replace(parts, Document::EditKind::Other, {});
    }
    const int originalIndex = int(m_documents->documents().indexOf(qobject_cast<Document *>(original)));
    if (originalIndex >= 0)
        m_documents->activate(originalIndex);
    emit message(tr("Изменено файлов: %1").arg(byPath.size()));
}
