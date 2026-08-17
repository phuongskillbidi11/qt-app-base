#pragma once

#include "connection_state.h"

#include <amqpcpp.h>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>

#include <memory>

class MqConnection : public QObject, public AMQP::ConnectionHandler {
    Q_OBJECT

public:
    explicit MqConnection(QObject *parent = nullptr);
    ~MqConnection() override;

    void connectToHost(const QString &host,
                       quint16 port,
                       const QString &vhost,
                       const QString &user,
                       const QString &password);

    AMQP::Connection *connection() const;
    bool isReady() const;
    QString errorString() const;

signals:
    void connectionStateChanged(bool connected);

protected:
    void onData(AMQP::Connection *connection, const char *buffer, size_t size) override;
    void onReady(AMQP::Connection *connection) override;
    void onError(AMQP::Connection *connection, const char *message) override;
    void onClosed(AMQP::Connection *connection) override;
    uint16_t onNegotiate(AMQP::Connection *connection, uint16_t interval) override;

private:
    void readSocket();

    QTcpSocket socket_;
    QTimer heartbeatTimer_;
    ConnectionState state_;
    QByteArray inputBuffer_;
    std::unique_ptr<AMQP::Connection> connection_;
    QString host_;
    QString vhost_;
    QString user_;
    QString password_;
    QString error_;
    quint16 port_ = 0;
    bool ready_ = false;
    bool connectAttemptPending_ = false;
};
