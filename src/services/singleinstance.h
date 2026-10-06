#pragma once

#include <QLocalServer>
#include <QObject>
#include <QStringList>

// Один экземпляр на папку данных: повторный запуск (двойной клик по файлу,
// «Открыть с помощью») передаёт пути уже открытому окну и завершается.
class SingleInstance : public QObject {
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);

    // true — экземпляр уже запущен и получил пути: этому процессу пора выйти
    bool forwardToRunning(const QStringList &paths);
    // Стать основным экземпляром: принимать пути от следующих запусков
    void listen();

signals:
    void pathsReceived(const QStringList &paths); // пустой список — просто показать окно

private:
    QString m_name;
    QLocalServer m_server;
};
