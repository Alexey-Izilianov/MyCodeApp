#include "appsettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

namespace {

QString g_dataDir;
bool g_portable = false;

QString settingsPath()
{
    return appDataDir() + QStringLiteral("/settings.json");
}

} // namespace

void initAppDataDir(const QString &profile)
{
    const QString portable = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
    g_portable = QFileInfo(portable).isDir();
    g_dataDir = g_portable ? portable : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!profile.isEmpty())
        g_dataDir += QStringLiteral("/profiles/") + profile;
    QDir().mkpath(g_dataDir);
}

QString appDataDir()
{
    if (g_dataDir.isEmpty()) // тесты и вызовы до main
        return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return g_dataDir;
}

bool isPortable()
{
    return g_portable;
}

QJsonObject readJsonFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

bool writeJsonFile(const QString &path, const QJsonObject &object)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(object).toJson()) >= 0;
}

QVariant appSetting(const QString &key, const QVariant &fallback)
{
    const QJsonValue value = readJsonFile(settingsPath()).value(key);
    return value.isUndefined() ? fallback : value.toVariant();
}

void setAppSetting(const QString &key, const QVariant &value)
{
    QJsonObject settings = readJsonFile(settingsPath());
    settings.insert(key, QJsonValue::fromVariant(value));
    writeJsonFile(settingsPath(), settings);
}
