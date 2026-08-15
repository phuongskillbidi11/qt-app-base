# use_base_feature(<name> [LINK <targets>...] [UI])
#
# A base feature is a directory shipped by qt-app-base, not by the consuming app. Create
# src/features/<name>/ in the base, write the code, add one call in the consumer, done.
#
# If src/features/<name>_selftest/main.cpp exists in the base it is built and registered
# with CTest automatically. This mirrors add_feature_module(), while resolving sources from
# qt-app-base's own project directory instead of the outermost application's source directory.
#
# UI      the feature contains widgets, so link the ui layer too.
# LINK    extra targets this feature needs (a vendor SDK, Qt5::SerialPort, ...).
function(use_base_feature name)
    cmake_parse_arguments(ARG "UI" "" "LINK" ${ARGN})

    if(NOT DEFINED qt-app-base_SOURCE_DIR)
        message(FATAL_ERROR
            "use_base_feature(${name}): qt-app-base_SOURCE_DIR is not defined; "
            "qt-app-base must be add_subdirectory()'d first")
    endif()

    file(GLOB _srcs CONFIGURE_DEPENDS
        "${qt-app-base_SOURCE_DIR}/src/features/${name}/*.cpp"
        "${qt-app-base_SOURCE_DIR}/src/features/${name}/*.h")
    if(NOT _srcs)
        message(FATAL_ERROR
            "use_base_feature(${name}): ${qt-app-base_SOURCE_DIR}/src/features/${name}/ "
            "has no sources")
    endif()

    add_library(feature_${name} STATIC ${_srcs})
    target_include_directories(feature_${name}
        PUBLIC "${qt-app-base_SOURCE_DIR}/src/features/${name}")
    target_link_libraries(feature_${name} PUBLIC appcore appinfra ${ARG_LINK})
    if(ARG_UI)
        target_link_libraries(feature_${name} PUBLIC appui)
    endif()

    set(_selftest "${qt-app-base_SOURCE_DIR}/src/features/${name}_selftest/main.cpp")
    if(EXISTS "${_selftest}")
        add_executable(${name}_selftest "${_selftest}")
        target_link_libraries(${name}_selftest PRIVATE feature_${name})
        add_test(NAME ${name} COMMAND ${name}_selftest)
    else()
        # Not fatal - some features are pure UI. But say so, because a feature with no
        # testable part usually means the pure logic was never separated from the I/O,
        # and that is a design problem rather than a missing file.
        message(STATUS "use_base_feature(${name}): no self-test "
                       "(${qt-app-base_SOURCE_DIR}/src/features/${name}_selftest/main.cpp)")
    endif()
endfunction()
