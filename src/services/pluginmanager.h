#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>
#include <functional>
#include <memory>

#include "src/gui/editerview.h"

class QPluginLoader;

// Плагины: DLL из <папка программы>/plugins и %LOCALAPPDATA%/appMyCodeApp/plugins.
// Описание читается без загрузки; включённые загружаются при старте,
// выключение — выгрузка без перезапуска. Список выключенных — в settings.json.
class PluginManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(EditorView *editor MEMBER m_editor NOTIFY editorChanged)
    // [{id, name, version, description, author, path, enabled, loaded, error}]
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    // [{id, title, shortcut}] — команды загруженных плагинов
    Q_PROPERTY(QVariantList commands READ commands NOTIFY commandsChanged)
    Q_PROPERTY(QStringList folders READ folders CONSTANT)

public:
    explicit PluginManager(QObject *parent = nullptr);
    ~PluginManager() override;

    QVariantList plugins() const;
    QVariantList commands() const;
    QStringList folders() const;

    Q_INVOKABLE void rescan();
    Q_INVOKABLE void setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void run(const QString &commandId);

signals:
    void editorChanged();
    void pluginsChanged();
    void commandsChanged();
    void message(const QString &text);

private:
    class Host;
    struct Entry {
        QString id;
        QString path;
        QVariantMap meta;
        QString error;
        std::unique_ptr<QPluginLoader> loader;
        std::unique_ptr<Host> host;
    };
    struct CommandEntry {
        QString pluginId;
        QString title;
        QString shortcut;
        std::function<void()> run;
    };

    bool load(Entry &entry);
    void unload(Entry &entry);

    QPointer<EditorView> m_editor;
    std::vector<std::unique_ptr<Entry>> m_entries;
    QStringList m_commandOrder;
    QHash<QString, CommandEntry> m_commands; // ключ — «плагин.команда»
};
