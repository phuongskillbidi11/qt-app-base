#include "update_installer.h"

#include "app_log.h"
#include "platform.h"

#include "curl_client.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

UpdateInstaller::UpdateInstaller(QObject *parent)
    : QObject(parent),
      m_http(new CurlClient(this)) {
}

void UpdateInstaller::download(const QString &version, const QUrl &url,
                               const QString &sha256) {
    m_verified = false;
    m_installerPath.clear();

    const QString installerPath = QDir(QStandardPaths::writableLocation(
        QStandardPaths::TempLocation)).filePath(
            Platform::installerFileName(QCoreApplication::applicationName()));

    m_http->download(url, installerPath,
                     [this, version, sha256, installerPath](bool ok, const QString &error) {
        if (!ok) {
            AppLog::warn(QString("update download failed: %1").arg(error));
            QFile::remove(installerPath);
            return;
        }

        QFile installerFile(installerPath);
        if (!installerFile.open(QIODevice::ReadOnly)) {
            AppLog::error(QString("downloaded update could not be read back: %1")
                .arg(installerFile.errorString()));
            QFile::remove(installerPath);
            return;
        }
        // Hash what is actually on disk, since that is the file that would be executed —
        // not a copy of it held in memory.
        QCryptographicHash hash(QCryptographicHash::Sha256);
        const bool hashed = hash.addData(&installerFile);
        installerFile.close();
        if (!hashed) {
            AppLog::error("downloaded update could not be hashed");
            QFile::remove(installerPath);
            return;
        }

        const QString actualHash = QString::fromLatin1(hash.result().toHex());
        if (QString::compare(actualHash, sha256, Qt::CaseInsensitive) != 0) {
            QFile::remove(installerPath);
            AppLog::error(QString("update installer SHA-256 mismatch; expected=%1; actual=%2")
                .arg(sha256, actualHash));
            return;
        }

        m_installerPath = installerPath;
        m_verified = true;
        AppLog::info(QString("update installer verified; version=%1; sha256=%2")
            .arg(version, actualHash));
        emit readyToInstall(version);
    });
}

void UpdateInstaller::applyAndRestart() {
    if (!m_verified || m_installerPath.isEmpty()) {
        AppLog::error("update install requested without a verified installer");
        return;
    }

    if (!Platform::supportsSelfUpdate()) {
        // Said plainly rather than silently doing nothing: on platforms where updates
        // belong to a package manager, an app that claims to have installed one is worse
        // than an app that admits it cannot.
        AppLog::warn("self-update is not supported on this platform; installer left at "
                     + m_installerPath);
        emit selfUpdateUnsupported(m_installerPath);
        return;
    }

    if (!QProcess::startDetached(m_installerPath, Platform::silentInstallArguments())) {
        AppLog::error("verified update installer could not be started");
        return;
    }

    AppLog::info("verified update installer started; handing over and exiting");
    QCoreApplication::quit();
}
