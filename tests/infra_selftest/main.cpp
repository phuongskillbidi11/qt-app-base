#include "app_log.h"
#include "app_settings.h"
#include "platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("qt-app-base-selftest");
    QCoreApplication::setApplicationName("infra_selftest");

    bool allPassed = true;
    const auto check = [&allPassed](bool passed, const char *message) {
        std::printf("%s: %s\n", passed ? "PASS" : "FAIL", message);
        allPassed = allPassed && passed;
    };

    // --- secrets never reach disk ------------------------------------------
    check(stripSecret("protocol=TCP,ip=10.0.0.1,passwd=hunter2", "passwd")
              == "protocol=TCP,ip=10.0.0.1,passwd=",
          "stripSecret blanks a trailing secret but keeps the key");
    check(stripSecret("passwd=hunter2,ip=10.0.0.1", "passwd") == "passwd=,ip=10.0.0.1",
          "stripSecret blanks a secret in the middle");
    check(stripSecret("ip=10.0.0.1", "passwd") == "ip=10.0.0.1",
          "stripSecret leaves a string without the key untouched");
    check(!stripSecret("passwd=hunter2", "passwd").contains("hunter2"),
          "the secret value is gone from the result");

    // --- the path must be sane on the platform default, before any redirection ---
    // QSettings is still on its platform default here. On Windows that is the registry, so
    // QSettings::fileName() returns a registry key, not a filesystem path. If filePath()
    // does not notice, it resolves that key against the root of the current drive — which
    // is exactly what it used to do, creating C:\HKEY_CURRENT_USER\Software\... on every
    // machine that ran the tests, and failing wherever a drive root is not writable.
    //
    // The assertion is that the fallback lands under the platform's application-data
    // location. That is the real contract, and it is what the old behaviour violated:
    // C:\HKEY_CURRENT_USER\... is a perfectly ordinary-looking path, so a weaker check —
    // "not a drive root", say — would have passed and caught nothing.
    // No I/O here, so this holds even where nothing is writable.
    const QString appDataRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString fallbackDir = QFileInfo(AppLog::filePath()).absolutePath();
    check(!appDataRoot.isEmpty() && fallbackDir.startsWith(appDataRoot),
          "without a file-backed QSettings the log lands under the app-data location");

    // --- from here the test writes, so it writes only where it owns the ground ---
    // Never the user's real profile. A self-test that leaves files under %APPDATA%
    // corrupts the evidence it exists to protect, and cannot run where that path is
    // locked down — which is the normal state of a production machine.
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) {
        std::printf("FAIL: could not create a temporary directory for the test\n");
        return 1;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, sandbox.path());

    const QString sandboxRoot = QDir(sandbox.path()).absolutePath();
    const QString logPath = AppLog::filePath();
    check(logPath.startsWith(sandboxRoot),
          "the log follows QSettings into the directory this test owns");

    // --- the log is inert until init() -------------------------------------
    // This rule exists because a shared source file can be compiled into both the real
    // application and a self-test. Without it, running the tests writes into the user's
    // real diagnostic log and corrupts the evidence it exists to preserve.
    QFile::remove(logPath);
    AppLog::info("this line must not be written");
    check(!QFileInfo::exists(logPath), "logging before init() writes nothing at all");

    AppLog::init();
    AppLog::info("after init");
    check(QFileInfo::exists(logPath), "logging after init() creates the file");

    QFile logFile(logPath);
    bool hasBoth = false;
    if (logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString contents = QString::fromUtf8(logFile.readAll());
        hasBoth = contents.contains("after init")
            && !contents.contains("must not be written");
        logFile.close();
    }
    check(hasBoth, "the log holds only what was written after init()");
    check(logPath.contains("infra_selftest"),
          "the log is named after the application, not hardcoded");
    check(AppLog::isHealthy(), "the log is healthy after init()");

    // --- the platform layer answers on whatever platform this is -----------
    check(!Platform::installerFileName("MyApp").isEmpty(),
          "the platform layer supplies an installer file name");
    // Not asserted as present: a machine may genuinely have no curl, and that is a
    // supported state — the updater reports it rather than crashing.
    std::printf("INFO: curl=%s selfUpdate=%s\n",
                Platform::curlExecutablePath().isEmpty()
                    ? "(none)" : qPrintable(Platform::curlExecutablePath()),
                Platform::supportsSelfUpdate() ? "yes" : "no");

    // --- parseMemInfo: both real /proc/meminfo kernel-format variants -------
    {
        qint64 available = 0;
        qint64 total = 0;
        const QString sample =
            "MemTotal:        2000000 kB\n"
            "MemFree:          300000 kB\n"
            "MemAvailable:     800000 kB\n"
            "Buffers:           50000 kB\n";
        check(Platform::parseMemInfo(sample, &available, &total)
                  && available == 800000LL * 1024 && total == 2000000LL * 1024,
              "parseMemInfo prefers MemAvailable over MemFree when both are present");
    }
    {
        qint64 available = 0;
        qint64 total = 0;
        const QString sample = "MemTotal:        2000000 kB\nMemFree:          300000 kB\n";
        check(Platform::parseMemInfo(sample, &available, &total)
                  && available == 300000LL * 1024 && total == 2000000LL * 1024,
              "parseMemInfo falls back to MemFree when MemAvailable is absent");
    }
    {
        qint64 available = 0;
        qint64 total = 0;
        check(!Platform::parseMemInfo("garbage, no colons here", &available, &total),
              "parseMemInfo returns false rather than guessing on unrecognized content");
    }

    // --- isResourceLow: boundary values around both thresholds --------------
    {
        Platform::SystemResources low;
        low.valid = true;
        low.availableRamBytes = 32LL * 1024 * 1024;   // below the 64MB floor
        low.availableDiskBytes = 100LL * 1024 * 1024;
        check(Platform::isResourceLow(low), "isResourceLow fires on low RAM alone");
    }
    {
        Platform::SystemResources low;
        low.valid = true;
        low.availableRamBytes = 500LL * 1024 * 1024;
        low.availableDiskBytes = 5LL * 1024 * 1024;   // below the 20MB floor
        check(Platform::isResourceLow(low), "isResourceLow fires on low disk alone");
    }
    {
        Platform::SystemResources ok;
        ok.valid = true;
        ok.availableRamBytes = 500LL * 1024 * 1024;
        ok.availableDiskBytes = 100LL * 1024 * 1024;
        check(!Platform::isResourceLow(ok), "isResourceLow stays quiet when both are healthy");
    }
    {
        Platform::SystemResources invalid;
        invalid.valid = false;
        invalid.availableRamBytes = 0;
        check(!Platform::isResourceLow(invalid),
              "isResourceLow does not fire when the underlying query itself failed");
    }

    // --- parseCpuInfo: the real-world /proc/cpuinfo format variants ---------
    check(Platform::parseCpuInfo("Hardware\t: Rockchip RK3568\nRevision\t: 0000\n")
              == "Rockchip RK3568",
          "parseCpuInfo prefers a Hardware line (common on ARM SBCs)");
    check(Platform::parseCpuInfo("Model\t\t: Raspberry Pi 4 Model B Rev 1.2\n")
              == "Raspberry Pi 4 Model B Rev 1.2",
          "parseCpuInfo falls back to a Model line");
    check(Platform::parseCpuInfo(
              "model name\t: Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz\n")
              == "Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz",
          "parseCpuInfo falls back to the x86 model name line");
    check(Platform::parseCpuInfo("CPU implementer\t: 0x41\nCPU part\t: 0xd08\n")
              == "unknown (implementer 0x41, part 0xd08)",
          "parseCpuInfo builds a labeled fallback from numeric codes rather than an empty string");

    const Platform::SystemInfo sysInfo = Platform::querySystemInfo();
    std::printf("INFO: chip=%s arch=%s os=%s\n",
                qPrintable(sysInfo.chipName), qPrintable(sysInfo.architecture),
                qPrintable(sysInfo.osVersion));

    // sandbox removes itself, and with it the log.
    return allPassed ? 0 : 1;
}
