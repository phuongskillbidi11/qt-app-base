#pragma once
#include <QString>

// Nap stylesheet tu :/styles va nho lua chon vao QSettings.
class ThemeManager {
public:
    enum Theme { Light, Dark };

    // Returns false when the stylesheet could not be loaded — see the .cpp for why that
    // must never be silent. Callers may ignore it; the failure is always logged.
    static bool apply(Theme t);

    // Registers the compiled-in .qrc. Called by apply(); exposed so a self-test can
    // verify the resource exists without creating a QApplication.
    static void initResources();
    static Theme current();
    static void save(Theme t);
    static Theme restore();          // mac dinh Light

private:
    static Theme s_current;
};
