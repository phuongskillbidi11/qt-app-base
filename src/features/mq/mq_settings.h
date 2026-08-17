#pragma once

#include "app_settings.h"

#include <QString>
#include <QtGlobal>

class MqSettings : public AppSettings {
public:
    QString host() const;
    quint16 port() const;
    QString vhost() const;
    QString user() const;
    QString password() const;
    qint64 dbSizeWarningThresholdBytes() const;

    void setHost(const QString &value);
    void setPort(quint16 value);
    void setVhost(const QString &value);
    void setUser(const QString &value);
    // An empty value leaves the stored password unchanged -- mirrors demo-server's
    // AmqpPublisher::applySettings(), so a blank field in the settings dialog never means
    // "erase the credential".
    void setPassword(const QString &value);
    void setDbSizeWarningThresholdBytes(qint64 value);

    bool isLocalBroker() const;
    QString connectionDescription() const;
};
