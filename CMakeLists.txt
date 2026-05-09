cmake_minimum_required(VERSION 3.14)
project(midi2cpp VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# midi2 C99 core, resolved as an external dependency.
#
# Three-layer fallback: parent project's target -> system install
# (vcpkg / conan / system package) -> FetchContent from GitHub.
# This is the same pattern the midi2 README documents for downstream
# consumers, applied to midi2cpp itself.
if(NOT TARGET midi2)
    find_package(midi2 0.3.4 QUIET CONFIG)
    if(NOT midi2_FOUND)
        include(FetchContent)
        FetchContent_Declare(midi2
            GIT_REPOSITORY https://github.com/sauloverissimo/midi2.git
            GIT_TAG        v0.3.4
        )
        FetchContent_MakeAvailable(midi2)
    endif()
endif()

# Library target.
#
# midi2cpp ships the C++ wrapper sources only; the C99 core lives in
# the external midi2 target this CMakeLists pulls above. Consumers
# linking midi2cpp transitively see midi2::midi2 because of the
# PUBLIC link below.
add_library(midi2cpp
    src/midi2_device.cpp
    src/midi2_ci.cpp
    src/midi2_host.cpp
    src/midi2_bridge.cpp
)

target_link_libraries(midi2cpp PUBLIC midi2::midi2)

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
