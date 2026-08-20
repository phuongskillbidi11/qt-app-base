#include "modbus_connection.h"

#include <QAbstractSocket>

ModbusConnection::ModbusConnection(QObject *parent) : ProtocolDriver(parent) {
    connect(&state_, &ConnectionState::connectionStateChanged,
            this, &ModbusConnection::connectionStateChanged);
    connect(&state_, &ConnectionState::connectionStateChanged, this, [this](bool connected) {
        if (!connected) {
            readBuffer_.clear();
            pendingRequest_ = PendingRequest::None;
        }
    });
    connect(&socket_, &QTcpSocket::connected, this, [this]() {
        state_.reportPendingConnectResult(true);
    });
    connect(&socket_,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this,
            [this](QAbstractSocket::SocketError) {
                error_ = socket_.errorString();
                state_.reportPendingConnectResult(false);
            });
    connect(&socket_, &QTcpSocket::readyRead, this, &ModbusConnection::onSocketReadyRead);
}

ModbusConnection::~ModbusConnection() = default;

void ModbusConnection::connectToHost(const QString &host, quint16 port) {
    error_.clear();
    readBuffer_.clear();
    pendingRequest_ = PendingRequest::None;
    state_.setHandlers(
        [this, host, port]() {
            error_.clear();
            socket_.connectToHost(host, port);
            return true;   // launched, not yet resolved -- see the connected/error handlers above
        },
        [this]() { return socket_.state() == QAbstractSocket::ConnectedState; },
        [this]() { socket_.abort(); },
        /*asyncResult=*/true);
    state_.beginAutoConnect();
}

void ModbusConnection::disconnectNow() {
    state_.disconnectNow();
    socket_.abort();
    readBuffer_.clear();
    pendingRequest_ = PendingRequest::None;
}

void ModbusConnection::readHoldingRegisters(quint8 unitId, quint16 startAddress, quint16 quantity) {
    if (!isReady() || pendingRequest_ != PendingRequest::None) {
        return;
    }
    ModbusCodec::ReadHoldingRegistersRequest request;
    request.transactionId = nextTransactionId_++;
    request.unitId = unitId;
    request.startAddress = startAddress;
    request.quantity = quantity;
    pendingRequest_ = PendingRequest::ReadHoldingRegisters;
    socket_.write(ModbusCodec::encodeReadHoldingRegistersRequest(request));
}

void ModbusConnection::writeSingleRegister(quint8 unitId, quint16 address, quint16 value) {
    if (!isReady() || pendingRequest_ != PendingRequest::None) {
        return;
    }
    ModbusCodec::WriteSingleRegisterRequest request;
    request.transactionId = nextTransactionId_++;
    request.unitId = unitId;
    request.address = address;
    request.value = value;
    pendingRequest_ = PendingRequest::WriteSingleRegister;
    socket_.write(ModbusCodec::encodeWriteSingleRegisterRequest(request));
}

void ModbusConnection::onSocketReadyRead() {
    readBuffer_.append(socket_.readAll());

    if (pendingRequest_ == PendingRequest::ReadHoldingRegisters) {
        const ModbusCodec::ReadHoldingRegistersResponse response =
            ModbusCodec::decodeReadHoldingRegistersResponse(readBuffer_);
        if (response.status == ModbusCodec::ResponseStatus::Incomplete) {
            return;   // wait for more bytes
        }
        readBuffer_.clear();
        pendingRequest_ = PendingRequest::None;
        if (response.status == ModbusCodec::ResponseStatus::Ok) {
            emit holdingRegistersRead(response.registers);
        } else if (response.status == ModbusCodec::ResponseStatus::Exception) {
            emit readFailed(QString("Modbus exception code %1").arg(response.exceptionCode));
        } else {
            emit readFailed("Malformed Modbus response");
        }
        return;
    }

    if (pendingRequest_ == PendingRequest::WriteSingleRegister) {
        const ModbusCodec::WriteSingleRegisterResponse response =
            ModbusCodec::decodeWriteSingleRegisterResponse(readBuffer_);
        if (response.status == ModbusCodec::ResponseStatus::Incomplete) {
            return;   // wait for more bytes
        }
        readBuffer_.clear();
        pendingRequest_ = PendingRequest::None;
        if (response.status == ModbusCodec::ResponseStatus::Ok) {
            emit singleRegisterWritten(response.address, response.value);
        } else if (response.status == ModbusCodec::ResponseStatus::Exception) {
            emit writeFailed(QString("Modbus exception code %1").arg(response.exceptionCode));
        } else {
            emit writeFailed("Malformed Modbus response");
        }
        return;
    }

    // Bytes arrived with nothing pending -- not expected by this design (one outstanding
    // request at a time, enforced by both readHoldingRegisters() and writeSingleRegister()).
    // Drop them rather than let them corrupt whatever the next real response turns out to be.
    readBuffer_.clear();
}

bool ModbusConnection::isReady() const {
    return state_.isConnected();
}

QString ModbusConnection::errorString() const {
    return error_;
}
