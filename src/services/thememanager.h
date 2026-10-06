#pragma once

#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// Темы: встроенные (assets/themes) и свои — JSON в %LOCALAPPDATA%/appMyCodeApp/themes.
// Своя тема может задавать не все цвета: остальные берутся из "base"
// (dark или light). Файл текущей темы отслеживается — правка видна сразу.
// Цвета интерфейса — свойство ui (Theme.qml), редактора — Palette.
class ThemeManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantMap ui READ ui NOTIFY changed)
    Q_PROPERTY(QString current READ current NOTIFY changed)
    Q_PROPERTY(QVariantList themes READ themes NOTIFY themesChanged) // [{id, name, nameEn, user}]

public:
    explicit ThemeManager(QObject *parent = nullptr);

    QVariantMap ui() const { return m_ui; }
    QString current() const { return m_current; }
    QVariantList themes() const { return m_themes; }

    Q_INVOKABLE void setTheme(const QString &id);
    // Копия текущей темы в папке пользователя; возвращает путь — открыть и править
    Q_INVOKABLE QString createUserTheme();

signals:
    void changed();
    void themesChanged();
    void message(const QString &text);

private:
    static QString userDir();
    QString pathOf(const QString &id) const;
    void scan();
    bool apply(const QString &id, QString *error);
    void watchCurrent();

    QString m_current;
    QVariantMap m_ui;
    QVariantList m_themes;
    QFileSystemWatcher m_watcher;
    QTimer m_reload; // редакторы пишут файл в несколько шагов — ждём паузы
};
