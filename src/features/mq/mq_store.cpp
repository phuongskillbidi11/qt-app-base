#include "mq_store.h"

#include "database.h"
#include "statement.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QStringList>

#include <sqlite3.h>

#include <stdexcept>

namespace {

const char kSelectColumns[] =
    "id, envelope_id, envelope_ts, envelope_type, direction, payload, "
    "redelivered, acked, display_title, search_text, created_at";

StoredMessage readStoredMessage(Statement &statement) {
    StoredMessage message;
    message.id = statement.getInt64(0);
    message.envelopeId = statement.getText(1);
    message.envelopeTimestamp =
        QDateTime::fromString(statement.getText(2), Qt::ISODateWithMs);
    message.envelopeType = statement.getText(3);
    message.direction = statement.getText(4);

    QJsonParseError parseError;
    const QJsonDocument payloadDocument =
        QJsonDocument::fromJson(statement.getText(5).toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && payloadDocument.isObject()) {
        message.payload = payloadDocument.object();
    }

    message.redelivered = statement.getInt(6) != 0;
    message.acked = statement.getInt(7) != 0;
    message.displayTitle = statement.getText(8);
    message.searchText = statement.getText(9);
    message.createdAt = QDateTime::fromString(statement.getText(10), Qt::ISODateWithMs);
    return message;
}

QVector<StoredMessage> readRows(Statement &statement) {
    QVector<StoredMessage> rows;
    while (statement.step()) {
        rows.append(readStoredMessage(statement));
    }
    return rows;
}

}  // namespace

MqStore::MqStore(const QString &path)
    : path_(resolvePath(path)), database_(std::make_unique<Database>(path_)) {
    initializeSchema();
}

MqStore::~MqStore() = default;

bool MqStore::insert(const MqCodec::Envelope &envelope,
                     const QString &direction,
                     const QString &displayTitle,
                     const QString &searchText) {
    Statement statement(database_->handle(), QStringLiteral(R"sql(
        INSERT OR IGNORE INTO messages (
            envelope_id, envelope_ts, envelope_type, direction, payload,
            redelivered, acked, display_title, search_text, created_at
        ) VALUES (?, ?, ?, ?, ?, 0, 0, ?, ?, ?)
    )sql"));
    statement.bind(1, envelope.id);
    statement.bind(2, envelope.timestamp.toUTC().toString(Qt::ISODateWithMs));
    statement.bind(3, envelope.type);
    statement.bind(4, direction);
    statement.bind(5, QString::fromUtf8(
                          QJsonDocument(envelope.payload).toJson(QJsonDocument::Compact)));
    statement.bind(6, displayTitle);
    statement.bind(7, searchText);
    statement.bind(8, QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    statement.step();
    return sqlite3_changes(database_->handle()) != 0;
}

void MqStore::markAcked(const QString &envelopeId) {
    Statement statement(database_->handle(), QStringLiteral(
        "UPDATE messages SET acked = 1 WHERE envelope_id = ?"));
    statement.bind(1, envelopeId);
    statement.step();
}

QVector<StoredMessage> MqStore::recent(int limit) {
    Statement statement(database_->handle(),
                        QStringLiteral("SELECT ") + QString::fromLatin1(kSelectColumns)
                            + QStringLiteral(
                                " FROM messages ORDER BY created_at DESC, id DESC LIMIT ?"));
    statement.bind(1, limit);
    return readRows(statement);
}

QVector<StoredMessage> MqStore::search(const QString &type,
                                       const QDateTime &from,
                                       const QDateTime &to,
                                       const QString &searchText,
                                       bool receivedOnly) {
    QStringList conditions;
    if (!type.isEmpty()) {
        conditions.append(QStringLiteral("envelope_type = ?"));
    }
    if (from.isValid()) {
        conditions.append(QStringLiteral("created_at >= ?"));
    }
    if (to.isValid()) {
        conditions.append(QStringLiteral("created_at <= ?"));
    }
    if (!searchText.isEmpty()) {
        conditions.append(QStringLiteral("search_text LIKE ?"));
    }
    if (receivedOnly) {
        conditions.append(QStringLiteral("direction = 'received'"));
    }

    QString sql = QStringLiteral("SELECT ") + QString::fromLatin1(kSelectColumns)
        + QStringLiteral(" FROM messages");
    if (!conditions.isEmpty()) {
        sql += QStringLiteral(" WHERE ") + conditions.join(QStringLiteral(" AND "));
    }
    sql += QStringLiteral(" ORDER BY created_at DESC, id DESC");

    Statement statement(database_->handle(), sql);
    int parameter = 1;
    if (!type.isEmpty()) {
        statement.bind(parameter++, type);
    }
    if (from.isValid()) {
        statement.bind(parameter++, from.toUTC().toString(Qt::ISODateWithMs));
    }
    if (to.isValid()) {
        statement.bind(parameter++, to.toUTC().toString(Qt::ISODateWithMs));
    }
    if (!searchText.isEmpty()) {
        statement.bind(parameter++, QStringLiteral("%") + searchText + QStringLiteral("%"));
    }
    return readRows(statement);
}

qint64 MqStore::fileSizeBytes() const {
    return QFileInfo(path_).size();
}

bool MqStore::purge(const QDateTime &from, const QDateTime &to) {
    Statement statement(database_->handle(), QStringLiteral(
        "DELETE FROM messages WHERE created_at >= ? AND created_at <= ?"));
    statement.bind(1, from.toUTC().toString(Qt::ISODateWithMs));
    statement.bind(2, to.toUTC().toString(Qt::ISODateWithMs));
    statement.step();
    return sqlite3_changes(database_->handle()) != 0;
}

QString MqStore::resolvePath(const QString &path) {
    QString resolved = path;
    if (resolved.isEmpty()) {
        const QString directory =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!QDir().mkpath(directory)) {
            throw std::runtime_error("Failed to create application data directory");
        }
        resolved = QDir(directory).filePath(QStringLiteral("messages.sqlite"));
    } else if (resolved != QStringLiteral(":memory:")) {
        const QString directory = QFileInfo(resolved).absolutePath();
        if (!QDir().mkpath(directory)) {
            throw std::runtime_error("Failed to create database directory");
        }
    }
    return resolved;
}

void MqStore::initializeSchema() {
    database_->exec(QStringLiteral(R"sql(
        CREATE TABLE IF NOT EXISTS messages (
            id              INTEGER PRIMARY KEY,
            envelope_id     TEXT NOT NULL UNIQUE,
            envelope_ts     TEXT NOT NULL,
            envelope_type   TEXT NOT NULL,
            direction       TEXT NOT NULL,
            payload         TEXT NOT NULL,
            redelivered     INTEGER NOT NULL DEFAULT 0,
            acked           INTEGER NOT NULL DEFAULT 0,
            display_title   TEXT NOT NULL DEFAULT '',
            search_text     TEXT NOT NULL DEFAULT '',
            created_at      TEXT NOT NULL
        );
    )sql"));
    database_->exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_messages_envelope_type "
        "ON messages(envelope_type);"));
    database_->exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_messages_created_at ON messages(created_at);"));
    database_->exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_messages_search_text ON messages(search_text);"));
    database_->exec(QStringLiteral("PRAGMA user_version = 1;"));
}
