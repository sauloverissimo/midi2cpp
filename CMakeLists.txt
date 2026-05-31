cmake_minimum_required(VERSION 3.14)

# ESP-IDF Component Manager path. When IDF resolves this directory as
# a component (clone under managed_components/midi2cpp + idf_component.yml
# at the root), it sets ESP_PLATFORM before processing this file. Route
# to idf_component_register and skip the rest, leaving the native CMake
# build (project / install / export / find_package fallback) untouched
# for every other consumer.
if(ESP_PLATFORM)
    idf_component_register(
        SRCS
            src/midi2.c
            src/midi2_device.cpp
            src/midi2_ci.cpp
            src/midi2_host.cpp
            src/midi2_bridge.cpp
        INCLUDE_DIRS src
    )
    target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
    return()
endif()

project(midi2cpp VERSION 0.6.0
        DESCRIPTION "C++ MIDI 2.0 platform: a wrapper that bundles the midi2 core"
        LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Library target.
#
# midi2cpp bundles the midi2 C99 core: src/midi2.c is the single
# implementation translation unit (it defines MIDI2_IMPLEMENTATION and
# includes the vendored src/midi2.h amalgam). No external midi2 dependency.
add_library(midi2cpp
    src/midi2.c
    src/midi2_device.cpp
    src/midi2_ci.cpp
    src/midi2_host.cpp
    src/midi2_bridge.cpp
)

target_include_directories(midi2cpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

# Warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(midi2cpp PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter   # spec uses many unused params in stubs
    )
endif()

# Exportable alias for consumers: midi2cpp::midi2cpp
add_library(midi2cpp::midi2cpp ALIAS midi2cpp)

# Tests: build only if this is the top-level project or tests explicitly requested
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(MIDI2CPP_BUILD_TESTS_DEFAULT ON)
else()
    set(MIDI2CPP_BUILD_TESTS_DEFAULT OFF)
endif()
option(MIDI2CPP_BUILD_TESTS "Build host-side unit tests" ${MIDI2CPP_BUILD_TESTS_DEFAULT})
if(MIDI2CPP_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    # Tests use the same public hooks the platforms wire (setWriteFn,
    # setNowFn, feedRx). No special test mode — the contract is the contract.
    enable_testing()
    add_subdirectory(tests)
endif()
