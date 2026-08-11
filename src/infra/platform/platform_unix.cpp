#include "platform.h"

#include "app_log.h"

#include <QFileInfo>

#include <csignal>
#include <cstdlib>

namespace {

void crashHandler(int signalNumber) {
    // Deliberately minimal. Only async-signal-safe work belongs here in principle; the
    // log write is a pragmatic compromise, taken because a crash that leaves no trace is
    // worse than a crash whose handler is imperfect. Re-raise so the default handler
    // still produces a core dump.
    AppLog::error(QString("fatal signal %1").arg(signalNumber));
    std::signal(signalNumber, SIG_DFL);
    std::raise(signalNumber);
}

}  // namespace

namespace Platform {

QString curlExecutablePath() {
    for (const QString &candidate : {QStringLiteral("/usr/bin/curl"),
                                     QStringLiteral("/bin/curl"),
                                     QStringLiteral("/usr/local/bin/curl")}) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

QString installerFileName(const QString &applicationName) {
    return applicationName + "-setup.AppImage";
}

QStringList silentInstallArguments() {
    return {};
}

bool supportsSelfUpdate() {
    // Left false on purpose. Linux desktop apps are normally updated by a package manager
    // or an AppImage updater, and each distribution channel differs. Returning true here
    // without a real mechanism behind it would let the UI offer an update it cannot
    // perform — worse than saying plainly that updates are managed elsewhere.
    return false;
}

void installCrashHandler() {
    std::signal(SIGSEGV, crashHandler);
    std::signal(SIGABRT, crashHandler);
    std::signal(SIGFPE, crashHandler);
    std::signal(SIGILL, crashHandler);
}

}  // namespace Platform
