#include "singleinstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalSocket>

#include "appsettings.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr int kTimeoutMs = 1000;
}

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
    // Имя канала — своё у каждой папки данных: пользователь, портативная копия, профиль
    const QByteArray user = QCryptographicHash::hash(appDataDir().toUtf8(), QCryptographicHash::Sha1);
    m_name = QStringLiteral("MyCodeApp-") + QString::fromLatin1(user.toHex().left(12));
}

bool SingleInstance::forwardToRunning(const QStringList &paths)
{
    QLocalSocket socket;
    socket.connectToServer(m_name);
    if (!socket.waitForConnected(kTimeoutMs))
        return false;
#ifdef Q_OS_WIN
    // Иначе Windows не даст первому экземпляру выйти на передний план
    AllowSetForegroundWindow(ASFW_ANY);
#endif
    socket.write(paths.join(u'\n').toUtf8());
    socket.flush();
    socket.waitForBytesWritten(kTimeoutMs);
    socket.disconnectFromServer();
    return true;
}

void SingleInstance::listen()
{
    QLocalServer::removeServer(m_name); // канал, оставшийся после сбоя
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.listen(m_name);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            auto *data = new QByteArray;
            connect(socket, &QLocalSocket::readyRead, socket, [socket, data] { data->append(socket->readAll()); });
            connect(socket, &QLocalSocket::disconnected, this, [this, socket, data] {
                data->append(socket->readAll());
                const QString text = QString::fromUtf8(*data);
                delete data;
                socket->deleteLater();
                emit pathsReceived(text.split(u'\n', Qt::SkipEmptyParts));
            });
        }
    });
}
