# What already exists in src/

Read this before planning anything. **Do not re-invent any of it**, and do not add a
second way to do something that already has one.

The layering is `app → appui → appinfra → appcore`, enforced by what each CMake target
links — not by convention. A violation is a compile error.

---

## `src/core/` — links `Qt5::Core` ONLY

A `#include <QWidget>` in here does not build. That is the point.

### `connection_state.{h,cpp}`
Connect / disconnect / heartbeat / automatic reconnect, with a backoff ladder of
**5s, 5s, 5s, 15s, 15s, 30s, then 60s held**.

Knows nothing about any device: the application supplies `connect`, `probe` and
`disconnect` as callables. That is why the entire state machine is testable with no
hardware, and why the first three rungs are short — the common transient failure is a
peer that briefly went away, and recovery from that must stay fast.

**ONE chokepoint, ONE signal: `connectionStateChanged(bool)`.** Everything downstream
listens to that and nothing else. In the project this came from, heartbeat monitoring and
auto-reconnect were added later without changing a single line in any of seven UI tabs.

### `async/worker.{h,cpp}`
The **only** place allowed to run a blocking call.

`Worker::call(job, context, onDone)` runs `job` on a worker thread and delivers the result
on `context`'s thread. The `context` is held in a `QPointer`, so a widget closed
mid-operation cannot be delivered to.

Qt's own asynchronous APIs — `QNetworkAccessManager`, `QProcess` — **must not** be wrapped
in it.

*Why it exists:* a device SDK's `connect()` was once called synchronously from the GUI
thread. Against an unreachable device each attempt froze the window for 2.0 s; retried
every 5 s, the app was unresponsive **21% of the time**, permanently, and nobody noticed
for months because a reachable device connects instantly.

---

## `src/infra/` — `Qt5::Core` + `Qt5::Network`, no widgets

### `log/app_log.{h,cpp}`
Append-only, flushed per line (survives a force-kill), rotates at 1 MB.

**Inert until `AppLog::init()`.** A source file compiled into both the real application
and a self-test would otherwise write into the user's real diagnostic log and corrupt the
evidence it exists to preserve.

### `settings/app_settings.{h,cpp}`
The only place that touches `QSettings`. Applications subclass `AppSettings` and add typed
accessors — never scatter raw `QSettings` calls.

`stripSecret()` must be called before anything credential-like reaches disk or the log.
A settings file is trivially copied off a shared machine.

### `singleinstance/single_instance.{h,cpp}`
`QLocalServer` guard. A second launch raises the first window and exits 0.

*Why:* where a resource allows only one client, two instances fight forever and the user
sees a permanent "Reconnecting…".

### `update/`
GitHub Releases check → SHA-256 verified download → install. `curl_client`,
`update_checker`, `update_installer`.

- HTTPS goes through the **OS curl**, never Qt's SSL stack (Qt 5.15 needs OpenSSL, which
  is EOL and not shipped with Qt).
- `APP_UPDATE_REPO` and `APP_INSTALLER_ASSET` come from CMake. One repo per app.
- The `-dev` version is skipped, or a developer build offers to downgrade itself.

### `platform/`
**Every** OS difference lives here: `platform.h` plus one `.cpp` per platform.
There is no `#ifdef _WIN32` anywhere else in the repo, and adding one is a design error,
not a shortcut.

Covers: the curl path, the installer file name and its silent-install arguments, whether
self-update is possible at all, and the crash handler.

---

## `src/ui/` — `Qt5::Widgets`

### `theme/`
Light and dark QSS. `ThemeManager::apply()` returns `bool` and logs on failure — it must
never fail silently. `Q_INIT_RESOURCE` is required because the `.qrc` lives inside a
static library and the linker would otherwise drop its initialiser.

### `widgets/table_style.{h,cpp}`
`tuneTable`, `alignNumericColumn`, `attachEmptyHint`, `makeBarButton`, `repolish`.

**`alignNumericColumn()` walks every row.** Do not call it per insert — set the alignment
on the item as it is constructed. At a few thousand rows the per-insert version costs
millions of calls and looks like a hang.

---

## `tests/`
`core_selftest`, `infra_selftest`, `ui_selftest`, registered with CTest.

CI runs `ctest`. **There is no hand-maintained list of tests anywhere**, deliberately: the
project this came from kept one in its CI workflow, let it go one entry out of date, and
shipped a whole phase whose tests were built but never run while CI stayed green.

---

## Adding a feature

A feature is a **directory**, and it splits three ways:

```
src/features/<name>/            <name>_codec.{h,cpp}    pure logic, no I/O
                                <name>_service.{h,cpp}  I/O, through Worker
                                <name>_tab.{h,cpp}      widgets
src/features/<name>_selftest/   main.cpp                tests the codec
```

One line in `CMakeLists.txt`: `add_feature_module(<name> UI)`.

The self-test is discovered and registered automatically. **If a feature has no testable
part, the pure logic has not been separated from the I/O yet** — that is a design problem,
not a missing file.
