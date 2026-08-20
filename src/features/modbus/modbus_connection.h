#pragma once

#include "modbus_codec.h"
#include "protocol_driver.h"
#include "connection_state.h"

#include <QByteArray>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
#include <QVector>

#include <cstdint>

// Modbus TCP has no application-level handshake -- "ready" is the TCP connect succeeding,
// one layer shallower than MqConnection's equivalent (which waits for AMQP's onReady(),
// past the TCP connect). Still asynchronous (QTcpSocket::connectToHost() does not resolve
// synchronously), so this still needs ConnectionState's asyncResult path -- see spec.md D2
// of .plans/2026-08-18-modbus-tcp-driver.
//
// readHoldingRegisters()/writeSingleRegister() write one encoded request and buffer incoming
// bytes across possibly-multiple readyRead signals -- TCP has no message boundaries, so a
// response frame can legitimately arrive split. Only one request is ever outstanding at a
// time (pendingRequest_ enforces this across both request types -- see
// .plans/2026-08-20-modbus-write-single-register/spec.md D2), so the whole buffer is cleared
// once a full frame decodes -- not a general-purpose Modbus master's pipelining, which would
// need to know exactly how many bytes one frame consumed and trim only those.
class ModbusConnection : public ProtocolDriver {
    Q_OBJECT
public:
    explicit ModbusConnection(QObject *parent = nullptr);
    ~ModbusConnection() override;

    void connectToHost(const QString &host, quint16 port);
    void disconnectNow();

    // Applies to every future request; does not affect one already in flight. Default is
    // 1000ms if never called — see spec.md D2.
    void setRequestTimeoutMs(int ms);

    // Both no-op (do not write to the socket) unless isReady() and no other request is
    // already outstanding -- see spec.md D2 of
    // .plans/2026-08-20-modbus-write-single-register.
    void readHoldingRegisters(quint8 unitId, quint16 startAddress, quint16 quantity);
    void writeSingleRegister(quint8 unitId, quint16 address, quint16 value);

    bool isReady() const override;
    QString errorString() const override;

signals:
    void holdingRegistersRead(QVector<uint16_t> registers);
    void readFailed(QString error);
    void singleRegisterWritten(quint16 address, quint16 value);
    void writeFailed(QString error);

private:
    enum class PendingRequest { None, ReadHoldingRegisters, WriteSingleRegister };

    void onSocketReadyRead();
    void onRequestTimeout();

    QTcpSocket socket_;
    ConnectionState state_;
    QString error_;
    QByteArray readBuffer_;
    uint16_t nextTransactionId_ = 1;
    PendingRequest pendingRequest_ = PendingRequest::None;
    QTimer requestTimer_;
    int requestTimeoutMs_ = 1000;
};
