#include "mq_codec.h"
#include "mq_buffer.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QtGlobal>

#include <cstdio>

int main() {
    bool allPassed = true;
    const auto check = [&allPassed](bool passed, const char *message) {
        std::printf("%s: %s\n", passed ? "PASS" : "FAIL", message);
        allPassed = allPassed && passed;
    };

    const QString body = "first message";
    const QByteArray firstData = MqCodec::encode(body);
    const QByteArray secondData = MqCodec::encode(body);
    const MqCodec::DecodeResult first = MqCodec::decode(firstData);
    const MqCodec::DecodeResult second = MqCodec::decode(secondData);

    check(first.valid && first.envelope.source == "qt-mq-lab"
              && first.envelope.type == "lab.note"
              && first.envelope.payload.value("text").toString() == body,
          "encode then decode preserves the typed note envelope");
    check(first.valid && second.valid && !first.envelope.id.isEmpty()
              && !second.envelope.id.isEmpty() && first.envelope.id != second.envelope.id,
          "encode creates a non-empty unique id");

    QJsonObject emptyIdEnvelope = QJsonDocument::fromJson(firstData).object();
    emptyIdEnvelope.insert("id", "");
    check(!MqCodec::decode(QJsonDocument(emptyIdEnvelope).toJson(QJsonDocument::Compact)).valid,
          "decode rejects an empty id");
    check(!MqCodec::decode("{not valid json").valid,
          "decode rejects malformed JSON without crashing");

    QJsonObject oldBodyEnvelope = QJsonDocument::fromJson(firstData).object();
    oldBodyEnvelope.remove("payload");
    oldBodyEnvelope.insert("body", body);
    check(!MqCodec::decode(QJsonDocument(oldBodyEnvelope).toJson(QJsonDocument::Compact)).valid,
          "decode rejects the old body shape without payload");

    const QDateTime now = QDateTime::currentDateTimeUtc();
    check(first.valid && first.envelope.timestamp.timeSpec() == Qt::UTC
              && qAbs(first.envelope.timestamp.secsTo(now)) <= 60,
          "timestamp is UTC and within one minute of now");

    const QByteArray stream("abcdefghijkl");
    const QList<QByteArray> expectedRecords{"abcd", "efgh", "ijkl"};
    const auto fixedRecordConsumer = [](QList<QByteArray> &records) {
        return [&records](const char *data, quint64 size) -> quint64 {
            if (size < 4) {
                return 0;
            }
            records.append(QByteArray(data, 4));
            return 4;
        };
    };

    QByteArray oneByteBuffer;
    QList<QByteArray> oneByteRecords;
    const auto oneByteConsumer = fixedRecordConsumer(oneByteRecords);
    for (const char byte : stream) {
        MqBuffer::feed(oneByteBuffer, QByteArray(1, byte), oneByteConsumer);
    }
    check(oneByteRecords == expectedRecords && oneByteBuffer.isEmpty(),
          "feed preserves one-byte chunks until three records are complete");

    QByteArray splitBuffer;
    QList<QByteArray> splitRecords;
    const auto splitConsumer = fixedRecordConsumer(splitRecords);
    MqBuffer::feed(splitBuffer, stream.left(5), splitConsumer);
    MqBuffer::feed(splitBuffer, stream.mid(5), splitConsumer);
    check(splitRecords == expectedRecords && splitBuffer.isEmpty(),
          "feed parses chunks split across record boundaries");

    QByteArray partialBuffer;
    QList<QByteArray> partialRecords;
    MqBuffer::feed(partialBuffer, stream.left(6), fixedRecordConsumer(partialRecords));
    check(partialRecords == QList<QByteArray>{"abcd"} && partialBuffer == "ef",
          "feed retains trailing partial data");

    return allPassed ? 0 : 1;
}
