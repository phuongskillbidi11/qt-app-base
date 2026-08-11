#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

// HTTPS fetching via Windows' built-in curl.exe, which uses Schannel — the operating
// system's own TLS stack.
//
// Deliberately NOT QNetworkAccessManager. Qt 5.15 performs HTTPS through OpenSSL, which
// is not part of Qt and would have to be shipped alongside the app; without it every
// request fails with "TLS initialization failed". The only OpenSSL that Qt 5.15.2 can
// load is the 1.1.x branch, which reached end of life in September 2023 — bundling it
// would mean shipping unpatched crypto to factory machines and owning its security
// updates indefinitely. curl.exe has been part of Windows since 10 1803 and is patched
// by Windows Update.
//
// QProcess is asynchronous, so this satisfies the "never block the UI thread" rule
// (spec.md R6) without introducing threads.
class CurlClient : public QObject {
    Q_OBJECT
public:
    explicit CurlClient(QObject *parent = nullptr);

    // Fetch a URL into memory. onDone(ok, body, errorText).
    void fetch(const QUrl &url,
               std::function<void(bool, const QByteArray &, const QString &)> onDone);

    // Download a URL to a file. onDone(ok, errorText).
    void download(const QUrl &url, const QString &destinationPath,
                  std::function<void(bool, const QString &)> onDone);

    // Absolute path to the system curl.exe, or an empty string if it is missing.
    static QString curlPath();

private:
    void run(const QStringList &arguments, int timeoutSeconds,
             std::function<void(bool, const QByteArray &, const QString &)> onDone);
};
