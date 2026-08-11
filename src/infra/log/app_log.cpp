#include "app_log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0-dev"
#endif

namespace {

bool g_initialized = false;

void writeLine(const QString &level, const QString &message) {
    if (!g_initialized) {
        return;
    }

    const QString path = AppLog::filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QString singleLine = message;
    singleLine.replace('\r', ' ');
    singleLine.replace('\n', ' ');
    const QString line = QString("%1  %2  %3\n")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"),
             level.leftJustified(5, ' '), singleLine);
    file.write(line.toUtf8());
    file.flush();
    file.close();
}

}  // namespace

namespace AppLog {

void init() {
    const QString path = filePath();
    const QFileInfo existingLog(path);
    if (existingLog.exists() && existingLog.size() > 1024 * 1024) {
        const QString rotatedPath = path + ".1";
        QFile::remove(rotatedPath);
        QFile::rename(path, rotatedPath);
    }

    g_initialized = true;
    info(QString("application started; version=%1; pid=%2")
        .arg(QString::fromLatin1(APP_VERSION))
        .arg(QCoreApplication::applicationPid()));
}

QString filePath() {
    // Beside the INI, whatever QSettings decided that is on this platform, and named
    // after the application rather than hardcoded — the base must not know the app.
    const QFileInfo settingsFile(QSettings().fileName());
    const QString name = QCoreApplication::applicationName().isEmpty()
        ? QStringLiteral("app") : QCoreApplication::applicationName();
    return settingsFile.dir().filePath(name + ".log");
}

void info(const QString &message) {
    writeLine("INFO", message);
}

void warn(const QString &message) {
    writeLine("WARN", message);
}

void error(const QString &message) {
    writeLine("ERROR", message);
}

}  // namespace AppLog
