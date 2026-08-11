# Things that cost days, written down so they cost minutes

Every entry here was learned by losing time to it in a real project. Most share one
property: **the failure is silent.** Nothing crashes, nothing logs, and the symptom is
something vague like "it looks wrong" or "it feels slow".

---

## Qt + CMake

**`CMAKE_AUTORCC` off means no `.qrc` is ever compiled — silently.**
`QFile::open(":/styles/theme.qss")` just returns `false`. The app runs with an empty
stylesheet and looks merely unstyled. Symptom in the source project: a Dark mode menu item
that toggled its own checkmark and changed nothing else, for weeks.
*Guard:* `tests/ui_selftest` opens both stylesheets and asserts they are non-empty.

**A `.qrc` inside a STATIC library needs `Q_INIT_RESOURCE`.**
The linker drops the generated initialiser because nothing references it. Same silent
symptom as above. `ThemeManager::initResources()` exists solely for this.

**After adding a Qt module, re-run `windeployqt`.**
A build directory receives its Qt DLLs from past `windeployqt` runs, not from CMake.
Add `Qt5::Network` and the debug build then fails to start with a loader error — and
because the loader fails *before* `main()`, **there is no log at all**, just a process that
vanishes. The release path is unaffected: `windeployqt` scans the exe's imports.

**`QTabBar::tabSizeHint()` measures with the widget's font, not the sub-control's.**
Styling `QTabBar::tab { font-weight: 600 }` in QSS makes the painted text wider than the
rect the widget reserved, and characters get clipped. Put the weight on `QTabBar` itself.

---

## Threading

**Never call a blocking function from the GUI thread. Not once.**
Measured in the source project: a device SDK's `connect()` took 2.0 s against an
unreachable host, retried every 5 s, leaving the UI unresponsive **21% of the time** —
permanently. It went unnoticed for months because a *reachable* device connects instantly
and never exercises the path. Everything blocking goes through `Worker::call()`.

**Retry with a backoff ladder, not a flat interval.**
Same measurement: the ladder in `ConnectionState` took that 21% down to 3.4%. Keep the
first rungs short — the common transient failure is a peer that briefly went away, and
recovery from that must stay fast.

---

## Networking

**Qt 5.15 needs OpenSSL for HTTPS, and it is not shipped with Qt.**
Every request fails with "TLS initialization failed". The only OpenSSL Qt 5.15 loads is
1.1.x, end-of-life since September 2023 — bundling it means shipping unpatched crypto and
owning its updates forever. Use the OS `curl` instead (Schannel on Windows), resolved by
**absolute path** so a `curl` earlier on `PATH` cannot be substituted.

**Do not call `abort()` on a `QNetworkReply` that has already finished.**
Qt sets the error twice and logs *"Internal problem, this method must only be called
once"*, and a following `readAll()` hits a closed device. Guard with `isRunning()` and
`isOpen()`. A timeout timer firing against an instantly-refused connection is enough to
trigger it.

**Keep `qInstallMessageHandler` and route Qt warnings into the log.**
Both defects above surfaced only as Qt warnings. Nowhere else.

---

## Packaging and updates

**A per-user install is what makes silent self-update possible.**
`%LOCALAPPDATA%\Programs` with `PrivilegesRequired=lowest`. A factory operator account is
usually not an administrator, and `Program Files` is not writable without elevation, so an
app installed there can never update itself unattended.

**One repository per application.**
Two apps sharing a repo share `releases/latest`, so each finds whichever was released last
and installs the other's installer over itself — with a SHA-256 that verifies correctly,
because the file genuinely is the one the release advertises. Hence `APP_UPDATE_REPO`.

**Skip the update check for `-dev` builds.**
`0.0.0-dev` compares lower than every release, so a developer's own build offers to
"update" itself down to the last release and overwrite the thing being tested.

**Verify the hash of the file on disk, not a copy in memory.**
The file on disk is the one that gets executed.

---

## Windows behaviour

**One instance, or none.**
Without a guard, a user who opens the app from a desktop icon — not realising it already
auto-started at logon — creates a second copy. Where a resource allows only one client
(a serial device, a panel, an exclusive lock), the two fight forever and the operator sees
a permanent "Reconnecting…". `SingleInstance` raises the existing window instead.

**Windows Error Reporting does not always record a crash.**
The source project had an access violation on every shutdown that produced no `.dmp`, no
Application event-log entry, nothing. It became visible only because the app wrote its own
log with `SetUnhandledExceptionFilter`. If it matters that you learn about a crash, log it
yourself.

**`GetWindowRect` returns logical pixels for a non-DPI-aware process.**
UI Automation's `BoundingRectangle` returns physical ones. Size screenshot bitmaps from
the latter or the capture comes out cropped.

---

## Process

**A green board of automated gates proves less than it looks.**
Grep gates prove an absence; a diff-shape gate proves a shape; self-tests prove pure
functions. None can see *"these two callers disagree about a string format"* or *"this is
O(n²) at real data volume"*. Three defects of exactly that kind passed a fully green
gate board in one phase and were found only by reading the code with the real device's
numbers in mind.

**A gate that fires on correct code is worse than no gate.**
One gate searched for a bare function name and matched the comment explaining why that
function was deliberately not called. A gate that cries wolf teaches people to skim past
red output.

**The person who writes the code should not be the only one who verifies it.**
A delete button was written and self-verified by the same author; the verification
happened to cover only the read paths, and the button shipped reporting success while
doing nothing.
