#pragma once

#include <QString>

namespace AppLog {

// Resolves the log path, rotates if oversized (T2), and writes the first line.
// Safe to call once from main() before anything else logs.
void init();

// False after a log write fails; remains false for the rest of the process.
bool isHealthy();

// Beside the QSettings file when it exists, under the platform application-data
// location otherwise, and named after the application.
QString filePath();

void info(const QString &message);
void warn(const QString &message);
void error(const QString &message);

}  // namespace AppLog
