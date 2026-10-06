#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include "src/gui/editerview.h"

// Сниппеты в формате VS Code: {"Имя": {"prefix": "for", "body": [...], "description": "..."}}.
// Встроенные — assets/snippets/<язык>.json, свои — %LOCALAPPDATA%/appMyCodeApp/snippets/
// <язык>.json (тот же префикс заменяет встроенный) и global.json — для всех языков.
// Свои файлы отслеживаются: сохранили — сразу работает.
class SnippetStore : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit SnippetStore(QObject *parent = nullptr);

    // Варианты для списка автодополнения: [{label, detail, body}]
    Q_INVOKABLE QVariantList completions(const QString &language, const QString &prefix) const;
    // Tab: слово перед курсором — префикс сниппета? Тогда развернуть
    Q_INVOKABLE bool expandAtCursor(EditorView *editor) const;
    // Свой файл сниппетов языка (создаётся с примером); путь — открыть во вкладке
    Q_INVOKABLE QString userFile(const QString &language);

signals:
    void message(const QString &text);

private:
    struct Snippet {
        QString prefix;
        QString body;
        QString description;
    };
    using Set = QVector<Snippet>;

    static QString languageKey(const QString &language); // «C++» -> «cpp»
    static QString userDir();
    void reload();
    void loadFile(const QString &path, QHash<QString, Set> &into, const QString &key);
    Set snippetsFor(const QString &language) const;

    QHash<QString, Set> m_builtin;
    QHash<QString, Set> m_user;
    QFileSystemWatcher m_watcher;
};
