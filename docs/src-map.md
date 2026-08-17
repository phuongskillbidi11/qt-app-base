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

### `src/infra/db/`
Vendored SQLite plus the `Database`/`Statement` wrapper; all database access goes through one
`Worker`, so the wrapper needs no mutex.

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

## `src/features/mq/` — a feature the base ships, not an app's own

AMQP/RabbitMQ client: `mq_connection` (socket + `AMQP::ConnectionHandler`), `mq_service`
(channel, publish/consume/ack/reject, `AMQP::mandatory` + `recall()` so an unroutable message
is reported instead of silently dropped), `mq_codec` (the envelope, pure), `mq_settings`
(broker host/port/vhost/credentials, through `AppSettings`), `mq_tab` and
`mq_settings_dialog` (the widgets). No self-declared topology — the app fails loudly against a
broker that hasn't had `definitions.json` applied to it, rather than creating what it expects.

`mq_store` durably stores each opaque payload with its envelope, direction and acknowledgement
metadata plus presenter-supplied `display_title`/`search_text`; it never reads `payload`, and its
query schema lives entirely in those derived fields plus envelope columns.

`qt-mq-lab` is this feature's reference consumer: `use_base_feature(mq UI LINK amqpcpp)`, its
own submodule supplying the `amqpcpp` target. The base does not vendor AMQP-CPP itself — see
"Adding a feature the base ships" below for why.

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

### Adding a feature the base ships

`add_feature_module()` only ever finds a feature living in the **consuming app's own**
`src/features/` — it resolves `CMAKE_SOURCE_DIR`, which CMake fixes to the outermost project's
source directory for the whole configure run, never the base's, no matter how deeply
`add_subdirectory` nests it. A feature the base itself ships (like `mq`) needs
`use_base_feature(<name> [UI] [LINK <targets>...])` instead
(`cmake/UseBaseFeature.cmake`), which resolves `qt-app-base_SOURCE_DIR` — a variable CMake
sets automatically from the base's own `project(qt-app-base ...)` call, visible from the
consumer's scope the moment `add_subdirectory` returns.

The base ships the feature's **source only**. A vendored dependency it needs (AMQP-CPP for
`mq`) is *not* pulled into the base itself — each consuming app still vendors it and passes
its target name via `LINK`, exactly as it would for its own `add_feature_module`-based feature.
`appinfra` links `Qt5::Core` and `Qt5::Network` only, deliberately; folding a protocol library
into the base is the first crack in that.

**A submodule pin is not a live reference.** If your app consumes the base through a git
submodule (as `qt-mq-lab` does, at `external/base`), editing a *different* local checkout of
`qt-app-base` changes nothing about what your app builds — the submodule is frozen at whatever
commit it was last pointed at. A change to the base only reaches a consumer once it is pushed
and the consumer's submodule pointer is explicitly updated (`git -C external/base fetch && git
-C external/base checkout <commit>`, then commit that pointer change in the consumer's own
repo). Confusing the two checkouts costs a full "why is my macro unknown" debugging cycle —
it already has, once.
