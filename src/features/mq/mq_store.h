#pragma once

#include "mq_codec.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <memory>

class Database;

struct StoredMessage {
    qint64 id = 0;
    QString envelopeId;
    QDateTime envelopeTimestamp;
    QString envelopeType;
    QString direction;
    QJsonObject payload;
    bool redelivered = false;
    bool acked = false;
    QString displayTitle;
    QString searchText;
    QDateTime createdAt;
};

Q_DECLARE_METATYPE(StoredMessage)
Q_DECLARE_METATYPE(QVector<StoredMessage>)

// Persists message contents as an unencrypted SQLite file. Payloads may contain
// personal data; encryption at rest is deliberately deferred and must be revisited
// before this store is used outside the current development-tool scope.
class MqStore {
public:
    // An empty path selects QStandardPaths::AppDataLocation/messages.sqlite.
    explicit MqStore(const QString &path = QString());
    ~MqStore();

    MqStore(const MqStore &) = delete;
    MqStore &operator=(const MqStore &) = delete;

    bool insert(const MqCodec::Envelope &envelope,
                const QString &direction,
                const QString &displayTitle,
                const QString &searchText);
    void markAcked(const QString &envelopeId);

    QVector<StoredMessage> recent(int limit);
    QVector<StoredMessage> search(const QString &type,
                                  const QDateTime &from,
                                  const QDateTime &to,
                                  const QString &searchText,
                                  bool receivedOnly);

    qint64 fileSizeBytes() const;
    bool purge(const QDateTime &from, const QDateTime &to);

private:
    static QString resolvePath(const QString &path);
    void initializeSchema();

    QString path_;
    std::unique_ptr<Database> database_;
};
