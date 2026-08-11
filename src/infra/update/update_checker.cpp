#include "update_checker.h"

#include "app_log.h"

#include "curl_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QVector>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0-dev"
#endif

namespace {

constexpr int kInitialCheckDelayMs = 3000;
constexpr int kCheckIntervalMs = 6 * 60 * 60 * 1000;

// Both come from CMake, never hardcoded here.
//
// APP_UPDATE_REPO must name the app's OWN repository. Two applications sharing one repo
// would share `releases/latest`, so each would find whichever app was released last and
// install the other's installer — over itself, with a SHA-256 that verifies correctly
// because the file really is the one the release advertises. That is why the base gives
// every app its own repo and takes this as a parameter.
#ifndef APP_UPDATE_REPO
#define APP_UPDATE_REPO ""
#endif
#ifndef APP_INSTALLER_ASSET
#define APP_INSTALLER_ASSET ""
#endif

const QString kLatestReleaseUrl =
    QStringLiteral("https://api.github.com/repos/%1/releases/latest")
        .arg(QString::fromLatin1(APP_UPDATE_REPO));
const QString kInstallerAssetName = QString::fromLatin1(APP_INSTALLER_ASSET);

bool parseVersion(const QString &version, QVector<int> *components) {
    components->clear();
    const QStringList parts = version.split('.');
    if (parts.isEmpty()) {
        return false;
    }

    const QRegularExpression leadingInteger("^(\\d+)");
    for (const QString &part : parts) {
        const QRegularExpressionMatch match = leadingInteger.match(part);
        if (!match.hasMatch()) {
            return false;
        }
        bool ok = false;
        const int value = match.captured(1).toInt(&ok);
        if (!ok) {
            return false;
        }
        components->append(value);
    }
    return true;
}

bool isNewerVersion(const QString &remoteVersion, const QString &localVersion, bool *valid) {
    QVector<int> remote;
    QVector<int> local;
    *valid = parseVersion(remoteVersion, &remote) && parseVersion(localVersion, &local);
    if (!*valid) {
        return false;
    }

    const int componentCount = qMax(remote.size(), local.size());
    for (int i = 0; i < componentCount; ++i) {
        const int remotePart = i < remote.size() ? remote.at(i) : 0;
        const int localPart = i < local.size() ? local.at(i) : 0;
        if (remotePart != localPart) {
            return remotePart > localPart;
        }
    }
    return false;
}

}  // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent),
      m_http(new CurlClient(this)),
      m_timer(new QTimer(this)) {
    m_timer->setInterval(kCheckIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &UpdateChecker::checkNow);
}

void UpdateChecker::start() {
    if (m_started) {
        return;
    }
    m_started = true;

    // A local build is versioned 0.0.0-dev, which compares lower than every published
    // release — so without this it would offer to "update" a developer's own build down
    // to the latest release, and accepting would overwrite the very build being tested.
    // Released builds get their version from the tag and are unaffected.
    if (QString::fromLatin1(APP_VERSION).contains(QLatin1String("-dev"))) {
        AppLog::info("update check disabled for a development build");
        return;
    }

    QTimer::singleShot(kInitialCheckDelayMs, this, &UpdateChecker::checkNow);
    m_timer->start();
}

void UpdateChecker::checkNow() {
    AppLog::info("update check started");

    m_http->fetch(QUrl(kLatestReleaseUrl),
                  [this](bool ok, const QByteArray &body, const QString &error) {
        if (!ok) {
            AppLog::warn(QString("update check failed: %1").arg(error));
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            AppLog::warn(QString("update check returned malformed JSON: %1")
                .arg(parseError.errorString()));
            return;
        }

        const QJsonObject release = document.object();
        if (release.value("prerelease").toBool()) {
            AppLog::warn("update check ignored a prerelease");
            return;
        }

        QString remoteVersion = release.value("tag_name").toString();
        if (remoteVersion.startsWith('v')) {
            remoteVersion.remove(0, 1);
        }
        if (remoteVersion.isEmpty()) {
            AppLog::warn("update check response has no tag_name");
            return;
        }

        QJsonObject installerAsset;
        const QJsonArray assets = release.value("assets").toArray();
        for (const QJsonValue &value : assets) {
            const QJsonObject asset = value.toObject();
            if (asset.value("name").toString() == kInstallerAssetName) {
                installerAsset = asset;
                break;
            }
        }
        if (installerAsset.isEmpty()) {
            AppLog::warn("update check found no installer asset named " + kInstallerAssetName);
            return;
        }

        const QUrl downloadUrl(installerAsset.value("browser_download_url").toString());
        QString digest = installerAsset.value("digest").toString();
        if (!digest.startsWith("sha256:", Qt::CaseInsensitive)) {
            AppLog::warn("update check installer asset has no SHA-256 digest");
            return;
        }
        digest.remove(0, 7);
        if (!downloadUrl.isValid() || downloadUrl.isEmpty() || digest.isEmpty()) {
            AppLog::warn("update check installer asset metadata is incomplete");
            return;
        }

        const QString localVersion = QString::fromLatin1(APP_VERSION);
        bool versionsValid = false;
        const bool newer = isNewerVersion(remoteVersion, localVersion, &versionsValid);
        if (!versionsValid) {
            AppLog::warn(QString("update check could not compare versions local=%1 remote=%2")
                .arg(localVersion, remoteVersion));
            return;
        }

        AppLog::info(QString("update check found release version=%1; current=%2")
            .arg(remoteVersion, localVersion));
        if (newer) {
            emit updateAvailable(remoteVersion, downloadUrl, digest);
        }
    });
}
