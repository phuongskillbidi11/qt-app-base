#include "platform.h"

#include "app_log.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QSysInfo>

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

bool startSelfInstall(const QString &downloadedPath, const QString & /*currentExecutablePath*/,
                       QString *errorOut) {
    // Inno Setup: install without UI, do not reboot, and relaunch the app afterwards.
    // The installer script must implement the /relaunch flag itself.
    const QStringList args = {QStringLiteral("/SILENT"), QStringLiteral("/NORESTART"),
                               QStringLiteral("/relaunch=1")};
    if (!QProcess::startDetached(downloadedPath, args)) {
        if (errorOut) {
            *errorOut = "installer could not be started";
        }
        return false;
    }
    return true;
}

bool supportsSelfUpdate() {
    return true;
}

void installCrashHandler() {
    SetUnhandledExceptionFilter(crashHandler);
}

SystemResources checkSystemResources(const QString &logDirectory) {
    SystemResources result;
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        result.availableRamBytes = static_cast<qint64>(memStatus.ullAvailPhys);
        result.totalRamBytes = static_cast<qint64>(memStatus.ullTotalPhys);
        result.valid = true;
    }
    ULARGE_INTEGER freeBytes;
    ULARGE_INTEGER totalBytes;
    const QString path = logDirectory.isEmpty() ? QStringLiteral(".") : logDirectory;
    if (GetDiskFreeSpaceExW(reinterpret_cast<LPCWSTR>(path.utf16()), &freeBytes, &totalBytes,
                            nullptr)) {
        result.availableDiskBytes = static_cast<qint64>(freeBytes.QuadPart);
        result.totalDiskBytes = static_cast<qint64>(totalBytes.QuadPart);
    } else {
        result.valid = false;
    }
    return result;
}

SystemInfo querySystemInfo() {
    SystemInfo info;
    QSettings cpuKey(
        QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
        QSettings::NativeFormat);
    info.chipName = cpuKey.value(QStringLiteral("ProcessorNameString")).toString();
    if (info.chipName.isEmpty()) {
        info.chipName = QStringLiteral("unknown");
    }
    info.architecture = QSysInfo::currentCpuArchitecture();
    info.osVersion = QSysInfo::prettyProductName();
    return info;
}

}  // namespace Platform
