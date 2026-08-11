#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class CurlClient;

class UpdateInstaller : public QObject {
    Q_OBJECT
public:
    explicit UpdateInstaller(QObject *parent = nullptr);
    void download(const QString &version, const QUrl &url, const QString &sha256);
    void applyAndRestart();

signals:
    void readyToInstall(const QString &version);
    // Emitted instead of installing where Platform::supportsSelfUpdate() is false. The
    // path is the verified download, so the UI can tell the user where it is.
    void selfUpdateUnsupported(const QString &installerPath);

private:
    CurlClient *m_http = nullptr;
    QString m_installerPath;
    bool m_verified = false;
};
