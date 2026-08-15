#pragma once

#include <QObject>
#include <QDateTime>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <memory>

namespace AMQP {
class Channel;
}

class MqConnection;

class MqService : public QObject {
    Q_OBJECT

public:
    explicit MqService(MqConnection *connection, QObject *parent = nullptr);
    ~MqService() override;

    bool publish(const QString &body);

public slots:
    bool startConsuming();
    void pauseConsuming(bool paused);
    void confirmInserted(quint64 deliveryTag);

signals:
    void published(QString id);
    void messageReceived(QString id,
                         QDateTime timestamp,
                         QString type,
                         QJsonObject payload,
                         bool redelivered,
                         quint64 deliveryTag);
    void errorOccurred(QString message);
    // One message was discarded. The connection and channel are unaffected.
    void messageRejected(QString reason);
    void messageReturned(QString routingKey, QString description);

private:
    bool ensureChannel();
    void cancelConsumer();
    void rejectDelivery(quint64 deliveryTag, const QString &reason);

    MqConnection *connection_;
    std::unique_ptr<AMQP::Channel> channel_;
    QString consumerTag_;
    QSet<quint64> pendingDeliveries_;
    QSet<quint64> committedDeliveries_;
    bool paused_ = false;
    bool consumerStarting_ = false;
    bool consuming_ = false;
    bool cancelPending_ = false;
};
