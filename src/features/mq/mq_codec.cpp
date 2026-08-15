#include "mq_codec.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUuid>

QByteArray MqCodec::encode(const QString &body) {
    const QJsonObject envelope{
        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"ts", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"v", 1},
        {"src", "qt-mq-lab"},
        {"type", "lab.note"},
        {"payload", QJsonObject{{"text", body}}},
    };
    return QJsonDocument(envelope).toJson(QJsonDocument::Compact);
}

MqCodec::DecodeResult MqCodec::decode(const QByteArray &data) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }

    const QJsonObject object = document.object();
    const QJsonValue idValue = object.value("id");
    const QJsonValue timestampValue = object.value("ts");
    const QJsonValue versionValue = object.value("v");
    const QJsonValue sourceValue = object.value("src");
    const QJsonValue typeValue = object.value("type");
    const QJsonValue payloadValue = object.value("payload");
    if (!idValue.isString() || idValue.toString().isEmpty()
        || !timestampValue.isString() || !versionValue.isDouble()
        || versionValue.toDouble() != 1.0 || !sourceValue.isString()
        || !typeValue.isString() || !payloadValue.isObject()) {
        return {};
    }

    const QDateTime timestamp =
        QDateTime::fromString(timestampValue.toString(), Qt::ISODateWithMs);
    if (!timestamp.isValid() || timestamp.timeSpec() != Qt::UTC) {
        return {};
    }

    DecodeResult result;
    result.envelope.id = idValue.toString();
    result.envelope.timestamp = timestamp;
    result.envelope.version = 1;
    result.envelope.source = sourceValue.toString();
    result.envelope.type = typeValue.toString();
    result.envelope.payload = payloadValue.toObject();
    result.valid = true;
    return result;
}
