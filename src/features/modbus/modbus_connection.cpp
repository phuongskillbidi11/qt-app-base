#include "modbus_connection.h"

#include <QAbstractSocket>

ModbusConnection::ModbusConnection(QObject *parent) : ProtocolDriver(parent) {
    connect(&state_, &ConnectionState::connectionStateChanged,
            this, &ModbusConnection::connectionStateChanged);
    connect(&state_, &ConnectionState::connectionStateChanged, this, [this](bool connected) {
        if (!connected) {
            readBuffer_.clear();
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
}

void ModbusConnection::readHoldingRegisters(quint8 unitId, quint16 startAddress, quint16 quantity) {
    if (!isReady()) {
        return;
    }
    ModbusCodec::ReadHoldingRegistersRequest request;
    request.transactionId = nextTransactionId_++;
    request.unitId = unitId;
    request.startAddress = startAddress;
    request.quantity = quantity;
    socket_.write(ModbusCodec::encodeReadHoldingRegistersRequest(request));
}

void ModbusConnection::onSocketReadyRead() {
    readBuffer_.append(socket_.readAll());
    const ModbusCodec::ReadHoldingRegistersResponse response =
        ModbusCodec::decodeReadHoldingRegistersResponse(readBuffer_);
    if (response.status == ModbusCodec::ResponseStatus::Incomplete) {
        return;   // wait for more bytes
    }
    readBuffer_.clear();
    if (response.status == ModbusCodec::ResponseStatus::Ok) {
        emit holdingRegistersRead(response.registers);
    } else if (response.status == ModbusCodec::ResponseStatus::Exception) {
        emit readFailed(QString("Modbus exception code %1").arg(response.exceptionCode));
    } else {
        emit readFailed("Malformed Modbus response");
    }
}

bool ModbusConnection::isReady() const {
    return state_.isConnected();
}

QString ModbusConnection::errorString() const {
    return error_;
}
