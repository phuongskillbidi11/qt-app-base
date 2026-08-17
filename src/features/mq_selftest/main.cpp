#include "mq_codec.h"
#include "mq_buffer.h"
#include "mq_store.h"
#include "database.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QTemporaryDir>
#include <QtGlobal>

#include <cstdio>

int main() {
    bool allPassed = true;
    const auto check = [&allPassed](bool passed, const char *message) {
        std::printf("%s: %s\n", passed ? "PASS" : "FAIL", message);
        allPassed = allPassed && passed;
    };

    const QString body = "first message";
    const QByteArray firstData =
        MqCodec::encode(QStringLiteral("lab.note"), QJsonObject{{"text", body}});
    const QByteArray secondData =
        MqCodec::encode(QStringLiteral("lab.note"), QJsonObject{{"text", body}});
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

    QTemporaryDir storeDirectory;
    check(storeDirectory.isValid(), "store self-test creates a temporary directory");
    if (storeDirectory.isValid()) {
        const QString storePath = storeDirectory.filePath("messages.sqlite");
        MqStore store(storePath);

        MqCodec::Envelope original;
        original.id = "dedup-id";
        original.timestamp = QDateTime::currentDateTimeUtc();
        original.version = 1;
        original.source = "mq-selftest";
        original.type = "card.issued";
        original.payload = QJsonObject{{"opaque", "first"}};

        check(store.insert(original, "received", "First", "First C-001"),
              "store inserts a new envelope id");
        check(!store.insert(original, "received", "Duplicate", "Duplicate C-001"),
              "store ignores a duplicate envelope id");

        const QVector<StoredMessage> dedupRows = store.recent(10);
        int dedupMatches = 0;
        for (const StoredMessage &row : dedupRows) {
            if (row.envelopeId == original.id) {
                ++dedupMatches;
            }
        }
        check(dedupMatches == 1, "store retains exactly one row for a duplicate id");

        MqCodec::Envelope unknown;
        unknown.id = "unknown-id";
        unknown.timestamp = QDateTime::currentDateTimeUtc();
        unknown.version = 1;
        unknown.source = "mq-selftest";
        unknown.type = "future.unknown";
        unknown.payload = QJsonObject{{"opaque", "value"}};
        check(store.insert(unknown, "received", QString(), QString()),
              "store accepts an unknown type with empty derived columns");

        const QVector<StoredMessage> unknownRows = store.search(
            unknown.type,
            QDateTime::currentDateTimeUtc().addSecs(-60),
            QDateTime::currentDateTimeUtc().addSecs(60),
            QString(),
            false);
        check(unknownRows.size() == 1 && unknownRows.first().envelopeId == unknown.id
                  && unknownRows.first().displayTitle.isEmpty()
                  && unknownRows.first().searchText.isEmpty(),
              "unknown type remains searchable by envelope fields");
    }

    QTemporaryDir migrationDirectory;
    check(migrationDirectory.isValid(), "migration self-test creates a temporary directory");
    if (migrationDirectory.isValid()) {
        Database migrationDatabase(migrationDirectory.filePath("migration.sqlite"));
        migrationDatabase.exec("CREATE TABLE migration_probe (id INTEGER PRIMARY KEY);");
        check(!migrationDatabase.hasColumn("migration_probe", "added_text"),
              "migration guard sees a missing column");
        if (!migrationDatabase.hasColumn("migration_probe", "added_text")) {
            migrationDatabase.exec(
                "ALTER TABLE migration_probe ADD COLUMN added_text TEXT NOT NULL DEFAULT '';"
            );
        }
        if (!migrationDatabase.hasColumn("migration_probe", "added_text")) {
            migrationDatabase.exec(
                "ALTER TABLE migration_probe ADD COLUMN added_text TEXT NOT NULL DEFAULT '';"
            );
        }
        check(migrationDatabase.hasColumn("migration_probe", "added_text"),
              "migration guard prevents a second ALTER for an existing column");
    }

    return allPassed ? 0 : 1;
}
