#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

// Сочетания клавиш команд меню. Команда объявляет себя (Command.qml) с id,
// названием и сочетанием по умолчанию; переопределения пользователя — в
// %LOCALAPPDATA%/appMyCodeApp/keybindings.json ({"file.save": "Ctrl+Alt+S"},
// пустая строка — без сочетания). Файл можно править руками: изменения
// подхватываются сразу.
class Keymap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // [{id, title, shortcut, defaultShortcut, custom}] по названию
    Q_PROPERTY(QVariantList commands READ commands NOTIFY changed)

public:
    explicit Keymap(QObject *parent = nullptr);

    int revision() const { return m_revision; }
    QVariantList commands() const;

    Q_INVOKABLE void declare(const QString &id, const QString &title, const QString &defaultShortcut);
    Q_INVOKABLE void undeclare(const QString &id) { m_commands.remove(id); }
    Q_INVOKABLE QString sequence(const QString &id, const QString &defaultShortcut) const;
    // Назначить сочетание; у другой команды с тем же сочетанием оно снимается —
    // возвращается её название (или пустая строка)
    Q_INVOKABLE QString assign(const QString &id, const QString &shortcut);
    Q_INVOKABLE void reset(const QString &id);
    Q_INVOKABLE void resetAll();
    // Нажатие -> «Ctrl+Shift+K». Буквы и знаки берутся по физической клавише
    // (скан-коду), поэтому в русской раскладке получается то же, что в английской
    Q_INVOKABLE QString shortcutFromKey(int key, int modifiers, quint32 nativeScanCode) const;

signals:
    void changed();

private:
    struct Command {
        QString title;
        QString defaultShortcut;
    };

    static QString path();
    void load();
    void save();
    QString effective(const QString &id) const;

    QHash<QString, Command> m_commands;
    QHash<QString, QString> m_overrides;
    QFileSystemWatcher m_watcher;
    int m_revision = 0;
};
