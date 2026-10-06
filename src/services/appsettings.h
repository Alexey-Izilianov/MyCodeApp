#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariant>

// Папка данных (настройки, сессии, журналы, темы, сниппеты, плагины):
// %LOCALAPPDATA%/appMyCodeApp; портативный режим — папка data рядом с exe
// (достаточно её создать); профиль — подпапка profiles/<имя>.
void initAppDataDir(const QString &profile);
QString appDataDir();
bool isPortable();

QJsonObject readJsonFile(const QString &path);
bool writeJsonFile(const QString &path, const QJsonObject &object);

// settings.json — плоский объект «ключ: значение»
QVariant appSetting(const QString &key, const QVariant &fallback = {});
void setAppSetting(const QString &key, const QVariant &value);
