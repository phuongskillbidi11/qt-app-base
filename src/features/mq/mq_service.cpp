#include "mq_service.h"

#include "mq_codec.h"
#include "mq_connection.h"

#include <amqpcpp.h>

#include <limits>

namespace {
constexpr char kExchange[] = "hsf.card";
constexpr char kQueue[] = "hsf.card.issued";
}

MqService::MqService(MqConnection *connection, QObject *parent)
    : QObject(parent),
      connection_(connection) {}

MqService::~MqService() = default;

bool MqService::publish(const QString &body) {
    if (!ensureChannel()) {
        return false;
    }

    const QByteArray payload = MqCodec::encode(body);
    const MqCodec::DecodeResult decoded = MqCodec::decode(payload);
    if (!decoded.valid) {
        emit errorOccurred("Could not decode the newly encoded message");
        return false;
    }

    AMQP::Envelope envelope(payload.constData(), static_cast<uint64_t>(payload.size()));
    envelope.setDeliveryMode(2);
    if (!channel_->publish(kExchange, decoded.envelope.type.toStdString(), envelope,
                           AMQP::mandatory)) {
        emit errorOccurred("Could not publish the message");
        return false;
    }

    emit published(decoded.envelope.id);
    return true;
}

bool MqService::startConsuming() {
    if (paused_ || consuming_ || consumerStarting_) {
        return true;
    }
    if (!ensureChannel()) {
        return false;
    }

    constexpr uint16_t prefetch = 10;
    channel_->setQos(prefetch);
    consumerStarting_ = true;

    AMQP::DeferredConsumer &consumer = channel_->consume(kQueue);
    consumer.onSuccess([this](const std::string &tag) {
        consumerStarting_ = false;
        consuming_ = true;
        consumerTag_ = QString::fromStdString(tag);
        if (paused_) {
            cancelConsumer();
        }
    });
    consumer.onReceived(
        [this](const AMQP::Message &message, uint64_t deliveryTag, bool redelivered) {
            if (message.bodySize() > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
                rejectDelivery(static_cast<quint64>(deliveryTag), "message too large");
                return;
            }

            const QByteArray payload(message.body(), static_cast<int>(message.bodySize()));
            const MqCodec::DecodeResult decoded = MqCodec::decode(payload);
            if (!decoded.valid) {
                rejectDelivery(static_cast<quint64>(deliveryTag), "invalid envelope");
                return;
            }

            const quint64 tag = static_cast<quint64>(deliveryTag);
            pendingDeliveries_.insert(tag);
            emit messageReceived(decoded.envelope.id,
                                 decoded.envelope.timestamp,
                                 decoded.envelope.type,
                                 decoded.envelope.payload,
                                 redelivered,
                                 tag);
        });
    consumer.onCancelled([this](const std::string &) {
        consumerStarting_ = false;
        consuming_ = false;
        cancelPending_ = false;
        consumerTag_.clear();
        if (!paused_) {
            startConsuming();
        }
    });
    consumer.onError([this](const char *message) {
        consumerStarting_ = false;
        emit errorOccurred(QString::fromUtf8(message));
    });
    return true;
}

void MqService::pauseConsuming(bool paused) {
    paused_ = paused;
    if (paused_) {
        cancelConsumer();
        return;
    }

    const QSet<quint64> committed = committedDeliveries_;
    for (quint64 deliveryTag : committed) {
        confirmInserted(deliveryTag);
    }
    if (!consuming_ && !cancelPending_) {
        startConsuming();
    }
}

void MqService::confirmInserted(quint64 deliveryTag) {
    if (!pendingDeliveries_.contains(deliveryTag)) {
        return;
    }
    if (paused_) {
        committedDeliveries_.insert(deliveryTag);
        return;
    }
    if (!channel_ || !channel_->ack(static_cast<uint64_t>(deliveryTag))) {
        emit errorOccurred("Could not acknowledge the inserted message");
        return;
    }

    committedDeliveries_.remove(deliveryTag);
    pendingDeliveries_.remove(deliveryTag);
}

void MqService::cancelConsumer() {
    if (!channel_ || consumerTag_.isEmpty() || cancelPending_) {
        return;
    }

    cancelPending_ = true;
    channel_->cancel(consumerTag_.toStdString())
        .onSuccess([this](const std::string &) {
            cancelPending_ = false;
            consuming_ = false;
            consumerTag_.clear();
            if (!paused_) {
                startConsuming();
            }
        })
        .onError([this](const char *message) {
            cancelPending_ = false;
            emit errorOccurred(QString::fromUtf8(message));
        });
}

void MqService::rejectDelivery(quint64 deliveryTag, const QString &reason) {
    // No AMQP::requeue: a message that failed to decode will fail identically on redelivery,
    // and requeueing it loops forever while holding one of the ten prefetch slots. Step 3 adds
    // a dead-letter exchange so rejected messages are kept for inspection rather than dropped.
    if (!channel_ || !channel_->reject(static_cast<uint64_t>(deliveryTag))) {
        emit errorOccurred("Could not reject an undecodable message");
        return;
    }

    pendingDeliveries_.remove(deliveryTag);
    committedDeliveries_.remove(deliveryTag);
    emit messageRejected(reason);
}

bool MqService::ensureChannel() {
    if (channel_) {
        return true;
    }
    if (!connection_ || !connection_->connection()) {
        emit errorOccurred("AMQP connection is not ready");
        return false;
    }

    channel_ = std::make_unique<AMQP::Channel>(connection_->connection());
    channel_->onError([this](const char *message) {
        emit errorOccurred(QString::fromUtf8(message));
    });
    channel_->recall().onReturned(
        [this](const AMQP::Message &message, int16_t, const std::string &description) {
            emit messageReturned(QString::fromStdString(message.routingkey()),
                                 QString::fromStdString(description));
        });
    return true;
}
