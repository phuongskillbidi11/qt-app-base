#include "single_instance.h"

#include <QLocalSocket>

namespace {
constexpr int kConnectTimeoutMs = 500;
constexpr char kRaiseMessage[] = "raise";
}

SingleInstance::SingleInstance(const QString &key, QObject *parent)
    : QObject(parent), m_key(key) {
    QLocalSocket probe;
    probe.connectToServer(m_key, QIODevice::WriteOnly);
    if (probe.waitForConnected(kConnectTimeoutMs)) {
        probe.disconnectFromServer();
        return;
    }

    if (!m_server.listen(m_key)) {
        QLocalServer::removeServer(m_key);
        if (!m_server.listen(m_key)) {
            return;
        }
    }

    m_primary = true;
    connect(&m_server, &QLocalServer::newConnection, this, [this]() {
        while (m_server.hasPendingConnections()) {
            QLocalSocket *socket = m_server.nextPendingConnection();
            connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
                if (!socket->readAll().isEmpty()) {
                    emit raiseRequested();
                }
                socket->disconnectFromServer();
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

bool SingleInstance::isPrimary() const {
    return m_primary;
}

bool SingleInstance::signalPrimaryToRaise() {
    QLocalSocket socket;
    socket.connectToServer(m_key, QIODevice::WriteOnly);
    if (!socket.waitForConnected(kConnectTimeoutMs)) {
        return false;
    }
    if (socket.write(kRaiseMessage) < 0 || !socket.waitForBytesWritten(kConnectTimeoutMs)) {
        return false;
    }
    socket.disconnectFromServer();
    return true;
}
