#include "thememanager.h"

#include "app_log.h"
#include "app_settings.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>

ThemeManager::Theme ThemeManager::s_current = ThemeManager::Light;

// Resources compiled into a STATIC library are not registered automatically — the linker
// drops the generated initialiser because nothing references it. Without this the app
// runs with an empty stylesheet and simply looks unstyled, with no error anywhere. In the
// project this base came from, the equivalent failure (AUTORCC never enabled) went
// unnoticed for weeks: Dark mode toggled its checkmark and changed nothing.
void ThemeManager::initResources() {
    Q_INIT_RESOURCE(styles);
}

bool ThemeManager::apply(Theme t) {
    initResources();
    const QString path = (t == Dark) ? QStringLiteral(":/styles/theme_dark.qss")
                                     : QStringLiteral(":/styles/theme_light.qss");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Loudly, not silently. A theme that fails to load is a build/packaging fault,
        // and the only symptom a user can report is "it looks wrong" — useless without
        // this line in the log.
        AppLog::error("theme stylesheet could not be opened: " + path
                      + " (is the .qrc compiled and the resource initialised?)");
        return false;
    }
    QTextStream in(&f);
    const QString sheet = in.readAll();
    if (sheet.isEmpty()) {
        AppLog::error("theme stylesheet is empty: " + path);
        return false;
    }
    qApp->setStyleSheet(sheet);
    s_current = t;
    return true;
}

ThemeManager::Theme ThemeManager::current() { return s_current; }

// Persisted through AppSettings rather than QSettings directly: Phase 12b keeps every
// settings key in src/settings/app_settings.* so there is one place to look for what the
// app writes to disk.
void ThemeManager::save(Theme t) {
    AppSettings settings;
    settings.setTheme(t == Dark ? QStringLiteral("dark") : QStringLiteral("light"));
}

ThemeManager::Theme ThemeManager::restore() {
    AppSettings settings;
    return settings.theme() == QLatin1String("dark") ? Dark : Light;
}
