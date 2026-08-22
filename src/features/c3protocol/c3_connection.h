#pragma once

#include "c3_codec.h"
#include "protocol_driver.h"
#include "connection_state.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>
#include <QVariant>

#include <cstdint>

// C3/InBio panel: unlike ModbusConnection (no application-level handshake -- "ready" is the
// TCP connect succeeding), and like MqConnection (waits for AMQP's own onReady(), past the
// TCP connect) -- isReady() only becomes true once CONNECT_SESSION has actually completed.
// See spec.md D4 of .plans/2026-08-21-c3-protocol-session.
class C3Connection : public ProtocolDriver {
    Q_OBJECT
public:
    explicit C3Connection(QObject *parent = nullptr);
    ~C3Connection() override;

    void connectToHost(const QString &host, quint16 port);
    void disconnectNow();
    void requestTableData(const QString &tableName);
    void requestTableRecordCount(const QString &tableName);
    void requestRealtimeLog();
    void requestDeviceParams(const QStringList &parameterNames);
    void setDeviceParams(const QVariantMap &values);
    void controlDoorOutput(uint8_t doorNumber, uint8_t durationSeconds);   // 1-254 only; for 0
                                                                        // or 255 use the
                                                                        // dedicated methods
                                                                        // below instead
    void controlDoorOutputNormalOpen(uint8_t doorNumber);   // explicit opt-in for duration=255
                                                          // (stay open indefinitely) -- never
                                                          // reachable as anyone's default
    void controlDoorClose(uint8_t doorNumber);               // duration=0
    void controlAuxOutput(uint8_t auxNumber, uint8_t durationSeconds);
    void cancelAlarm();
    void restartDevice();
    void setNormalOpenState(uint8_t doorNumber, bool enabled);

    bool isReady() const override;
    QString errorString() const override;

signals:
    void sessionEstablished(quint16 sessionId);
    void sessionFailed(QString error);
    void tableDataReceived(QString tableName, QVector<QVariantMap> records);
    void tableDataFailed(QString tableName, QString error);
    void tableRecordCountReceived(QString tableName, quint32 count);
    void tableRecordCountFailed(QString tableName, QString error);
    void realtimeLogReceived(QVector<C3Codec::RtLogRecord> records);
    void realtimeLogKeyValueReceived(QVector<QVariantMap> records);
    void realtimeLogFailed(QString error);
    void deviceParamsReceived(QVariantMap values);
    void deviceParamsFailed(QString error);
    void setParamsAcknowledged();
    void setParamsFailed(QString error);
    void controlAcknowledged();
    void controlFailed(QString error);

private:
    enum class HandshakeStep { None, AwaitingSessionReply, AwaitingSessionLessReply };
    enum class DataQueryStep { None, AwaitingTableConfig, AwaitingTableData,
                               AwaitingBigDataChunk, AwaitingFreeDataAck };
    enum class CountQueryStep { None, AwaitingTableConfig, AwaitingCount };
    enum class RtLogQueryStep { None, AwaitingBinaryReply, AwaitingKeyValueReply };

    void onSocketConnected();
    void onSocketError();
    void onSocketReadyRead();
    void onHandshakeTimeout();
    void onDataQueryTimeout();
    void onCountQueryTimeout();
    void onRtLogQueryTimeout();
    void onControlTimeout();
    void onGetParamTimeout();
    void onSetParamTimeout();
    void sendControlRequest(C3Codec::ControlOperation operation, uint8_t param1, uint8_t param2,
                            uint8_t param3);

    QTcpSocket socket_;
    ConnectionState state_;
    QString error_;
    QByteArray readBuffer_;
    QTimer handshakeTimer_;
    bool connectAttemptPending_ = false;
    HandshakeStep handshakeStep_ = HandshakeStep::None;
    uint16_t sessionId_ = 0;
    bool sessionLess_ = false;
    int32_t requestNr_ = C3Codec::kPreSessionRequestNr;
    bool dataQueryPending_ = false;
    DataQueryStep dataQueryStep_ = DataQueryStep::None;
    QTimer dataQueryTimer_;
    C3Codec::DataTableConfig tableConfig_;
    QString pendingTableName_;
    QByteArray bigDataBuffer_;
    uint32_t bigDataExpectedLength_ = 0;
    bool countQueryPending_ = false;
    CountQueryStep countQueryStep_ = CountQueryStep::None;
    QTimer countQueryTimer_;
    int countTableIndex_ = -1;
    QString pendingCountTableName_;
    bool rtLogQueryPending_ = false;
    RtLogQueryStep rtLogQueryStep_ = RtLogQueryStep::None;
    QTimer rtLogQueryTimer_;
    bool controlPending_ = false;
    QTimer controlTimer_;
    bool getParamPending_ = false;
    QTimer getParamTimer_;
    bool setParamPending_ = false;
    QTimer setParamTimer_;
    uint8_t rtlogCommand_ = C3Codec::kCommandRtlogBinary;   // persists across calls -- once
                                                          // switched to key/value it stays
                                                          // switched, matching
                                                          // zkaccess-c3-py's own behavior
};
