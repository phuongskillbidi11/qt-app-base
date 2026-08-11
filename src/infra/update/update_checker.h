#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class CurlClient;
class QTimer;

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);
    void start();

signals:
    void updateAvailable(const QString &version, const QUrl &url, const QString &sha256);

private:
    void checkNow();

    CurlClient *m_http = nullptr;
    QTimer *m_timer = nullptr;
    bool m_started = false;
};
