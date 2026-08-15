#include "mq_connection.h"

#include "mq_buffer.h"

#include <QAbstractSocket>

#include <string>

MqConnection::MqConnection(QObject *parent)
    : QObject(parent) {
    heartbeatTimer_.setSingleShot(false);
    connect(&heartbeatTimer_, &QTimer::timeout, this, [this]() {
        if (connection_) {
            connection_->heartbeat();
        }
    });
    connect(&socket_, &QTcpSocket::connected, this, [this]() {
        connection_ = std::make_unique<AMQP::Connection>(
            this,
            AMQP::Login(user_.toStdString(), password_.toStdString()),
            vhost_.toStdString());
    });
    connect(&socket_, &QTcpSocket::readyRead, this, &MqConnection::readSocket);
    connect(&socket_,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this,
            [this](QAbstractSocket::SocketError) {
                error_ = socket_.errorString();
                ready_ = false;
                heartbeatTimer_.stop();
            });
}

MqConnection::~MqConnection() = default;

void MqConnection::connectToHost(const QString &host,
                                 quint16 port,
                                 const QString &vhost,
                                 const QString &user,
                                 const QString &password) {
    heartbeatTimer_.stop();
    connection_.reset();
    inputBuffer_.clear();
    ready_ = false;
    error_.clear();
    vhost_ = vhost;
    user_ = user;
    password_ = password;
    socket_.connectToHost(host, port);
}

AMQP::Connection *MqConnection::connection() const {
    return connection_.get();
}

bool MqConnection::isReady() const {
    return ready_;
}

QString MqConnection::errorString() const {
    return error_;
}

void MqConnection::onData(AMQP::Connection *, const char *buffer, size_t size) {
    socket_.write(buffer, static_cast<qint64>(size));
}

void MqConnection::onReady(AMQP::Connection *) {
    ready_ = true;
    error_.clear();
}

void MqConnection::onError(AMQP::Connection *, const char *message) {
    ready_ = false;
    heartbeatTimer_.stop();
    error_ = QString::fromUtf8(message);
}

void MqConnection::onClosed(AMQP::Connection *) {
    ready_ = false;
    heartbeatTimer_.stop();
    socket_.disconnectFromHost();
}

uint16_t MqConnection::onNegotiate(AMQP::Connection *, uint16_t interval) {
    if (interval == 0) {
        heartbeatTimer_.stop();
    } else {
        heartbeatTimer_.start(static_cast<int>(interval) * 500);
    }
    return interval;
}

void MqConnection::readSocket() {
    MqBuffer::feed(inputBuffer_, socket_.readAll(), [this](const char *data, quint64 size) {
        if (!connection_) {
            return quint64{0};
        }
        return connection_->parse(data, static_cast<size_t>(size));
    });
}
