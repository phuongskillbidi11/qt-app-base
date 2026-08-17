#pragma once

#include <QByteArray>
#include <QString>

#include <sqlite3.h>

#include <stdexcept>
#include <string>

// Thin RAII wrapper around a prepared sqlite3_stmt.
class Statement {
public:
    Statement(sqlite3 *db, const QString &sql) {
        const QByteArray sqlUtf8 = sql.toUtf8();
        if (sqlite3_prepare_v2(db, sqlUtf8.constData(), -1, &stmt_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("SQL prepare failed: ") + sqlite3_errmsg(db));
        }
    }

    ~Statement() {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    void bind(int index, const QString &value) {
        const QByteArray valueUtf8 = value.toUtf8();
        sqlite3_bind_text(stmt_, index, valueUtf8.constData(), -1, SQLITE_TRANSIENT);
    }

    void bind(int index, long long value) {
        sqlite3_bind_int64(stmt_, index, value);
    }

    void bind(int index, int value) {
        sqlite3_bind_int(stmt_, index, value);
    }

    void bindNull(int index) {
        sqlite3_bind_null(stmt_, index);
    }

    // Advances to the next row. Returns true if a row is available.
    bool step() {
        const int result = sqlite3_step(stmt_);
        if (result == SQLITE_ROW) {
            return true;
        }
        if (result == SQLITE_DONE) {
            return false;
        }
        throw std::runtime_error(std::string("SQL step failed: ")
                                 + sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }

    void reset() {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }

    bool isNull(int column) const {
        return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
    }

    QString getText(int column) const {
        const unsigned char *text = sqlite3_column_text(stmt_, column);
        return text ? QString::fromUtf8(reinterpret_cast<const char *>(text)) : QString();
    }

    long long getInt64(int column) const {
        return sqlite3_column_int64(stmt_, column);
    }

    int getInt(int column) const {
        return sqlite3_column_int(stmt_, column);
    }

    sqlite3_stmt *raw() const { return stmt_; }

private:
    sqlite3_stmt *stmt_ = nullptr;
};
