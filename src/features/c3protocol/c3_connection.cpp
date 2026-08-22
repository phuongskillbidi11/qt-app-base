#include "c3_connection.h"

#include <QAbstractSocket>

namespace {
constexpr int kHandshakeTimeoutMs = 3000;
}

C3Connection::C3Connection(QObject *parent) : ProtocolDriver(parent) {
    connect(&state_, &ConnectionState::connectionStateChanged,
            this, &C3Connection::connectionStateChanged);
    connect(&socket_, &QTcpSocket::connected, this, &C3Connection::onSocketConnected);
    connect(&socket_,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &C3Connection::onSocketError);
    connect(&socket_, &QTcpSocket::readyRead, this, &C3Connection::onSocketReadyRead);
    handshakeTimer_.setSingleShot(true);
    connect(&handshakeTimer_, &QTimer::timeout, this, &C3Connection::onHandshakeTimeout);
    dataQueryTimer_.setSingleShot(true);
    connect(&dataQueryTimer_, &QTimer::timeout, this, &C3Connection::onDataQueryTimeout);
    countQueryTimer_.setSingleShot(true);
    connect(&countQueryTimer_, &QTimer::timeout, this, &C3Connection::onCountQueryTimeout);
    rtLogQueryTimer_.setSingleShot(true);
    connect(&rtLogQueryTimer_, &QTimer::timeout, this, &C3Connection::onRtLogQueryTimeout);
    controlTimer_.setSingleShot(true);
    connect(&controlTimer_, &QTimer::timeout, this, &C3Connection::onControlTimeout);
    getParamTimer_.setSingleShot(true);
    connect(&getParamTimer_, &QTimer::timeout, this, &C3Connection::onGetParamTimeout);
    setParamTimer_.setSingleShot(true);
    connect(&setParamTimer_, &QTimer::timeout, this, &C3Connection::onSetParamTimeout);
}

C3Connection::~C3Connection() = default;

void C3Connection::connectToHost(const QString &host, quint16 port) {
    error_.clear();
    state_.setHandlers(
        [this, host, port]() {
            error_.clear();
            readBuffer_.clear();
            connectAttemptPending_ = true;
            socket_.connectToHost(host, port);
            return true;   // launched, not yet resolved
        },
        [this]() { return (sessionId_ != 0 || sessionLess_) && !connectAttemptPending_ && socket_.state() == QAbstractSocket::ConnectedState; },
        [this]() { socket_.abort(); handshakeTimer_.stop(); },
        /*asyncResult=*/true);
    state_.beginAutoConnect();
}

void C3Connection::onSocketConnected() {
    requestNr_ = C3Codec::kPreSessionRequestNr;
    handshakeStep_ = HandshakeStep::AwaitingSessionReply;
    readBuffer_.clear();
    QByteArray frame = C3Codec::encodeConnectSessionRequest({});
    socket_.write(frame);
    handshakeTimer_.start(kHandshakeTimeoutMs);
}

void C3Connection::onSocketError() {
    error_ = socket_.errorString();
    handshakeTimer_.stop();
    dataQueryPending_ = false;
    dataQueryStep_ = DataQueryStep::None;
    dataQueryTimer_.stop();
    countQueryPending_ = false;
    countQueryStep_ = CountQueryStep::None;
    countQueryTimer_.stop();
    rtLogQueryPending_ = false;
    rtLogQueryStep_ = RtLogQueryStep::None;
    rtLogQueryTimer_.stop();
    controlPending_ = false;
    controlTimer_.stop();
    getParamPending_ = false;
    getParamTimer_.stop();
    setParamPending_ = false;
    setParamTimer_.stop();
    if (connectAttemptPending_) {
        connectAttemptPending_ = false;
        handshakeStep_ = HandshakeStep::None;
        emit sessionFailed(error_);
        state_.reportPendingConnectResult(false);
    }
}

void C3Connection::onHandshakeTimeout() {
    error_ = QStringLiteral("C3 session handshake timed out");
    socket_.abort();
    if (connectAttemptPending_) {
        connectAttemptPending_ = false;
        handshakeStep_ = HandshakeStep::None;
        emit sessionFailed(error_);
        state_.reportPendingConnectResult(false);
    }
}

void C3Connection::onSocketReadyRead() {
    if (connectAttemptPending_) {
        readBuffer_.append(socket_.readAll());

        if (handshakeStep_ == HandshakeStep::AwaitingSessionReply) {
            const C3Codec::ConnectSessionResponse response =
                C3Codec::decodeConnectSessionResponse(readBuffer_);
            if (response.status == C3Codec::ResponseStatus::Incomplete) return;
            readBuffer_.clear();
            if (response.status == C3Codec::ResponseStatus::Ok) {
                handshakeTimer_.stop();
                connectAttemptPending_ = false;
                sessionId_ = response.sessionId;
                sessionLess_ = false;
                requestNr_ = C3Codec::kPreSessionRequestNr + 1;
                error_.clear();
                emit sessionEstablished(sessionId_);
                state_.reportPendingConnectResult(true);
            } else if (response.status == C3Codec::ResponseStatus::Rejected) {
                // Real panels have been confirmed to reject session-based connect outright --
                // fall back to session-less, matching zkaccess-c3-py's own connect() behavior.
                // See tests.md's F1 result.
                handshakeStep_ = HandshakeStep::AwaitingSessionLessReply;
                socket_.write(C3Codec::encodeConnectSessionLessRequest({}));
                handshakeTimer_.start(kHandshakeTimeoutMs);
            } else {
                handshakeTimer_.stop();
                connectAttemptPending_ = false;
                error_ = QStringLiteral("C3 session handshake malformed reply");
                emit sessionFailed(error_);
                state_.reportPendingConnectResult(false);
            }
        } else if (handshakeStep_ == HandshakeStep::AwaitingSessionLessReply) {
            const C3Codec::GenericReply response = C3Codec::decodeGenericReply(readBuffer_);
            if (response.status == C3Codec::ResponseStatus::Incomplete) return;
            handshakeTimer_.stop();
            readBuffer_.clear();
            connectAttemptPending_ = false;
            if (response.status == C3Codec::ResponseStatus::Ok) {
                sessionId_ = 0;
                sessionLess_ = true;
                error_.clear();
                emit sessionEstablished(0);   // 0 means "connected, session-less" -- no real ID exists
                state_.reportPendingConnectResult(true);
            } else {
                error_ = QStringLiteral("C3 session-less handshake rejected or malformed");
                emit sessionFailed(error_);
                state_.reportPendingConnectResult(false);
            }
        }
        return;
    }

    if (dataQueryPending_) {
        readBuffer_.append(socket_.readAll());
        if (dataQueryStep_ == DataQueryStep::AwaitingTableConfig) {
            const C3Codec::DataTableConfigResponse response =
                C3Codec::decodeDataTableConfigResponse(readBuffer_, !sessionLess_);
            if (response.status == C3Codec::ResponseStatus::Incomplete) return;
            dataQueryTimer_.stop();
            readBuffer_.clear();
            if (response.status != C3Codec::ResponseStatus::Ok) {
                dataQueryPending_ = false;
                dataQueryStep_ = DataQueryStep::None;
                emit tableDataFailed(pendingTableName_, QStringLiteral("failed to fetch DATATABLE_CFG"));
                return;
            }
            const C3Codec::DataTableConfig *matchedTable = nullptr;
            for (const auto &table : response.tables) {
                if (table.name == pendingTableName_) { matchedTable = &table; break; }
            }
            if (!matchedTable) {
                dataQueryPending_ = false;
                dataQueryStep_ = DataQueryStep::None;
                emit tableDataFailed(pendingTableName_,
                                     QStringLiteral("panel has no '%1' table").arg(pendingTableName_));
                return;
            }
            tableConfig_ = *matchedTable;
            QVector<int> fieldIndexes;
            for (const auto &fld : tableConfig_.fields) fieldIndexes.append(fld.index);
            dataQueryStep_ = DataQueryStep::AwaitingTableData;
            socket_.write(C3Codec::encodeGetDataRequest(
                tableConfig_.index, fieldIndexes, !sessionLess_, sessionId_, requestNr_));
            if (!sessionLess_) ++requestNr_;
            dataQueryTimer_.start(kHandshakeTimeoutMs);
        } else if (dataQueryStep_ == DataQueryStep::AwaitingTableData) {
            const C3Codec::GetDataResponse response = C3Codec::decodeGetDataResponse(
                readBuffer_, tableConfig_.index, tableConfig_.fields, !sessionLess_);
            if (response.status == C3Codec::GetDataStatus::Incomplete) return;
            dataQueryTimer_.stop();
            readBuffer_.clear();
            if (response.status == C3Codec::GetDataStatus::BigDataPending) {
                if (response.prepareInfo.compressed) {
                    dataQueryPending_ = false;
                    dataQueryStep_ = DataQueryStep::None;
                    emit tableDataFailed(
                        pendingTableName_,
                        QStringLiteral("'%1' table reply is compressed -- not supported")
                            .arg(pendingTableName_));
                    return;
                }
                bigDataBuffer_.clear();
                bigDataExpectedLength_ = response.prepareInfo.dataLength;
                dataQueryStep_ = DataQueryStep::AwaitingBigDataChunk;
                socket_.write(C3Codec::encodeTransmitDataRequest(0, !sessionLess_, sessionId_,
                                                                  requestNr_));
                if (!sessionLess_) ++requestNr_;
                dataQueryTimer_.start(kHandshakeTimeoutMs);
                return;
            }
            dataQueryPending_ = false;
            dataQueryStep_ = DataQueryStep::None;
            if (response.status == C3Codec::GetDataStatus::Ok) {
                emit tableDataReceived(pendingTableName_, response.records);
            } else {
                emit tableDataFailed(pendingTableName_,
                                     QStringLiteral("failed to fetch '%1' table GETDATA").arg(pendingTableName_));
            }
        } else if (dataQueryStep_ == DataQueryStep::AwaitingBigDataChunk) {
            const C3Codec::TransmitDataResponse response =
                C3Codec::decodeTransmitDataResponse(readBuffer_, !sessionLess_);
            if (response.status == C3Codec::ResponseStatus::Incomplete) return;
            dataQueryTimer_.stop();
            readBuffer_.clear();
            if (response.status != C3Codec::ResponseStatus::Ok ||
                response.offset != static_cast<uint32_t>(bigDataBuffer_.size())) {
                dataQueryPending_ = false;
                dataQueryStep_ = DataQueryStep::None;
                emit tableDataFailed(pendingTableName_,
                                     QStringLiteral("failed to fetch '%1' table big-data chunk")
                                         .arg(pendingTableName_));
                return;
            }
            bigDataBuffer_.append(response.chunkData);
            if (static_cast<uint32_t>(bigDataBuffer_.size()) < bigDataExpectedLength_) {
                dataQueryStep_ = DataQueryStep::AwaitingBigDataChunk;
                socket_.write(C3Codec::encodeTransmitDataRequest(
                    static_cast<uint32_t>(bigDataBuffer_.size()), !sessionLess_, sessionId_,
                    requestNr_));
                if (!sessionLess_) ++requestNr_;
                dataQueryTimer_.start(kHandshakeTimeoutMs);
                return;
            }
            dataQueryStep_ = DataQueryStep::AwaitingFreeDataAck;
            socket_.write(C3Codec::encodeFreeDataRequest(!sessionLess_, sessionId_, requestNr_));
            if (!sessionLess_) ++requestNr_;
            dataQueryTimer_.start(kHandshakeTimeoutMs);
        } else if (dataQueryStep_ == DataQueryStep::AwaitingFreeDataAck) {
            const C3Codec::GenericReply response = C3Codec::decodeGenericReply(readBuffer_);
            if (response.status == C3Codec::ResponseStatus::Incomplete) return;
            dataQueryTimer_.stop();
            readBuffer_.clear();
            dataQueryPending_ = false;
            dataQueryStep_ = DataQueryStep::None;
            const C3Codec::GetDataResponse parsed = C3Codec::parseGetDataPayload(
                bigDataBuffer_, tableConfig_.index, tableConfig_.fields);
            bigDataBuffer_.clear();
            if (parsed.status == C3Codec::GetDataStatus::Ok) {
                emit tableDataReceived(pendingTableName_, parsed.records);
            } else {
                emit tableDataFailed(pendingTableName_,
                                     QStringLiteral("failed to parse assembled '%1' table data")
                                         .arg(pendingTableName_));
            }
        }
    }

    if (rtLogQueryPending_) {
        readBuffer_.append(socket_.readAll());
        if (rtLogQueryStep_ == RtLogQueryStep::AwaitingBinaryReply) {
            const C3Codec::RtLogBinaryResponse response =
                C3Codec::decodeRtlogBinaryResponse(readBuffer_, !sessionLess_);
            if (response.status == C3Codec::RtLogStatus::Incomplete) return;
            rtLogQueryTimer_.stop();
            readBuffer_.clear();
            rtLogQueryPending_ = false;
            rtLogQueryStep_ = RtLogQueryStep::None;
            if (response.status == C3Codec::RtLogStatus::Ok) {
                emit realtimeLogReceived(response.records);
            } else if (response.status == C3Codec::RtLogStatus::NotBinaryMode) {
                // Matches zkaccess-c3-py's own behavior exactly: switch mode for future calls,
                // report an empty result this call rather than an error -- this is not a
                // failure, the panel just told us which mode to use from now on.
                rtlogCommand_ = C3Codec::kCommandRtlogKeyValue;
                emit realtimeLogReceived(QVector<C3Codec::RtLogRecord>());
            } else {
                emit realtimeLogFailed(QStringLiteral("failed to fetch RT log (binary)"));
            }
        } else if (rtLogQueryStep_ == RtLogQueryStep::AwaitingKeyValueReply) {
            const C3Codec::RtLogKeyValueResponse response =
                C3Codec::decodeRtlogKeyValueResponse(readBuffer_, !sessionLess_);
            if (response.status == C3Codec::ResponseStatus::Incomplete) return;
            rtLogQueryTimer_.stop();
            readBuffer_.clear();
            rtLogQueryPending_ = false;
            rtLogQueryStep_ = RtLogQueryStep::None;
            if (response.status == C3Codec::ResponseStatus::Ok) {
                emit realtimeLogKeyValueReceived(response.records);
            } else {
                emit realtimeLogFailed(QStringLiteral("failed to fetch RT log (key/value)"));
            }
        }
        return;
    }

    if (controlPending_) {
        readBuffer_.append(socket_.readAll());
        const C3Codec::GenericReply response = C3Codec::decodeGenericReply(readBuffer_);
        if (response.status == C3Codec::ResponseStatus::Incomplete) return;
        controlTimer_.stop();
        readBuffer_.clear();
        controlPending_ = false;
        if (response.status == C3Codec::ResponseStatus::Ok) {
            emit controlAcknowledged();
        } else {
            emit controlFailed(QStringLiteral("CONTROL command failed"));
        }
        return;
    }

    if (getParamPending_) {
        readBuffer_.append(socket_.readAll());
        const C3Codec::GetParamResponse response =
            C3Codec::decodeGetParamResponse(readBuffer_, !sessionLess_);
        if (response.status == C3Codec::ResponseStatus::Incomplete) return;
        getParamTimer_.stop();
        readBuffer_.clear();
        getParamPending_ = false;
        if (response.status == C3Codec::ResponseStatus::Ok) {
            emit deviceParamsReceived(response.values);
        } else {
            emit deviceParamsFailed(QStringLiteral("GETPARAM request failed"));
        }
        return;
    }
    if (setParamPending_) {
        readBuffer_.append(socket_.readAll());
        const C3Codec::GenericReply reply = C3Codec::decodeGenericReply(readBuffer_);
        if (reply.status == C3Codec::ResponseStatus::Incomplete) return;
        setParamTimer_.stop();
        readBuffer_.clear();
        setParamPending_ = false;
        if (reply.status == C3Codec::ResponseStatus::Ok) {
            emit setParamsAcknowledged();
        } else {
            emit setParamsFailed(QStringLiteral("SETPARAM request failed"));
        }
        return;
    }
    if (countQueryPending_) {
        readBuffer_.append(socket_.readAll());
        if (countQueryStep_ == CountQueryStep::AwaitingTableConfig) {
            const C3Codec::DataTableConfigResponse response =
                C3Codec::decodeDataTableConfigResponse(readBuffer_, !sessionLess_);
            if (response.status == C3Codec::ResponseStatus::Incomplete) return;
            countQueryTimer_.stop();
            readBuffer_.clear();
            if (response.status != C3Codec::ResponseStatus::Ok) {
                countQueryPending_ = false;
                countQueryStep_ = CountQueryStep::None;
                emit tableRecordCountFailed(pendingCountTableName_,
                                            QStringLiteral("failed to fetch DATATABLE_CFG"));
                return;
            }
            const C3Codec::DataTableConfig *matchedTable = nullptr;
            for (const auto &table : response.tables) {
                if (table.name == pendingCountTableName_) { matchedTable = &table; break; }
            }
            if (!matchedTable) {
                countQueryPending_ = false;
                countQueryStep_ = CountQueryStep::None;
                emit tableRecordCountFailed(
                    pendingCountTableName_,
                    QStringLiteral("panel has no '%1' table").arg(pendingCountTableName_));
                return;
            }
            countTableIndex_ = matchedTable->index;
            countQueryStep_ = CountQueryStep::AwaitingCount;
            socket_.write(C3Codec::encodeGetDataCountRequest(countTableIndex_, !sessionLess_,
                                                              sessionId_, requestNr_));
            if (!sessionLess_) ++requestNr_;
            countQueryTimer_.start(kHandshakeTimeoutMs);
        } else if (countQueryStep_ == CountQueryStep::AwaitingCount) {
            const C3Codec::GetDataCountResponse response =
                C3Codec::decodeGetDataCountResponse(readBuffer_, !sessionLess_);
            if (response.status == C3Codec::ResponseStatus::Incomplete) return;
            countQueryTimer_.stop();
            readBuffer_.clear();
            countQueryPending_ = false;
            countQueryStep_ = CountQueryStep::None;
            if (response.status == C3Codec::ResponseStatus::Ok) {
                emit tableRecordCountReceived(pendingCountTableName_, response.count);
            } else {
                emit tableRecordCountFailed(
                    pendingCountTableName_,
                    QStringLiteral("failed to fetch '%1' table GETDATACOUNT")
                        .arg(pendingCountTableName_));
            }
        }
        return;
    }
}

void C3Connection::requestTableData(const QString &tableName) {
    if (!isReady() || dataQueryPending_) return;
    dataQueryPending_ = true;
    dataQueryStep_ = DataQueryStep::AwaitingTableConfig;
    pendingTableName_ = tableName;
    readBuffer_.clear();
    socket_.write(C3Codec::encodeDataTableConfigRequest(!sessionLess_, sessionId_, requestNr_));
    if (!sessionLess_) ++requestNr_;
    dataQueryTimer_.start(kHandshakeTimeoutMs);
}

void C3Connection::onDataQueryTimeout() {
    dataQueryPending_ = false;
    dataQueryStep_ = DataQueryStep::None;
    emit tableDataFailed(pendingTableName_, QStringLiteral("table data query timed out"));
}

void C3Connection::requestTableRecordCount(const QString &tableName) {
    if (!isReady() || countQueryPending_) return;
    countQueryPending_ = true;
    countQueryStep_ = CountQueryStep::AwaitingTableConfig;
    pendingCountTableName_ = tableName;
    readBuffer_.clear();
    socket_.write(C3Codec::encodeDataTableConfigRequest(!sessionLess_, sessionId_, requestNr_));
    if (!sessionLess_) ++requestNr_;
    countQueryTimer_.start(kHandshakeTimeoutMs);
}

void C3Connection::onCountQueryTimeout() {
    countQueryPending_ = false;
    countQueryStep_ = CountQueryStep::None;
    emit tableRecordCountFailed(pendingCountTableName_,
                                QStringLiteral("table record count query timed out"));
}

void C3Connection::requestRealtimeLog() {
    if (!isReady() || rtLogQueryPending_) return;
    rtLogQueryPending_ = true;
    readBuffer_.clear();
    if (rtlogCommand_ == C3Codec::kCommandRtlogBinary) {
        rtLogQueryStep_ = RtLogQueryStep::AwaitingBinaryReply;
        socket_.write(C3Codec::encodeRtlogBinaryRequest(!sessionLess_, sessionId_, requestNr_));
    } else {
        rtLogQueryStep_ = RtLogQueryStep::AwaitingKeyValueReply;
        socket_.write(C3Codec::encodeRtlogKeyValueRequest(!sessionLess_, sessionId_, requestNr_));
    }
    if (!sessionLess_) ++requestNr_;
    rtLogQueryTimer_.start(kHandshakeTimeoutMs);
}

void C3Connection::onRtLogQueryTimeout() {
    rtLogQueryPending_ = false;
    rtLogQueryStep_ = RtLogQueryStep::None;
    emit realtimeLogFailed(QStringLiteral("RT log query timed out"));
}

void C3Connection::sendControlRequest(C3Codec::ControlOperation operation, uint8_t param1,
                                      uint8_t param2, uint8_t param3) {
    if (!isReady() || controlPending_) return;
    controlPending_ = true;
    readBuffer_.clear();
    socket_.write(C3Codec::encodeControlRequest(operation, param1, param2, param3,
                                                !sessionLess_, sessionId_, requestNr_));
    if (!sessionLess_) ++requestNr_;
    controlTimer_.start(kHandshakeTimeoutMs);
}

void C3Connection::controlDoorOutput(uint8_t doorNumber, uint8_t durationSeconds) {
    sendControlRequest(C3Codec::ControlOperation::Output, doorNumber,
                       static_cast<uint8_t>(C3Codec::ControlOutputAddress::Door),
                       durationSeconds);
}

void C3Connection::controlDoorOutputNormalOpen(uint8_t doorNumber) {
    sendControlRequest(C3Codec::ControlOperation::Output, doorNumber,
                       static_cast<uint8_t>(C3Codec::ControlOutputAddress::Door), 255);
}

void C3Connection::controlDoorClose(uint8_t doorNumber) {
    sendControlRequest(C3Codec::ControlOperation::Output, doorNumber,
                       static_cast<uint8_t>(C3Codec::ControlOutputAddress::Door), 0);
}

void C3Connection::controlAuxOutput(uint8_t auxNumber, uint8_t durationSeconds) {
    sendControlRequest(C3Codec::ControlOperation::Output, auxNumber,
                       static_cast<uint8_t>(C3Codec::ControlOutputAddress::Aux),
                       durationSeconds);
}

void C3Connection::cancelAlarm() {
    sendControlRequest(C3Codec::ControlOperation::CancelAlarm, 0, 0, 0);
}

void C3Connection::restartDevice() {
    sendControlRequest(C3Codec::ControlOperation::RestartDevice, 0, 0, 0);
}

void C3Connection::setNormalOpenState(uint8_t doorNumber, bool enabled) {
    sendControlRequest(C3Codec::ControlOperation::EnableDisableNormalOpen, doorNumber,
                       enabled ? 1 : 0, 0);
}

void C3Connection::onControlTimeout() {
    controlPending_ = false;
    emit controlFailed(QStringLiteral("CONTROL command timed out"));
}

void C3Connection::requestDeviceParams(const QStringList &parameterNames) {
    if (!isReady() || getParamPending_) return;
    getParamPending_ = true;
    readBuffer_.clear();
    socket_.write(C3Codec::encodeGetParamRequest(parameterNames, !sessionLess_, sessionId_,
                                                 requestNr_));
    if (!sessionLess_) ++requestNr_;
    getParamTimer_.start(kHandshakeTimeoutMs);
}

void C3Connection::onGetParamTimeout() {
    getParamPending_ = false;
    emit deviceParamsFailed(QStringLiteral("GETPARAM request timed out"));
}

void C3Connection::setDeviceParams(const QVariantMap &values) {
    if (!isReady() || setParamPending_) return;
    setParamPending_ = true;
    readBuffer_.clear();
    socket_.write(C3Codec::encodeSetParamRequest(values, !sessionLess_, sessionId_,
                                                  requestNr_));
    if (!sessionLess_) ++requestNr_;
    setParamTimer_.start(kHandshakeTimeoutMs);
}

void C3Connection::onSetParamTimeout() {
    setParamPending_ = false;
    emit setParamsFailed(QStringLiteral("SETPARAM request timed out"));
}

void C3Connection::disconnectNow() {
    if ((sessionId_ != 0 || sessionLess_) && socket_.state() == QAbstractSocket::ConnectedState) {
        if (sessionLess_) {
            socket_.write(C3Codec::encodeSessionLessDisconnectRequest());
        } else {
            socket_.write(C3Codec::encodeDisconnectRequest({sessionId_, requestNr_}));
        }
        socket_.flush();
    }
    sessionId_ = 0;
    sessionLess_ = false;
    handshakeTimer_.stop();
    handshakeStep_ = HandshakeStep::None;
    dataQueryPending_ = false;
    dataQueryStep_ = DataQueryStep::None;
    dataQueryTimer_.stop();
    countQueryPending_ = false;
    countQueryStep_ = CountQueryStep::None;
    countQueryTimer_.stop();
    rtLogQueryPending_ = false;
    rtLogQueryStep_ = RtLogQueryStep::None;
    rtLogQueryTimer_.stop();
    controlPending_ = false;
    controlTimer_.stop();
    getParamPending_ = false;
    getParamTimer_.stop();
    setParamPending_ = false;
    setParamTimer_.stop();
    state_.disconnectNow();
}

bool C3Connection::isReady() const {
    return (sessionId_ != 0 || sessionLess_) && socket_.state() == QAbstractSocket::ConnectedState;
}

QString C3Connection::errorString() const {
    return error_;
}
