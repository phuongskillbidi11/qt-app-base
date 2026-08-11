# qt-app-base

A starting point for Qt 5 desktop applications: logging, settings, single-instance
guard, theming, self-update, a connection state machine, and a threading boundary —
plus the CMake and CI shape to go with them.

Extracted from a production Windows/Qt application, but **not Windows-only**: everything
platform-specific lives behind `src/infra/platform/`, with one implementation file per
platform and no `#ifdef` anywhere else.

## Layers

Dependencies point one way. The layering is enforced by what each target links, so a
violation is a **compile error**, not a code-review comment.

```
demo/ or your app/     main.cpp, windows, wiring
        ↓
appui                  Qt5::Widgets — theme, table helpers
        ↓
appinfra               Qt5::Core + Qt5::Network — log, settings, single-instance,
                       update, platform layer
        ↓
appcore                Qt5::Core ONLY — connection state machine, worker thread
```

`appcore` does not link `Qt5::Widgets`, so a `#include <QWidget>` in there does not build.

## Adding a feature

A feature is a **directory**, not a list of files.

```
src/features/myfeature/            myfeature_codec.{h,cpp}    pure logic, no I/O
                                   myfeature_service.{h,cpp}  I/O, via Worker
                                   myfeature_tab.{h,cpp}      widgets
src/features/myfeature_selftest/   main.cpp                   tests the codec
```

Then one line in `CMakeLists.txt`:

```cmake
add_feature_module(myfeature UI)
```

The self-test is discovered and registered with CTest automatically — nothing to add to
CI. That is deliberate: the project this came from kept its test list by hand in the CI
workflow, forgot to update it, and shipped a phase whose tests were built but never run
while CI stayed green.

**The split into a pure part and an I/O part is the convention that matters.** It is why
the pure part can be tested with no device, no network and no widgets. If a feature has
nothing testable, the logic and the I/O have not been separated yet.

## Build

```bash
cmake -B build -DBUILD_DEMO=ON -DCMAKE_PREFIX_PATH=/path/to/Qt5
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows, add `-G "Visual Studio 18 2026" -A Win32` (or your architecture) and run
`windeployqt` on the built exe before launching it.

## Using it from an application

Keep the base as its own repository and pin it:

```bash
git submodule add <base-url> external/base
git -C external/base checkout v1.0.0
```

```cmake
set(APP_UPDATE_REPO    "youruser/yourapp")
set(APP_INSTALLER_ASSET "YourAppSetup.exe")
set(APP_VERSION        "1.2.3")
add_subdirectory(external/base)
target_link_libraries(yourapp PRIVATE appui appinfra appcore)
```

Pinning means a base fix does not silently change an application already running in
production. That is a safety property, not overhead — bump each app deliberately.

**One repository per application.** Two apps in one repo share `releases/latest`, so each
would find the other's release and install the other's installer over itself, with a
SHA-256 that verifies correctly because the file really is the one the release advertises.

## The demo is a gate, not an example

`BUILD_DEMO=ON` builds `basedemo`. It has no device and no network: a fake connection
driven through the real `ConnectionState`, and a fake blocking call through the real
`Worker`. If it opens styled, toggles theme, refuses a second instance by raising the
first, writes a log, and stays responsive during the 3-second blocking call, the base is
intact.

## Read this before writing code

`docs/gotchas.md` — every entry cost real days, and most of the failures are silent.
