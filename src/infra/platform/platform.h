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

struct SystemResources {
    bool valid = false;
    qint64 availableRamBytes = 0;
    qint64 totalRamBytes = 0;
    qint64 availableDiskBytes = 0;
    qint64 totalDiskBytes = 0;
};

struct SystemInfo {
    QString chipName;       // e.g. "ARMv8 Processor rev 4" -- never empty, see parseCpuInfo()
    QString architecture;   // e.g. "aarch64", "x86_64" -- from QSysInfo::currentCpuArchitecture()
    QString osVersion;      // e.g. "Debian GNU/Linux 11 (bullseye)" -- from QSysInfo::prettyProductName()
};

// Pure: parses /proc/meminfo's own text format (one "Key:    12345 kB" line per row).
// Prefers MemAvailable (accounts for reclaimable caches, present since Linux 3.14); falls
// back to MemFree if MemAvailable is absent (older kernels). Returns false if neither line
// is found. Total always comes from MemTotal.
bool parseMemInfo(const QString &procMeminfoContent, qint64 *availableBytesOut,
                  qint64 *totalBytesOut);

// Pure: warn-worthy resource headroom, per this base's own defaults (named threshold
// constants in platform_common.cpp -- see spec.md D3 of .plans/2026-08-22-resource-check).
// A consuming application with different memory needs may want its own thresholds instead
// of calling this directly.
bool isResourceLow(const SystemResources &resources);

// I/O: queries current RAM and disk headroom. `logDirectory` is used to resolve which
// filesystem/disk to measure for the disk figures -- pass AppLog::filePath()'s directory so
// the check reflects the same disk the app actually writes its own log/settings/store to. A
// consuming app whose own data lives on a different mount point should call this again with
// that path rather than assuming one call covers everything.
SystemResources checkSystemResources(const QString &logDirectory);

// Pure: extracts a human-readable chip/SoC name from /proc/cpuinfo's own text. Tries, in
// order: a "Hardware" line (common on ARM SBCs), a "Model" line (some ARM boards), a
// "model name" line (the x86 convention) -- and if none of those exist, falls back to a
// clearly-labeled string built from the numeric "CPU implementer"/"CPU part" codes rather
// than silently returning an empty name.
QString parseCpuInfo(const QString &procCpuinfoContent);

// I/O: queries chip/architecture/OS identity for the System Info view.
SystemInfo querySystemInfo();

}  // namespace Platform
