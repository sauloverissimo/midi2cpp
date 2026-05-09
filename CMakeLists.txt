cmake_minimum_required(VERSION 3.14)
project(midi2_cpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# midi2 C99 core, resolved as an external dependency.
#
# Three-layer fallback: parent project's target -> system install
# (vcpkg / conan / system package) -> FetchContent from GitHub.
# This is the same pattern the midi2 README documents for downstream
# consumers, applied to midi2_cpp itself.
if(NOT TARGET midi2)
    find_package(midi2 0.3.2 QUIET CONFIG)
    if(NOT midi2_FOUND)
        include(FetchContent)
        FetchContent_Declare(midi2
            GIT_REPOSITORY https://github.com/sauloverissimo/midi2.git
            GIT_TAG        v0.3.2
        )
        FetchContent_MakeAvailable(midi2)
    endif()
endif()

# Library target.
#
# src/midi2.c is still compiled here so the host test suite, the
# arduino-cli compile job, and the standalone CMake build all see a
# single coherent translation unit. The recipes under examples/ are
# being migrated to depend on midi2::midi2 directly; once every recipe
# has migrated, src/midi2.{h,c} will be removed and midi2_cpp will
# link against the external midi2 target unconditionally.
add_library(midi2_cpp
    src/midi2.c
    src/midi2_device.cpp
    src/midi2_ci.cpp
    src/midi2_host.cpp
    src/midi2_bridge.cpp
)

target_include_directories(midi2_cpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

# Warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(midi2_cpp PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter   # spec uses many unused params in stubs
    )
endif()

# Exportable alias for consumers: midi2_cpp::midi2_cpp
add_library(midi2_cpp::midi2_cpp ALIAS midi2_cpp)

# Tests: build only if this is the top-level project or tests explicitly requested
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(MIDI2_CPP_BUILD_TESTS_DEFAULT ON)
else()
    set(MIDI2_CPP_BUILD_TESTS_DEFAULT OFF)
endif()
option(MIDI2_CPP_BUILD_TESTS "Build host-side unit tests" ${MIDI2_CPP_BUILD_TESTS_DEFAULT})
if(MIDI2_CPP_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    # Tests use the same public hooks the platforms wire (setWriteFn,
    # setNowFn, feedRx). No special test mode — the contract is the contract.
    enable_testing()
    add_subdirectory(tests)
endif()
