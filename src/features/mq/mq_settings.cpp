#include "mq_settings.h"

namespace {
constexpr char kHostKey[] = "mq/host";
constexpr char kPortKey[] = "mq/port";
constexpr char kVhostKey[] = "mq/vhost";
constexpr char kUserKey[] = "mq/user";
constexpr char kPasswordKey[] = "mq/password";
}

QString MqSettings::host() const {
    return value(kHostKey, "localhost").toString();
}

quint16 MqSettings::port() const {
    bool valid = false;
    const uint configuredPort = value(kPortKey, 5672).toUInt(&valid);
    return valid && configuredPort <= 65535
        ? static_cast<quint16>(configuredPort)
        : static_cast<quint16>(5672);
}

QString MqSettings::vhost() const {
    return value(kVhostKey, "/").toString();
}

QString MqSettings::user() const {
    return value(kUserKey, "dev").toString();
}

QString MqSettings::password() const {
    return value(kPasswordKey, QString()).toString();
}

void MqSettings::setHost(const QString &newValue) {
    setValue(kHostKey, newValue);
}

void MqSettings::setPort(quint16 newValue) {
    setValue(kPortKey, newValue);
}

void MqSettings::setVhost(const QString &newValue) {
    setValue(kVhostKey, newValue);
}

void MqSettings::setUser(const QString &newValue) {
    setValue(kUserKey, newValue);
}

void MqSettings::setPassword(const QString &newValue) {
    if (newValue.isEmpty()) {
        return;
    }
    setValue(kPasswordKey, newValue);
}

bool MqSettings::isLocalBroker() const {
    const QString configuredHost = host();
    return configuredHost.compare("localhost", Qt::CaseInsensitive) == 0
        || configuredHost == "127.0.0.1";
}

QString MqSettings::connectionDescription() const {
    const QString description =
        QString("host=%1,port=%2,vhost=%3,user=%4,password=%5")
            .arg(host())
            .arg(port())
            .arg(vhost())
            .arg(user())
            .arg(password());
    return stripSecret(description, "password");
}
