#pragma once

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

// Проводник Windows: MyCodeApp в «Открыть с помощью» для текстовых и исходных
// файлов и «Открыть в MyCodeApp» в меню папок. Пишется в HKCU — права
// администратора не нужны; путь к exe берётся текущий.
class ShellIntegration : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool supported READ isSupported CONSTANT)
    Q_PROPERTY(bool registered READ isRegistered NOTIFY registeredChanged)

public:
    explicit ShellIntegration(QObject *parent = nullptr);

    bool isSupported() const;
    // Зарегистрировано именно для этого exe (после переноса программы — нет)
    bool isRegistered() const;

    Q_INVOKABLE bool setRegistered(bool registered);

signals:
    void registeredChanged();
};
