#include "app_log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <cstdio>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0-dev"
#endif

namespace {

bool g_initialized = false;
bool g_writeFailed = false;

void writeLine(const QString &level, const QString &message) {
    if (!g_initialized) {
        return;
    }

    const QString path = AppLog::filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (!g_writeFailed) {
            g_writeFailed = true;
            const QByteArray nativePath = QDir::toNativeSeparators(path).toLocal8Bit();
            const QByteArray errorText = file.errorString().toLocal8Bit();
            std::fprintf(stderr, "AppLog: failed to open %s: %s\n",
                         nativePath.constData(), errorText.constData());
        }
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

bool isHealthy() {
    return !g_writeFailed;
}

QString filePath() {
    const QSettings settings;
    const QString directory = settings.format() == QSettings::IniFormat
        ? QFileInfo(settings.fileName()).dir().absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString name = QCoreApplication::applicationName().isEmpty()
        ? QStringLiteral("app") : QCoreApplication::applicationName();
    return QDir(directory).filePath(name + ".log");
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
