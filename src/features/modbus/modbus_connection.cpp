#include "modbus_connection.h"

#include <QAbstractSocket>

ModbusConnection::ModbusConnection(QObject *parent) : ProtocolDriver(parent) {
    requestTimer_.setSingleShot(true);
    connect(&requestTimer_, &QTimer::timeout, this, &ModbusConnection::onRequestTimeout);
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
    requestTimer_.stop();
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
    requestTimer_.stop();
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
    requestTimer_.start(requestTimeoutMs_);
}

void ModbusConnection::readInputRegisters(quint8 unitId, quint16 startAddress, quint16 quantity) {
    if (!isReady() || pendingRequest_ != PendingRequest::None) {
        return;
    }
    ModbusCodec::ReadInputRegistersRequest request;
    request.transactionId = nextTransactionId_++;
    request.unitId = unitId;
    request.startAddress = startAddress;
    request.quantity = quantity;
    pendingRequest_ = PendingRequest::ReadInputRegisters;
    socket_.write(ModbusCodec::encodeReadInputRegistersRequest(request));
    requestTimer_.start(requestTimeoutMs_);
}

void ModbusConnection::readCoils(quint8 unitId, quint16 startAddress, quint16 quantity) {
    if (!isReady() || pendingRequest_ != PendingRequest::None) {
        return;
    }
    ModbusCodec::ReadCoilsRequest request;
    request.transactionId = nextTransactionId_++;
    request.unitId = unitId;
    request.startAddress = startAddress;
    request.quantity = quantity;
    pendingRequest_ = PendingRequest::ReadCoils;
    socket_.write(ModbusCodec::encodeReadCoilsRequest(request));
    requestTimer_.start(requestTimeoutMs_);
}

void ModbusConnection::readDiscreteInputs(quint8 unitId, quint16 startAddress, quint16 quantity) {
    if (!isReady() || pendingRequest_ != PendingRequest::None) {
        return;
    }
    ModbusCodec::ReadDiscreteInputsRequest request;
    request.transactionId = nextTransactionId_++;
    request.unitId = unitId;
    request.startAddress = startAddress;
    request.quantity = quantity;
    pendingRequest_ = PendingRequest::ReadDiscreteInputs;
    socket_.write(ModbusCodec::encodeReadDiscreteInputsRequest(request));
    requestTimer_.start(requestTimeoutMs_);
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
    requestTimer_.start(requestTimeoutMs_);
}

void ModbusConnection::writeMultipleRegisters(quint8 unitId, quint16 startAddress,
                                               const QVector<quint16> &values) {
    if (!isReady() || pendingRequest_ != PendingRequest::None) {
        return;
    }
    ModbusCodec::WriteMultipleRegistersRequest request;
    request.transactionId = nextTransactionId_++;
    request.unitId = unitId;
    request.startAddress = startAddress;
    request.values = values;
    pendingRequest_ = PendingRequest::WriteMultipleRegisters;
    socket_.write(ModbusCodec::encodeWriteMultipleRegistersRequest(request));
    requestTimer_.start(requestTimeoutMs_);
}

void ModbusConnection::writeSingleCoil(quint8 unitId, quint16 address, bool value) {
    if (!isReady() || pendingRequest_ != PendingRequest::None) {
        return;
    }
    ModbusCodec::WriteSingleCoilRequest request;
    request.transactionId = nextTransactionId_++;
    request.unitId = unitId;
    request.address = address;
    request.value = value;
    pendingRequest_ = PendingRequest::WriteSingleCoil;
    socket_.write(ModbusCodec::encodeWriteSingleCoilRequest(request));
    requestTimer_.start(requestTimeoutMs_);
}

void ModbusConnection::writeMultipleCoils(quint8 unitId, quint16 startAddress,
                                           const QVector<bool> &values) {
    if (!isReady() || pendingRequest_ != PendingRequest::None) {
        return;
    }
    ModbusCodec::WriteMultipleCoilsRequest request;
    request.transactionId = nextTransactionId_++;
    request.unitId = unitId;
    request.startAddress = startAddress;
    request.values = values;
    pendingRequest_ = PendingRequest::WriteMultipleCoils;
    socket_.write(ModbusCodec::encodeWriteMultipleCoilsRequest(request));
    requestTimer_.start(requestTimeoutMs_);
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
        requestTimer_.stop();
        if (response.status == ModbusCodec::ResponseStatus::Ok) {
            emit holdingRegistersRead(response.registers);
        } else if (response.status == ModbusCodec::ResponseStatus::Exception) {
            emit readFailed(QString("Modbus exception code %1").arg(response.exceptionCode));
        } else {
            emit readFailed("Malformed Modbus response");
        }
        return;
    }

    if (pendingRequest_ == PendingRequest::ReadInputRegisters) {
        const ModbusCodec::ReadInputRegistersResponse response =
            ModbusCodec::decodeReadInputRegistersResponse(readBuffer_);
        if (response.status == ModbusCodec::ResponseStatus::Incomplete) {
            return;   // wait for more bytes
        }
        readBuffer_.clear();
        pendingRequest_ = PendingRequest::None;
        requestTimer_.stop();
        if (response.status == ModbusCodec::ResponseStatus::Ok) {
            emit inputRegistersRead(response.registers);
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
        requestTimer_.stop();
        if (response.status == ModbusCodec::ResponseStatus::Ok) {
            emit singleRegisterWritten(response.address, response.value);
        } else if (response.status == ModbusCodec::ResponseStatus::Exception) {
            emit writeFailed(QString("Modbus exception code %1").arg(response.exceptionCode));
        } else {
            emit writeFailed("Malformed Modbus response");
        }
        return;
    }

    if (pendingRequest_ == PendingRequest::WriteMultipleRegisters) {
        const ModbusCodec::WriteMultipleRegistersResponse response =
            ModbusCodec::decodeWriteMultipleRegistersResponse(readBuffer_);
        if (response.status == ModbusCodec::ResponseStatus::Incomplete) {
            return;   // wait for more bytes
        }
        readBuffer_.clear();
        pendingRequest_ = PendingRequest::None;
        requestTimer_.stop();
        if (response.status == ModbusCodec::ResponseStatus::Ok) {
            emit multipleRegistersWritten(response.startAddress, response.quantity);
        } else if (response.status == ModbusCodec::ResponseStatus::Exception) {
            emit multipleWriteFailed(
                QString("Modbus exception code %1").arg(response.exceptionCode));
        } else {
            emit multipleWriteFailed("Malformed Modbus response");
        }
        return;
    }

    if (pendingRequest_ == PendingRequest::ReadCoils) {
        const ModbusCodec::ReadCoilsResponse response =
            ModbusCodec::decodeReadCoilsResponse(readBuffer_);
        if (response.status == ModbusCodec::ResponseStatus::Incomplete) {
            return;
        }
        readBuffer_.clear();
        pendingRequest_ = PendingRequest::None;
        requestTimer_.stop();
        if (response.status == ModbusCodec::ResponseStatus::Ok) {
            emit coilsRead(response.coils);
        } else if (response.status == ModbusCodec::ResponseStatus::Exception) {
            emit readFailed(QString("Modbus exception code %1").arg(response.exceptionCode));
        } else {
            emit readFailed("Malformed Modbus response");
        }
        return;
    }

    if (pendingRequest_ == PendingRequest::ReadDiscreteInputs) {
        const ModbusCodec::ReadDiscreteInputsResponse response =
            ModbusCodec::decodeReadDiscreteInputsResponse(readBuffer_);
        if (response.status == ModbusCodec::ResponseStatus::Incomplete) {
            return;
        }
        readBuffer_.clear();
        pendingRequest_ = PendingRequest::None;
        requestTimer_.stop();
        if (response.status == ModbusCodec::ResponseStatus::Ok) {
            emit discreteInputsRead(response.inputs);
        } else if (response.status == ModbusCodec::ResponseStatus::Exception) {
            emit readFailed(QString("Modbus exception code %1").arg(response.exceptionCode));
        } else {
            emit readFailed("Malformed Modbus response");
        }
        return;
    }

    if (pendingRequest_ == PendingRequest::WriteSingleCoil) {
        const ModbusCodec::WriteSingleCoilResponse response =
            ModbusCodec::decodeWriteSingleCoilResponse(readBuffer_);
        if (response.status == ModbusCodec::ResponseStatus::Incomplete) {
            return;
        }
        readBuffer_.clear();
        pendingRequest_ = PendingRequest::None;
        requestTimer_.stop();
        if (response.status == ModbusCodec::ResponseStatus::Ok) {
            emit singleCoilWritten(response.address, response.value);
        } else if (response.status == ModbusCodec::ResponseStatus::Exception) {
            emit singleCoilWriteFailed(
                QString("Modbus exception code %1").arg(response.exceptionCode));
        } else {
            emit singleCoilWriteFailed("Malformed Modbus response");
        }
        return;
    }

    if (pendingRequest_ == PendingRequest::WriteMultipleCoils) {
        const ModbusCodec::WriteMultipleCoilsResponse response =
            ModbusCodec::decodeWriteMultipleCoilsResponse(readBuffer_);
        if (response.status == ModbusCodec::ResponseStatus::Incomplete) {
            return;
        }
        readBuffer_.clear();
        pendingRequest_ = PendingRequest::None;
        requestTimer_.stop();
        if (response.status == ModbusCodec::ResponseStatus::Ok) {
            emit multipleCoilsWritten(response.startAddress, response.quantity);
        } else if (response.status == ModbusCodec::ResponseStatus::Exception) {
            emit multipleCoilWriteFailed(
                QString("Modbus exception code %1").arg(response.exceptionCode));
        } else {
            emit multipleCoilWriteFailed("Malformed Modbus response");
        }
        return;
    }

    // Bytes arrived with nothing pending -- not expected by this design (one outstanding
    // request at a time, enforced by both readHoldingRegisters() and writeSingleRegister()).
    // Drop them rather than let them corrupt whatever the next real response turns out to be.
    readBuffer_.clear();
}

void ModbusConnection::setRequestTimeoutMs(int ms) {
    requestTimeoutMs_ = ms;
}

void ModbusConnection::onRequestTimeout() {
    const PendingRequest timedOut = pendingRequest_;
    pendingRequest_ = PendingRequest::None;
    readBuffer_.clear();
    if (timedOut == PendingRequest::ReadHoldingRegisters
        || timedOut == PendingRequest::ReadInputRegisters
        || timedOut == PendingRequest::ReadCoils
        || timedOut == PendingRequest::ReadDiscreteInputs) {
        emit readFailed("timed out waiting for a response");
    } else if (timedOut == PendingRequest::WriteSingleRegister) {
        emit writeFailed("timed out waiting for a response");
    } else if (timedOut == PendingRequest::WriteMultipleRegisters) {
        emit multipleWriteFailed("timed out waiting for a response");
    } else if (timedOut == PendingRequest::WriteSingleCoil) {
        emit singleCoilWriteFailed("timed out waiting for a response");
    } else if (timedOut == PendingRequest::WriteMultipleCoils) {
        emit multipleCoilWriteFailed("timed out waiting for a response");
    }
}

bool ModbusConnection::isReady() const {
    return state_.isConnected();
}

QString ModbusConnection::errorString() const {
    return error_;
}
