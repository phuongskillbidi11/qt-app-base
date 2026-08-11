#include "app_log.h"
#include "app_settings.h"
#include "platform.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

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

    // --- the log is inert until init() -------------------------------------
    // This rule exists because a shared source file can be compiled into both the real
    // application and a self-test. Without it, running the tests writes into the user's
    // real diagnostic log and corrupts the evidence it exists to preserve.
    const QString logPath = AppLog::filePath();
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

    // --- the platform layer answers on whatever platform this is -----------
    check(!Platform::installerFileName("MyApp").isEmpty(),
          "the platform layer supplies an installer file name");
    // Not asserted as present: a machine may genuinely have no curl, and that is a
    // supported state — the updater reports it rather than crashing.
    std::printf("INFO: curl=%s selfUpdate=%s\n",
                Platform::curlExecutablePath().isEmpty()
                    ? "(none)" : qPrintable(Platform::curlExecutablePath()),
                Platform::supportsSelfUpdate() ? "yes" : "no");

    QFile::remove(logPath);
    return allPassed ? 0 : 1;
}
