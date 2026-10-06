#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

// Язык интерфейса: исходные строки — русские, английский — перевод из
// i18n/mycodeapp_en.ts (встроен в программу). Выбор хранится в settings.json,
// по умолчанию — по языку системы. Переключение — сразу, без перезапуска.
class Localization : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT) // [{code, name}]

public:
    explicit Localization(QObject *parent = nullptr);

    // Из main() до загрузки QML
    static void applySavedLanguage();
    static QString current();

    QString language() const { return current(); }
    QVariantList languages() const;
    Q_INVOKABLE void setLanguage(const QString &code);

signals:
    void languageChanged();

private:
    static void install(const QString &code);
};
