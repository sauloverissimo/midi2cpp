cmake_minimum_required(VERSION 3.14)
project(midi2_cpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Library target
add_library(midi2_cpp
    src/midi2.c
    src/midi2_device.cpp
    src/midi2_ci.cpp
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
