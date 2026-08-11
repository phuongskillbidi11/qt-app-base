#pragma once

#include <QString>
#include <QVariant>

// The one place in an application allowed to touch QSettings.
//
// Everything else asks this class, so there is a single list of keys, a single place to
// look when a value comes back wrong, and one chokepoint through which every write to
// disk passes. That last point is what makes a rule like "this credential is never
// persisted" checkable rather than aspirational.
//
// The base provides only what every app needs (the theme). Applications derive from this
// class and add their own typed accessors — never raw QSettings calls scattered around.
class AppSettings {
public:
    virtual ~AppSettings() = default;

    // "light" (default) or "dark".
    QString theme() const;
    void setTheme(const QString &value);

protected:
    // For derived classes' own keys. Keeps QSettings itself confined to app_settings.cpp.
    QVariant value(const QString &key, const QVariant &fallback = QVariant()) const;
    void setValue(const QString &key, const QVariant &value);
};

// Returns a "key=value,key=value" string with the value of `secretKey` blanked, keeping
// the key itself: "…,passwd=secret" -> "…,passwd=". Call before writing anything that may
// contain a credential to disk or to the log.
//
// This exists because a settings file is trivially copied off a shared machine, and a
// password sitting in one is a password you have given away. Blanking rather than
// removing keeps the string's shape valid for whatever parses it later.
QString stripSecret(const QString &text, const QString &secretKey);
