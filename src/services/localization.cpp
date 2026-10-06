#include "localization.h"

#include <QCoreApplication>
#include <QLocale>
#include <QQmlEngine>
#include <QTranslator>

#include "appsettings.h"

namespace {

const QString kSettingKey = QStringLiteral("language");
QString g_current = QStringLiteral("ru");

QTranslator &translator()
{
    static QTranslator instance;
    return instance;
}

} // namespace

Localization::Localization(QObject *parent)
    : QObject(parent)
{
}

void Localization::applySavedLanguage()
{
    const QString systemDefault = QLocale::system().language() == QLocale::Russian ? QStringLiteral("ru")
                                                                                    : QStringLiteral("en");
    install(appSetting(kSettingKey, systemDefault).toString());
}

QString Localization::current()
{
    return g_current;
}

void Localization::install(const QString &code)
{
    QCoreApplication::removeTranslator(&translator());
    g_current = QStringLiteral("ru");
    if (code == QStringLiteral("en") && translator().load(QStringLiteral(":/i18n/mycodeapp_en.qm"))) {
        QCoreApplication::installTranslator(&translator());
        g_current = code;
    }
}

QVariantList Localization::languages() const
{
    return {QVariantMap{{QStringLiteral("code"), QStringLiteral("ru")}, {QStringLiteral("name"), QStringLiteral("Русский")}},
            QVariantMap{{QStringLiteral("code"), QStringLiteral("en")}, {QStringLiteral("name"), QStringLiteral("English")}}};
}

void Localization::setLanguage(const QString &code)
{
    if (code == g_current)
        return;
    install(code);
    setAppSetting(kSettingKey, g_current);
    if (QQmlEngine *engine = qmlEngine(this))
        engine->retranslate(); // все qsTr в QML пересчитываются
    emit languageChanged();
}
