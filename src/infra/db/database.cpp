#include "database.h"

#include <QByteArray>

#include <stdexcept>
#include <string>

Database::Database(const QString &path) {
    const QByteArray pathUtf8 = path.toUtf8();
    if (sqlite3_open(pathUtf8.constData(), &db_) != SQLITE_OK) {
        const std::string message = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Failed to open database at " + pathUtf8.toStdString()
                                 + ": " + message);
    }
    sqlite3_busy_timeout(db_, 5000);
    exec(QStringLiteral("PRAGMA foreign_keys = ON;"));
    exec(QStringLiteral("PRAGMA journal_mode = WAL;"));
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void Database::exec(const QString &sql) {
    const QByteArray sqlUtf8 = sql.toUtf8();
    char *errorMessage = nullptr;
    if (sqlite3_exec(db_, sqlUtf8.constData(), nullptr, nullptr, &errorMessage) != SQLITE_OK) {
        const std::string message = errorMessage ? errorMessage : "unknown error";
        sqlite3_free(errorMessage);
        throw std::runtime_error("SQL exec failed: " + message
                                 + "\nQuery: " + sqlUtf8.toStdString());
    }
}

bool Database::hasColumn(const QString &table, const QString &column) {
    Statement statement(db_, QStringLiteral(
        "SELECT COUNT(*) FROM pragma_table_info(?) WHERE name = ?"));
    statement.bind(1, table);
    statement.bind(2, column);
    if (!statement.step()) {
        return false;
    }
    return statement.getInt(0) > 0;
}

long long Database::lastInsertRowId() {
    return sqlite3_last_insert_rowid(db_);
}
