#pragma once

#include "modbus_codec.h"
#include "protocol_driver.h"
#include "connection_state.h"

#include <QByteArray>
#include <QString>
#include <QTcpSocket>
#include <QVector>

#include <cstdint>

// Modbus TCP has no application-level handshake -- "ready" is the TCP connect succeeding,
// one layer shallower than MqConnection's equivalent (which waits for AMQP's onReady(),
// past the TCP connect). Still asynchronous (QTcpSocket::connectToHost() does not resolve
// synchronously), so this still needs ConnectionState's asyncResult path -- see spec.md D2
// of .plans/2026-08-18-modbus-tcp-driver.
//
// readHoldingRegisters() (added in .plans/2026-08-18-modbus-live-widget's D1) writes one
// encoded request and buffers incoming bytes across possibly-multiple readyRead signals --
// TCP has no message boundaries, so a response frame can legitimately arrive split. This
// class only ever has one outstanding request at a time (its only caller polls on a timer
// and waits for a terminal result before the next tick), so the whole buffer is cleared once
// a full frame decodes -- not a general-purpose Modbus master's pipelining, which would need
// to know exactly how many bytes one frame consumed and trim only those.
class ModbusConnection : public ProtocolDriver {
    Q_OBJECT
public:
    explicit ModbusConnection(QObject *parent = nullptr);
    ~ModbusConnection() override;

    void connectToHost(const QString &host, quint16 port);
    void disconnectNow();

    // No-op (does not write to the socket) unless isReady() -- guards against issuing a
    // read against a socket mid-teardown during a reconnect. See spec.md D1/R2 of
    // .plans/2026-08-18-modbus-live-widget.
    void readHoldingRegisters(quint8 unitId, quint16 startAddress, quint16 quantity);

    bool isReady() const override;
    QString errorString() const override;

signals:
    void holdingRegistersRead(QVector<uint16_t> registers);
    void readFailed(QString error);

private:
    void onSocketReadyRead();

    QTcpSocket socket_;
    ConnectionState state_;
    QString error_;
    QByteArray readBuffer_;
    uint16_t nextTransactionId_ = 1;
};
