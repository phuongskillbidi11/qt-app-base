#include "curl_client.h"

#include "app_log.h"
#include "platform.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStringList>

CurlClient::CurlClient(QObject *parent) : QObject(parent) {
}

QString CurlClient::curlPath() {
    // Resolved once, and by the platform layer — this file must stay free of OS specifics.
    static const QString path = QDir::toNativeSeparators(Platform::curlExecutablePath());
    return path;
}

void CurlClient::run(const QStringList &arguments, int timeoutSeconds,
                     std::function<void(bool, const QByteArray &, const QString &)> onDone) {
    const QString curl = curlPath();
    if (curl.isEmpty()) {
        AppLog::warn("no system curl found; update checking is unavailable");
        onDone(false, QByteArray(), QStringLiteral("curl not found"));
        return;
    }

    auto *process = new QProcess(this);
    QStringList fullArguments;
    // -sS: quiet, but still report errors.  -L: follow redirects (GitHub asset URLs
    // redirect to objects.githubusercontent.com).  --fail: a non-2xx response becomes a
    // non-zero exit code instead of an error page written to stdout.
    fullArguments << QStringLiteral("-sS") << QStringLiteral("-L") << QStringLiteral("--fail")
                  << QStringLiteral("--max-time") << QString::number(timeoutSeconds)
                  << arguments;

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [process, onDone](int exitCode, QProcess::ExitStatus status) {
                const QByteArray out = process->readAllStandardOutput();
                const QString err = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
                process->deleteLater();
                if (status != QProcess::NormalExit || exitCode != 0) {
                    onDone(false, QByteArray(),
                           err.isEmpty() ? QStringLiteral("curl exit code %1").arg(exitCode) : err);
                    return;
                }
                onDone(true, out, QString());
            });
    connect(process, &QProcess::errorOccurred, this,
            [process, onDone](QProcess::ProcessError) {
                const QString err = process->errorString();
                process->deleteLater();
                onDone(false, QByteArray(), err);
            });

    process->start(curl, fullArguments);
}

void CurlClient::fetch(const QUrl &url,
                       std::function<void(bool, const QByteArray &, const QString &)> onDone) {
    run({QStringLiteral("-H"), QStringLiteral("Accept: application/vnd.github+json"),
         QStringLiteral("-H"), QStringLiteral("User-Agent: ZKTecoProtocol"),
         url.toString()},
        30, std::move(onDone));
}

void CurlClient::download(const QUrl &url, const QString &destinationPath,
                          std::function<void(bool, const QString &)> onDone) {
    run({QStringLiteral("-H"), QStringLiteral("User-Agent: ZKTecoProtocol"),
         QStringLiteral("-o"), QDir::toNativeSeparators(destinationPath),
         url.toString()},
        600,
        [onDone](bool ok, const QByteArray &, const QString &error) { onDone(ok, error); });
}
