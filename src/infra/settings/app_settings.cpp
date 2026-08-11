#include "app_settings.h"

#include <QSettings>

namespace {
constexpr char kThemeKey[] = "ui/theme";
}

QString AppSettings::theme() const {
    return value(kThemeKey, "light").toString();
}

void AppSettings::setTheme(const QString &themeName) {
    setValue(kThemeKey, themeName);
}

QVariant AppSettings::value(const QString &key, const QVariant &fallback) const {
    QSettings settings;
    return settings.value(key, fallback);
}

void AppSettings::setValue(const QString &key, const QVariant &newValue) {
    QSettings settings;
    settings.setValue(key, newValue);
}

QString stripSecret(const QString &text, const QString &secretKey) {
    const QString needle = secretKey + "=";
    const int keyIndex = text.indexOf(needle);
    if (keyIndex < 0) {
        return text;
    }

    const int valueStart = keyIndex + needle.size();
    const int valueEnd = text.indexOf(',', valueStart);
    if (valueEnd < 0) {
        return text.left(valueStart);
    }
    return text.left(valueStart) + text.mid(valueEnd);
}
