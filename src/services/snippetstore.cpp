#include "snippetstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTimer>

#include "appsettings.h"

namespace {
constexpr int kMaxCompletions = 20;
const QString kGlobal = QStringLiteral("global");
}

SnippetStore::SnippetStore(QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(userDir());
    auto *delay = new QTimer(this); // редакторы пишут файл в несколько шагов
    delay->setSingleShot(true);
    delay->setInterval(150);
    connect(delay, &QTimer::timeout, this, &SnippetStore::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, delay, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, delay, qOverload<>(&QTimer::start));

    for (const QFileInfo &file : QDir(QStringLiteral(":/assets/snippets")).entryInfoList({QStringLiteral("*.json")}))
        loadFile(file.filePath(), m_builtin, file.completeBaseName());
    reload();
}

QString SnippetStore::userDir()
{
    return appDataDir() + QStringLiteral("/snippets");
}

QString SnippetStore::languageKey(const QString &language)
{
    static const QHash<QString, QString> keys = {
        {QStringLiteral("C++"), QStringLiteral("cpp")}, {QStringLiteral("C"), QStringLiteral("cpp")},
        {QStringLiteral("Python"), QStringLiteral("python")}, {QStringLiteral("QML"), QStringLiteral("qml")},
        {QStringLiteral("JSON"), QStringLiteral("json")}, {QStringLiteral("Markdown"), QStringLiteral("markdown")},
    };
    return keys.value(language, QStringLiteral("text"));
}

void SnippetStore::reload()
{
    m_user.clear();
    const QFileInfoList files = QDir(userDir()).entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    for (const QFileInfo &file : files)
        loadFile(file.filePath(), m_user, file.completeBaseName());
    // Сохранение заменой файла снимает слежение — пути добавляются заново
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().contains(userDir()))
        m_watcher.addPath(userDir());
    for (const QFileInfo &file : files)
        m_watcher.addPath(file.filePath());
}

void SnippetStore::loadFile(const QString &path, QHash<QString, Set> &into, const QString &key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        emit message(tr("Сниппеты %1: %2 (символ %3)").arg(QFileInfo(path).fileName(), error.errorString()).arg(error.offset));
        return;
    }
    Set &set = into[key];
    const QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject item = it.value().toObject();
        const QJsonValue bodyValue = item.value(QStringLiteral("body"));
        QStringList lines;
        if (bodyValue.isArray()) {
            for (const QJsonValue &line : bodyValue.toArray())
                lines.append(line.toString());
        } else {
            lines.append(bodyValue.toString());
        }
        const QString description = item.value(QStringLiteral("description")).toString(it.key());
        const QJsonValue prefixValue = item.value(QStringLiteral("prefix"));
        const QJsonArray prefixes = prefixValue.isArray() ? prefixValue.toArray() : QJsonArray{prefixValue};
        for (const QJsonValue &prefix : prefixes)
            if (!prefix.toString().isEmpty())
                set.append({prefix.toString(), lines.join(u'\n'), description});
    }
}

SnippetStore::Set SnippetStore::snippetsFor(const QString &language) const
{
    const QString key = languageKey(language);
    Set result;
    QSet<QString> prefixes; // свой сниппет с тем же префиксом заменяет встроенный
    for (const Set &set : {m_user.value(key), m_user.value(kGlobal), m_builtin.value(key), m_builtin.value(kGlobal)}) {
        for (const Snippet &s : set) {
            if (!prefixes.contains(s.prefix)) {
                prefixes.insert(s.prefix);
                result.append(s);
            }
        }
    }
    return result;
}

QVariantList SnippetStore::completions(const QString &language, const QString &prefix) const
{
    QVariantList out;
    if (prefix.isEmpty())
        return out;
    for (const Snippet &s : snippetsFor(language)) {
        if (s.prefix.startsWith(prefix, Qt::CaseInsensitive)) {
            out.append(QVariantMap{{QStringLiteral("label"), s.prefix},
                                   {QStringLiteral("detail"), s.description},
                                   {QStringLiteral("body"), s.body}});
            if (out.size() == kMaxCompletions)
                break;
        }
    }
    return out;
}

bool SnippetStore::expandAtCursor(EditorView *editor) const
{
    if (!editor || editor->readOnly() || editor->cursorCount() != 1 || !editor->selectedText().isEmpty())
        return false;
    const QString word = editor->wordBeforeCursor();
    if (word.isEmpty())
        return false;
    for (const Snippet &s : snippetsFor(editor->language())) {
        if (s.prefix == word) {
            const int column = editor->cursorColumn();
            editor->insertSnippet(s.body, editor->cursorLine(), column - int(word.size()), column);
            return true;
        }
    }
    return false;
}

QString SnippetStore::userFile(const QString &language)
{
    const QString path = userDir() + u'/' + languageKey(language) + QStringLiteral(".json");
    if (!QFileInfo::exists(path)) {
        const QJsonObject example{{tr("Пример: заметка с датой"),
                                   QJsonObject{{QStringLiteral("prefix"), QStringLiteral("todo")},
                                               {QStringLiteral("body"), QJsonArray{QStringLiteral("TODO ($CURRENT_YEAR-$CURRENT_MONTH-$CURRENT_DATE): ${1:что сделать}$0")}},
                                               {QStringLiteral("description"), tr("TODO с сегодняшней датой")}}}};
        writeJsonFile(path, example);
        reload();
    }
    return path;
}
