#include "platform.h"

#include "app_log.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include <cerrno>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <cstring>

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
    return applicationName + "-update";
}

bool startSelfInstall(const QString &downloadedPath, const QString &currentExecutablePath,
                       QString *errorOut) {
    const QString stagedPath = currentExecutablePath + ".new";

    QFile::remove(stagedPath);  // a stale leftover from a previous failed attempt
    if (!QFile::copy(downloadedPath, stagedPath)) {
        if (errorOut) {
            *errorOut = "could not stage the new binary next to " + currentExecutablePath;
        }
        return false;
    }
    if (!QFile::setPermissions(stagedPath,
            QFile::permissions(stagedPath) | QFileDevice::ExeOwner | QFileDevice::ExeUser
            | QFileDevice::ExeGroup | QFileDevice::ExeOther)) {
        QFile::remove(stagedPath);
        if (errorOut) {
            *errorOut = "could not make the staged binary executable";
        }
        return false;
    }

    // Same-directory rename() is atomic (same filesystem, guaranteed by staging next to
    // the target rather than in a temp directory) and safe on a running executable: Linux
    // keeps serving the old, now-unlinked inode to this still-running process until it
    // exits. The real path only ever points to a fully-written file, before and after.
    if (::rename(stagedPath.toLocal8Bit().constData(),
                 currentExecutablePath.toLocal8Bit().constData()) != 0) {
        const QString reason = QString::fromLocal8Bit(std::strerror(errno));
        QFile::remove(stagedPath);
        if (errorOut) {
            *errorOut = "could not replace " + currentExecutablePath + ": " + reason;
        }
        return false;
    }

    if (!QProcess::startDetached(currentExecutablePath)) {
        if (errorOut) {
            *errorOut = "new binary installed but could not be relaunched";
        }
        return false;
    }
    return true;
}

bool supportsSelfUpdate() {
    // A real mechanism, not a placeholder: startSelfInstall() replaces the running binary
    // in place via an atomic same-directory rename(), then relaunches it. See its own
    // comment for why this is safe on a running executable.
    return true;
}

void installCrashHandler() {
    std::signal(SIGSEGV, crashHandler);
    std::signal(SIGABRT, crashHandler);
    std::signal(SIGFPE, crashHandler);
    std::signal(SIGILL, crashHandler);
}

}  // namespace Platform
