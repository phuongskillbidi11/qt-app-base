#include "platform.h"

#include "app_log.h"

#include <QDir>
#include <QFileInfo>

#include <windows.h>

namespace {

LONG WINAPI crashHandler(EXCEPTION_POINTERS *info) {
    if (info != nullptr && info->ExceptionRecord != nullptr) {
        AppLog::error(QString("unhandled exception; code=0x%1; address=0x%2")
            .arg(static_cast<quint32>(info->ExceptionRecord->ExceptionCode), 0, 16)
            .arg(reinterpret_cast<quintptr>(info->ExceptionRecord->ExceptionAddress), 0, 16));
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

namespace Platform {

QString curlExecutablePath() {
    // Absolute System32 path, not bare "curl.exe": a curl earlier on PATH must not be
    // able to stand in for the OS one. Present since Windows 10 1803 and patched by
    // Windows Update, and it uses Schannel rather than OpenSSL.
    const QByteArray systemRoot = qgetenv("SystemRoot");
    if (systemRoot.isEmpty()) {
        return QString();
    }
    const QString path =
        QDir(QString::fromLocal8Bit(systemRoot)).filePath("System32/curl.exe");
    return QFileInfo::exists(path) ? path : QString();
}

QString installerFileName(const QString &applicationName) {
    return applicationName + "-setup.exe";
}

QStringList silentInstallArguments() {
    // Inno Setup: install without UI, do not reboot, and relaunch the app afterwards.
    // The installer script must implement the /relaunch flag itself.
    return {QStringLiteral("/SILENT"), QStringLiteral("/NORESTART"),
            QStringLiteral("/relaunch=1")};
}

bool supportsSelfUpdate() {
    return true;
}

void installCrashHandler() {
    SetUnhandledExceptionFilter(crashHandler);
}

}  // namespace Platform
