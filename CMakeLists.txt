cmake_minimum_required(VERSION 3.21)
project(qt-app-base LANGUAGES C CXX VERSION 0.1.0)

# ---------------------------------------------------------------------------
# This file builds the BASE only: three static libraries plus a demo application
# that proves they work. An application built on this base adds this directory with
# add_subdirectory() and links the libraries; it does not copy them.
#
# Nothing here assumes Windows. Platform differences live behind src/infra/platform,
# and any architecture restriction (a 32-bit-only vendor DLL, for example) belongs in
# the application's own CMakeLists, never here.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
# Without this no .qrc is ever compiled, and the failure is SILENT: QFile::open on
# ":/styles/..." simply returns false, so the app runs with an empty stylesheet and looks
# merely unstyled rather than broken. Cost the source project a day.
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AddFeatureModule)
include(UseBaseFeature)
enable_testing()

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Network Widgets)
find_package(Threads REQUIRED)

if(NOT DEFINED APP_VERSION)
    set(APP_VERSION "0.0.0-dev")
endif()

# OFF by default so a plain `git clone && cmake --build .` never needs external/AMQP-CPP --
# only a maintainer who explicitly wants the demo's RabbitMQ tab initializes that submodule
# and passes -DBUILD_DEMO_MQ=ON. See .plans/2026-08-23-demo-rabbitmq-tab/spec.md D1/D2.
option(BUILD_DEMO_MQ
    "Also build the mq feature's RabbitMQ tab into the demo (needs external/AMQP-CPP)" OFF)

# --- layer 1: core ---------------------------------------------------------
# Pure logic and the threading boundary. Links Qt5::Core ONLY — that is what makes the
# layering real: a #include <QWidget> in here is a compile error, not a review comment.
add_library(appcore STATIC
    src/core/connection_state.cpp
    src/core/connection_state.h
    src/core/async/worker.cpp
    src/core/async/worker.h
    src/core/protocol/protocol_driver.h)
target_include_directories(appcore PUBLIC src/core src/core/async src/core/protocol)
target_link_libraries(appcore PUBLIC Qt5::Core)

# --- layer 2: infra --------------------------------------------------------
# Talks to the operating system: files, settings, sockets, subprocesses. No widgets.
set(INFRA_PLATFORM_SOURCES src/infra/platform/platform.h
    src/infra/platform/platform_common.cpp)
if(WIN32)
    list(APPEND INFRA_PLATFORM_SOURCES src/infra/platform/platform_win.cpp)
else()
    list(APPEND INFRA_PLATFORM_SOURCES src/infra/platform/platform_unix.cpp)
endif()

add_library(appinfra STATIC
    src/infra/log/app_log.cpp
    src/infra/log/app_log.h
    src/infra/db/database.cpp
    src/infra/db/database.h
    src/infra/db/statement.h
    src/infra/db/sqlite3.c
    src/infra/db/sqlite3.h
    src/infra/db/sqlite3ext.h
    src/infra/settings/app_settings.cpp
    src/infra/settings/app_settings.h
    src/infra/singleinstance/single_instance.cpp
    src/infra/singleinstance/single_instance.h
    src/infra/update/curl_client.cpp
    src/infra/update/curl_client.h
    src/infra/update/update_checker.cpp
    src/infra/update/update_checker.h
    src/infra/update/update_installer.cpp
    src/infra/update/update_installer.h
    ${INFRA_PLATFORM_SOURCES})
target_include_directories(appinfra PUBLIC
    src/infra/log src/infra/db src/infra/settings src/infra/singleinstance
    src/infra/update src/infra/platform)
target_link_libraries(appinfra PUBLIC Qt5::Core Qt5::Network appcore Threads::Threads ${CMAKE_DL_LIBS})
target_compile_definitions(appinfra PUBLIC APP_VERSION="${APP_VERSION}")
# Each application supplies its own. Empty here means the base's demo never checks for
# updates, which is correct — the base has no releases of its own.
target_compile_definitions(appinfra PUBLIC
    APP_UPDATE_REPO="${APP_UPDATE_REPO}"
    APP_INSTALLER_ASSET="${APP_INSTALLER_ASSET}")

# --- layer 3: ui -----------------------------------------------------------
add_library(appui STATIC
    src/ui/theme/thememanager.cpp
    src/ui/theme/thememanager.h
    src/ui/theme/styles.qrc
    src/ui/widgets/table_style.cpp
    src/ui/widgets/table_style.h)
target_include_directories(appui PUBLIC src/ui/theme src/ui/widgets)
target_link_libraries(appui PUBLIC Qt5::Widgets appcore appinfra)

# --- base self-tests -------------------------------------------------------
add_executable(core_selftest tests/core_selftest/main.cpp)
target_link_libraries(core_selftest PRIVATE appcore)
add_test(NAME core COMMAND core_selftest)

add_executable(infra_selftest tests/infra_selftest/main.cpp)
target_link_libraries(infra_selftest PRIVATE appinfra)
add_test(NAME infra COMMAND infra_selftest)

add_executable(ui_selftest tests/ui_selftest/main.cpp)
target_link_libraries(ui_selftest PRIVATE appui)
add_test(NAME ui COMMAND ui_selftest)

# --- demo application ------------------------------------------------------
# Not an example to read — a gate. If this builds, runs, styles itself, guards against a
# second instance and writes a log, the base is intact.
if(BUILD_DEMO)
    add_feature_module(modbus)
    add_feature_module(c3protocol)
    if(BUILD_DEMO_MQ)
        add_subdirectory(external/AMQP-CPP)
        if(WIN32)
            # Same fix qt-mq-lab's own top-level CMakeLists.txt already carries -- AMQP-CPP's
            # message.h calls std::numeric_limits<...>::max()-shaped expressions that collide
            # with <windows.h>'s max/min macros once anything pulls windows.h in transitively.
            # Scoped to this target only, not applied base-wide.
            target_compile_definitions(amqpcpp PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
        endif()
        add_feature_module(mq UI LINK amqpcpp)
    endif()
    add_executable(basedemo WIN32 demo/main.cpp demo/demowindow.cpp demo/demowindow.h)
    target_link_libraries(basedemo PRIVATE appui appinfra appcore feature_modbus feature_c3protocol)
    if(BUILD_DEMO_MQ)
        target_link_libraries(basedemo PRIVATE feature_mq)
        target_compile_definitions(basedemo PRIVATE BUILD_DEMO_MQ)
    endif()
endif()
