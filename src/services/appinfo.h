#pragma once

#include <QCoreApplication>
#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

#include "appsettings.h"

// Сведения для окна «О программе»
class AppInfo : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(bool portable READ portable CONSTANT)

public:
    using QObject::QObject;

    QString version() const { return QCoreApplication::applicationVersion(); }
    QString qtVersion() const { return QString::fromLatin1(qVersion()); }
    QString dataDir() const { return appDataDir(); }
    bool portable() const { return isPortable(); }
};
