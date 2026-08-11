#include "thememanager.h"

#include <QApplication>
#include <QFile>

#include <cstdio>

// Guards the failure mode that is hardest to notice: a theme that does not load.
// Nothing crashes, nothing logs by default, the app just looks plain — and "it looks a
// bit off" is not a bug report anyone acts on. Two separate causes produce it:
// CMAKE_AUTORCC not set (the .qrc is never compiled), and a .qrc inside a static library
// whose initialiser the linker discards.
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    bool allPassed = true;
    const auto check = [&allPassed](bool passed, const char *message) {
        std::printf("%s: %s\n", passed ? "PASS" : "FAIL", message);
        allPassed = allPassed && passed;
    };

    ThemeManager::initResources();

    for (const char *path : {":/styles/theme_light.qss", ":/styles/theme_dark.qss"}) {
        QFile file(QString::fromLatin1(path));
        const bool opened = file.open(QIODevice::ReadOnly | QIODevice::Text);
        const qint64 size = opened ? file.size() : 0;
        std::printf("%s: %s exists and is %lld bytes\n",
                    (opened && size > 0) ? "PASS" : "FAIL", path,
                    static_cast<long long>(size));
        allPassed = allPassed && opened && size > 0;
    }

    check(ThemeManager::apply(ThemeManager::Light), "the light theme applies");
    check(!qApp->styleSheet().isEmpty(), "the application stylesheet is not empty");
    const QString light = qApp->styleSheet();

    check(ThemeManager::apply(ThemeManager::Dark), "the dark theme applies");
    check(qApp->styleSheet() != light,
          "dark and light produce different stylesheets");
    check(ThemeManager::current() == ThemeManager::Dark,
          "current() reports what was last applied");

    return allPassed ? 0 : 1;
}
