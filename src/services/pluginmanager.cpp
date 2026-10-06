#include "pluginmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QLibrary>
#include <QPluginLoader>
#include <QSet>

#include "appsettings.h"
#include "include/mycodeapp/plugin.h"

namespace {

const QString kDisabledKey = QStringLiteral("disabledPlugins");
const QString kIidPrefix = QStringLiteral("org.mycodeapp.Plugin/");

} // namespace

// Хост одного плагина: его команды помечаются его id и убираются при выгрузке
class PluginManager::Host : public mycodeapp::PluginHost {
public:
    Host(PluginManager *manager, QString pluginId)
        : m_manager(manager), m_pluginId(std::move(pluginId)) {}

    void addCommand(const QString &id, const QString &title, const QString &defaultShortcut,
                    std::function<void()> run) override
    {
        const QString key = m_pluginId + u'.' + id;
        if (!m_manager->m_commands.contains(key))
            m_manager->m_commandOrder.append(key);
        m_manager->m_commands.insert(key, {m_pluginId, title, defaultShortcut, std::move(run)});
        emit m_manager->commandsChanged();
    }
    QString currentFilePath() const override { return editor() ? editor()->filePath() : QString(); }
    QString currentText() const override { return editor() ? editor()->text() : QString(); }
    QString selectedText() const override { return editor() ? editor()->selectedText() : QString(); }
    void replaceSelection(const QString &text) override
    {
        if (editor() && !editor()->readOnly())
            editor()->replaceSelection(text);
    }
    void setCurrentText(const QString &text) override
    {
        if (editor() && !editor()->readOnly())
            editor()->setText(text);
    }
    void showMessage(const QString &text) override { emit m_manager->message(text); }

private:
    EditorView *editor() const { return m_manager->m_editor; }

    PluginManager *m_manager;
    QString m_pluginId;
};

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(folders().last());
    rescan();
}

PluginManager::~PluginManager()
{
    for (auto &entry : m_entries)
        unload(*entry);
}

QStringList PluginManager::folders() const
{
    return {QCoreApplication::applicationDirPath() + QStringLiteral("/plugins"),
            appDataDir() + QStringLiteral("/plugins")};
}

void PluginManager::rescan()
{
    for (auto &entry : m_entries)
        unload(*entry);
    m_entries.clear();

    const QStringList disabled = appSetting(kDisabledKey).toStringList();
    QSet<QString> seen;
    for (const QString &folder : folders()) {
        for (const QFileInfo &file : QDir(folder).entryInfoList(QDir::Files, QDir::Name)) {
            if (!QLibrary::isLibrary(file.fileName()))
                continue;
            auto entry = std::make_unique<Entry>();
            entry->path = file.absoluteFilePath();
            // Описание — без загрузки DLL
            const QJsonObject metaData = QPluginLoader(entry->path).metaData();
            const QString iid = metaData.value(QStringLiteral("IID")).toString();
            if (!iid.startsWith(kIidPrefix))
                continue; // чужая библиотека
            entry->meta = metaData.value(QStringLiteral("MetaData")).toObject().toVariantMap();
            entry->id = entry->meta.value(QStringLiteral("id"), file.baseName()).toString();
            if (iid != QLatin1String(MyCodeAppPlugin_iid))
                entry->error = tr("Плагин для другой версии API (%1)").arg(iid.mid(kIidPrefix.size()));
            else if (seen.contains(entry->id))
                entry->error = tr("Плагин с id «%1» уже загружен из другой папки").arg(entry->id);
            seen.insert(entry->id);
            if (entry->error.isEmpty() && !disabled.contains(entry->id))
                load(*entry);
            m_entries.push_back(std::move(entry));
        }
    }
    emit pluginsChanged();
}

bool PluginManager::load(Entry &entry)
{
    entry.error.clear();
    entry.loader = std::make_unique<QPluginLoader>(entry.path);
    auto *plugin = qobject_cast<mycodeapp::Plugin *>(entry.loader->instance());
    if (!plugin) {
        entry.error = entry.loader->errorString();
        entry.loader->unload();
        entry.loader.reset();
        return false;
    }
    entry.host = std::make_unique<Host>(this, entry.id);
    try {
        plugin->activate(entry.host.get());
    } catch (const std::exception &e) {
        entry.error = tr("Ошибка при запуске: %1").arg(QString::fromLocal8Bit(e.what()));
        unload(entry);
        return false;
    }
    return true;
}

void PluginManager::unload(Entry &entry)
{
    if (!entry.loader)
        return;
    if (auto *plugin = qobject_cast<mycodeapp::Plugin *>(entry.loader->instance()))
        plugin->deactivate();
    // Команды держат код плагина — убрать до выгрузки DLL
    for (auto it = m_commands.begin(); it != m_commands.end();) {
        if (it->pluginId == entry.id) {
            m_commandOrder.removeAll(it.key());
            it = m_commands.erase(it);
        } else {
            ++it;
        }
    }
    emit commandsChanged();
    entry.host.reset();
    entry.loader->unload();
    entry.loader.reset();
}

QVariantList PluginManager::plugins() const
{
    const QStringList disabled = appSetting(kDisabledKey).toStringList();
    QVariantList list;
    for (const auto &entry : m_entries) {
        QVariantMap item = entry->meta;
        item.insert(QStringLiteral("id"), entry->id);
        item.insert(QStringLiteral("path"), QDir::toNativeSeparators(entry->path));
        item.insert(QStringLiteral("enabled"), !disabled.contains(entry->id));
        item.insert(QStringLiteral("loaded"), entry->loader != nullptr);
        item.insert(QStringLiteral("error"), entry->error);
        list.append(item);
    }
    return list;
}

QVariantList PluginManager::commands() const
{
    QVariantList list;
    for (const QString &key : m_commandOrder) {
        const CommandEntry &c = m_commands[key];
        list.append(QVariantMap{{QStringLiteral("id"), key},
                                {QStringLiteral("title"), c.title},
                                {QStringLiteral("shortcut"), c.shortcut}});
    }
    return list;
}

void PluginManager::setEnabled(const QString &id, bool enabled)
{
    QStringList disabled = appSetting(kDisabledKey).toStringList();
    disabled.removeAll(id);
    if (!enabled)
        disabled.append(id);
    setAppSetting(kDisabledKey, disabled);

    for (auto &entry : m_entries) {
        if (entry->id != id)
            continue;
        if (enabled && !entry->loader && load(*entry))
            emit message(tr("Плагин «%1» включён").arg(entry->meta.value(QStringLiteral("name"), id).toString()));
        else if (!enabled && entry->loader)
            unload(*entry);
    }
    emit pluginsChanged();
}

void PluginManager::run(const QString &commandId)
{
    const auto it = m_commands.constFind(commandId);
    if (it == m_commands.cend())
        return;
    const std::function<void()> command = it->run; // копия: команда может изменить список
    try {
        command();
    } catch (const std::exception &e) {
        emit message(tr("Плагин: %1").arg(QString::fromLocal8Bit(e.what())));
    }
}
