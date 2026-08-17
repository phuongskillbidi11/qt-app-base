#pragma once

#include <QString>

#include <sqlite3.h>

#include "statement.h"

// Owns one SQLite connection. Not thread-safe by design - callers must serialize
// access through exactly one Worker instance. A second caller reaching this
// concurrently is a bug in the caller, not something this class defends against.
class Database {
public:
    explicit Database(const QString &path);
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // Runs a statement that returns no rows (INSERT/UPDATE/DELETE/DDL).
    void exec(const QString &sql);

    // True when the table already carries the column.
    //
    // CREATE TABLE IF NOT EXISTS does nothing at all once the table exists.
    // Adding a column therefore needs an explicit ALTER, and the ALTER needs
    // this guard because SQLite errors when the column is already there.
    bool hasColumn(const QString &table, const QString &column);

    long long lastInsertRowId();

    sqlite3 *handle() const { return db_; }

private:
    sqlite3 *db_ = nullptr;
};
