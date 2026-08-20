#pragma once

#include <QString>

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

// File name to download a verified update as, e.g. "MyApp-setup.exe" on Windows or
// "MyApp-update" on Linux (a plain binary, not a package).
QString installerFileName(const QString &applicationName);

// Starts installing an already-verified update. `downloadedPath` is the verified file on
// disk; `currentExecutablePath` is this process's own executable path (only used by
// platforms that replace it in place). Must only be called after supportsSelfUpdate()
// returned true. Returns true if installation was started successfully, in which case the
// caller must quit immediately, handing control to whatever this started. Returns false
// (with *errorOut set to a log-worthy message) if it could not even be started, in which
// case the caller must NOT quit.
bool startSelfInstall(const QString &downloadedPath, const QString &currentExecutablePath,
                       QString *errorOut);

// True when this platform can install an update by itself. False on platforms where
// updates are managed elsewhere (a package manager, an app store), in which case the app
// should tell the user rather than pretend it updated.
bool supportsSelfUpdate();

// Install a handler that writes a log line when the process dies on a fault, so an
// abnormal death still leaves evidence. A no-op where the platform has no equivalent.
void installCrashHandler();

}  // namespace Platform
