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

    // sandbox removes itself, and with it the log.
    return allPassed ? 0 : 1;
}
