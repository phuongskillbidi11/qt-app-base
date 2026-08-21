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

### `protocol/protocol_driver.h`
The interface every field-protocol connection reports through: `isReady()`,
`errorString()`, `connectionStateChanged(bool)`. Deliberately does not include a "start
connecting" method — every protocol's real connect parameters differ, and one conformer
(`MqConnection`) is not enough evidence to design a generic one from.

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

Covers: the curl path, the installer file name, how a verified update is actually
installed (Inno Setup on Windows, an atomic same-directory binary swap on Linux), and the
crash handler.

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

### `src/features/modbus/` — a second `ProtocolDriver` conformer

Modbus TCP: `modbus_connection` (`QTcpSocket` + `ConnectionState`, implements
`ProtocolDriver` — "ready" is the TCP connect succeeding, no application-level handshake
unlike `mq`; sends/receives all 8 standard Modbus function codes (01/02/03/04/05/06/15/16)
via one method per function, tracking which single request is outstanding so responses
decode with the right codec function, and a per-request timeout (`setRequestTimeoutMs()`)
that fails a stuck request cleanly instead of blocking every future one), `modbus_codec`
(MBAP framing, pure, one request/response struct pair per function code -- registers as
`QVector<uint16_t>`, coils/discrete inputs as `QVector<bool>` with verified LSB-first
bit-packing). The demo's own UI selects among all 8 via one Function dropdown rather than
one widget row per function -- see `.plans/2026-08-21-modbus-unified-function-ui/spec.md`.
Builds
standalone within `qt-app-base` itself (`add_feature_module`); consumed by
`demo/demowindow.cpp`'s live Modbus group.

### `src/features/c3protocol/` — a third `ProtocolDriver` conformer

ZKTeco C3/InBio access-control panel wire protocol (TCP port 4370) -- reverse-engineered
(no official ZKTeco spec exists), learned by reading `zkaccess-c3-py` (GPL-3.0) as
documentation only, then written from scratch in C++. `c3_connection` (`QTcpSocket` +
`ConnectionState`, implements `ProtocolDriver` -- "ready" follows an application-level
CONNECT_SESSION handshake, like `mq` and unlike `modbus`), `c3_codec` (frame CRC-16/ARC +
CONNECT_SESSION/DISCONNECT encode/decode, pure). Scoped to exactly the session handshake --
no device data, no control commands, no file I/O yet; see
`.plans/2026-08-21-c3-protocol-session/spec.md` for why. Builds standalone within
`qt-app-base` itself (`add_feature_module`), not consumed by any app yet.
Also sends CONTROL commands (door/aux output, cancel alarm, restart, normal-open state) via
`C3Connection`'s six control methods -- door/aux *output* commands physically actuate real
hardware; see `.plans/2026-08-21-c3-control/spec.md` for the safety boundary this feature's
own live testing follows (only CANCEL_ALARM and one confirmed, short-duration door unlock are
ever exercised against real hardware automatically -- RESTART_DEVICE and the normal-open-state
change are encode-only, never sent live without a separate, explicit go-ahead).
Also reads any table via `DATATABLE_CFG` + `GETDATA` (kv-text table-schema discovery plus
binary packed records) via `C3Connection::requestTableData(tableName)` -- proven against
`user`, `transaction`, and `template`; see `.plans/2026-08-21-c3-getdata-user-table/spec.md`
and `.plans/2026-08-21-c3-transaction-template-tables/spec.md`. No `templatev10` (its `B`-typed
field is unhandled), no writes yet.
Also reads the real-time event/door-alarm log (`RTLOG_BINARY`, 16-byte fixed binary records,
with an automatic fallback to `RTLOG_KEYVALUE` text mode) via
`C3Connection::requestRealtimeLog()` -- a separate, simpler command family from
`DATATABLE_CFG`/`GETDATA`, needing no schema discovery; see
`.plans/2026-08-21-c3-rtlog/spec.md`.
Also reads device parameters (`GETPARAM` -- serial number, firmware version, device name,
door/aux counts, or any other named parameter the panel supports) via
`C3Connection::requestDeviceParams(names)`, reusing the same kv-text parser as
`DATATABLE_CFG`/`RTLOG_KEYVALUE`; see `.plans/2026-08-21-c3-getparam/spec.md`. Read-only --
no parameter-write command implemented yet.

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
