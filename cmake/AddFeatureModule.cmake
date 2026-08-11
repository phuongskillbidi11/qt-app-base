# add_feature_module(<name> [LINK <targets>...] [UI])
#
# A feature is a directory, not a list of files. Create src/features/<name>/, write the
# code, add one call here, done.
#
# If src/features/<name>_selftest/main.cpp exists it is built and registered with CTest
# automatically. That automatic registration is the point: the project this base came from
# listed its self-tests by hand in the CI workflow, and when a new module was added the
# list was not updated — so a whole phase's tests were built, never run, and CI stayed
# green while proving nothing about the new code.
#
# UI      the feature contains widgets, so link the ui layer too.
# LINK    extra targets this feature needs (a vendor SDK, Qt5::SerialPort, ...).
function(add_feature_module name)
    cmake_parse_arguments(ARG "UI" "" "LINK" ${ARGN})

    file(GLOB _srcs CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/features/${name}/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/features/${name}/*.h")
    if(NOT _srcs)
        message(FATAL_ERROR "add_feature_module(${name}): src/features/${name}/ has no sources")
    endif()

    add_library(feature_${name} STATIC ${_srcs})
    target_include_directories(feature_${name}
        PUBLIC "${CMAKE_SOURCE_DIR}/src/features/${name}")
    target_link_libraries(feature_${name} PUBLIC appcore appinfra ${ARG_LINK})
    if(ARG_UI)
        target_link_libraries(feature_${name} PUBLIC appui)
    endif()

    set(_selftest "${CMAKE_SOURCE_DIR}/src/features/${name}_selftest/main.cpp")
    if(EXISTS "${_selftest}")
        add_executable(${name}_selftest "${_selftest}")
        target_link_libraries(${name}_selftest PRIVATE feature_${name})
        add_test(NAME ${name} COMMAND ${name}_selftest)
    else()
        # Not fatal — some features are pure UI. But say so, because a feature with no
        # testable part usually means the pure logic was never separated from the I/O,
        # and that is a design problem rather than a missing file.
        message(STATUS "add_feature_module(${name}): no self-test "
                       "(src/features/${name}_selftest/main.cpp)")
    endif()
endfunction()
