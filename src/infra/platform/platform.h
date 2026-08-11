#pragma once

#include <QString>
#include <QStringList>

// Everything in this project that differs between operating systems lives behind this
// header. There is exactly one implementation file per platform (platform_win.cpp,
// platform_unix.cpp) and CMake picks one; no other file in the base may contain a
// platform #ifdef.
//
// The rule exists because the code this base was extracted from had its Windows
// assumptions spread across main.cpp, mainwindow.cpp and three files under update/ —
// which is invisible until the day someone tries to build on Linux and discovers the
// port is not a port but a rewrite.
namespace Platform {

// Absolute path to a curl executable, or an empty string when none is present.
//
// Deliberately not "whatever curl is on PATH": on Windows the System32 copy is resolved
// by absolute path so a curl earlier on PATH cannot be substituted, and it uses Schannel
// (the OS TLS stack), which is why this base does not need OpenSSL shipped alongside Qt.
QString curlExecutablePath();

// File name to download an update installer as, e.g. "MyApp-setup.exe" on Windows.
QString installerFileName(const QString &applicationName);

// Arguments that make the downloaded installer run unattended and hand control back.
QStringList silentInstallArguments();

// True when this platform can install an update by executing the downloaded file.
// False on platforms where updates are managed elsewhere (a package manager, an app
// store), in which case the app should tell the user rather than pretend it updated.
bool supportsSelfUpdate();

// Install a handler that writes a log line when the process dies on a fault, so an
// abnormal death still leaves evidence. A no-op where the platform has no equivalent.
void installCrashHandler();

}  // namespace Platform
